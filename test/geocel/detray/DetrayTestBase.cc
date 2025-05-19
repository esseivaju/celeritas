//------------------------------- -*- C++ -*- -------------------------------//
// Copyright Celeritas contributors: see top-level COPYRIGHT file for details
// SPDX-License-Identifier: (Apache-2.0 OR MIT)
//---------------------------------------------------------------------------//
//! \file geocel/detray/DetrayTestBase.cc
//---------------------------------------------------------------------------//
#include "DetrayTestBase.hh"

#include "geocel/CheckedGeoTrackView.t.hh"
#include "geocel/GenericGeoTestBase.t.hh"
#include "geocel/detray/DetrayData.hh"
#include "geocel/detray/DetrayParams.hh"
#include "geocel/detray/DetrayTrackView.hh"

namespace celeritas
{
namespace test
{
//---------------------------------------------------------------------------//
template class CheckedGeoTrackView<DetrayTrackView>;
template class GenericGeoTestBase<DetrayParams>;

//---------------------------------------------------------------------------//
}  // namespace test
}  // namespace celeritas
