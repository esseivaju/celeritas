//------------------------------- -*- C++ -*- -------------------------------//
// Copyright Celeritas contributors: see top-level COPYRIGHT file for details
// SPDX-License-Identifier: (Apache-2.0 OR MIT)
//---------------------------------------------------------------------------//
//! \file celeritas/user/detail/StepGatherAction.cc
//---------------------------------------------------------------------------//
#include "StepGatherAction.hh"

#include <mutex>
#include <utility>

#include "corecel/Assert.hh"
#include "corecel/Macros.hh"
#include "corecel/cont/Range.hh"
#include "corecel/io/Logger.hh"
#include "celeritas/global/ActionLauncher.hh"
#include "celeritas/global/CoreParams.hh"
#include "celeritas/global/CoreState.hh"
#include "celeritas/global/CoreTrackData.hh"
#include "celeritas/global/TrackExecutor.hh"

#include "StepGatherExecutor.hh"
#include "StepParams.hh"
#include "../StepData.hh"

namespace celeritas
{
namespace detail
{
//---------------------------------------------------------------------------//
/*!
 * Capture construction arguments.
 */
template<StepPoint P>
StepGatherAction<P>::StepGatherAction(ActionId id,
                                      SPConstStepParams params,
                                      VecInterface callbacks,
                                      std::string const& name)
    : id_(id), params_(std::move(params)), callbacks_(std::move(callbacks))
{
    CELER_EXPECT(id_);
    CELER_EXPECT(!callbacks_.empty() || P == StepPoint::pre);
    CELER_EXPECT(params_);

    label_ = name.empty() ? "step-gather-" : name + "-step-gather-";
    label_ += P == StepPoint::pre ? "pre" : "post";

    description_ = "gather ";
    description_ += (P == StepPoint::pre ? "pre" : "post");
    description_ += "-step steps/hits";
    CELER_ENSURE(!description_.empty());
}

//---------------------------------------------------------------------------//
/*!
 * Prepare callback storage outside the stepping loop.
 */
template<StepPoint P>
template<MemSpace M>
void StepGatherAction<P>::begin_run_impl(CoreState<M>& state)
{
    if constexpr (P == StepPoint::post)
    {
        auto& data = params_->state_ref<M>(state.aux());
        for (auto const& callback : callbacks_)
        {
            callback->begin_run(StepState<M>{data, state.stream_id()});
        }
    }
}

template<StepPoint P>
void StepGatherAction<P>::begin_run(CoreParams const&, CoreStateHost& state)
{
    this->begin_run_impl(state);
}

template<StepPoint P>
void StepGatherAction<P>::begin_run(CoreParams const&, CoreStateDevice& state)
{
    this->begin_run_impl(state);
}

//---------------------------------------------------------------------------//
/*!
 * Deliver callbacks only after the producing step has completed.
 */
template<StepPoint P>
void StepGatherAction<P>::complete_step_impl(StreamId stream) const
{
    if constexpr (P == StepPoint::post)
    {
        for (auto const& callback : callbacks_)
        {
            callback->process_pending_steps(stream);
        }
    }
}

template<StepPoint P>
void StepGatherAction<P>::complete_step(CoreParams const&,
                                        CoreStateHost& state) const
{
    this->complete_step_impl(state.stream_id());
}

template<StepPoint P>
void StepGatherAction<P>::complete_step(CoreParams const&,
                                        CoreStateDevice& state) const
{
    this->complete_step_impl(state.stream_id());
}

//---------------------------------------------------------------------------//
/*!
 * Gather step attributes from host data, and execute callbacks at end of step.
 */
template<StepPoint P>
void StepGatherAction<P>::step(CoreParams const& params,
                               CoreStateHost& state) const
{
    // Extract the local step state data
    auto const& step_params = params_->ref<MemSpace::native>();
    auto& step_state = params_->state_ref<MemSpace::native>(state.aux());

    // Run the action
    auto execute = TrackExecutor{
        params.ptr<MemSpace::native>(),
        state.ptr(),
        detail::StepGatherExecutor<P>{step_params, step_state}};
    launch_action(*this, params, state, execute);

    if (P == StepPoint::post)
    {
        // Execute callbacks at the end of the step
        StepState<MemSpace::native> cb_state{step_state, state.stream_id()};
        for (auto const& sp_callback : callbacks_)
        {
            sp_callback->process_steps(cb_state);
        }
    }
}

//---------------------------------------------------------------------------//
#if !CELER_USE_DEVICE
template<StepPoint P>
void StepGatherAction<P>::step(CoreParams const&, CoreStateDevice&) const
{
    CELER_NOT_CONFIGURED("CUDA OR HIP");
}
#endif

//---------------------------------------------------------------------------//
// EXPLICIT INSTANTIATION
//---------------------------------------------------------------------------//

template class StepGatherAction<StepPoint::pre>;
template class StepGatherAction<StepPoint::post>;

//---------------------------------------------------------------------------//
}  // namespace detail
}  // namespace celeritas
