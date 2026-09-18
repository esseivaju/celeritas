//------------------------------- -*- C++ -*- -------------------------------//
// Copyright Celeritas contributors: see top-level COPYRIGHT file for details
// SPDX-License-Identifier: (Apache-2.0 OR MIT)
//---------------------------------------------------------------------------//
//! \file celeritas/ext/detail/SteppingActionProcessor.test.cc
//---------------------------------------------------------------------------//
#include "celeritas/ext/detail/SteppingActionProcessor.hh"

#include <functional>
#include <future>
#include <stdexcept>
#include <G4DynamicParticle.hh>
#include <G4EventManager.hh>
#include <G4LogicalVolume.hh>
#include <G4LogicalVolumeStore.hh>
#include <G4MultiSteppingAction.hh>
#include <G4ParticleTable.hh>
#include <G4Region.hh>
#include <G4Step.hh>
#include <G4Track.hh>
#include <G4UserSteppingAction.hh>
#include <G4VUserTrackInformation.hh>

#include "corecel/sys/ActionRegistry.hh"
#include "geocel/GeantGeoParams.hh"
#include "geocel/UnitUtils.hh"
#include "geocel/VolumeParams.hh"
#include "celeritas/SimpleCmsTestBase.hh"
#include "celeritas/UnitTypes.hh"
#include "celeritas/ext/GeantSteppingAction.hh"
#include "celeritas/global/CoreState.hh"
#include "celeritas/global/Stepper.hh"
#include "celeritas/phys/ParticleParams.hh"
#include "celeritas/track/TrackInitParams.hh"
#include "celeritas/user/StepCollector.hh"

#include "celeritas_test.hh"

namespace celeritas
{
namespace detail
{
namespace test
{
namespace
{
class FunctionAction final : public G4UserSteppingAction
{
  public:
    explicit FunctionAction(std::function<void(G4Step const*)> f)
        : f_(std::move(f))
    {
    }
    void UserSteppingAction(G4Step const* s) final { f_(s); }

  private:
    std::function<void(G4Step const*)> f_;
};

struct TruthInfo final : G4VUserTrackInformation
{
    int generation;
    int* destroyed;
    TruthInfo(int gen, int* d) : generation(gen), destroyed(d) {}
    ~TruthInfo() final { ++*destroyed; }
};

struct RestoreRegion
{
    G4LogicalVolume* volume;
    G4Region* old_region;
    ~RestoreRegion()
    {
        volume->GetRegion()->SetRegionalSteppingAction(nullptr);
        volume->SetRegion(old_region);
    }
};
}  // namespace

class SteppingActionProcessorTest : public ::celeritas::test::SimpleCmsTestBase
{
  protected:
    void SetUp() override
    {
        this->geometry();
        particles_ = {G4ParticleTable::GetParticleTable()->FindParticle(22)};
        ASSERT_NE(nullptr, particles_.front());
        GeantTrackReconstruction::get_current_event_id = [] { return 0; };
        processor_ = std::make_unique<SteppingActionProcessor>(particles_);
        processor_->track_reconstruction()->init_event();
        G4Track original(
            new G4DynamicParticle(particles_[0], {1, 0, 0}, 1), 0, {});
        original.SetTrackID(17);
        EXPECT_EQ(PrimaryId{0},
                  processor_->track_reconstruction()->acquire(original));
    }
    void TearDown() override
    {
        G4EventManager::GetEventManager()->SetUserAction(
            static_cast<G4UserSteppingAction*>(nullptr));
        action_.reset();
        processor_->track_reconstruction()->clear();
        processor_.reset();
        GeantTrackReconstruction::get_current_event_id = nullptr;
    }
    void set_action(std::unique_ptr<G4UserSteppingAction> a)
    {
        G4EventManager::GetEventManager()->SetUserAction(a.get());
        action_ = std::move(a);
    }
    StepOutput step(
        TrackId id = TrackId{0}, TrackId parent = {}, size_type count = 1)
    {
        StepOutput out;
        out.track_id = {id};
        out.parent_id = {parent};
        out.primary_id = {PrimaryId{0}};
        out.event_id = {EventId{0}};
        out.particle_id = {ParticleId{0}};
        out.track_step_count = {count};
        out.track_status = {TrackStatus::alive};
        out.step_length = {1};
        out.weight = {1};
        out.energy_deposition = {units::MevEnergy{0}};
        out.num_volume_levels = 2;
        auto const& names = this->volumes()->volume_instance_labels();
        for (auto sp : {StepPoint::pre, StepPoint::post})
        {
            auto& p = out.points[sp];
            p.pos = {::celeritas::test::from_cm(Real3{100, 0, 0})};
            p.dir = {{1, 0, 0}};
            p.time = {0};
            p.energy = {units::MevEnergy{1}};
            p.volume_instance_ids = {names.find_unique("world_PV"),
                                     names.find_unique("si_tracker_pv")};
        }
        return out;
    }
    SecondaryBirth birth(size_type id, size_type parent, size_type count = 1)
    {
        SecondaryBirth b;
        b.sim.track_id = TrackId{id};
        b.sim.parent_id = TrackId{parent};
        b.sim.primary_id = PrimaryId{0};
        b.sim.event_id = EventId{0};
        b.sim.weight = 1;
        b.particle = {ParticleId{0}, units::MevEnergy{0.5}};
        b.direction = {1, 0, 0};
        b.position = ::celeritas::test::from_cm(Real3{100, 0, 0});
        b.parent_step = count;
        return b;
    }
    std::vector<G4ParticleDefinition const*> particles_;
    std::unique_ptr<SteppingActionProcessor> processor_;
    std::unique_ptr<G4UserSteppingAction> action_;
};

class AsyncSteppingActionTest : public SteppingActionProcessorTest
{
  protected:
    SPConstTrackInit build_init() final
    {
        TrackInitParams::Input inp;
        inp.capacity = 8192;
        inp.max_events = 1;
        inp.save_secondaries = true;
        return std::make_shared<TrackInitParams>(inp);
    }

