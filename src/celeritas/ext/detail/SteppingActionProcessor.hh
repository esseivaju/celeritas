//------------------------------- -*- C++ -*- -------------------------------//
// Copyright Celeritas contributors: see top-level COPYRIGHT file for details
// SPDX-License-Identifier: (Apache-2.0 OR MIT)
//---------------------------------------------------------------------------//
//! \file celeritas/ext/detail/SteppingActionProcessor.hh
//---------------------------------------------------------------------------//
#pragma once

#include <thread>

#include "corecel/cont/Span.hh"
#include "celeritas/global/ActionInterface.hh"
#include "celeritas/track/TrackInitData.hh"
#include "celeritas/user/StepInterface.hh"
#include "celeritas/user/StepSnapshot.hh"

#include "GeantStepReconstruction.hh"

namespace celeritas
{
namespace detail
{
//---------------------------------------------------------------------------//
/*!
 * Own saved steps and persistent Geant4 tracks on a single worker.
 *
 * Borrowed secondary pointers are valid during dispatch only. Their tracks and
 * user information persist in the shared metadata store until flush cleanup.
 * An exception leaves the pending batch marked undelivered and propagates to
 * the caller: a later capture cannot silently overwrite it.
 */
class SteppingActionProcessor
{
  public:
    using SPTracks = std::shared_ptr<GeantTrackReconstruction>;
    using VecParticle = GeantTrackReconstruction::VecParticle;

    // Construct the worker-local step and enable persistent metadata
    SteppingActionProcessor(VecParticle const&, SPTracks = {});

    template<MemSpace M>
    void initialize(StepStateData<Ownership::reference, M> const&);
    void initialize_births(size_type);

    void save_steps(HostRef<StepStateData> const&);
    void save_steps(DeviceRef<StepStateData> const&);
    template<MemSpace M>
    void save_births(CoreState<M>&);
    void dispatch(CoreState<MemSpace::host>&);
    void dispatch(CoreState<MemSpace::device>&);

    // Dispatch a saved batch (also used by focused reconstruction tests)
    void dispatch(StepOutput const&, Span<SecondaryBirth const>);

    //! Shared metadata for sensitive detector reconstruction and offloading
    SPTracks const& track_reconstruction() const
    {
        return reconstruction_.track_reconstruction();
    }

  private:
    std::thread::id thread_;
    GeantStepReconstruction reconstruction_;
    StepSnapshot steps_;
    std::vector<SecondaryBirth, PinnedAllocator<SecondaryBirth>> births_;
    bool births_ready_{false};

    template<MemSpace M>
    void save_steps_impl(StepStateData<Ownership::reference, M> const&);
    template<MemSpace M>
    void dispatch_impl(CoreState<M>&);
    void validate_thread() const;
};
}  // namespace detail
}  // namespace celeritas
