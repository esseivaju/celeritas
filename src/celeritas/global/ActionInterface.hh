//------------------------------- -*- C++ -*- -------------------------------//
// Copyright Celeritas contributors: see top-level COPYRIGHT file for details
// SPDX-License-Identifier: (Apache-2.0 OR MIT)
//---------------------------------------------------------------------------//
//! \file celeritas/global/ActionInterface.hh
//! \sa Stepper.test.cc
//---------------------------------------------------------------------------//
#pragma once

#include "corecel/sys/ActionInterface.hh"
#include "celeritas/Types.hh"

namespace celeritas
{
//---------------------------------------------------------------------------//
class CoreParams;
template<MemSpace M>
class CoreState;

//---------------------------------------------------------------------------//
// TYPE ALIASES
//---------------------------------------------------------------------------//
//! Interface called at beginning of the core stepping loop
using CoreBeginRunActionInterface
    = BeginRunActionInterface<CoreParams, CoreState>;

//! Action interface for core stepping loop
using CoreStepActionInterface = StepActionInterface<CoreParams, CoreState>;

//---------------------------------------------------------------------------//
/*!
 * Complete host processing after a step's stream work has finished.
 *
 * Actions implementing this optional interface must also implement
 * \c CoreStepActionInterface. Completion runs on the caller of \c Stepper::get
 * in step action order. It must not access data from subsequently staged
 * primaries or enqueue transport work.
 */
class CoreStepCompletionActionInterface
    : public ActionTypeTraits<CoreParams, CoreState>,
      public virtual ActionInterface
{
  public:
    virtual void complete_step(CoreParams const&, CoreStateHost&) const = 0;
    virtual void complete_step(CoreParams const&, CoreStateDevice&) const = 0;
};

//---------------------------------------------------------------------------//
// HELPER FUNCTIONS
//---------------------------------------------------------------------------//
/*!
 * Whether the TrackOrder will sort tracks by actions at the given step order.
 */
inline constexpr bool is_action_sorted(StepActionOrder aorder,
                                       TrackOrder torder)
{
    // CAUTION: check that this matches \c SortTracksAction::SortTracksAction
    return (aorder == StepActionOrder::post
            && torder == TrackOrder::reindex_step_limit_action)
           || (aorder == StepActionOrder::along
               && torder == TrackOrder::reindex_along_step_action)
           || (torder == TrackOrder::reindex_both_action
               && (aorder == StepActionOrder::post
                   || aorder == StepActionOrder::along));
}

//---------------------------------------------------------------------------//
/*!
 * Whether track sorting (reindexing) is enabled.
 */
inline constexpr bool is_action_sorted(TrackOrder torder)
{
    auto to_int = [](TrackOrder v) {
        return static_cast<std::underlying_type_t<TrackOrder>>(v);
    };
    return to_int(torder) >= to_int(TrackOrder::begin_reindex_)
           && to_int(torder) < to_int(TrackOrder::end_reindex_);
}

//---------------------------------------------------------------------------//
}  // namespace celeritas
