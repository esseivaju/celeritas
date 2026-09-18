//------------------------------- -*- C++ -*- -------------------------------//
// Copyright Celeritas contributors: see top-level COPYRIGHT file for details
// SPDX-License-Identifier: (Apache-2.0 OR MIT)
//---------------------------------------------------------------------------//
//! \file celeritas/ext/detail/GeantStepReconstruction.hh
//! \sa HitProcessor.test.cc
//---------------------------------------------------------------------------//
#pragma once

#include <memory>
#include <vector>
#include <G4TouchableHandle.hh>

#include "celeritas/user/DetectorSteps.hh"
#include "celeritas/user/StepData.hh"

#include "TouchableUpdaterInterface.hh"
#include "../GeantTrackReconstruction.hh"

class G4LogicalVolume;
class G4Step;
class G4StepPoint;

namespace celeritas
{
namespace detail
{
//---------------------------------------------------------------------------//
/*!
 * Reconstruct a borrowed Geant4 step for sensitive detectors or user actions.
 *
 * This worker-local object owns the step points and touchables. Its call
 * operator updates selected fields and optionally a persistent track. Returned
 * objects are borrowed until the next call; construction and destruction must
 * occur on the owning Geant4 thread.
 */
class GeantStepReconstruction
{
  public:
    using SPConstVecLV
        = std::shared_ptr<std::vector<G4LogicalVolume const*> const>;
    using VecParticle = GeantTrackReconstruction::VecParticle;
    using StepPointBool = EnumArray<StepPoint, bool>;

    // Construct with selected fields and optional shared track metadata
    GeantStepReconstruction(VecParticle const&,
                            StepSelection const&,
                            StepPointBool const&,
                            SPConstVecLV = {},
                            std::shared_ptr<GeantTrackReconstruction> = {});

    // Reconstruct a single saved step
    G4Step* operator()(StepOutput const&, size_type) const;

    //! Access the shared metadata store
    std::shared_ptr<GeantTrackReconstruction> const& track_reconstruction() const
    {
        return track_reconstruction_;
    }

  private:
    SPConstVecLV detector_volumes_;
    StepSelection ss_;
    std::shared_ptr<G4Step> step_;
    std::shared_ptr<GeantTrackReconstruction> track_reconstruction_;
    EnumArray<StepPoint, G4StepPoint*> step_points_{{nullptr, nullptr}};
    EnumArray<StepPoint, G4TouchableHandle> touch_handle_;
    std::unique_ptr<TouchableUpdaterInterface> update_touchable_;
    bool step_post_status_{false};
};

//---------------------------------------------------------------------------//
}  // namespace detail
}  // namespace celeritas
