//------------------------------- -*- C++ -*- -------------------------------//
// Copyright Celeritas contributors: see top-level COPYRIGHT file for details
// SPDX-License-Identifier: (Apache-2.0 OR MIT)
//---------------------------------------------------------------------------//
//! \file corecel/sys/StreamTestUtils.hh
//! \sa celeritas/user/DetectorSteps.test.cc
//! \sa celeritas/global/Stepper.test.cc
//---------------------------------------------------------------------------//
#pragma once

#include <atomic>
#include <chrono>
#include <thread>

namespace celeritas
{
namespace test
{
//---------------------------------------------------------------------------//
/*!
 * Block a test stream until explicitly released, with a deadlock timeout.
 *
 * Keep this object alive until the stream finishes. A timeout indicates that
 * the tested host operation synchronized instead of returning asynchronously.
 */
struct StreamTestGate
{
    std::atomic<bool> released{false};
    std::atomic<bool> timed_out{false};

    static inline void wait(void*);
};

//---------------------------------------------------------------------------//
//! Host function enqueued on the stream under test.
void StreamTestGate::wait(void* ptr)
{
    auto& gate = *static_cast<StreamTestGate*>(ptr);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!gate.released)
    {
        if (std::chrono::steady_clock::now() > deadline)
        {
            gate.timed_out = true;
            break;
        }
        std::this_thread::yield();
    }
}

//---------------------------------------------------------------------------//
}  // namespace test
}  // namespace celeritas
