//----------------------------------*-cu-*-----------------------------------//
// Copyright 2021-2022 UT-Battelle, LLC, and other Celeritas developers.
// See the top-level COPYRIGHT file for details.
// SPDX-License-Identifier: (Apache-2.0 OR MIT)
//---------------------------------------------------------------------------//
//! \file MuBremsstrahlungInteract.cu
//! \note Auto-generated by gen-interactor.py: DO NOT MODIFY!
//---------------------------------------------------------------------------//
#include "base/device_runtime_api.h"
#include "base/Assert.hh"
#include "base/KernelParamCalculator.device.hh"
#include "comm/Device.hh"
#include "../detail/MuBremsstrahlungLauncher.hh"

using namespace celeritas::detail;

namespace celeritas
{
namespace generated
{
namespace
{
__global__ void mu_bremsstrahlung_interact_kernel(
    const detail::MuBremsstrahlungDeviceRef mu_bremsstrahlung_data,
    const ModelInteractRef<MemSpace::device> model)
{
    auto tid = KernelParamCalculator::thread_id();
    if (!(tid < model.states.size()))
        return;

    detail::MuBremsstrahlungLauncher<MemSpace::device> launch(mu_bremsstrahlung_data, model);
    launch(tid);
}
} // namespace

void mu_bremsstrahlung_interact(
    const detail::MuBremsstrahlungDeviceRef& mu_bremsstrahlung_data,
    const ModelInteractRef<MemSpace::device>& model)
{
    CELER_EXPECT(mu_bremsstrahlung_data);
    CELER_EXPECT(model);
    CELER_LAUNCH_KERNEL(mu_bremsstrahlung_interact,
                        celeritas::device().default_block_size(),
                        model.states.size(),
                        mu_bremsstrahlung_data, model);
}

} // namespace generated
} // namespace celeritas
