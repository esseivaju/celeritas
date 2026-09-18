//------------------------------- -*- C++ -*- -------------------------------//
// Copyright Celeritas contributors: see top-level COPYRIGHT file for details
// SPDX-License-Identifier: (Apache-2.0 OR MIT)
//---------------------------------------------------------------------------//
//! \file celeritas/ext/detail/GeantStepReconstruction.cc
//---------------------------------------------------------------------------//
#include "GeantStepReconstruction.hh"

#include <cstddef>
#include <utility>
#include <CLHEP/Units/SystemOfUnits.h>
#include <G4LogicalVolume.hh>
#include <G4Step.hh>
#include <G4StepPoint.hh>
#include <G4ThreeVector.hh>
#include <G4TouchableHandle.hh>
#include <G4TouchableHistory.hh>
#include <G4Track.hh>
#include <G4TransportationManager.hh>
#include <G4VSensitiveDetector.hh>
#include <G4Version.hh>

#include "corecel/Assert.hh"
#include "corecel/Types.hh"
#include "corecel/cont/EnumArray.hh"
#include "corecel/cont/Range.hh"
#include "corecel/io/Logger.hh"
#include "corecel/sys/ScopedProfiling.hh"
#include "corecel/sys/TraceCounter.hh"
#include "geocel/GeantGeoParams.hh"
#include "celeritas/Types.hh"
#include "celeritas/ext/GeantTrackReconstruction.hh"
#include "celeritas/user/DetectorSteps.hh"
#include "celeritas/user/StepData.hh"

#include "LevelTouchableUpdater.hh"
#include "../GeantStepPointView.hh"
#include "../GeantStepView.hh"

#define HP_ASSIGN_TRANSFORMED(SELECTION, VIEW, VAR, DATA, TRANSFORM, IDX) \
    if (SELECTION.VAR) \
    { \
        CELER_ASSERT(IDX < DATA.VAR.size()); \
        VIEW.VAR(TRANSFORM(DATA.VAR[IDX])); \
    }

