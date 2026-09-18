//------------------------------- -*- C++ -*- -------------------------------//
// Copyright Celeritas contributors: see top-level COPYRIGHT file for details
// SPDX-License-Identifier: (Apache-2.0 OR MIT)
//---------------------------------------------------------------------------//
//! \file celeritas/ext/detail/SteppingActionProcessor.cc
//---------------------------------------------------------------------------//
#include "SteppingActionProcessor.hh"

#include <map>
#include <tuple>
#include <G4EventManager.hh>
#include <G4LogicalVolume.hh>
#include <G4Region.hh>
#include <G4Step.hh>
#include <G4Track.hh>
#include <G4UserSteppingAction.hh>

#include "corecel/cont/Range.hh"
#include "corecel/data/Copier.hh"
#include "celeritas/global/CoreState.hh"

namespace celeritas
{
namespace detail
{
namespace
{
// Transport edits cannot be applied to already transported Celeritas steps.
auto transport_state(G4Track const& t)
{
    return std::make_tuple(t.GetTrackStatus(),
                           t.GetTrackID(),
                           t.GetParentID(),
                           t.GetPosition(),
                           t.GetMomentumDirection(),
                           t.GetKineticEnergy(),
                           t.GetGlobalTime(),
                           t.GetLocalTime(),
                           t.GetProperTime(),
                           t.GetWeight(),
                           t.GetCurrentStepNumber(),
                           t.GetTrackLength(),
                           t.GetStepLength(),
                           t.GetPolarization());
}

auto point_state(G4StepPoint const& p)
{
    return std::make_tuple(p.GetPosition(),
                           p.GetMomentumDirection(),
                           p.GetKineticEnergy(),
                           p.GetGlobalTime(),
                           p.GetLocalTime(),
                           p.GetProperTime(),
                           p.GetWeight(),
                           p.GetStepStatus(),
                           p.GetPhysicalVolume());
}

// Always remove borrowed secondary pointers, including during stack unwinding.
struct ClearSecondaries
{
    G4Step& step;
    ~ClearSecondaries() { step.GetfSecondary()->clear(); }
};
}  // namespace

//---------------------------------------------------------------------------//
/*!
 * Construct on the owning worker, sharing the sensitive detector track store.
 */
SteppingActionProcessor::SteppingActionProcessor(VecParticle const& particles,
                                                 SPTracks tracks)
    : thread_(std::this_thread::get_id())
    , reconstruction_(
          particles, StepSelection::all(), {{true, true}}, {}, std::move(tracks))
{
    this->track_reconstruction()->enable_track_mapping();
}

//---------------------------------------------------------------------------//
//! Prepare the worker's step storage before transport begins.
template<MemSpace M>
void SteppingActionProcessor::initialize(
    StepStateData<Ownership::reference, M> const& state)
{
    this->validate_thread();
    steps_.initialize(state);
}

//! Allocate birth storage before any transfers can be submitted.
void SteppingActionProcessor::initialize_births(size_type capacity)
{
    this->validate_thread();
    CELER_VALIDATE(!steps_.pending(),
                   << "undelivered Geant4 stepping callback batch");
    births_.resize(capacity);
}

void SteppingActionProcessor::save_steps(HostRef<StepStateData> const& state)
{
    this->save_steps_impl(state);
}
void SteppingActionProcessor::save_steps(DeviceRef<StepStateData> const& state)
{
    this->save_steps_impl(state);
}
void SteppingActionProcessor::dispatch(CoreState<MemSpace::host>& state)
{
    this->dispatch_impl(state);
}
void SteppingActionProcessor::dispatch(CoreState<MemSpace::device>& state)
{
    this->dispatch_impl(state);
}

//---------------------------------------------------------------------------//
template<MemSpace M>
void SteppingActionProcessor::save_steps_impl(
    StepStateData<Ownership::reference, M> const& state)
{
    this->validate_thread();
    steps_.capture(state);
    births_ready_ = false;
}

//---------------------------------------------------------------------------//
template<MemSpace M>
void SteppingActionProcessor::save_births(CoreState<M>& state)
{
    this->validate_thread();
    CELER_VALIDATE(steps_.pending() && !births_ready_,
                   << "missing step snapshot or repeated secondary capture");
    auto const& source = state.ref().init.secondary_births;
    CELER_VALIDATE(births_.size() == source.size(),
                   << "secondary snapshot capacity changed during transport");
    if (!births_.empty())
    {
        Copier<SecondaryBirth, MemSpace::host>{make_span(births_),
                                               state.stream_id()}(
            M, source[AllItems<SecondaryBirth, M>{}]);
    }
    births_ready_ = true;
}

//---------------------------------------------------------------------------//
//! Deliver completed snapshots after the step event, without stream waits.
template<MemSpace M>
void SteppingActionProcessor::dispatch_impl(CoreState<M>& state)
{
    this->validate_thread();
    CELER_VALIDATE(births_ready_, << "missing secondary snapshot");
    auto const& output = steps_.complete();
    if (state.warming_up())
    {
        CELER_VALIDATE(!output,
                       << "active steps during Geant4 callback warmup");
    }
    else
    {
        this->dispatch(output, make_span(births_));
    }
    steps_.clear();
    births_ready_ = false;
}

template void SteppingActionProcessor::initialize(
    HostRef<StepStateData> const&);
template void SteppingActionProcessor::initialize(
    DeviceRef<StepStateData> const&);
template void SteppingActionProcessor::save_births(CoreState<MemSpace::host>&);
template void SteppingActionProcessor::save_births(
    CoreState<MemSpace::device>&);

//---------------------------------------------------------------------------//
/*!
 * Reconstruct parents before their children and invoke Geant4's action chain.
 *
 * Sensitive detector completion callbacks have already run. Global dispatch
 * goes through the registered object (including composite actions), followed
 * by the regional action selected from the pre-step volume.
 */
void SteppingActionProcessor::dispatch(StepOutput const& steps,
                                       Span<SecondaryBirth const> births)
{
    this->validate_thread();
    std::multimap<TrackId, SecondaryBirth const*> by_parent;
    for (auto const& birth : births)
    {
        if (birth)
            by_parent.emplace(birth.sim.parent_id, &birth);
    }

    for (auto i : range(steps.size()))
    {
        G4Step* step = reconstruction_(steps, i);
        CELER_VALIDATE(step, << "failed to reconstruct Geant4 callback step");
        auto* secondary = step->GetfSecondary();
        secondary->clear();
        // Reset GetSecondaryInCurrentStep's offset before populating the list.
        // Restore the saved pre point after CopyPostToPreStepPoint.
        G4StepPoint pre = *step->GetPreStepPoint();
        auto post_status = step->GetPostStepPoint()->GetStepStatus();
        step->CopyPostToPreStepPoint();
        *step->GetPreStepPoint() = pre;
        step->GetPostStepPoint()->SetStepStatus(post_status);
        ClearSecondaries clear{*step};

        auto matching = by_parent.equal_range(steps.track_id[i]);
        for (auto iter = matching.first; iter != matching.second; ++iter)
        {
            auto const& birth = *iter->second;
            CELER_VALIDATE(
                birth.parent_step == steps.track_step_count[i]
                    && birth.sim.primary_id == steps.primary_id[i]
                    && birth.sim.event_id == steps.event_id[i],
                << "secondary birth does not match completed parent step");
            G4Track& child
                = this->track_reconstruction()->insert_secondary(birth);
            child.SetTouchableHandle(
                step->GetPostStepPoint()->GetTouchableHandle());
            if (auto* pv = step->GetPostStepPoint()->GetPhysicalVolume())
                child.SetLogicalVolumeAtVertex(pv->GetLogicalVolume());
            secondary->push_back(&child);
        }
        by_parent.erase(matching.first, matching.second);

        auto* manager = G4EventManager::GetEventManager();
        auto* global = manager ? manager->GetUserSteppingAction() : nullptr;
        auto* pv = step->GetPreStepPoint()->GetPhysicalVolume();
        auto* region = pv ? pv->GetLogicalVolume()->GetRegion() : nullptr;
        auto* regional = region ? region->GetRegionalSteppingAction() : nullptr;

        auto before = transport_state(*step->GetTrack());
        auto pre_before = point_state(*step->GetPreStepPoint());
        auto post_before = point_state(*step->GetPostStepPoint());
        std::vector<decltype(before)> child_before;
        for (auto* child : *secondary)
            child_before.push_back(transport_state(*child));

        if (global)
            global->UserSteppingAction(step);
        if (regional)
            regional->UserSteppingAction(step);

        CELER_VALIDATE(
            before == transport_state(*step->GetTrack())
                && pre_before == point_state(*step->GetPreStepPoint())
                && post_before == point_state(*step->GetPostStepPoint()),
            << "Geant4 stepping action changed transport state; "
               "only observation and user metadata are supported");
        CELER_VALIDATE(secondary->size() == child_before.size(),
                       << "Geant4 stepping action changed the secondary list");
        for (auto j : range(secondary->size()))
        {
            CELER_VALIDATE(
                child_before[j] == transport_state(*(*secondary)[j]),
                << "Geant4 stepping action changed secondary transport state");
        }
    }
    CELER_VALIDATE(by_parent.empty(),
                   << "secondary birth has no completed parent step");
}

//---------------------------------------------------------------------------//
void SteppingActionProcessor::validate_thread() const
{
    CELER_VALIDATE(thread_ == std::this_thread::get_id(),
                   << "Geant4 callbacks must execute on their owning worker");
}
}  // namespace detail
}  // namespace celeritas
