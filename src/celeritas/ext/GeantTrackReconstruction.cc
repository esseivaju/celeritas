//------------------------------- -*- C++ -*- -------------------------------//
// Copyright Celeritas contributors: see top-level COPYRIGHT file for details
// SPDX-License-Identifier: (Apache-2.0 OR MIT)
//---------------------------------------------------------------------------//
//! \file celeritas/ext/GeantTrackReconstruction.cc
//---------------------------------------------------------------------------//
#include "GeantTrackReconstruction.hh"

#include <limits>
#include <mutex>
#include <G4DynamicParticle.hh>
#include <G4Event.hh>
#include <G4EventManager.hh>
#include <G4ParticleDefinition.hh>
#include <G4Step.hh>
#include <G4ThreeVector.hh>
#include <G4Track.hh>
#include <G4VProcess.hh>
#include <G4VUserTrackInformation.hh>

#include "corecel/Config.hh"

#include "corecel/Assert.hh"
#include "corecel/io/Logger.hh"
#include "geocel/g4/Convert.hh"
#include "celeritas/Types.hh"
#include "celeritas/UnitTypes.hh"
#include "celeritas/track/TrackInitData.hh"

namespace celeritas
{
namespace
{
//---------------------------------------------------------------------------//
[[maybe_unused]] int get_g4_current_event_id()
{
    auto* evtman = G4EventManager::GetEventManager();
    CELER_ASSERT(evtman);
    auto* evt = evtman->GetConstCurrentEvent();
    if (!evt)
    {
        // Use a different "invalid" event ID from the default g4_event_id_
        return -2;
    }
    return evt->GetEventID();
}
}  // namespace

//---------------------------------------------------------------------------//
/*!
 * Event ID function pointer for unit testing when CELERITAS_DEBUG.
 *
 * When constructing a class instance, if the function pointer is null, it will
 * be set to a function that gets the Geant4 event manager's active event.
 */
GeantTrackReconstruction::EventIdGetter
    GeantTrackReconstruction::get_current_event_id{nullptr};

//---------------------------------------------------------------------------//
/*!
 * Allocate and initialize a valid Geant4 step object.
 *
 * The allocation is done like \c G4SteppingManager constructor but we reset
 * values to invalid ones.
 */
auto GeantTrackReconstruction::make_g4step() -> SPStep
{
    auto step = std::make_shared<G4Step>();

    // Allocate secondary vector, needed to keep some SDs from crashing
    step->NewSecondaryVector();

    // Set invalid values for unsupported SD attributes
    step->SetNonIonizingEnergyDeposit(-std::numeric_limits<double>::infinity());
    for (G4StepPoint* p : {step->GetPreStepPoint(), step->GetPostStepPoint()})
    {
        p->SetStepStatus(fUserDefinedLimit);
        // Time since track was created
        p->SetLocalTime(std::numeric_limits<double>::infinity());
        // Time in rest frame since track was created
        p->SetProperTime(std::numeric_limits<double>::infinity());
        // Speed (TODO: use ParticleView)
        p->SetVelocity(std::numeric_limits<double>::infinity());
        // Safety distance
        p->SetSafety(std::numeric_limits<double>::infinity());
        // Polarization (default to zero)
        p->SetPolarization(G4ThreeVector());
    }

    return step;
}

//---------------------------------------------------------------------------//
/*!
 * Construct with particle definitions for track reconstruction.
 */
GeantTrackReconstruction::GeantTrackReconstruction(
    VecParticle const& particles, SPStep step)
    : step_(std::move(step))
{
    CELER_EXPECT(!particles.empty());
    CELER_EXPECT(step_);

    // Create track for each particle type
    for (G4ParticleDefinition const* pd : particles)
    {
        CELER_ASSERT(pd);
        auto track = std::make_unique<G4Track>(
            new G4DynamicParticle(pd, G4ThreeVector()), 0.0, G4ThreeVector());
        track->SetTrackID(0);
        track->SetParentID(0);
        tracks_.emplace_back(std::move(track));
    }

    // Set the step for all tracks
    for (auto const& track : tracks_)
    {
        track->SetStep(step_.get());
    }

    // Reset event interface used for test mocking
    if constexpr (CELERITAS_DEBUG)
    {
        static std::mutex mu;
        std::scoped_lock lock{mu};

        if (get_current_event_id == nullptr)
        {
            get_current_event_id = get_g4_current_event_id;
        }
    }
}

//---------------------------------------------------------------------------//
/*!
 * Unset the user information for all tracks
 */
GeantTrackReconstruction::~GeantTrackReconstruction()
{
    try
    {
        CELER_LOG(debug) << "Deallocating track reconstruction";
        if (!g4_track_data_.empty())
        {
            CELER_LOG_LOCAL(warning)
                << R"(Geant4 track data was not cleared during the event)";
        }
        this->clear();
    }
    catch (...)  // NOLINT(bugprone-empty-catch)
    {
        // Ignore anything bad that happens while destroying
    }
}

//---------------------------------------------------------------------------//
/*!
 * Clear G4Track reconstruction data.
 *
 * This should be done when all Celeritas tracks have been completed, since
 * afterward it will be impossible to reconstruct them.
 *
 * The primary ID offset is saved to ensure consistency when flushing before
 * an event is complete.
 */
void GeantTrackReconstruction::clear()
{
    // Set primary id offset
    start_ = start_ + g4_track_data_.size();

    for (auto& track : tracks_)
    {
        // Clear the user information to prevent double deletion:
        // GeantTrackReconstruction owns the track user info
        track->SetUserInformation(nullptr);
    }
    g4_track_data_.clear();
    pending_tracks_.clear();
    mapped_tracks_.clear();
}

//---------------------------------------------------------------------------//
/*!
 * At the start of an event, reset the primary ID counter.
 *
 * \pre The track data *must* have been previously flushed with the \c clear
 * command.
 */
void GeantTrackReconstruction::init_event()
{
    CELER_EXPECT(g4_track_data_.empty());
    CELER_EXPECT(mapped_tracks_.empty() && pending_tracks_.empty());
    start_ = PrimaryId(0);
    next_secondary_id_ = -1;
    if constexpr (CELERITAS_DEBUG)
    {
        g4_event_id_ = get_current_event_id();
    }
}

//---------------------------------------------------------------------------//
/*!
 * Register mapping from Celeritas PrimaryID to Geant4 TrackID. This will take
 * ownership of the G4VUserTrackInformation and unset it in the primary track.
 */
PrimaryId GeantTrackReconstruction::acquire(G4Track& primary)
{
    if constexpr (CELERITAS_DEBUG)
    {
        int cur_event_id = get_current_event_id();
        CELER_VALIDATE(g4_event_id_ == cur_event_id,
                       << "GeantTrackReconstruction::init_event was not "
                          "called: last event "
                       << g4_event_id_ << " != current event " << cur_event_id);
    }
    auto primary_id = start_ + g4_track_data_.size();
    g4_track_data_.emplace_back(AcquiredData{primary});
    if (track_mapping_)
    {
        CELER_VALIDATE(primary.GetTrackID() > 0,
                       << "registered Geant4 tracks must have positive IDs");
        auto track = std::make_unique<G4Track>(primary);
        auto& saved = g4_track_data_.back();
        saved.restore(*track);
        saved.release_user_info();
        // Geant4's copy constructor resets transport counters.
        track->AddTrackLength(
            primary.GetTrackLength() - track->GetTrackLength());
        while (track->GetCurrentStepNumber() < primary.GetCurrentStepNumber())
        {
            track->IncrementCurrentStepNumber();
        }
        pending_tracks_.emplace(primary_id, std::move(track));
    }
    return primary_id;
}

//---------------------------------------------------------------------------//
/*!
 * Enable individual track ownership and metadata persistence.
 *
 * This must precede acquiring any tracks. Each reconstructed track owns its
 * user information, just as in Geant4. The map belongs to the current framework
 * event and is cleared only after transport and callbacks have been drained.
 */
void GeantTrackReconstruction::enable_track_mapping()
{
    CELER_EXPECT(g4_track_data_.empty() && mapped_tracks_.empty());
    track_mapping_ = true;
}

//---------------------------------------------------------------------------//
/*!
 * Bind an offloaded primary or retrieve a previously registered descendant.
 */
G4Track& GeantTrackReconstruction::view(
    ParticleId particle, PrimaryId primary, TrackId id, TrackId parent)
{
    if (!track_mapping_)
    {
        return this->view(particle, primary);
    }
    CELER_EXPECT(id && particle < tracks_.size());
    auto iter = mapped_tracks_.find(id);
    if (iter == mapped_tracks_.end())
    {
        CELER_VALIDATE(!parent,
                       << "missing secondary birth for track " << id.get());
        auto pending = pending_tracks_.find(primary);
        CELER_VALIDATE(pending != pending_tracks_.end(),
                       << "missing offloaded primary "
                       << primary.unchecked_get());
        iter = mapped_tracks_
                   .emplace(id, TrackEntry{std::move(pending->second), 0})
                   .first;
        pending_tracks_.erase(pending);
    }
    auto& track = *iter->second.track;
    CELER_VALIDATE(track.GetParticleDefinition()
                       == tracks_[particle.get()]->GetParticleDefinition(),
                   << "particle type changed for reconstructed track "
                   << id.get());
    track.SetStep(step_.get());
    step_->SetTrack(&track);
    return track;
}

//---------------------------------------------------------------------------//
/*!
 * Create a persistent secondary with an event-unique negative Geant4 ID.
 *
 * IDs are not reused across flushes. User information starts empty: callbacks
 * can attach experiment-specific information, which cannot be cloned
 * generically.
 */
G4Track& GeantTrackReconstruction::insert_secondary(SecondaryBirth const& birth)
{
    CELER_EXPECT(track_mapping_ && birth);
    auto parent = mapped_tracks_.find(birth.sim.parent_id);
    CELER_VALIDATE(parent != mapped_tracks_.end(),
                   << "missing reconstructed parent");
    CELER_VALIDATE(mapped_tracks_.count(birth.sim.track_id) == 0,
                   << "duplicate secondary birth");
    CELER_VALIDATE(next_secondary_id_ > std::numeric_limits<int>::min(),
                   << "Geant4 secondary ID range exhausted");
    auto particle = birth.particle.particle_id;
    CELER_EXPECT(particle < tracks_.size());
    auto track = std::make_unique<G4Track>(
        new G4DynamicParticle(
            tracks_[particle.get()]->GetParticleDefinition(),
            to_g4vector(static_array_cast<double>(birth.direction)),
            birth.particle.energy.value()),
        native_to_geant<units::ClhepTime>(birth.sim.time),
        native_to_geant<units::ClhepLength>(
            static_array_cast<double>(birth.position)));
    track->SetTrackID(next_secondary_id_--);
    track->SetParentID(parent->second.track->GetTrackID());
    track->SetWeight(birth.sim.weight);
    track->SetVertexPosition(track->GetPosition());
    track->SetVertexMomentumDirection(track->GetMomentumDirection());
    track->SetVertexKineticEnergy(track->GetKineticEnergy());
    auto result = mapped_tracks_.emplace(birth.sim.track_id,
                                         TrackEntry{std::move(track), 0});
    return *result.first->second.track;
}

//---------------------------------------------------------------------------//
/*!
 * Update counters once for a completed step, with length in Geant4 units.
 *
 * Both hit and user-action reconstruction can call this for the same step.
 * Advancing twice is a no-op; skipping or reversing steps is an error.
 */
void GeantTrackReconstruction::advance(
    TrackId id, size_type count, double length)
{
    CELER_EXPECT(track_mapping_);
    auto& entry = mapped_tracks_.at(id);
    if (entry.step_count == count)
    {
        return;
    }
    CELER_VALIDATE(count == entry.step_count + 1,
                   << "out-of-order step for reconstructed track " << id.get());
    CELER_VALIDATE(
        entry.track->GetCurrentStepNumber() < std::numeric_limits<int>::max(),
        << "Geant4 step number range exhausted");
    entry.track->IncrementCurrentStepNumber();
    entry.track->AddTrackLength(length);
    entry.step_count = count;
}

//---------------------------------------------------------------------------//
/*!
 * Restore the G4Track from the reconstruction data.
 *
 * Returns the track for the given particle ID with restored primary track
 * information.
 */
G4Track& GeantTrackReconstruction::view(ParticleId particle_id,
                                        PrimaryId primary_id) const
{
    CELER_EXPECT(primary_id && primary_id >= start_);
    CELER_EXPECT(primary_id < start_ + g4_track_data_.size());
    if constexpr (CELERITAS_DEBUG)
    {
        int cur_event_id = get_current_event_id();
        CELER_VALIDATE(g4_event_id_ == cur_event_id,
                       << "cannot view a track from another event: "
                       << g4_event_id_ << " != current event " << cur_event_id);
    }

    G4Track& track = this->view(particle_id);
    g4_track_data_[primary_id - start_].restore(track);
    return track;
}

//---------------------------------------------------------------------------//
/*!
 * View a track with the given particle ID.
 */
G4Track& GeantTrackReconstruction::view(ParticleId particle_id) const
{
    CELER_EXPECT(particle_id < tracks_.size());
    G4Track& track = *tracks_[particle_id.unchecked_get()];
    step_->SetTrack(&track);
    return track;
}

//---------------------------------------------------------------------------//
// GEANTTRACKRECONSTRUCTION::ACQUIREDDATA
//---------------------------------------------------------------------------//
/*!
 * Restore the G4Track from the reconstruction data. Takes ownership of the
 * user information by unsetting it in the original track.
 */
GeantTrackReconstruction::AcquiredData::AcquiredData(G4Track& track)
    : track_id_{track.GetTrackID()}
    , parent_id_{track.GetParentID()}
    , user_info_{track.GetUserInformation()}
    , creator_process_{track.GetCreatorProcess()}
{
    CELER_EXPECT(*this);
    // Clear user information so that it doesn't get deleted with the G4Track
    track.SetUserInformation(nullptr);
}

//---------------------------------------------------------------------------//
/*!
 * Restore the G4Track from the reconstruction data. The restored track does
 * not have ownership of the user information, user must take care to reset it
 * before deletion of the track.
 */
void GeantTrackReconstruction::AcquiredData::restore(G4Track& track) const
{
    CELER_EXPECT(*this);
    track.SetTrackID(track_id_);
    track.SetParentID(parent_id_);
    track.SetUserInformation(user_info_.get());
    track.SetCreatorProcess(creator_process_);
}

//---------------------------------------------------------------------------//
}  // namespace celeritas
