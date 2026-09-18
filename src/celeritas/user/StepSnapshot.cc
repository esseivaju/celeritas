//------------------------------- -*- C++ -*- -------------------------------//
// Copyright Celeritas contributors: see top-level COPYRIGHT file for details
// SPDX-License-Identifier: (Apache-2.0 OR MIT)
//---------------------------------------------------------------------------//
//! \file celeritas/user/StepSnapshot.cc
//---------------------------------------------------------------------------//
#include "StepSnapshot.hh"

#include "corecel/data/Copier.hh"
#include "corecel/data/Ref.hh"

#include "detail/VisitStepFields.hh"

namespace celeritas
{
//---------------------------------------------------------------------------//
/*!
 * Allocate selected arrays before stepping, using the complete slot capacity.
 */
template<MemSpace M>
void StepSnapshot::initialize(
    StepStateData<Ownership::reference, M> const& state)
{
    CELER_VALIDATE(!pending_, << "cannot resize an undelivered step snapshot");
    capacity_ = state.size();
    output_.num_volume_levels = state.num_volume_levels;
    valid_slots_.reserve(capacity_);
    buffer_size_ = 0;
    detail::visit_step_fields(
        output_,
        state.data,
        state.num_volume_levels,
        [this](auto& dst, auto const& src, size_type) {
            dst.resize(src.size());
            buffer_size_
                += dst.capacity()
                   * sizeof(typename std::decay_t<decltype(dst)>::value_type);
        });
}

//---------------------------------------------------------------------------//
/*!
 * Enqueue a full snapshot without waiting or allocating pinned memory.
 */
template<MemSpace M>
void StepSnapshot::capture(StepStateData<Ownership::reference, M> const& state)
{
    CELER_VALIDATE(!pending_, << "undelivered step snapshot");
    CELER_VALIDATE(capacity_ > 0 && state.size() == capacity_
                       && state.num_volume_levels == output_.num_volume_levels,
                   << "step snapshot was not initialized for this state");
    detail::visit_step_fields(
        output_,
        state.data,
        state.num_volume_levels,
        [](auto& dst, auto const& src, size_type) {
            CELER_VALIDATE(
                src.size() <= dst.capacity(),
                << "step snapshot selection changed during transport");
            dst.resize(src.size());
        });
    pending_ = true;
    compacted_ = false;
    detail::visit_step_fields(
        output_,
        state.data,
        state.num_volume_levels,
        [&state](auto& dst, auto const& src, size_type) {
            if (!src.empty())
            {
                using T = typename std::decay_t<decltype(dst)>::value_type;
                Copier<T, MemSpace::host>{make_span(dst), state.stream_id}(
                    M, src[AllItems<T, M>{}]);
            }
        });
}

//---------------------------------------------------------------------------//
/*!
 * Filter the completed snapshot in place, preserving track-slot order.
 */
StepOutput const& StepSnapshot::complete()
{
    CELER_VALIDATE(pending_, << "missing completed-step snapshot");
    if (!compacted_)
    {
        valid_slots_.clear();
        for (auto i : range(output_.track_id.size()))
        {
            if (output_.track_id[i]
                && (output_.detector_id.empty() || output_.detector_id[i]))
            {
                valid_slots_.push_back(i);
            }
        }
        detail::visit_step_fields(
            output_,
            output_,
            output_.num_volume_levels,
            [this](auto& dst, auto const&, size_type width) {
                if (dst.empty())
                    return;
                size_type offset = 0;
                for (auto slot : valid_slots_)
                {
                    for (auto j : range(width))
                        dst[offset++] = dst[slot * width + j];
                }
                dst.resize(offset);
            });
        compacted_ = true;
    }
    return output_;
}

//---------------------------------------------------------------------------//
/*!
 * Release a delivered snapshot for reuse without freeing its storage.
 */
void StepSnapshot::clear()
{
    CELER_VALIDATE(pending_ && compacted_,
                   << "cannot clear an incomplete step snapshot");
    pending_ = false;
}

//---------------------------------------------------------------------------//
template void StepSnapshot::initialize(HostRef<StepStateData> const&);
template void StepSnapshot::initialize(DeviceRef<StepStateData> const&);
template void StepSnapshot::capture(HostRef<StepStateData> const&);
template void StepSnapshot::capture(DeviceRef<StepStateData> const&);

//---------------------------------------------------------------------------//
}  // namespace celeritas
