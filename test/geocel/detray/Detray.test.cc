//------------------------------- -*- C++ -*- -------------------------------//
// Copyright Celeritas contributors: see top-level COPYRIGHT file for details
// SPDX-License-Identifier: (Apache-2.0 OR MIT)
//---------------------------------------------------------------------------//
//! \file geocel/detray/Detray.test.cc
//---------------------------------------------------------------------------//

#include "corecel/Config.hh"

#include "geocel/GenericGeoParameterizedTest.hh"
#include "geocel/GeoTests.hh"
#include "geocel/detray/DetrayTestBase.hh"

#include "celeritas_test.hh"
#include "detray/geometry/tracking_surface.hpp"

namespace celeritas
{
namespace test
{
//---------------------------------------------------------------------------//

class TwoBoxesDetrayTest
    : public GenericGeoParameterizedTest<DetrayTestBase, TwoBoxesGeoTest>
{
};

TEST_F(TwoBoxesDetrayTest, accessors)
{
    this->impl().test_accessors();
}

TEST_F(TwoBoxesDetrayTest, track)
{
    // Templated test
    TwoBoxesGeoTest::test_detailed_tracking(this);
}

//---------------------------------------------------------------------------//
}  // namespace test
}  // namespace celeritas
