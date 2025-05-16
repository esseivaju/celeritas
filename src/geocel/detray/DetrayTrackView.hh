//------------------------------- -*- C++ -*- -------------------------------//
// Copyright Celeritas contributors: see top-level COPYRIGHT file for details
// SPDX-License-Identifier: (Apache-2.0 OR MIT)
//---------------------------------------------------------------------------//
//! \file geocel/detray/DetrayTrackView.hh
//---------------------------------------------------------------------------//
#pragma once

#include <detray/navigation/navigator.hpp>

#include "corecel/Config.hh"

#include "corecel/Assert.hh"
#include "corecel/math/ArraySoftUnit.hh"
#include "corecel/sys/ThreadId.hh"
#include "geocel/Types.hh"

#include "DetrayData.hh"

namespace celeritas
{
//---------------------------------------------------------------------------//
/*!
 * Navigate through a Detray geometry on a single thread.
 */
class DetrayTrackView
{
  public:
    //!@{
    //! \name Type aliases
    using Initializer_t = GeoTrackInitializer;
    using ParamsRef = NativeCRef<DetrayParamsData>;
    using StateRef = NativeRef<DetrayStateData>;
    template<class Detector_t>
    using Navigator = void;
    //!@}

    //! Helper struct for initializing from an existing geometry state
    struct DetailedInitializer
    {
        DetrayTrackView const& other;  //!< Existing geometry
        Real3 const& dir;  //!< New direction
    };

    inline CELER_FUNCTION DetrayTrackView(ParamsRef const& data,
                                          StateRef const& stateview,
                                          TrackSlotId tid);
    // Initialize the state
    inline CELER_FUNCTION DetrayTrackView& operator=(Initializer_t const& init);
    // Initialize the state from a parent state and new direction
    inline CELER_FUNCTION DetrayTrackView&
    operator=(DetailedInitializer const& init);

    //// STATIC ACCESSORS ////

    //! A tiny push to make sure tracks do not get stuck at boundaries
    static CELER_CONSTEXPR_FUNCTION real_type extra_push() { return 1e-13; }

    //// ACCESSORS ////

    //!@{
    //! State accessors
    CELER_FORCEINLINE_FUNCTION Real3 const& pos() const { return pos_; }
    CELER_FORCEINLINE_FUNCTION Real3 const& dir() const { return dir_; }
    //!@}

    // Get the current volume's ID
    inline CELER_FUNCTION VolumeId volume_id() const
    {
        CELER_NOT_IMPLEMENTED("TODO");
    }
    // Get the ID of the current volume instance
    inline CELER_FUNCTION VolumeInstanceId volume_instance_id() const
    {
        CELER_NOT_IMPLEMENTED("TODO");
    }
    // Get the depth in the geometry hierarchy
    inline CELER_FUNCTION LevelId level() const
    {
        CELER_NOT_IMPLEMENTED("TODO");
    }
    // Get the volume instance ID for all levels
    inline CELER_FUNCTION void volume_instance_id(Span<VolumeInstanceId>) const
    {
        CELER_NOT_IMPLEMENTED("TODO");
    }

    //!@{
    //! VecGeom states are never "on" a surface
    CELER_FUNCTION SurfaceId surface_id() const { return {}; }
    //!@}

    // Whether the track is outside the valid geometry region
    CELER_FORCEINLINE_FUNCTION bool is_outside() const
    {
        CELER_NOT_IMPLEMENTED("TODO");
    }
    // Whether the track is exactly on a surface
    CELER_FORCEINLINE_FUNCTION bool is_on_boundary() const
    {
        CELER_NOT_IMPLEMENTED("TODO");
    }
    //! Whether the last operation resulted in an error
    CELER_FORCEINLINE_FUNCTION bool failed() const { return false; }

    //// OPERATIONS ////

    // Find the distance to the next boundary (infinite max)
    inline CELER_FUNCTION Propagation find_next_step()
    {
        CELER_NOT_IMPLEMENTED("TODO");
    }

    // Find the distance to the next boundary, up to and including a step
    inline CELER_FUNCTION Propagation find_next_step(real_type)
    {
        CELER_NOT_IMPLEMENTED("TODO");
    }

    // Find the safety at the current position (infinite max)
    inline CELER_FUNCTION real_type find_safety()
    {
        CELER_NOT_IMPLEMENTED("TODO");
    }

    // Find the safety at the current position up to a maximum step distance
    inline CELER_FUNCTION real_type find_safety(real_type)
    {
        CELER_NOT_IMPLEMENTED("TODO");
    }

    // Move to the boundary in preparation for crossing it
    inline CELER_FUNCTION void move_to_boundary()
    {
        CELER_NOT_IMPLEMENTED("TODO");
    }

    // Move within the volume
    inline CELER_FUNCTION void move_internal(real_type)
    {
        CELER_NOT_IMPLEMENTED("TODO");
    }

    // Move within the volume to a specific point
    inline CELER_FUNCTION void move_internal(Real3 const&)
    {
        CELER_NOT_IMPLEMENTED("TODO");
    }

    // Cross from one side of the current surface to the other
    inline CELER_FUNCTION void cross_boundary()
    {
        CELER_NOT_IMPLEMENTED("TODO");
    }

    // Change direction
    inline CELER_FUNCTION void set_dir(Real3 const&)
    {
        CELER_NOT_IMPLEMENTED("TODO");
    }

  private:
    // TODO: implement all operations
    [[maybe_unused]] ParamsRef const& params_;
    Real3& pos_;
    Real3& dir_;
};

//---------------------------------------------------------------------------//
// INLINE DEFINITIONS
//---------------------------------------------------------------------------//
/*!
 * Construct from persistent and state data.
 */
CELER_FUNCTION
DetrayTrackView::DetrayTrackView(ParamsRef const& params,
                                 StateRef const& states,
                                 TrackSlotId tid)
    : params_(params), pos_(states.pos[tid]), dir_(states.dir[tid])
{
}

//---------------------------------------------------------------------------//
/*!
 * Construct the state.
 *
 * Expensive. This function should only be called to initialize an event from a
 * starting location and direction, but excess secondaries will also be
 * initialized this way.
 */
CELER_FUNCTION DetrayTrackView&
DetrayTrackView::operator=(Initializer_t const& init)
{
    CELER_EXPECT(is_soft_unit_vector(init.dir));

    // Initialize position/direction
    pos_ = init.pos;
    dir_ = init.dir;
    return *this;
}

//---------------------------------------------------------------------------//
/*!
 * Construct the state from a direction and a copy of the parent state.
 *
 * This is a faster method of creating secondaries from a parent that has just
 * been absorbed, or when filling in an empty track from a parent that is still
 * alive.
 */
CELER_FUNCTION
DetrayTrackView& DetrayTrackView::operator=(DetailedInitializer const& init)
{
    CELER_EXPECT(is_soft_unit_vector(init.dir));

    if (this != &init.other)
    {
        // Copy the navigation state and position from the parent state
        pos_ = init.other.pos_;
    }

    // Set up the next state and initialize the direction
    dir_ = init.dir;

    return *this;
}

//---------------------------------------------------------------------------//
}  // namespace celeritas
