//------------------------------- -*- C++ -*- -------------------------------//
// Copyright Celeritas contributors: see top-level COPYRIGHT file for details
// SPDX-License-Identifier: (Apache-2.0 OR MIT)
//---------------------------------------------------------------------------//
//! \file celeritas/ext/GeantSd.test.cc
//---------------------------------------------------------------------------//
#include "celeritas/ext/GeantSd.hh"

#include <G4LogicalVolume.hh>
#include <G4LogicalVolumeStore.hh>
#include <G4NistManager.hh>
#include <G4Orb.hh>
#include <G4ParticleDefinition.hh>

#include "corecel/Config.hh"

#include "corecel/ScopedLogStorer.hh"
#include "corecel/io/Logger.hh"
#include "corecel/sys/ActionRegistry.hh"
#include "corecel/sys/Device.hh"
#include "geocel/GeantGeoUtils.hh"
#include "geocel/UnitUtils.hh"
#include "geocel/VolumeParams.hh"
#include "celeritas/SimpleCmsTestBase.hh"
#include "celeritas/ext/GeantSdOutput.hh"
#include "celeritas/geo/CoreGeoParams.hh"
#include "celeritas/global/Stepper.hh"
#include "celeritas/inp/Scoring.hh"
#include "celeritas/phys/ParticleParams.hh"
#include "celeritas/user/StepCollector.hh"

#include "SensDetTestBase.hh"
#include "SimpleSensitiveDetector.hh"
#include "celeritas_test.hh"

namespace celeritas
{
namespace test
{
//---------------------------------------------------------------------------//
class SimpleCmsTest : public SensDetTestBase, public SimpleCmsTestBase
{
  protected:
    void SetUp() override
    {
        sd_setup_.ignore_zero_deposition = false;
        sd_setup_.track = false;

        this->geometry();
        scoped_log_.clear();
    }

    SPConstGeantGeo build_geant_geo(std::string const& filename) const override
    {
        auto result = SensDetTestBase::build_geant_geo(filename);

        // Create unused volume after building geometry
        G4Material* mat
            = G4NistManager::Instance()->FindOrBuildMaterial("G4_AIR");
        SimpleCmsTest::detached_lv = new G4LogicalVolume(
            new G4Orb("unused_solid", 10.0), mat, "unused");

        return result;
    }

    SetStr detector_volumes() const final
    {
        // *Don't* add SD for si_tracker
        return {"em_calorimeter", "had_calorimeter"};
    }

    std::vector<std::string> volume_names(
        std::vector<VolumeId> const& vols) const
    {
        auto const& labels = this->volumes()->volume_labels();

        std::vector<std::string> result;
        for (VolumeId vid : vols)
        {
            result.push_back(labels.at(vid).name);
        }
        return result;
    }

    std::vector<std::string> particle_names(
        GeantSd::VecParticle const& particles) const
    {
        std::vector<std::string> result;
        for (auto* par : particles)
        {
            CELER_ASSERT(par);
            result.push_back(par->GetParticleName());
        }
        return result;
    }

    GeantSd make_hit_manager(bool make_hit_proc = true)
    {
        CELER_EXPECT(!processor_);
        GeantSd result(*this->particle(), sd_setup_, 1);

        if (make_hit_proc)
        {
            processor_ = result.make_local_processor(StreamId{0});
            EXPECT_TRUE(processor_);
        }

        return result;
    }

    std::string get_diagnostics(GeantSd const& hm)
    {
        GeantSdOutput out(
            std::shared_ptr<GeantSd const>(&hm, [](GeantSd const*) {}));
        return to_string(out);
    }

