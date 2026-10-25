//------------------------------- -*- C++ -*- -------------------------------//
// Copyright Celeritas contributors: see top-level COPYRIGHT file for details
// SPDX-License-Identifier: (Apache-2.0 OR MIT)
//---------------------------------------------------------------------------//
//! \file celeritas/optical/gen/detail/GeneratorCounterExecutors.hh
//---------------------------------------------------------------------------//
#pragma once

#include "corecel/Macros.hh"
#include "corecel/Types.hh"
#include "corecel/data/ObserverPtr.hh"
#include "corecel/math/Algorithms.hh"
#include "corecel/sys/ThreadId.hh"
#include "celeritas/phys/GeneratorCounters.hh"
#include "celeritas/track/CoreStateCounters.hh"

namespace celeritas
{
namespace optical
{
namespace detail
{
//---------------------------------------------------------------------------//
using GenCountersPtr = ObserverPtr<GeneratorStateCounters, MemSpace::native>;
using CoreCountersPtr = ObserverPtr<CoreStateCounters, MemSpace::native>;

//---------------------------------------------------------------------------//
/*!
 * Assign the current generator counters from a value sent from the host.
 */
struct AssignGeneratorCountersExecutor
{
    GenCountersPtr counters;
    GeneratorCounters<> value;

    CELER_FORCEINLINE_FUNCTION void operator()(ThreadId tid) const
    {
        CELER_EXPECT(tid == ThreadId{0});
        counters->current = value;
    }
};

//---------------------------------------------------------------------------//
/*!
 * Clear the completed generator counters after they are read on the host.
 */
struct ClearCompletedExecutor
{
    GenCountersPtr counters;

    CELER_FORCEINLINE_FUNCTION void operator()(ThreadId tid) const
    {
        CELER_EXPECT(tid == ThreadId{0});
        counters->completed = {};
    }
};

//---------------------------------------------------------------------------//
/*!
 * Set the number of pending photons from the total of a prefix sum.
 *
 * If the core state counters are given, the number of newly buffered photons
 * is also added to the core state's pending count.
 */
struct UpdatePendingExecutor
{
    GenCountersPtr counters;
    ObserverPtr<size_type const, MemSpace::native> total;
    CoreCountersPtr core_counters;

    CELER_FORCEINLINE_FUNCTION void operator()(ThreadId tid) const
    {
        CELER_EXPECT(tid == ThreadId{0});
        CELER_EXPECT(total);

        auto& current = counters->current;
        size_type num_pending = *total;
        CELER_ASSERT(num_pending >= current.num_pending);
        if (core_counters)
        {
            core_counters->num_pending += num_pending - current.num_pending;
        }
        current.num_pending = num_pending;
    }
};

//---------------------------------------------------------------------------//
/*!
 * Update the generator and core state counters after generating photons.
 *
 * The number of tracks generated in this step is the smaller of the number of
 * vacancies and the number of photons pending in the generator. When all the
 * pending photons have been generated, the buffered data is marked as
 * completed and the current counters are cleared.
 */
struct UpdateGeneratorCountersExecutor
{
    CoreCountersPtr core_counters;
    GenCountersPtr counters;
    size_type state_size{};

    CELER_FORCEINLINE_FUNCTION void operator()(ThreadId tid) const
    {
        CELER_EXPECT(tid == ThreadId{0});
        CELER_EXPECT(state_size >= core_counters->num_vacancies);

        auto& core = *core_counters;
        auto& current = counters->current;
        auto& completed = counters->completed;

        size_type num_gen = min(core.num_vacancies, current.num_pending);

        // Update the optical core state counters
        core.num_pending -= num_gen;
        core.num_generated += num_gen;
        core.num_vacancies -= num_gen;

        // Update the generator counters
        current.num_pending -= num_gen;
        current.num_generated += num_gen;
        completed.num_generated += num_gen;

        if (current.num_pending == 0)
        {
            // All buffered data has been consumed
            completed.buffer_size += current.buffer_size;
            current = {};
        }

        // Update the number of active tracks. This must be done even if no
        // new tracks were generated
        core.num_active = state_size - core.num_vacancies;
    }
};

//---------------------------------------------------------------------------//
}  // namespace detail
}  // namespace optical
}  // namespace celeritas
