//----------------------------------*-cu-*-----------------------------------//
// Copyright 2021 UT-Battelle, LLC, and other Celeritas developers.
// See the top-level COPYRIGHT file for details.
// SPDX-License-Identifier: (Apache-2.0 OR MIT)
//---------------------------------------------------------------------------//
//! \file KleinNishinaInteract.cu
//! \note Auto-generated by gen-interactor.py: DO NOT MODIFY!
//---------------------------------------------------------------------------//
#include "base/Assert.hh"
#include "base/KernelParamCalculator.cuda.hh"
#include "../detail/KleinNishinaLauncher.hh"

using namespace celeritas::detail;

namespace celeritas
{
namespace generated
{
namespace
{
__global__ void klein_nishina_interact_kernel(
    const detail::KleinNishinaDeviceRef klein_nishina_data,
    const ModelInteractRef<MemSpace::device> model)
{
    auto tid = KernelParamCalculator::thread_id();
    if (!(tid < model.states.size()))
        return;

    detail::KleinNishinaLauncher<MemSpace::device> launch(klein_nishina_data, model);
    launch(tid);
}
} // namespace

void klein_nishina_interact(
    const detail::KleinNishinaDeviceRef& klein_nishina_data,
    const ModelInteractRef<MemSpace::device>& model)
{
    CELER_EXPECT(klein_nishina_data);
    CELER_EXPECT(model);

    static const KernelParamCalculator calc_kernel_params(
        klein_nishina_interact_kernel, "klein_nishina_interact");
    auto params = calc_kernel_params(model.states.size());
    klein_nishina_interact_kernel<<<params.grid_size, params.block_size>>>(
        klein_nishina_data, model);
    CELER_CUDA_CHECK_ERROR();
}

} // namespace generated
} // namespace celeritas