namespace celeritas
{
namespace detail
{
namespace
{
//---------------------------------------------------------------------------//
/*!
 * Get the geometry step status when volume instance IDs are present.
 *
 * Note that this isn't entirely accurate if crossing from one
 * replica/parameterised region to another. For that we will need to map
 * distinct (instance id, replica number) to unique volume instances. Perhaps
 * that would be better done by using "touchables" globally and reconstructing
 * volume instances in post.
 */
G4StepStatus get_step_status(DetectorStepOutput const& out,
                             size_type step_index)
{
    auto pre = LevelTouchableUpdater::volume_instances(
        out, step_index, StepPoint::pre);
    auto post = LevelTouchableUpdater::volume_instances(
        out, step_index, StepPoint::post);
    for (auto i : range(out.num_volume_levels))
    {
        if (pre[i] != post[i])
        {
            // Volume instance changed
            if (i == 0 && post[i] == VolumeInstanceId{})
            {
                // Exited the geometry
                return G4StepStatus::fWorldBoundary;
            }
            // Changed volumes
            return G4StepStatus::fGeomBoundary;
        }
        if (!pre[i])
        {
            // Empty volume sentinel encountered: exit
            break;
        }
    }
    return G4StepStatus::fUserDefinedLimit;
}

//---------------------------------------------------------------------------//
}  // namespace

//---------------------------------------------------------------------------//
/*!
 * Construct local navigator and step data.
 */
GeantStepReconstruction::GeantStepReconstruction(
    VecParticle const& particles,
    StepSelection const& selection,
    StepPointBool const& locate_touchable,
    SPConstVecLV detector_volumes,
    std::shared_ptr<GeantTrackReconstruction> tracks)
    : detector_volumes_(std::move(detector_volumes))
    , ss_{selection}
    , step_post_status_{ss_.points[StepPoint::pre].volume_instance_ids
                        && ss_.points[StepPoint::post].volume_instance_ids}
{
    step_ = GeantTrackReconstruction::make_g4step();
    CELER_ASSERT(step_);
    if (tracks)
    {
        track_reconstruction_ = std::move(tracks);
    }
    else if (!particles.empty())
    {
        // Reconstruct track
        track_reconstruction_
            = std::make_shared<GeantTrackReconstruction>(particles, step_);
    }
    CELER_ASSERT(ss_.particle_id == static_cast<bool>(track_reconstruction_)
                 && ss_.primary_id == ss_.particle_id);

    GeantStepView step_view{*step_};

    for (auto sp : range(StepPoint::size_))
    {
        if (!ss_.points[sp])
        {
            step_view.delete_step_point(sp);
            CELER_ASSERT(!locate_touchable[sp]);
        }
        else
        {
            auto point_view = step_view.step_point(sp);
            point_view.clear_unsupported();
            step_points_[sp] = &point_view.step_point();
            if (locate_touchable[sp])
            {
                // Create touchable handle for this step point
                touch_handle_[sp] = new G4TouchableHistory;
                step_points_[sp]->SetTouchableHandle(touch_handle_[sp]);
                if (!update_touchable_)
                {
                    CELER_EXPECT(ss_.points[sp].volume_instance_ids);
                    // FIXME: pass geant geo into this constructor
                    auto ggeo = ::celeritas::global_geant_geo().lock();
                    CELER_ASSERT(ggeo);
                    update_touchable_ = std::make_unique<LevelTouchableUpdater>(
                        std::move(ggeo));
                }
            }
        }
    }
}

//---------------------------------------------------------------------------//
/*!
 * Reconstruct a step, returning null if its touchable cannot be located.
 */
G4Step* GeantStepReconstruction::operator()(StepOutput const& out,
                                            size_type i) const
{
    CELER_EXPECT(i < out.size());

    GeantStepView step_view{*step_};

#define HP_ASSIGN_STEP(VAR, TRANSFORM) \
    HP_ASSIGN_TRANSFORMED(ss_, step_view, VAR, out, TRANSFORM, i)

    HP_ASSIGN_STEP(energy_deposition, GeantStepView::Energy);
    HP_ASSIGN_STEP(step_length, native_value_to<GeantStepView::Length>);

#undef HP_ASSIGN_STEP

    for (auto sp : range(StepPoint::size_))
    {
        auto* g4sp = step_points_[sp];
        if (!g4sp)
        {
            continue;
        }

        if (auto& touch_handle = touch_handle_[sp])
        {
            CELER_ASSERT(update_touchable_);
            // Update navigation state
            bool success = (*update_touchable_)(out, i, sp, touch_handle());

            if (CELER_UNLIKELY(!success))
            {
                // Inconsistent touchable: skip this energy deposition
                CELER_LOG_LOCAL(error) << "Omitting energy deposition of "
                                       << step_view.energy_deposition();
                return nullptr;
            }
        }

        GeantStepPointView sp_view{*g4sp};

#define HP_ASSIGN_SP(VAR, TRANSFORM) \
    HP_ASSIGN_TRANSFORMED( \
        ss_.points[sp], sp_view, VAR, out.points[sp], TRANSFORM, i)

        HP_ASSIGN_SP(time, native_value_to<GeantStepPointView::Time>);
        HP_ASSIGN_SP(pos, native_value_to<GeantStepPointView::Length>);
        HP_ASSIGN_SP(energy, GeantStepPointView::Energy);
        HP_ASSIGN_SP(dir, static_array_cast<double>);
#undef HP_ASSIGN_SP

        // Celeritas weight does not currently change across a step
        HP_ASSIGN_TRANSFORMED(
            ss_, sp_view, weight, out, static_cast<G4double>, i);

        // Copy attributes from logical volume
        if (sp == StepPoint::pre && detector_volumes_)
        {
            G4LogicalVolume const* lv
                = (*detector_volumes_)[out.detector_id[i].get()];
            CELER_ASSERT(lv);
            // Use lv already known from the in-volume detector
            sp_view.update_from_volume(*lv);
        }
        else
        {
            // Look up LV from the touchable
            sp_view.update_from_volume();
        }
    }

    // Reconstruct tracks and IDs if particles were provided
    CELER_ASSERT(static_cast<bool>(track_reconstruction_)
                 == !out.particle_id.empty());
    if (track_reconstruction_)
    {
        CELER_ASSERT(i < out.particle_id.size());
        CELER_ASSERT(i < out.primary_id.size());
        // Get track corresponding to the particle type, and reload primary
        // data if possible
        G4Track& g4track = track_reconstruction_->view(
            out.particle_id[i],
            out.primary_id[i],
            out.track_id.empty() ? TrackId{} : out.track_id[i],
            out.parent_id.empty() ? TrackId{} : out.parent_id[i]);
        step_->SetTrack(&g4track);
        g4track.SetStep(step_.get());
        g4track.SetStepLength(step_->GetStepLength());
        if (!out.track_status.empty())
        {
            g4track.SetTrackStatus(out.track_status[i] == TrackStatus::killed
                                       ? fStopAndKill
                                       : fAlive);
        }
        if (track_reconstruction_->has_track_mapping()
            && !out.track_step_count.empty())
        {
            track_reconstruction_->advance(out.track_id[i],
                                           out.track_step_count[i],
                                           step_->GetStepLength());
        }
        // Copy step information to the corresponding track
        GeantStepView{*step_}.update_track();
    }

    if (step_post_status_)
    {
        // Update the post-step status based on the geometry instances
        auto* g4sp = step_->GetPostStepPoint();
        CELER_ASSERT(g4sp);
        g4sp->SetStepStatus(get_step_status(out, i));
    }

    return step_.get();
}

//---------------------------------------------------------------------------//
}  // namespace detail
}  // namespace celeritas
