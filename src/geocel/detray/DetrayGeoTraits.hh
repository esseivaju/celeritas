//------------------------------- -*- C++ -*- -------------------------------//
// Copyright Celeritas contributors: see top-level COPYRIGHT file for details
// SPDX-License-Identifier: (Apache-2.0 OR MIT)
//---------------------------------------------------------------------------//
//! \file geocel/detray/DetrayGeoTraits.hh
//---------------------------------------------------------------------------//
#pragma once

#include "corecel/Config.hh"

#include "geocel/GeoTraits.hh"

namespace celeritas
{
//---------------------------------------------------------------------------//
class DetrayParams;
class DetrayTrackView;
template<Ownership W, MemSpace M>
struct DetrayParamsData;
template<Ownership W, MemSpace M>
struct DetrayStateData;

#if CELERITAS_USE_DETRAY
//---------------------------------------------------------------------------//
/*!
 * Traits specialization for Detray geometry.
 */
template<>
struct GeoTraits<DetrayParams>
{
    //! Params data used during runtime
    template<Ownership W, MemSpace M>
    using ParamsData = DetrayParamsData<W, M>;

    //! State data used during runtime
    template<Ownership W, MemSpace M>
    using StateData = DetrayStateData<W, M>;

    //! Geometry track view
    using TrackView = DetrayTrackView;

    //! Descriptive name for the geometry
    static constexpr char const name[] = "detray";

    //! TO BE REMOVED: "native" file extension for this geometry
    static constexpr char const ext[] = ".detray";
};
#else
//! Detray is unavailable
template<>
struct GeoTraits<DetrayParams> : NotConfiguredGeoTraits
{
};
#endif

//---------------------------------------------------------------------------//
}  // namespace celeritas
