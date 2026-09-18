//------------------------------ -*- cuda -*- -------------------------------//
// Copyright Celeritas contributors: see top-level COPYRIGHT file for details
// SPDX-License-Identifier: (Apache-2.0 OR MIT)
//---------------------------------------------------------------------------//
//! \file celeritas/user/DetectorSteps.cu
//---------------------------------------------------------------------------//
#include "DetectorSteps.hh"

#include <thrust/copy.h>
#include <thrust/device_ptr.h>
#include <thrust/execution_policy.h>
#include <thrust/iterator/counting_iterator.h>

#include "corecel/data/Collection.hh"
#include "corecel/data/Copier.hh"
#include "corecel/data/ObserverPtr.device.hh"
#include "corecel/sys/Device.hh"
#include "corecel/sys/KernelLauncher.device.hh"
#include "corecel/sys/KernelParamCalculator.device.hh"
#include "corecel/sys/ScopedProfiling.hh"
#include "corecel/sys/Stream.hh"
#include "corecel/sys/Thrust.device.hh"

#include "StepData.hh"

#include "detail/StepScratchCopyExecutor.hh"
#include "detail/VisitStepFields.hh"

using namespace celeritas::literals;

namespace celeritas
{
namespace
{
//---------------------------------------------------------------------------//
template<class T>
using StateRef
    = celeritas::StateCollection<T, Ownership::reference, MemSpace::native>;

template<class T>
using ItemRef
    = celeritas::Collection<T, Ownership::reference, MemSpace::native>;

//---------------------------------------------------------------------------//
struct HasId
{
    template<class Id>
    CELER_FORCEINLINE_FUNCTION bool operator()(Id const& d) const
    {
        return static_cast<bool>(d);
    }
};

//---------------------------------------------------------------------------//
size_type count_num_valid(
    StepStateData<Ownership::reference, MemSpace::device> const& state)
{
    // Store the thread IDs of active tracks that are in a detector
    auto start = device_pointer_cast(state.valid_id.data());
    auto copy_ids = [&](auto const& mask) {
        return thrust::copy_if(thrust_execute_on(state.stream_id),
                               thrust::make_counting_iterator(0_sz),
                               thrust::make_counting_iterator(state.size()),
                               device_pointer_cast(mask.data()),
                               start,
                               HasId{});
    };
    auto end = state.data.detector_id.empty()
                   ? copy_ids(state.data.track_id)
                   : copy_ids(state.data.detector_id);
    return end - start;
}

//---------------------------------------------------------------------------//
template<class T>
void copy_field(DetectorStepOutput::PinnedVec<T>* dst,
                StateRef<T> const& src,
                size_type num_valid,
                size_type,
                StreamId stream)
{
    if (src.empty() || num_valid == 0)
    {
        // This field is not in use or had no hits
        dst->clear();
        return;
    }
    dst->resize(num_valid);
    // Copy all items from valid threads
    Copier<T, MemSpace::host> copy{{dst->data(), num_valid}, stream};
    copy(MemSpace::device, {src.data().get(), num_valid});
}

//---------------------------------------------------------------------------//
template<class T>
void copy_field(DetectorStepOutput::PinnedVec<T>* dst,
                ItemRef<T> const& src,
                size_type num_valid,
                size_type per_thread,
                StreamId stream)
{
    CELER_EXPECT(per_thread > 0 || src.empty());
    if (src.empty() || num_valid == 0)
    {
        // This attribute is not in use
        dst->clear();
        return;
    }
    dst->resize(num_valid * per_thread);
    // Copy all items from valid threads
    Copier<T, MemSpace::host> copy{{dst->data(), num_valid * per_thread},
                                   stream};
    copy(MemSpace::device, {src.data().get(), num_valid * per_thread});
}

//---------------------------------------------------------------------------//
}  // namespace

//---------------------------------------------------------------------------//
/*!
 * Copy to host results from tracks that interacted with a detector.
 */
template<>
void copy_steps<MemSpace::device>(
    DetectorStepOutput* output,
    StepStateData<Ownership::reference, MemSpace::device> const& state)
{
    CELER_EXPECT(output);

    ScopedProfiling profile_this{"copy-steps"};

    // Get the number of threads that are active and in a detector
    size_type const num_valid = count_num_valid(state);

    // Gather the step data on device
    {
        auto execute_thread = detail::StepScratchCopyExecutor{state, num_valid};
        static KernelLauncher<decltype(execute_thread)> const launch_kernel(
            "gather-step-scratch");
        launch_kernel(num_valid, state.stream_id, execute_thread);
    }

    detail::visit_step_fields(
        *output,
        state.scratch,
        state.num_volume_levels,
        [&](auto& dst, auto const& src, size_type width) {
            copy_field(&dst, src, num_valid, width, state.stream_id);
        });

    output->num_volume_levels = state.num_volume_levels;

    // Copies must be complete before returning
    CELER_DEVICE_API_CALL(
        StreamSynchronize(celeritas::device().stream(state.stream_id).get()));

    CELER_ENSURE(output->detector_id.empty()
                 || output->detector_id.size() == num_valid);
    CELER_ENSURE(output->track_id.size() == num_valid);
}

//---------------------------------------------------------------------------//
}  // namespace celeritas