    template<MemSpace M>
    void test_async()
    {
        this->disable_status_checker();
        auto manager = std::make_shared<GeantSd>(this->make_hit_manager());
        auto collector
            = StepCollector::make_and_insert(*this->core(), {manager});
        if constexpr (M == MemSpace::device)
            device().create_streams(1);
        StepperInput inp;
        inp.params = this->core();
        inp.stream_id = StreamId{0};
        inp.num_track_slots = 2;
        inp.actions = std::make_shared<ActionSequence>(
            *this->action_reg(), ActionSequence::Options{});
        Stepper<M> step(inp);
        auto const& hits = this->detectors().at("em_calorimeter")->hits();
        step.warm_up();
        EXPECT_TRUE(hits.energy_deposition.empty());

        Primary primary;
        primary.particle_id = this->particle()->find(pdg::gamma());
        primary.energy = units::MevEnergy{1};
        primary.position = from_cm(Real3{130, 0, 0});
        primary.direction = {1, 0, 0};
        primary.event_id = EventId{0};
        primary.primary_id = PrimaryId{0};
        step.async({&primary, 1});
        EXPECT_TRUE(hits.energy_deposition.empty());
        for (int i = 0; i < 2; ++i)
        {
            static_cast<void>(step.ready());
            step.wait();
            EXPECT_TRUE(hits.energy_deposition.empty());
        }
        step.get();
        EXPECT_EQ(1, hits.energy_deposition.size());
    }

