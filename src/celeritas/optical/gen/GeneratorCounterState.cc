//------------------------------- -*- C++ -*- -------------------------------//
// Copyright Celeritas contributors: see top-level COPYRIGHT file for details
// SPDX-License-Identifier: (Apache-2.0 OR MIT)
//---------------------------------------------------------------------------//
//! \file celeritas/optical/gen/GeneratorCounterState.cc
//---------------------------------------------------------------------------//
#include "GeneratorCounterState.hh"

#include "corecel/Assert.hh"
#include "corecel/data/CollectionBuilder.hh"
#include "corecel/sys/Device.hh"

#include "detail/GeneratorCounterAlgorithms.hh"

namespace celeritas
{
namespace optical
{
//---------------------------------------------------------------------------//
/*!
 * Construct with the stream used to update the counters.
 */
template<MemSpace M>
GeneratorCounterState<M>::GeneratorCounterState(StreamId stream)
    : stream_{stream}
{
    resize(&counters_, 1);
    detail::assign_counters(this->device_counters(), CountersT{}, stream_);
    detail::clear_completed(this->device_counters(), stream_);
    if constexpr (M == MemSpace::device)
    {
        copied_ = DeviceEvent{celeritas::device()};
    }
}

//---------------------------------------------------------------------------//
/*!
 * Set the current counters.
 *
 * This updates the host copy immediately and enqueues the update of the
 * state's copy. The buffer bound is set to the new buffer size.
 */
template<MemSpace M>
void GeneratorCounterState<M>::assign(CountersT const& value)
{
    detail::assign_counters(this->device_counters(), value, stream_);
    this->counters = value;
    bound_ = value.buffer_size;
    growth_ = 0;
    copy_pending_ = false;
}

//---------------------------------------------------------------------------//
/*!
 * Get a conservative upper bound on the current buffer size.
 *
 * If a copy enqueued by \c copy_async has completed, the bound is tightened
 * using the copied buffer size and the growth since the copy was enqueued.
 */
template<MemSpace M>
size_type GeneratorCounterState<M>::buffer_bound()
{
    if (copy_pending_ && copied_.ready())
    {
        bound_ = snapshot_.value().current.buffer_size + growth_;
        copy_pending_ = false;
    }
    return bound_;
}

//---------------------------------------------------------------------------//
/*!
 * Increase the buffer bound by the maximum number of items added.
 */
template<MemSpace M>
void GeneratorCounterState<M>::grow_bound(size_type count)
{
    bound_ += count;
    growth_ += count;
}

//---------------------------------------------------------------------------//
/*!
 * Enqueue a copy of the counters used to tighten the buffer bound.
 *
 * This should be called after all the kernels that modify the buffer size in
 * a step have been enqueued. A new copy is not enqueued while a previous one
 * is still pending, since the host could not use it any earlier.
 */
template<MemSpace M>
void GeneratorCounterState<M>::copy_async()
{
    if (copy_pending_ && !copied_.ready())
    {
        return;
    }
    snapshot_.copy_async(this->device_counters(), stream_);
    if constexpr (M == MemSpace::device)
    {
        copied_.record(celeritas::device().stream(stream_));
    }
    copy_pending_ = true;
    growth_ = 0;
}

//---------------------------------------------------------------------------//
/*!
 * Synchronize and update the host counters and statistics.
 *
 * The completed counts are added to the accumulated statistics and cleared.
 */
template<MemSpace M>
void GeneratorCounterState<M>::sync_counters()
{
    snapshot_.copy_async(this->device_counters(), stream_);
    if constexpr (M == MemSpace::device)
    {
        copied_.record(celeritas::device().stream(stream_));
        copied_.sync();
    }
    auto const& value = snapshot_.value();
    this->counters = value.current;
    this->accum.buffer_size += value.completed.buffer_size;
    this->accum.num_generated += value.completed.num_generated;
    detail::clear_completed(this->device_counters(), stream_);

    bound_ = value.current.buffer_size;
    growth_ = 0;
    copy_pending_ = false;
}

//---------------------------------------------------------------------------//
/*!
 * Clear the current counters after the loop aborted early.
 */
template<MemSpace M>
void GeneratorCounterState<M>::reset_counters()
{
    this->assign(CountersT{});
}

//---------------------------------------------------------------------------//
// EXPLICIT INSTANTIATION
//---------------------------------------------------------------------------//

template class GeneratorCounterState<MemSpace::host>;
template class GeneratorCounterState<MemSpace::device>;

//---------------------------------------------------------------------------//
}  // namespace optical
}  // namespace celeritas
