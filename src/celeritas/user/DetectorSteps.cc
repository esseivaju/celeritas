//------------------------------- -*- C++ -*- -------------------------------//
// Copyright Celeritas contributors: see top-level COPYRIGHT file for details
// SPDX-License-Identifier: (Apache-2.0 OR MIT)
//---------------------------------------------------------------------------//
//! \file celeritas/user/DetectorSteps.cc
//---------------------------------------------------------------------------//
#include "DetectorSteps.hh"

#include "corecel/Assert.hh"
#include "corecel/cont/Range.hh"
#include "corecel/data/Collection.hh"
#include "corecel/sys/ScopedProfiling.hh"

#include "StepData.hh"

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
template<class Id>
size_type count_num_valid(StateRef<Id> const& detector)
{
    size_type size{0};
    for (Id id : detector[AllItems<Id>{}])
    {
        if (id)
        {
            ++size;
        }
    }
    return size;
}

//---------------------------------------------------------------------------//
template<class T, class Id>
void assign_field(DetectorStepOutput::PinnedVec<T>* dst,
                  StateRef<T> const& src,
                  StateRef<Id> const& detector,
                  size_type size)

{
    if (src.empty())
    {
        // This attribute is not in use
        dst->clear();
        return;
    }

    // Copy all items from valid threads
    dst->resize(size);

    auto iter = dst->begin();
    for (TrackSlotId tid : range(TrackSlotId{detector.size()}))
    {
        if (detector[tid])
        {
            *iter++ = src[tid];
        }
    }
    CELER_ASSERT(iter == dst->end());
}

//---------------------------------------------------------------------------//
template<class T, class Id>
void assign_field(DetectorStepOutput::PinnedVec<T>* dst,
                  ItemRef<T> const& src,
                  StateRef<Id> const& detector,
                  size_type size,
                  size_type per_thread)

{
    if (src.empty())
    {
        // This attribute is not in use
        dst->clear();
        return;
    }

    // Copy all items from valid threads
    dst->resize(size * per_thread);

    auto iter = dst->begin();
    for (TrackSlotId tid : range(TrackSlotId{detector.size()}))
    {
        if (detector[tid])
        {
            for (size_type i = 0; i != per_thread; ++i)
            {
                *iter++ = src[ItemId<T>{per_thread * tid.unchecked_get() + i}];
            }
        }
    }
    CELER_ASSERT(iter == dst->end());
}

//---------------------------------------------------------------------------//
}  // namespace

//---------------------------------------------------------------------------//
/*!
 * Consolidate results from tracks that interacted with a detector.
 */
template<>
void copy_steps<MemSpace::host>(
    DetectorStepOutput* output,
    StepStateData<Ownership::reference, MemSpace::host> const& state)
{
    CELER_EXPECT(output);

    ScopedProfiling profile_this{"copy-steps"};

    // Use detector validity when configured, otherwise track validity.
    auto copy_selected = [&](auto const& mask) {
        size_type size = count_num_valid(mask);

        // Resize and copy if the fields are present
#define DS_ASSIGN(FIELD) \
    assign_field(&(output->FIELD), state.data.FIELD, mask, size)

        DS_ASSIGN(detector_id);
        DS_ASSIGN(track_id);

        for (auto sp : range(StepPoint::size_))
        {
            DS_ASSIGN(points[sp].time);
            DS_ASSIGN(points[sp].pos);
            DS_ASSIGN(points[sp].dir);
            DS_ASSIGN(points[sp].energy);
            DS_ASSIGN(points[sp].volume_id);
            if (state.num_volume_levels > 0)
            {
                assign_field(&(output->points[sp].volume_instance_ids),
                             state.data.points[sp].volume_instance_ids,
                             mask,
                             size,
                             state.num_volume_levels);
            }
        }

        DS_ASSIGN(event_id);
        DS_ASSIGN(parent_id);
        DS_ASSIGN(primary_id);
        DS_ASSIGN(post_step_action_id);
        DS_ASSIGN(track_step_count);
        DS_ASSIGN(step_length);
        DS_ASSIGN(weight);
        DS_ASSIGN(particle_id);
        DS_ASSIGN(energy_deposition);
        DS_ASSIGN(track_status);

        output->num_volume_levels = state.num_volume_levels;

#undef DS_ASSIGN

        CELER_ENSURE(output->track_id.size() == size);
    };
    if (!state.data.detector_id.empty())
    {
        copy_selected(state.data.detector_id);
    }
    else
    {
        copy_selected(state.data.track_id);
    }
}

//---------------------------------------------------------------------------//
}  // namespace celeritas
