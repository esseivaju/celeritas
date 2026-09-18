//------------------------------- -*- C++ -*- -------------------------------//
// Copyright Celeritas contributors: see top-level COPYRIGHT file for details
// SPDX-License-Identifier: (Apache-2.0 OR MIT)
//---------------------------------------------------------------------------//
//! \file celeritas/user/detail/VisitStepFields.hh
//! \sa ../DetectorSteps.test.cc
//---------------------------------------------------------------------------//
#pragma once

#include "corecel/cont/Range.hh"
#include "celeritas/Types.hh"

namespace celeritas
{
namespace detail
{
//---------------------------------------------------------------------------//
/*!
 * Visit matching step fields with the number of items per track slot.
 *
 * Both arguments can be step output or step state data. Keeping traversal in
 * one place ensures synchronous gathering and asynchronous snapshots preserve
 * the same fields, including flattened volume hierarchies.
 */
template<class D, class S, class F>
void visit_step_fields(D& dst, S const& src, size_type levels, F&& visit)
{
#define CELER_VISIT_STEP(FIELD) visit(dst.FIELD, src.FIELD, size_type{1})
    CELER_VISIT_STEP(detector_id);
    CELER_VISIT_STEP(track_id);
    for (auto sp : range(StepPoint::size_))
    {
        CELER_VISIT_STEP(points[sp].time);
        CELER_VISIT_STEP(points[sp].pos);
        CELER_VISIT_STEP(points[sp].dir);
        CELER_VISIT_STEP(points[sp].energy);
        CELER_VISIT_STEP(points[sp].volume_id);
        visit(dst.points[sp].volume_instance_ids,
              src.points[sp].volume_instance_ids,
              levels);
    }
    CELER_VISIT_STEP(event_id);
    CELER_VISIT_STEP(parent_id);
    CELER_VISIT_STEP(primary_id);
    CELER_VISIT_STEP(post_step_action_id);
    CELER_VISIT_STEP(track_step_count);
    CELER_VISIT_STEP(step_length);
    CELER_VISIT_STEP(weight);
    CELER_VISIT_STEP(particle_id);
    CELER_VISIT_STEP(energy_deposition);
    CELER_VISIT_STEP(track_status);
#undef CELER_VISIT_STEP
}

//---------------------------------------------------------------------------//
}  // namespace detail
}  // namespace celeritas
