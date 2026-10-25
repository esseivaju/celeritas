//------------------------------- -*- C++ -*- -------------------------------//
// Copyright Celeritas contributors: see top-level COPYRIGHT file for details
// SPDX-License-Identifier: (Apache-2.0 OR MIT)
//---------------------------------------------------------------------------//
//! \file celeritas/optical/gen/WlsGeneratorAction.cc
//---------------------------------------------------------------------------//
#include "WlsGeneratorAction.hh"

#include "corecel/Assert.hh"
#include "corecel/data/AuxParamsRegistry.hh"
#include "corecel/data/AuxStateVec.hh"
#include "corecel/io/Logger.hh"
#include "corecel/sys/ActionRegistry.hh"
#include "corecel/sys/KernelLauncher.hh"
#include "celeritas/optical/CoreParams.hh"
#include "celeritas/optical/CoreState.hh"
#include "celeritas/optical/CoreTrackData.hh"
#include "celeritas/optical/WavelengthShiftData.hh"
#include "celeritas/optical/action/ActionLauncher.hh"
#include "celeritas/optical/model/WavelengthShiftModel.hh"
#include "celeritas/phys/GeneratorRegistry.hh"

#include "WavelengthShiftGenerator.hh"

#include "detail/GeneratorAlgorithms.hh"
#include "detail/WlsGeneratorExecutor.hh"

namespace celeritas
{
namespace optical
{
namespace
{
//---------------------------------------------------------------------------//
//! Construct a state
template<MemSpace M>
auto make_state(StreamId stream, size_type size)
{
    auto result = std::make_unique<WlsGeneratorState<M>>(stream, size);

    CELER_ENSURE(*result);
    return result;
}

//---------------------------------------------------------------------------//
}  // namespace

//---------------------------------------------------------------------------//
/*!
 * Construct with IDs, WLS model, and buffer capacity.
 *
 * The second WLS model is optional.
 */
WlsGeneratorAction::WlsGeneratorAction(Input&& input)
    : GeneratorBase(input.action_id,
                    input.aux_id,
                    input.gen_id,
                    "wls-generate",
                    "generate photons from a wavelength shifting process")
    , wls_(std::move(input.wls))
    , wls2_(std::move(input.wls2))
    , capacity_(input.capacity)
{
    CELER_EXPECT(capacity_ > 0);
    CELER_EXPECT(wls_ || wls2_);
}

//---------------------------------------------------------------------------//
/*!
 * Build state data for a stream.
 */
auto WlsGeneratorAction::create_state(MemSpace m, StreamId id, size_type) const
    -> UPState
{
    if (m == MemSpace::host)
    {
        return make_state<MemSpace::host>(id, capacity_);
    }
    else if (m == MemSpace::device)
    {
        return make_state<MemSpace::device>(id, capacity_);
    }
    CELER_ASSERT_UNREACHABLE();
}

//---------------------------------------------------------------------------//
/*!
 * Execute the action with host data.
 */
void WlsGeneratorAction::step(CoreParams const& params,
                              CoreStateHost& state) const
{
    this->step_impl(params, state);
}

//---------------------------------------------------------------------------//
/*!
 * Execute the action with device data.
 */
void WlsGeneratorAction::step(CoreParams const& params,
                              CoreStateDevice& state) const
{
    this->step_impl(params, state);
}

//---------------------------------------------------------------------------//
/*!
 * Generate optical WLS photons from distribution data.
 *
 * The distributions created by tracks in the previous step are stored in the
 * track slots past the compacted buffer. Since the buffer size is only known
 * on device, the buffer operations are launched over an upper bound \em b of
 * the buffer size plus the number of track slots \em n , and the buffer
 * capacity must be at least \em b + 2n to store the distributions created
 * during this step.
 *
 * \todo Accumulate the total number of steps (distributions) in \c
 * accum.buffer_size
 */
template<MemSpace M>
void WlsGeneratorAction::step_impl(CoreParams const& params,
                                   CoreState<M>& state) const
{
    CELER_EXPECT(state.aux());

    auto& aux_state = get<WlsGeneratorState<M>>(*state.aux(), this->aux_id());
    auto& buffer = aux_state.store.ref().distributions;
    auto counters = aux_state.device_counters();

    // Upper bound on the buffered distributions, including the ones created
    // in the previous step
    size_type bound = aux_state.buffer_bound() + state.size();

    // Ensure the buffer is large enough to hold WLS distributions created
    // during this step
    CELER_VALIDATE(bound + state.size() <= buffer.size(),
                   << "insufficient capacity (" << buffer.size()
                   << ") for buffered optical WLS distribution data (total "
                      "capacity requirement of "
                   << bound + state.size() << ")");

    // Compact the buffer, storing the number of valid distributions, and
    // clear the unspecified data left past the compacted buffer
    detail::select_valid(buffer, bound, counters, state.stream_id());
    detail::clear_tail(buffer, bound, counters, state.stream_id());

    // Calculate the cumulative sum of the number of photons in the buffered
    // distributions, which determines which thread will generate photons
    // from which distribution. The new photons were already added to the
    // core state's pending count when the distributions were created.
    detail::inclusive_scan_photons(buffer,
                                   aux_state.store.ref().offsets,
                                   bound,
                                   counters,
                                   {},
                                   state.stream_id());

    // Generate the optical photons from the distribution data. To avoid
    // synchronization, the number of photons and vacancies are checked on
    // device.
    this->generate(params, state);

    // Compact the buffer again to remove stale distributions and free up
    // space to add new distributions during this step
    detail::select_valid(buffer, bound, counters, state.stream_id());

    // Clear data that tracks might write distributions to in this step, which
    // is in an unspecified state after compacting the buffer
    detail::clear_tail(buffer, bound, counters, state.stream_id());

    // Update the generator and optical core state counters
    this->update_counters(state);

    // Up to one distribution per track slot can be added during this step
    aux_state.grow_bound(state.size());
    aux_state.copy_async();
}

//---------------------------------------------------------------------------//
/*!
 * Launch a (host) kernel to generate optical photons.
 */
void WlsGeneratorAction::generate(CoreParams const& params,
                                  CoreStateHost& state) const
{
    CELER_EXPECT(state.aux());

    auto& aux_state = get<WlsGeneratorState<MemSpace::native>>(*state.aux(),
                                                               this->aux_id());

    // Generate optical photons in vacant track slots
    detail::WlsGeneratorExecutor execute{
        params.ptr<MemSpace::native>(),
        state.ptr(),
        wls_ ? wls_->host_ref() : NativeCRef<WavelengthShiftData>{},
        wls2_ ? wls2_->host_ref() : NativeCRef<WavelengthShiftData>{},
        aux_state.store.ref(),
        aux_state.device_counters()};
    launch_action(state.size(), execute);
}

//---------------------------------------------------------------------------//
#if !CELER_USE_DEVICE
void WlsGeneratorAction::generate(CoreParams const&, CoreStateDevice&) const
{
    CELER_NOT_CONFIGURED("CUDA OR HIP");
}
#endif

//---------------------------------------------------------------------------//
}  // namespace optical
}  // namespace celeritas
