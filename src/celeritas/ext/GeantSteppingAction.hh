//------------------------------- -*- C++ -*- -------------------------------//
// Copyright Celeritas contributors: see top-level COPYRIGHT file for details
// SPDX-License-Identifier: (Apache-2.0 OR MIT)
//---------------------------------------------------------------------------//
//! \file celeritas/ext/GeantSteppingAction.hh
//! \sa accel/TrackingManagerIntegration.test.cc
//---------------------------------------------------------------------------//
#pragma once

#include <memory>
#include <vector>

#include "corecel/Config.hh"

#include "celeritas/global/ActionInterface.hh"
#include "celeritas/user/StepInterface.hh"

class G4ParticleDefinition;

namespace celeritas
{
class GeantTrackReconstruction;
class ParticleParams;
namespace detail
{
class SteppingActionProcessor;
}

//---------------------------------------------------------------------------//
/*!
 * Forward saved Celeritas steps to the worker's registered Geant4 actions.
 *
 * The unfiltered collector saves completed steps at user_post. This action
 * dispatches at user_end, after secondary identity has been assigned, on the
 * calling Geant4 worker. Processors and their Geant4 objects must be created
 * and destroyed on that worker. Geant4 retains ownership of its actions.
 */
class GeantSteppingAction final : public StepInterface,
                                  public CoreStepActionInterface,
                                  public StaticConcreteAction
{
  public:
    using SPProcessor = std::shared_ptr<detail::SteppingActionProcessor>;
    using SPTracks = std::shared_ptr<GeantTrackReconstruction>;

    // Construct shared dispatch and particle mappings
    GeantSteppingAction(ActionId, ParticleParams const&, size_type num_streams);

    // Create worker-local reconstruction, sharing sensitive detector tracks
    SPProcessor make_local_processor(StreamId, SPTracks = {});

    //! Collect every completed step, including zero deposition
    Filters filters() const final { return {}; }
    //! Reconstruct all available step fields
    StepSelection selection() const final { return StepSelection::all(); }
    //! Dispatch after assigning identities to this step's secondaries
    StepActionOrder order() const final { return StepActionOrder::user_end; }

    void process_steps(HostStepState) final;
    void process_steps(DeviceStepState) final;
    void step(CoreParams const&, CoreStateHost&) const final;
    void step(CoreParams const&, CoreStateDevice&) const final;

  private:
    std::vector<G4ParticleDefinition const*> particles_;
    std::vector<std::weak_ptr<detail::SteppingActionProcessor>> processors_;

    SPProcessor processor(StreamId) const;
};

#if !CELERITAS_USE_GEANT4
inline GeantSteppingAction::GeantSteppingAction(
    ActionId id, ParticleParams const&, size_type)
    : StaticConcreteAction(id, "geant-stepping-actions")
{
    CELER_NOT_CONFIGURED("Geant4");
}
inline auto GeantSteppingAction::make_local_processor(StreamId, SPTracks)
    -> SPProcessor
{
    CELER_ASSERT_UNREACHABLE();
}
inline void GeantSteppingAction::process_steps(HostStepState)
{
    CELER_ASSERT_UNREACHABLE();
}
inline void GeantSteppingAction::process_steps(DeviceStepState)
{
    CELER_ASSERT_UNREACHABLE();
}
inline void GeantSteppingAction::step(CoreParams const&, CoreStateHost&) const
{
    CELER_ASSERT_UNREACHABLE();
}
inline void GeantSteppingAction::step(CoreParams const&, CoreStateDevice&) const
{
    CELER_ASSERT_UNREACHABLE();
}
#endif
}  // namespace celeritas