    template<MemSpace M>
    void test_async(int fail_at = -1)
    {
        this->disable_status_checker();
        auto core = this->core();
        auto action = std::make_shared<GeantSteppingAction>(
            this->action_reg()->next_id(), *this->particle(), 1);
        this->action_reg()->insert(action);
        auto processor = action->make_local_processor(StreamId{0});
        auto collector
            = StepCollector::make_and_insert(*core, {action}, "geant-user");
        auto& tracks = *processor->track_reconstruction();
        tracks.init_event();
        G4Track original(
            new G4DynamicParticle(particles_[0], {1, 0, 0}, 1), 0, {});
        original.SetTrackID(41);
        Primary primary;
        primary.primary_id = tracks.acquire(original);
        primary.particle_id = this->particle()->find(pdg::gamma());
        primary.energy = units::MevEnergy{1};
        primary.position = ::celeritas::test::from_cm(Real3{100, 0, 0});
        primary.direction = {1, 0, 0};
        primary.event_id = EventId{0};
        int calls = 0;
        this->set_action(std::make_unique<FunctionAction>([&](G4Step const*) {
            ++calls;
            if (fail_at == 0)
                throw std::logic_error("global callback failed");
        }));
        auto* lv
            = G4LogicalVolumeStore::GetInstance()->GetVolume("si_tracker");
        CELER_ASSERT(lv);
        G4Region region("async-callback-region");
        FunctionAction regional([&](G4Step const*) {
            if (fail_at == 1)
                throw std::logic_error("regional callback failed");
        });
        RestoreRegion restore{lv, lv->GetRegion()};
        region.SetRegionalSteppingAction(&regional);
        lv->SetRegion(&region);
        if constexpr (M == MemSpace::device)
            device().create_streams(1);
        StepperInput inp;
        inp.params = core;
        inp.stream_id = StreamId{0};
        inp.num_track_slots = 2;
        inp.actions = std::make_shared<ActionSequence>(
            *this->action_reg(), ActionSequence::Options{});
        Stepper<M> step(inp);
        step.warm_up();
        EXPECT_EQ(0, calls);
        step.async({&primary, 1});
        // Stage a later primary without consuming or overwriting snapshots.
        original.SetTrackID(42);
        primary.primary_id = tracks.acquire(original);
        step.push_primary(primary);
        step.stage_primaries();
        this->consume_steps(step, calls, fail_at);
    }

