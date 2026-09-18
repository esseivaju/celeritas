//------------------------------- -*- C++ -*- -------------------------------//
// Copyright Celeritas contributors: see top-level COPYRIGHT file for details
// SPDX-License-Identifier: (Apache-2.0 OR MIT)
//---------------------------------------------------------------------------//
//! \file celeritas/ext/detail/HitProcessor.cc
//---------------------------------------------------------------------------//
#include "HitProcessor.hh"

#include <G4LogicalVolume.hh>
#include <G4VSensitiveDetector.hh>

#include "corecel/io/Logger.hh"
#include "corecel/sys/ScopedProfiling.hh"
#include "corecel/sys/TraceCounter.hh"
#include "geocel/GeantGeoUtils.hh"

namespace celeritas
{
namespace detail
{
//---------------------------------------------------------------------------//
/*!
 * Construct worker-local reconstruction and sensitive detector dispatch.
 */
HitProcessor::HitProcessor(SPConstVecLV volumes,
                           VecParticle const& particles,
                           StepSelection const& selection,
                           StepPointBool const& locate)
    : detector_volumes_(std::move(volumes))
    , reconstruction_(particles, selection, locate, detector_volumes_)
    , thread_(std::this_thread::get_id())
{
    CELER_EXPECT(detector_volumes_ && !detector_volumes_->empty());
    CELER_LOG(debug) << "Setting up thread-local hit processor for "
                     << detector_volumes_->size() << " sensitive detectors";
    for (auto* lv : *detector_volumes_)
    {
        CELER_ASSERT(lv);
        auto* sd = lv->GetSensitiveDetector();
        CELER_VALIDATE(sd,
                       << "no sensitive detector is attached to volume '"
                       << StreamableLV{lv});
        detectors_.push_back(sd);
    }
}

//---------------------------------------------------------------------------//
/*!
 * Allocate snapshot storage before transport starts.
 */
template<MemSpace M>
void HitProcessor::initialize(
    StepStateData<Ownership::reference, M> const& state)
{
    this->validate_thread();
    steps_.initialize(state);
}

template void HitProcessor::initialize(StepStateHostRef const&);
template void HitProcessor::initialize(StepStateDeviceRef const&);

//---------------------------------------------------------------------------//
//! Capture host hits for delivery when the step result is consumed.
void HitProcessor::operator()(StepStateHostRef const& states)
{
    this->validate_thread();
    steps_.capture(states);
}

//---------------------------------------------------------------------------//
//! Enqueue device hit capture on the producing stream.
void HitProcessor::operator()(StepStateDeviceRef const& states)
{
    this->validate_thread();
    steps_.capture(states);
}

//---------------------------------------------------------------------------//
//! Deliver a completed snapshot on the owning Geant4 worker.
void HitProcessor::process_pending_steps()
{
    this->validate_thread();
    auto const& output = steps_.complete();
    (*this)(output);
    num_hits_ += output.size();
    steps_.clear();
}

//---------------------------------------------------------------------------//
//! Dispatch a batch of saved detector steps.
void HitProcessor::operator()(DetectorStepOutput const& out) const
{
    this->validate_thread();
    ScopedProfiling profile_this{"process-hits"};
    trace_counter("process-hits", out.size());
    for (auto i : range(out.size()))
    {
        (*this)(out, i);
    }
}

//---------------------------------------------------------------------------//
//! Reconstruct and dispatch one detector step.
void HitProcessor::operator()(DetectorStepOutput const& out, size_type i) const
{
    this->validate_thread();
    CELER_EXPECT(!out.detector_id.empty());
    if (auto* step = reconstruction_(out, i))
    {
        this->detector(out.detector_id[i])->Hit(step);
    }
}

//---------------------------------------------------------------------------//
//! Reject access to Geant4 objects from a different worker.
void HitProcessor::validate_thread() const
{
    CELER_VALIDATE(
        std::this_thread::get_id() == thread_,
        << "Geant4 sensitive detector callbacks require the owning worker");
}

//---------------------------------------------------------------------------//
}  // namespace detail
}  // namespace celeritas
