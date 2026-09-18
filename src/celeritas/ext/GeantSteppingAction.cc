//------------------------------- -*- C++ -*- -------------------------------//
// Copyright Celeritas contributors: see top-level COPYRIGHT file for details
// SPDX-License-Identifier: (Apache-2.0 OR MIT)
//---------------------------------------------------------------------------//
//! \file celeritas/ext/GeantSteppingAction.cc
//---------------------------------------------------------------------------//
#include "GeantSteppingAction.hh"

#include <G4ParticleTable.hh>

#include "corecel/cont/Range.hh"
#include "celeritas/global/CoreState.hh"
#include "celeritas/phys/ParticleParams.hh"

#include "detail/SteppingActionProcessor.hh"

namespace celeritas
{
//---------------------------------------------------------------------------//
/*!
 * Map particle definitions once, without allocating worker callback buffers.
 */
GeantSteppingAction::GeantSteppingAction(
    ActionId id, ParticleParams const& particles, size_type num_streams)
    : StaticConcreteAction(id,
                           "geant-stepping-actions",
                           "forward registered Geant4 stepping actions")
    , particles_(particles.size())
    , processors_(num_streams)
{
    CELER_EXPECT(num_streams > 0);
    auto* table = G4ParticleTable::GetParticleTable();
    for (auto pid : range(ParticleId{particles.size()}))
    {
        particles_[pid.get()]
            = table->FindParticle(particles.id_to_pdg(pid).get());
        CELER_VALIDATE(particles_[pid.get()],
                       << "missing Geant4 particle "
                       << particles.id_to_pdg(pid).get());
    }
}

//---------------------------------------------------------------------------//
/*!
 * Construct on the Geant4 worker that will execute the callbacks.
 */
auto GeantSteppingAction::make_local_processor(StreamId id, SPTracks tracks)
    -> SPProcessor
{
    CELER_EXPECT(id < processors_.size());
    CELER_EXPECT(processors_[id.get()].expired());
    auto result = std::make_shared<detail::SteppingActionProcessor>(
        particles_, std::move(tracks));
    processors_[id.get()] = result;
    return result;
}

//---------------------------------------------------------------------------//
void GeantSteppingAction::process_steps(HostStepState state)
{
    this->processor(state.stream_id)->save_steps(state.steps);
}

void GeantSteppingAction::process_steps(DeviceStepState state)
{
    this->processor(state.stream_id)->save_steps(state.steps);
}

void GeantSteppingAction::step(CoreParams const&, CoreStateHost& state) const
{
    this->processor(state.stream_id())->dispatch(state);
}

void GeantSteppingAction::step(CoreParams const&, CoreStateDevice& state) const
{
    this->processor(state.stream_id())->dispatch(state);
}

//---------------------------------------------------------------------------//
auto GeantSteppingAction::processor(StreamId id) const -> SPProcessor
{
    CELER_EXPECT(id < processors_.size());
    auto result = processors_[id.get()].lock();
    CELER_VALIDATE(
        result, << "Geant4 stepping actions require a local worker processor");
    return result;
}
}  // namespace celeritas
