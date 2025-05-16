//------------------------------- -*- C++ -*- -------------------------------//
// Copyright Celeritas contributors: see top-level COPYRIGHT file for details
// SPDX-License-Identifier: (Apache-2.0 OR MIT)
//---------------------------------------------------------------------------//
//! \file geocel/detray/DetrayParams.hh
//---------------------------------------------------------------------------//
#pragma once

#include "corecel/Assert.hh"
#include "corecel/data/ParamsDataInterface.hh"
#include "geocel/GeoParamsInterface.hh"
#include "geocel/detray/DetrayData.hh"
namespace celeritas
{
//---------------------------------------------------------------------------//
class DetrayParams final : public GeoParamsInterface,
                           public ParamsDataInterface<DetrayParamsData>
{
  public:
    // Construct from a GDML filename
    explicit DetrayParams(std::string const&)
    {
        CELER_NOT_IMPLEMENTED("TODO");
    }
    // Anchor virtual destructor
    ~DetrayParams() final = default;

    //! Whether safety distance calculations are accurate and precise
    bool supports_safety() const final { CELER_NOT_IMPLEMENTED("TODO"); }

    //! Outer bounding box of geometry
    BBox const& bbox() const final { CELER_NOT_IMPLEMENTED("TODO"); }

    //! Maximum nested volume instance depth
    LevelId::size_type max_depth() const final
    {
        CELER_NOT_IMPLEMENTED("TODO");
    }

    //// VOLUMES ////

    //! Get volume metadata
    VolumeMap const& volumes() const final { CELER_NOT_IMPLEMENTED("TODO"); }

    //! Get volume instance metadata
    VolInstanceMap const& volume_instances() const final
    {
        CELER_NOT_IMPLEMENTED("TODO");
    }

    //! Get the volume ID corresponding to a Geant4 logical volume
    VolumeId find_volume(G4LogicalVolume const*) const final
    {
        CELER_NOT_IMPLEMENTED("TODO");
    }

    //! Get the Geant4 PV corresponding to a volume instance
    GeantPhysicalInstance id_to_geant(VolumeInstanceId) const final
    {
        CELER_NOT_IMPLEMENTED("TODO");
    }
    //! Reference CPU geometry data
    HostRef const& host_ref() const final { return host_ref_; }

    //! Reference managed GPU geometry data
    DeviceRef const& device_ref() const final { return device_ref_; }

  private:
    // Host/device storage and reference
    HostRef host_ref_;
    DeviceRef device_ref_;
};
//---------------------------------------------------------------------------//
}  // namespace celeritas
