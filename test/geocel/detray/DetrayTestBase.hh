//------------------------------- -*- C++ -*- -------------------------------//
// Copyright Celeritas contributors: see top-level COPYRIGHT file for details
// SPDX-License-Identifier: (Apache-2.0 OR MIT)
//---------------------------------------------------------------------------//
//! \file geocel/detray/DetrayTestBase.hh
//---------------------------------------------------------------------------//
#pragma once

#include "geocel/GenericGeoTestBase.hh"
#include "geocel/detray/DetrayData.hh"
#include "geocel/detray/DetrayGeoTraits.hh"
#include "geocel/detray/DetrayParams.hh"
#include "geocel/detray/DetrayTrackView.hh"

namespace celeritas
{
namespace test
{
//---------------------------------------------------------------------------//
using DetrayTestBase = GenericGeoTestBase<DetrayParams>;

//---------------------------------------------------------------------------//
}  // namespace test
}  // namespace celeritas