  protected:
    inp::GeantSd sd_setup_;
    ::celeritas::test::ScopedLogStorer scoped_log_{&celeritas::world_logger()};
    static G4LogicalVolume const* detached_lv;
    GeantSd::SPProcessor processor_;
};

G4LogicalVolume const* SimpleCmsTest::detached_lv{nullptr};

TEST_F(SimpleCmsTest, async_host)
{
    this->test_async<MemSpace::host>();
}

TEST_F(SimpleCmsTest, TEST_IF_CELER_DEVICE(async_device))
{
    this->test_async<MemSpace::device>();
}

TEST_F(SimpleCmsTest, persistent_selection)
{
    sd_setup_.track = true;
    sd_setup_.step_length = false;
    GeantSd legacy(*this->particle(), sd_setup_, 1);
    EXPECT_FALSE(legacy.selection().parent_id);
    EXPECT_FALSE(legacy.selection().track_step_count);
    EXPECT_FALSE(legacy.selection().step_length);
    GeantSd persistent(*this->particle(), sd_setup_, 1, true);
    EXPECT_TRUE(persistent.selection().parent_id);
    EXPECT_TRUE(persistent.selection().track_step_count);
    EXPECT_TRUE(persistent.selection().step_length);
    EXPECT_TRUE(persistent.selection().track_status);
}

TEST_F(SimpleCmsTest, no_change)
{
    GeantSd man = this->make_hit_manager();

    EXPECT_EQ(0, man.geant_particles().size());
    EXPECT_EQ(2, man.geant_vols()->size());
    auto vnames = this->volume_names(man.celer_vols());
    static char const* const expected_vnames[]
        = {"em_calorimeter", "had_calorimeter"};
    EXPECT_VEC_EQ(expected_vnames, vnames);
    EXPECT_TRUE(scoped_log_.empty()) << scoped_log_;

    EXPECT_JSON_EQ(
        R"json({"_category":"internal","_label":"hit-manager","locate_touchable":[true,true],"lv_name":["em_calorimeter","had_calorimeter"],"sd_name":["em_calorimeter","had_calorimeter"],"sd_type":["celeritas::test::SimpleSensitiveDetector","celeritas::test::SimpleSensitiveDetector"],"vol_id":[2,3]})json",
        this->get_diagnostics(man));
}

TEST_F(SimpleCmsTest, delete_one)
{
    // Create tracks for each particle type
    sd_setup_.track = true;

    sd_setup_.skip_volumes = find_geant_volumes({"had_calorimeter"});
    GeantSd man = this->make_hit_manager();

    // Check volumes
    EXPECT_EQ(1, man.geant_vols()->size());
    auto vnames = this->volume_names(man.celer_vols());
    static char const* const expected_vnames[] = {"em_calorimeter"};
    EXPECT_VEC_EQ(expected_vnames, vnames);

    // Check particles
    auto pnames = this->particle_names(man.geant_particles());
    static std::string const expected_pnames[] = {"gamma", "e-", "e+"};
    EXPECT_VEC_EQ(expected_pnames, pnames);

    // Check log
    EXPECT_TRUE(scoped_log_.empty()) << scoped_log_;

    EXPECT_JSON_EQ(
        R"json({"_category":"internal","_label":"hit-manager","locate_touchable":[true,true],"lv_name":["em_calorimeter"],"sd_name":["em_calorimeter"],"sd_type":["celeritas::test::SimpleSensitiveDetector"],"vol_id":[2]})json",
        this->get_diagnostics(man));
}

TEST_F(SimpleCmsTest, add_duplicate)
{
    sd_setup_.force_volumes = find_geant_volumes({"em_calorimeter"});
    scoped_log_.level(LogLevel::debug);
    GeantSd man = this->make_hit_manager();
    scoped_log_.level(Logger::default_level());

    EXPECT_EQ(2, man.geant_vols()->size());
    auto vnames = this->volume_names(man.celer_vols());

    static char const* const expected_vnames[]
        = {"em_calorimeter", "had_calorimeter"};
    EXPECT_VEC_EQ(expected_vnames, vnames);
    if (CELERITAS_CORE_GEO == CELERITAS_CORE_GEO_VECGEOM)
    {
        static char const* const expected_log_messages[] = {
            R"(Mapped sensitive detector "em_calorimeter" on logical volume "em_calorimeter"@0x0 (ID=2) to volume ID 2)",
            R"(Mapped sensitive detector "had_calorimeter" on logical volume "had_calorimeter"@0x0 (ID=3) to volume ID 3)",
            R"(Ignored duplicate logical volume "em_calorimeter"@0x0 (ID=2))",
            "Setting up thread-local hit processor for 2 sensitive detectors",
        };
        EXPECT_VEC_EQ(expected_log_messages, scoped_log_.messages());
        static char const* const expected_log_levels[]
            = {"debug", "debug", "debug", "debug"};
        EXPECT_VEC_EQ(expected_log_levels, scoped_log_.levels());
    }

    EXPECT_JSON_EQ(
        R"json({"_category":"internal","_label":"hit-manager","locate_touchable":[true,true],"lv_name":["em_calorimeter","had_calorimeter"],"sd_name":["em_calorimeter","had_calorimeter"],"sd_type":["celeritas::test::SimpleSensitiveDetector","celeritas::test::SimpleSensitiveDetector"],"vol_id":[2,3]})json",
        this->get_diagnostics(man));
}

TEST_F(SimpleCmsTest, add_one)
{
    sd_setup_.force_volumes = find_geant_volumes({"si_tracker"});
    // Since we're asking for a volume that doesn't currently have an
    // SD attached, we can't make the hit processor
    GeantSd man = this->make_hit_manager(/* make_hit_proc = */ false);

    EXPECT_EQ(3, man.geant_vols()->size());
    auto vnames = this->volume_names(man.celer_vols());

    static char const* const expected_vnames[]
        = {"si_tracker", "em_calorimeter", "had_calorimeter"};
    EXPECT_VEC_EQ(expected_vnames, vnames);
    EXPECT_TRUE(scoped_log_.empty()) << scoped_log_;
    EXPECT_JSON_EQ(
        R"json({"_category":"internal","_label":"hit-manager","locate_touchable":[true,true],"lv_name":["si_tracker","em_calorimeter","had_calorimeter"],"sd_name":[null,"em_calorimeter","had_calorimeter"],"sd_type":[null,"celeritas::test::SimpleSensitiveDetector","celeritas::test::SimpleSensitiveDetector"],"vol_id":[1,2,3]})json",
        this->get_diagnostics(man));
}

TEST_F(SimpleCmsTest, no_detector)
{
    // No detectors
    sd_setup_.skip_volumes
        = find_geant_volumes({"em_calorimeter", "had_calorimeter"});
    EXPECT_THROW(this->make_hit_manager(), celeritas::RuntimeError);
    EXPECT_TRUE(scoped_log_.empty()) << scoped_log_;
}

TEST_F(SimpleCmsTest, detached_detector)
{
    // Detector for LV that isn't in the world tree
    sd_setup_.skip_volumes = {};
    sd_setup_.force_volumes = std::unordered_set<G4LogicalVolume const*>{
        SimpleCmsTest::detached_lv};
    EXPECT_THROW(
        try {
            this->make_hit_manager();
        } catch (celeritas::RuntimeError const& e) {
            EXPECT_EQ(
                R"(failed to find Geant4 volume(s) "unused" while mapping sensitive detectors)",
                e.details().what);
            throw;
        },
        celeritas::RuntimeError);
}

//---------------------------------------------------------------------------//
}  // namespace test
}  // namespace celeritas
