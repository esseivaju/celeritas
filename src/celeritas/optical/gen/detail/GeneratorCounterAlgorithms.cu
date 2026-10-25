//------------------------------ -*- cuda -*- -------------------------------//
// Copyright Celeritas contributors: see top-level COPYRIGHT file for details
// SPDX-License-Identifier: (Apache-2.0 OR MIT)
//---------------------------------------------------------------------------//
//! \file celeritas/optical/gen/detail/GeneratorCounterAlgorithms.cu
//---------------------------------------------------------------------------//
#include "GeneratorCounterAlgorithms.hh"

#include "corecel/Assert.hh"
#include "corecel/sys/KernelLauncher.device.hh"

#include "GeneratorCounterExecutors.hh"

namespace celeritas
{
namespace optical
{
namespace detail
{
namespace
{
using DeviceGenPtr = GenCountersObserver<MemSpace::device>;
}  // namespace

//---------------------------------------------------------------------------//
void assign_counters(DeviceGenPtr counters,
                     GeneratorCounters<> const& value,
                     StreamId stream_id)
{
    CELER_EXPECT(counters);
    AssignGeneratorCountersExecutor execute_thread{counters, value};
    static KernelLauncher<decltype(execute_thread)> const launch_kernel(
        "assign-generator-counters");
    launch_kernel(1, stream_id, execute_thread);
}

//---------------------------------------------------------------------------//
void clear_completed(DeviceGenPtr counters, StreamId stream_id)
{
    CELER_EXPECT(counters);
    ClearCompletedExecutor execute_thread{counters};
    static KernelLauncher<decltype(execute_thread)> const launch_kernel(
        "clear-generator-completed");
    launch_kernel(1, stream_id, execute_thread);
}

//---------------------------------------------------------------------------//
void update_pending(DeviceGenPtr counters,
                    ObserverPtr<size_type const, MemSpace::device> total,
                    CoreStateCounterRef<MemSpace::device> const& core_counters,
                    StreamId stream_id)
{
    CELER_EXPECT(counters);
    CELER_EXPECT(total);
    UpdatePendingExecutor execute_thread{
        counters,
        total,
        core_counters.empty() ? CoreCountersPtr{} : core_counters.data()};
    static KernelLauncher<decltype(execute_thread)> const launch_kernel(
        "update-generator-pending");
    launch_kernel(1, stream_id, execute_thread);
}

//---------------------------------------------------------------------------//
void update_counters(CoreStateCounterRef<MemSpace::device> const& core_counters,
                     DeviceGenPtr counters,
                     size_type state_size,
                     StreamId stream_id)
{
    CELER_EXPECT(core_counters.size() == 1);
    CELER_EXPECT(counters);
    UpdateGeneratorCountersExecutor execute_thread{
        core_counters.data(), counters, state_size};
    static KernelLauncher<decltype(execute_thread)> const launch_kernel(
        "update-generator-counters");
    launch_kernel(1, stream_id, execute_thread);
}

//---------------------------------------------------------------------------//
}  // namespace detail
}  // namespace optical
}  // namespace celeritas
