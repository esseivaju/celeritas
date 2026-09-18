//------------------------------- -*- C++ -*- -------------------------------//
// Copyright Celeritas contributors: see top-level COPYRIGHT file for details
// SPDX-License-Identifier: (Apache-2.0 OR MIT)
//---------------------------------------------------------------------------//
//! \file celeritas/ext/detail/HitProcessor.cc
//---------------------------------------------------------------------------//
#include "HitProcessor.hh"

#include <G4LogicalVolume.hh>
#include <G4VSensitiveDetector.hh>

#include "corecel/io/Logger.hh"
#include "corecel/sys/ScopedProfiling.hh"
#include "corecel/sys/TraceCounter.hh"
#include "geocel/GeantGeoUtils.hh"

namespace celeritas
{
namespace detail
{
//---------------------------------------------------------------------------//
/*!
 * Construct worker-local reconstruction and sensitive detector dispatch.
 */
HitProcessor::HitProcessor(SPConstVecLV volumes,
                           VecParticle const& particles,
                           StepSelection const& selection,
                           StepPointBool const& locate)
    : detector_volumes_(std::move(volumes))
    , reconstruction_(particles, selection, locate, detector_volumes_)
{
    CELER_EXPECT(detector_volumes_ && !detector_volumes_->empty());
    CELER_LOG(debug) << "Setting up thread-local hit processor for "
                     << detector_volumes_->size() << " sensitive detectors";
    for (auto* lv : *detector_volumes_)
    {
        CELER_ASSERT(lv);
        auto* sd = lv->GetSensitiveDetector();
        CELER_VALIDATE(sd,
                       << "no sensitive detector is attached to volume '"
                       << StreamableLV{lv});
        detectors_.push_back(sd);
    }
}

//---------------------------------------------------------------------------//
//! Copy and process host hits.
void HitProcessor::operator()(StepStateHostRef const& states)
{
    copy_steps(&steps_, states);
    num_hits_ += steps_.size();
    (*this)(steps_);
}

//---------------------------------------------------------------------------//
//! Copy and process device hits.
void HitProcessor::operator()(StepStateDeviceRef const& states)
{
    copy_steps(&steps_, states);
    num_hits_ += steps_.size();
    (*this)(steps_);
}

//---------------------------------------------------------------------------//
//! Dispatch a batch of saved detector steps.
void HitProcessor::operator()(DetectorStepOutput const& out) const
{
    ScopedProfiling profile_this{"process-hits"};
    trace_counter("process-hits", out.size());
    for (auto i : range(out.size()))
    {
        (*this)(out, i);
    }
}

//---------------------------------------------------------------------------//
//! Reconstruct and dispatch one detector step.
void HitProcessor::operator()(DetectorStepOutput const& out, size_type i) const
{
    CELER_EXPECT(!out.detector_id.empty());
    if (auto* step = reconstruction_(out, i))
    {
        this->detector(out.detector_id[i])->Hit(step);
    }
}

//---------------------------------------------------------------------------//
}  // namespace detail
}  // namespace celeritas
