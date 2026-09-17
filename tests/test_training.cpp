#include "core/training.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static jojo::ResolvedInputFrame sample_inputs() {
    jojo::ResolvedInputFrame inputs{};
    for (const auto action : jojo::all_game_actions()) {
        inputs[0].actions[action] = false;
        inputs[1].actions[action] = false;
    }
    inputs[0].actions[jojo::GameAction::attack_light] = true;
    inputs[1].actions[jojo::GameAction::left] = true;
    return inputs;
}

static jojo::TrainingFrameSample sample(std::uint64_t frame, jojo::FramePhase phase) {
    jojo::TrainingFrameSample s{};
    s.frame_index = frame;
    s.phase = phase;
    s.hitstop_frames = 2;
    s.hitstun_frames = 7;
    s.blockstun_frames = 4;
    s.attacker_recovery_frames = 3;
    s.defender_stun_frames = 8;
    s.cancel_window = phase == jojo::FramePhase::active;
    s.combo_count = 3;
    s.damage = 120;
    s.scaling_percent = 80;
    s.inputs = sample_inputs();
    s.collision_boxes.push_back({10.0f, 20.0f, 30.0f, 40.0f, jojo::CollisionBoxKind::attack, "attack"});
    s.collision_boxes.push_back({12.0f, 18.0f, 22.0f, 46.0f, jojo::CollisionBoxKind::vulnerable, "body"});
    return s;
}

static void test_timeline_is_bounded_and_monotonic() {
    jojo::TrainingTimeline timeline(3);
    CHECK(timeline.record(sample(10, jojo::FramePhase::startup)));
    CHECK(timeline.record(sample(11, jojo::FramePhase::active)));
    CHECK(!timeline.record(sample(11, jojo::FramePhase::active)));
    CHECK(!timeline.record(sample(9, jojo::FramePhase::startup)));
    CHECK(timeline.record(sample(12, jojo::FramePhase::recovery)));
    CHECK(timeline.record(sample(13, jojo::FramePhase::neutral)));
    CHECK(timeline.samples().size() == 3);
    CHECK(timeline.samples().front().frame_index == 11);
    CHECK(timeline.samples().back().frame_index == 13);
}

static void test_meter_has_semantics_beyond_color() {
    jojo::TrainingTimeline timeline(16);
    CHECK(timeline.record(sample(1, jojo::FramePhase::neutral)));
    CHECK(timeline.record(sample(2, jojo::FramePhase::startup)));
    CHECK(timeline.record(sample(3, jojo::FramePhase::startup)));
    CHECK(timeline.record(sample(4, jojo::FramePhase::active)));
    CHECK(timeline.record(sample(5, jojo::FramePhase::active)));
    CHECK(timeline.record(sample(6, jojo::FramePhase::recovery)));
    const auto meter = timeline.frame_meter();
    CHECK(meter.size() == 3);
    if (meter.size() == 3) {
        CHECK(meter[0].phase == jojo::FramePhase::startup);
        CHECK(meter[0].first_frame == 2);
        CHECK(meter[0].last_frame == 3);
        CHECK(meter[1].phase == jojo::FramePhase::active);
        CHECK(meter[2].phase == jojo::FramePhase::recovery);
        for (const auto& segment : meter) {
            CHECK(!segment.label.empty());
            CHECK(!segment.icon.empty());
        }
    }
}

static void test_combat_diagnostics_and_collision_validation() {
    auto s = sample(20, jojo::FramePhase::active);
    CHECK(jojo::frame_advantage(s) == 5);
    CHECK(s.hitstop_frames == 2);
    CHECK(s.hitstun_frames == 7);
    CHECK(s.blockstun_frames == 4);
    CHECK(s.cancel_window);
    CHECK(s.combo_count == 3);
    CHECK(s.damage == 120);
    CHECK(s.scaling_percent == 80);

    jojo::TrainingTimeline timeline(4);
    CHECK(timeline.record(s));
    auto invalid = sample(21, jojo::FramePhase::recovery);
    invalid.collision_boxes[0].width = -1.0f;
    CHECK(!timeline.record(invalid));
    invalid = sample(21, jojo::FramePhase::recovery);
    invalid.collision_boxes[0].label.clear();
    CHECK(!timeline.record(invalid));
}

static void test_input_history_is_stable_for_both_players() {
    jojo::TrainingTimeline timeline(4);
    CHECK(timeline.record(sample(30, jojo::FramePhase::active)));
    const auto history = timeline.input_history();
    CHECK(history.size() == 2);
    if (history.size() == 2) {
        CHECK(history[0].frame_index == 30);
        CHECK(history[0].player_index == 0);
        CHECK(history[0].actions.size() == 1);
        CHECK(history[0].actions[0] == jojo::GameAction::attack_light);
        CHECK(history[1].player_index == 1);
        CHECK(history[1].actions.size() == 1);
        CHECK(history[1].actions[0] == jojo::GameAction::left);
    }
}

static void test_pause_and_frame_step_are_exact() {
    jojo::TrainingPlaybackControl control{};
    CHECK(control.consume_frame_permission());
    control.set_paused(true);
    CHECK(control.paused());
    CHECK(!control.consume_frame_permission());
    CHECK(control.request_frame_step());
    CHECK(control.consume_frame_permission());
    CHECK(!control.consume_frame_permission());
    for (int i = 0; i < 20; ++i) CHECK(control.request_frame_step());
    int granted = 0;
    while (control.consume_frame_permission()) ++granted;
    CHECK(granted == 8);
    control.set_paused(false);
    CHECK(!control.paused());
    CHECK(control.consume_frame_permission());
}

static void test_snapshots_are_deterministic_and_integrity_checked() {
    const std::vector<std::uint8_t> bytes{1, 2, 3, 4, 5, 6};
    const auto a = jojo::make_training_snapshot(99, bytes);
    const auto b = jojo::make_training_snapshot(99, bytes);
    CHECK(a);
    CHECK(b);
    if (a && b) {
        CHECK(a.value.digest_hex == b.value.digest_hex);
        CHECK(a.value.state == bytes);
        CHECK(jojo::validate_training_snapshot(a.value));
        auto corrupted = a.value;
        corrupted.state[0] ^= 0xff;
        CHECK(!jojo::validate_training_snapshot(corrupted));
    }
}

static void test_ten_save_slots_replace_and_load() {
    jojo::TrainingStateSlots slots{};
    const std::vector<std::uint8_t> first{9, 8, 7};
    const std::vector<std::uint8_t> second{6, 5, 4};
    auto a = jojo::make_training_snapshot(100, first);
    auto b = jojo::make_training_snapshot(101, second);
    CHECK(a && b);
    CHECK(!slots.load(0));
    CHECK(!slots.save(10, a.value));
    CHECK(slots.save(0, a.value));
    CHECK(slots.save(0, b.value));
    const auto loaded = slots.load(0);
    CHECK(loaded);
    if (loaded) {
        CHECK(loaded.value.frame_index == 101);
        CHECK(loaded.value.state == second);
    }
}

int main() {
    test_timeline_is_bounded_and_monotonic();
    test_meter_has_semantics_beyond_color();
    test_combat_diagnostics_and_collision_validation();
    test_input_history_is_stable_for_both_players();
    test_pause_and_frame_step_are_exact();
    test_snapshots_are_deterministic_and_integrity_checked();
    test_ten_save_slots_replace_and_load();
    if (failures) {
        std::cerr << failures << " training assertion(s) failed\n";
        return 1;
    }
    std::cout << "training assertions passed\n";
    return 0;
}
