//----------------------------------*-cu-*-----------------------------------//
// Copyright 2021 UT-Battelle, LLC, and other Celeritas developers.
// See the top-level COPYRIGHT file for details.
// SPDX-License-Identifier: (Apache-2.0 OR MIT)
//---------------------------------------------------------------------------//
//! \file LivermorePEInteract.cu
//! \note Auto-generated by gen-interactor.py: DO NOT MODIFY!
//---------------------------------------------------------------------------//
#include "base/Assert.hh"
#include "base/KernelParamCalculator.cuda.hh"
#include "../detail/LivermorePELauncher.hh"

using namespace celeritas::detail;

namespace celeritas
{
namespace generated
{
namespace
{
__global__ void livermore_pe_interact_kernel(
    const detail::LivermorePEDeviceRef livermore_pe_data,
    const ModelInteractRef<MemSpace::device> model)
{
    auto tid = KernelParamCalculator::thread_id();
    if (!(tid < model.states.size()))
        return;

    detail::LivermorePELauncher<MemSpace::device> launch(livermore_pe_data, model);
    launch(tid);
}
} // namespace

void livermore_pe_interact(
    const detail::LivermorePEDeviceRef& livermore_pe_data,
    const ModelInteractRef<MemSpace::device>& model)
{
    CELER_EXPECT(livermore_pe_data);
    CELER_EXPECT(model);

    static const KernelParamCalculator calc_kernel_params(
        livermore_pe_interact_kernel, "livermore_pe_interact");
    auto params = calc_kernel_params(model.states.size());
    livermore_pe_interact_kernel<<<params.grid_size, params.block_size>>>(
        livermore_pe_data, model);
    CELER_CUDA_CHECK_ERROR();
}

} // namespace generated
} // namespace celeritas
