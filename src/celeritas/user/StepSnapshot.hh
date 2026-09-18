//------------------------------- -*- C++ -*- -------------------------------//
// Copyright Celeritas contributors: see top-level COPYRIGHT file for details
// SPDX-License-Identifier: (Apache-2.0 OR MIT)
//---------------------------------------------------------------------------//
//! \file celeritas/user/StepSnapshot.hh
//! \sa DetectorSteps.test.cc
//---------------------------------------------------------------------------//
#pragma once

#include "DetectorSteps.hh"
#include "StepData.hh"

namespace celeritas
{
//---------------------------------------------------------------------------//
/*!
 * Own one asynchronous batch of completed steps in reusable pinned storage.
 *
 * Initialize before transport, capture on the producing stream, then call
 * \c complete only after that stream's completion event has finished. Capture
 * copies full slot arrays without a device count query or synchronization.
 * Completion filters valid tracks and detectors in place. The storage cannot
 * be reused until \c clear, which should follow successful callback delivery.
 *
 * Destruction does not wait: the caller must complete outstanding transfers
 * before destroying this object, including during exception unwinding.
 */
class StepSnapshot
{
  public:
    StepSnapshot() = default;
    CELER_DEFAULT_MOVE_DELETE_COPY(StepSnapshot);

    template<MemSpace M>
    void initialize(StepStateData<Ownership::reference, M> const&);

    template<MemSpace M>
    void capture(StepStateData<Ownership::reference, M> const&);

    StepOutput const& complete();
    void clear();

    //! Whether a batch has been captured and not yet delivered
    bool pending() const { return pending_; }

    //! Pinned bytes reserved for the selected step fields
    size_type buffer_size() const { return buffer_size_; }

  private:
    StepOutput output_;
    std::vector<size_type> valid_slots_;
    size_type capacity_{0};
    size_type buffer_size_{0};
    bool pending_{false};
    bool compacted_{false};
};

//---------------------------------------------------------------------------//
}  // namespace celeritas