    void consume_steps(StepperInterface& step, int& calls, int fail_at)
    {
        for (int i = 0; i < 4; ++i)
        {
            auto before = calls;
            static_cast<void>(step.ready());
            step.wait();
            step.wait();
            EXPECT_EQ(before, calls);
            if (fail_at >= 0)
            {
                EXPECT_THROW(step.get(), std::logic_error);
                EXPECT_EQ(before + 1, calls);
                EXPECT_THROW(step.get(), std::logic_error);
                EXPECT_THROW(step.async(), std::logic_error);
                EXPECT_EQ(before + 1, calls);
                return;
            }
            auto result = step.get();
            EXPECT_EQ(before + result.active, calls);
            EXPECT_THROW(step.get(), RuntimeError);
            if (i < 3)
            {
                step.async();
                EXPECT_EQ(before + result.active, calls);
            }
        }
        EXPECT_GT(calls, 0);
    }
};

TEST_F(AsyncSteppingActionTest, host)
{
    this->test_async<MemSpace::host>();
}

TEST_F(AsyncSteppingActionTest, TEST_IF_CELER_DEVICE(device))
{
    this->test_async<MemSpace::device>();
}

TEST_F(AsyncSteppingActionTest, global_failure)
{
    this->test_async<MemSpace::host>(0);
}

TEST_F(AsyncSteppingActionTest, regional_failure)
{
    this->test_async<MemSpace::host>(1);
}

TEST_F(AsyncSteppingActionTest, TEST_IF_CELER_DEVICE(regional_failure_device))
{
    this->test_async<MemSpace::device>(1);
}

TEST_F(SteppingActionProcessorTest, absent_and_single)
{
    processor_->dispatch(this->step(), {});
    int count = 0;
    this->set_action(std::make_unique<FunctionAction>([&](G4Step const* s) {
        ++count;
        EXPECT_EQ(17, s->GetTrack()->GetTrackID());
        EXPECT_EQ(2, s->GetTrack()->GetCurrentStepNumber());
        EXPECT_EQ(0, s->GetTotalEnergyDeposit());
        EXPECT_TRUE(s->GetSecondaryInCurrentStep()->empty());
    }));
    processor_->dispatch(this->step(TrackId{0}, {}, 2), {});
    EXPECT_EQ(1, count);
    processor_->dispatch(StepOutput{}, {});
    EXPECT_EQ(1, count);
}

TEST_F(SteppingActionProcessorTest, composite_regional_and_crossing)
{
    auto out = this->step();
    // Reconstruct once to obtain the pre-step LV without consuming a new step.
    GeantStepReconstruction reconstruction(particles_,
                                           StepSelection::all(),
                                           {{true, true}},
                                           {},
                                           processor_->track_reconstruction());
    auto* s = reconstruction(out, 0);
    auto* lv = s->GetPreStepPoint()->GetPhysicalVolume()->GetLogicalVolume();
    auto* old_region = lv->GetRegion();
    G4Region pre_region("callback-pre"), post_region("callback-post");
    std::vector<int> called;
    FunctionAction regional([&](G4Step const* step) {
        called.push_back(4);
        EXPECT_EQ(
            lv,
            step->GetPreStepPoint()->GetPhysicalVolume()->GetLogicalVolume());
    });
    FunctionAction other([&](G4Step const*) { called.push_back(5); });
    pre_region.SetRegionalSteppingAction(&regional);
    post_region.SetRegionalSteppingAction(&other);
    lv->SetRegion(&pre_region);
    auto* post_lv
        = s->GetPreStepPoint()->GetTouchable()->GetVolume(1)->GetLogicalVolume();
    auto* old_post_region = post_lv->GetRegion();
    post_lv->SetRegion(&post_region);
    auto restore = [&] {
        lv->SetRegion(old_region);
        post_lv->SetRegion(old_post_region);
        pre_region.SetRegionalSteppingAction(nullptr);
        post_region.SetRegionalSteppingAction(nullptr);
    };
    auto top = std::make_unique<G4MultiSteppingAction>();
    top->push_back(std::make_unique<FunctionAction>(
        [&](G4Step const*) { called.push_back(1); }));
    auto nested = std::make_unique<G4MultiSteppingAction>();
    nested->push_back(std::make_unique<FunctionAction>(
        [&](G4Step const*) { called.push_back(2); }));
    nested->push_back(std::make_unique<FunctionAction>(
        [&](G4Step const*) { called.push_back(3); }));
    top->push_back(std::move(nested));
    this->set_action(std::move(top));
    // Crossing into the world must still dispatch the tracker region.
    out.points[StepPoint::post].volume_instance_ids[1] = {};
    processor_->dispatch(out, {});
    EXPECT_EQ((std::vector<int>{1, 2, 3, 4}), called);
    called.clear();
    this->set_action(nullptr);
    processor_->dispatch(this->step(TrackId{0}, {}, 2), {});
    EXPECT_EQ((std::vector<int>{4}), called);
    restore();
}

TEST_F(SteppingActionProcessorTest, truth_and_secondary_lists)
{
    int destroyed = 0;
    std::vector<int> seen, children;
    this->set_action(std::make_unique<FunctionAction>([&](G4Step const* s) {
        auto* track = s->GetTrack();
        auto* info = static_cast<TruthInfo*>(track->GetUserInformation());
        if (track->GetTrackID() > 0 && !info)
        {
            info = new TruthInfo(0, &destroyed);
            track->SetUserInformation(info);
        }
        ASSERT_NE(nullptr, info);
        seen.push_back(track->GetTrackID());
        children.push_back(s->GetNumberOfSecondariesInCurrentStep());
        for (auto* child : *s->GetSecondaryInCurrentStep())
        {
            EXPECT_EQ(track->GetTrackID(), child->GetParentID());
            EXPECT_EQ(nullptr, child->GetUserInformation());
            child->SetUserInformation(
                new TruthInfo(info->generation + 1, &destroyed));
        }
        if (track->GetTrackID() == -3)
            EXPECT_EQ(2, info->generation);
    }));
    std::vector<SecondaryBirth> births{this->birth(1, 0), this->birth(2, 0)};
    processor_->dispatch(this->step(), make_span(births));
    births = {this->birth(3, 1)};
    processor_->dispatch(this->step(TrackId{1}, TrackId{0}), make_span(births));
    processor_->dispatch(this->step(TrackId{2}, TrackId{0}), {});
    processor_->dispatch(this->step(TrackId{3}, TrackId{1}), {});
    processor_->dispatch(this->step(TrackId{0}, {}, 2), {});
    EXPECT_EQ((std::vector<int>{17, -1, -2, -3, 17}), seen);
    EXPECT_EQ((std::vector<int>{2, 1, 0, 0, 0}), children);
    EXPECT_EQ(0, destroyed);
    processor_->track_reconstruction()->clear();
    EXPECT_EQ(4, destroyed);
}

TEST_F(SteppingActionProcessorTest, terminal_and_world_exit)
{
    auto out = this->step();
    out.track_status[0] = TrackStatus::killed;
    out.points[StepPoint::post].volume_instance_ids = {{}, {}};
    int called = 0;
    this->set_action(std::make_unique<FunctionAction>([&](G4Step const* s) {
        ++called;
        EXPECT_EQ(fStopAndKill, s->GetTrack()->GetTrackStatus());
        EXPECT_EQ(fWorldBoundary, s->GetPostStepPoint()->GetStepStatus());
        EXPECT_EQ(nullptr, s->GetPostStepPoint()->GetPhysicalVolume());
    }));
    processor_->dispatch(out, {});
    EXPECT_EQ(1, called);
}

TEST_F(SteppingActionProcessorTest, warmup_inactive_and_failed_batch)
{
    HostVal<StepParamsData> params;
    params.selection = StepSelection::all();
    params.num_volume_levels = 2;
    HostCRef<StepParamsData> params_ref;
    params_ref = params;
    HostVal<StepStateData> state;
    resize(&state, params_ref, StreamId{0}, 1);
    HostRef<StepStateData> ref;
    ref = state;
    CoreState<MemSpace::host> core_state(*this->core(), StreamId{0}, 1);
    processor_->initialize(ref);
    processor_->initialize_births(
        core_state.ref().init.secondary_births.size());

    // Inactive slots are ignored during warmup and ordinary iterations.
    state.data.track_id[TrackSlotId{0}] = {};
    core_state.warming_up(true);
    processor_->save_steps(ref);
    processor_->save_births(core_state);
    processor_->dispatch(core_state);
    core_state.warming_up(false);
    processor_->save_steps(ref);
    processor_->save_births(core_state);
    processor_->dispatch(core_state);

    auto out = this->step();
    auto assign = [](auto const& src, auto& dst) {
        std::copy(src.begin(), src.end(), dst.data().get());
    };
#define COPY_STEP(FIELD) assign(out.FIELD, state.data.FIELD)
    COPY_STEP(track_id);
    COPY_STEP(parent_id);
    COPY_STEP(primary_id);
    COPY_STEP(event_id);
    COPY_STEP(particle_id);
    COPY_STEP(track_step_count);
    COPY_STEP(track_status);
    COPY_STEP(step_length);
    COPY_STEP(weight);
    COPY_STEP(energy_deposition);
#undef COPY_STEP
    for (auto sp : {StepPoint::pre, StepPoint::post})
    {
#define COPY_POINT(FIELD) \
    assign(out.points[sp].FIELD, state.data.points[sp].FIELD)
        COPY_POINT(pos);
        COPY_POINT(dir);
        COPY_POINT(time);
        COPY_POINT(energy);
        COPY_POINT(volume_instance_ids);
#undef COPY_POINT
    }
    int calls = 0;
    this->set_action(std::make_unique<FunctionAction>([&](G4Step const*) {
        ++calls;
        throw std::logic_error("callback failed");
    }));
    processor_->save_steps(ref);
    processor_->save_births(core_state);
    EXPECT_EQ(0, calls);
    EXPECT_THROW(processor_->dispatch(core_state), std::logic_error);
    EXPECT_EQ(1, calls);
    // A new snapshot must not overwrite the failed batch or replay it.
    EXPECT_THROW(processor_->save_steps(ref), RuntimeError);
    EXPECT_EQ(1, calls);
}

TEST_F(SteppingActionProcessorTest, mutations_exceptions_and_worker)
{
    auto top = std::make_unique<G4MultiSteppingAction>();
    top->push_back(std::make_unique<FunctionAction>(
        [](G4Step const* s) { s->GetTrack()->SetTrackStatus(fStopAndKill); }));
    top->push_back(std::make_unique<FunctionAction>(
        [](G4Step const* s) { s->GetTrack()->SetTrackStatus(fAlive); }));
    this->set_action(std::move(top));
    EXPECT_NO_THROW(processor_->dispatch(this->step(), {}));
    this->set_action(std::make_unique<FunctionAction>(
        [](G4Step const* s) { s->GetTrack()->SetTrackStatus(fSuspend); }));
    EXPECT_THROW(processor_->dispatch(this->step(TrackId{0}, {}, 2), {}),
                 RuntimeError);
    this->set_action(std::make_unique<FunctionAction>(
        [](G4Step const*) { throw std::logic_error("callback failed"); }));
    EXPECT_THROW(processor_->dispatch(this->step(TrackId{0}, {}, 3), {}),
                 std::logic_error);
    auto failure = std::async(std::launch::async, [&] {
        EXPECT_THROW(processor_->dispatch(StepOutput{}, {}), RuntimeError);
    });
    failure.get();
}
}  // namespace test
}  // namespace detail
}  // namespace celeritas
