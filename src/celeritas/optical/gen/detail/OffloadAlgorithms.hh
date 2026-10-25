//------------------------------- -*- C++ -*- -------------------------------//
// Copyright Celeritas contributors: see top-level COPYRIGHT file for details
// SPDX-License-Identifier: (Apache-2.0 OR MIT)
//---------------------------------------------------------------------------//
//! \file celeritas/optical/gen/detail/OffloadAlgorithms.hh
//---------------------------------------------------------------------------//
#pragma once

#include "corecel/Macros.hh"
#include "corecel/Types.hh"
#include "corecel/data/Collection.hh"
#include "corecel/data/ObserverPtr.hh"
#include "celeritas/optical/CoreState.hh"
#include "celeritas/phys/GeneratorCounters.hh"

#include "../GeneratorData.hh"

namespace celeritas
{
namespace detail
{
//---------------------------------------------------------------------------//
using celeritas::optical::GeneratorDistributionData;

template<class T, MemSpace M>
using ItemsRef = Collection<T, Ownership::reference, M>;

//---------------------------------------------------------------------------//
/*!
 * Get the distribution created by a track slot in the current step.
 *
 * The distributions created in a step are stored in the track slots past the
 * buffered distributions, whose number is read from the state's memory.
 */
struct OffloadedDistribution
{
    ObserverPtr<GeneratorDistributionData const, MemSpace::native> data;
    ObserverPtr<size_type const, MemSpace::native> buffer_size;

    CELER_FUNCTION GeneratorDistributionData operator()(size_type i) const
    {
        return data.get()[*buffer_size + i];
    }
};

//---------------------------------------------------------------------------//
/*!
 * Get the number of photons in the selected distributions.
 */
struct SelectedPhotonCount
{
    ObserverPtr<GeneratorDistributionData const, MemSpace::native> data;
    ObserverPtr<size_type const, MemSpace::native> num_selected;

    CELER_FUNCTION size_type operator()(size_type i) const
    {
        return i < *num_selected ? data.get()[i].num_photons : 0;
    }
};

//---------------------------------------------------------------------------//
// Append the valid distributions created in this step to the buffer
void append_distributions(
    optical::CoreState<MemSpace::host>&,
    ItemsRef<GeneratorDistributionData, MemSpace::host> const&,
    ObserverPtr<GeneratorStateCounters, MemSpace::host>,
    size_type,
    StreamId);
void append_distributions(
    optical::CoreState<MemSpace::device>&,
    ItemsRef<GeneratorDistributionData, MemSpace::device> const&,
    ObserverPtr<GeneratorStateCounters, MemSpace::device>,
    size_type,
    StreamId);

#if !CELER_USE_DEVICE
inline void append_distributions(
    optical::CoreState<MemSpace::device>&,
    ItemsRef<GeneratorDistributionData, MemSpace::device> const&,
    ObserverPtr<GeneratorStateCounters, MemSpace::device>,
    size_type,
    StreamId)
{
    CELER_NOT_CONFIGURED("CUDA OR HIP");
}
#endif

//---------------------------------------------------------------------------//
}  // namespace detail
}  // namespace celeritas
