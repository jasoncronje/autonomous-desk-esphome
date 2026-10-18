#include "../components/desk_motion/desk_motion.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

using desk_draft::Controller;

struct Rig {
  Controller desk;
  uint32_t now{1000};
  std::vector<uint8_t> tx;
  Rig() { desk.set_sender([this](uint8_t command) { tx.push_back(command); }); }
  void height(int value) {
    const uint8_t frame[] = {0x98, 0x98, 0, 0,
                            static_cast<uint8_t>(value), static_cast<uint8_t>(value)};
    desk.feed(frame, sizeof(frame), now);
  }
  void arm(int value = 90) { height(value); desk.set_enabled(true, now); }
  void tick(uint32_t ms = 50, int value = 90, bool connected = true) {
    now += ms;
    if (value >= 0) height(value);
    desk.tick(now, connected);
  }
  void silence_for(uint32_t ms, int value) {
    const auto count = tx.size();
    for (uint32_t elapsed = 0; elapsed < ms; elapsed += 50) tick(50, value);
    assert(tx.size() == count);
  }
};

void boot_and_disabled() {
  Rig r;
  assert(!r.desk.enabled());
  assert(!r.desk.target(100, r.now));
  assert(!r.desk.nudge(1, r.now));
  assert(!r.desk.hold(1, r.now));
  assert(!r.desk.preset(1, r.now));
  assert(!r.desk.save(1, r.now));
  r.silence_for(5000, 90);
  assert(!r.desk.active());
}

void stream_parser() {
  Rig r;
  const uint8_t partial1[] = {0, 0x98, 0x98, 0x03};
  const uint8_t partial2[] = {0x03, 81, 81};
  r.desk.feed(partial1, sizeof(partial1), r.now);
  assert(!r.desk.has_height());
  r.desk.feed(partial2, sizeof(partial2), r.now);
  assert(r.desk.height() == 81);
  const uint8_t corrupt[] = {0x98,0x98,0,0,82,83, 0x98,0x98,0,0,74,74,
                             0x98,0x98,0,0,124,124, 0x98,0x98,0,3,82,82};
  r.desk.feed(corrupt, sizeof(corrupt), r.now);
  assert(r.desk.height() == 81);
  r.tick(1000, -1);
  assert(!r.desk.fresh(r.now));  // Bad packets do not refresh the watchdog.
  r.height(123);
  assert(r.desk.height() == 123);
  assert(r.desk.fresh(r.now));
}

void target_smooth_and_crossing() {
  Rig r;
  r.arm();
  assert(r.desk.target(95, r.now));
  r.desk.tick(r.now, true);
  assert(r.tx == std::vector<uint8_t>{0});
  for (int h : {90,90,91,92,93,94}) r.tick(50, h);
  assert(r.tx.size() == 7);
  for (size_t i = 1; i < r.tx.size(); ++i) assert(r.tx[i] == 2);
  r.tick(50, 96);  // Skip the exact target; cancel instead of reversing.
  assert(!r.desk.active());
  r.silence_for(5000, 96);
}

void target_down_and_invalid() {
  Rig r;
  r.arm();
  assert(r.desk.target(87, r.now));
  r.desk.tick(r.now, true);
  r.tick(50, 89);
  assert(r.tx.back() == 1);
  r.tick(50, 87);
  assert(!r.desk.active());
  assert(!r.desk.target(std::numeric_limits<float>::quiet_NaN(), r.now));
  assert(!r.desk.target(std::numeric_limits<float>::infinity(), r.now));
  for (float value : {74.0f, 124.0f, 90.5f}) assert(!r.desk.target(value, r.now));
  r.silence_for(1000, 87);
}

void same_height_is_idle() {
  Rig r;
  r.arm();
  assert(r.desk.target(90, r.now));
  r.silence_for(5000, 90);
}

void missing_and_stale_feedback() {
  Rig r;
  r.desk.set_enabled(true, r.now);
  assert(!r.desk.target(100, r.now));
  r.height(90);
  r.now += Controller::FEEDBACK_MS;
  assert(!r.desk.preset(1, r.now));
  r.silence_for(1000, -1);
  r.height(90);
  assert(r.desk.target(100, r.now));
  r.tick(800, 90);
  r.tick(50, 90);
  assert(r.desk.active());
  r.tick(1000, -1);
  assert(!r.desk.active());
  r.silence_for(2000, 90);  // Feedback recovery cannot resume a move.
}

void stalled_target() {
  Rig r;
  r.arm();
  r.desk.target(100, r.now);
  for (int i = 0; i < 60; ++i) r.tick(50, 90);
  assert(!r.desk.active());
  assert(std::strcmp(r.desk.status(), "No height progress") == 0);
  r.silence_for(3000, 90);
}

void cancel_every_mode() {
  for (int mode = 0; mode < 6; ++mode) {
    Rig r;
    r.arm();
    if (mode == 0) r.desk.target(100, r.now);
    if (mode == 1) r.desk.hold(1, r.now);
    if (mode == 2) r.desk.nudge(1, r.now);
    if (mode == 3) r.desk.preset(1, r.now);
    if (mode == 4) r.desk.save(1, r.now);
    if (mode == 5) r.desk.start_hold(1, 123, r.now);
    r.desk.tick(r.now, true);  // Stop between neutral and pending key frame.
    r.desk.stop(r.now);
    assert(!r.desk.active());
    assert(!r.desk.renew_hold(123, r.now));
    r.silence_for(5000, 90);
  }
}

void stop_during_run() {
  for (bool preset : {false, true}) {
    Rig r;
    r.arm();
    if (preset) r.desk.preset(2, r.now);
    else r.desk.target(100, r.now);
    r.desk.tick(r.now, true);
    r.tick(50, 90);
    r.tick(50, 91);
    r.desk.set_enabled(false, r.now);  // Dedicated Stop also disarms.
    assert(!r.desk.target(110, r.now));
    r.silence_for(5000, 91);
  }
}

void replace_target_and_cancel_replacement() {
  Rig r;
  r.arm();
  r.desk.target(100, r.now);
  r.desk.tick(r.now, true);
  r.tick();
  assert(r.tx.back() == 2);
  r.desk.target(80, r.now);
  r.silence_for(700, 90);
  r.tick(50, 90);
  assert(r.tx.back() == 0);
  r.tick(50, 90);
  assert(r.tx.back() == 1);
  r.desk.target(100, r.now);
  r.desk.stop(r.now);
  r.silence_for(5000, 90);
}

void hold_lease_and_release() {
  Rig r;
  r.arm();
  assert(r.desk.start_hold(1, 1, r.now));
  r.desk.tick(r.now, true);
  r.tick();
  for (int i = 0; i < 5; ++i) {
    r.tick(200, 90 + i);
    assert(r.desk.renew_hold(1, r.now));
  }
  assert(!r.desk.renew_hold(2, r.now));
  r.desk.release_hold(2, r.now);
  assert(r.desk.active());
  r.desk.release_hold(1, r.now);
  assert(!r.desk.renew_hold(1, r.now));
  assert(!r.desk.start_hold(1, 1, r.now));
  r.silence_for(1000, 94);
  assert(r.desk.start_hold(1, 2, r.now));
  r.tick(750, 95);
  assert(!r.desk.active());
  assert(!r.desk.renew_hold(2, r.now));
  r.silence_for(1000, 95);
}

void disconnect_and_reconnect() {
  Rig r;
  r.arm();
  r.desk.target(100, r.now);
  r.desk.tick(r.now, true);
  r.tick(50, 90, false);
  assert(!r.desk.enabled());
  assert(!r.desk.active());
  r.silence_for(3000, 90);
  assert(!r.desk.target(100, r.now));
}

void travel_limits_and_direction() {
  for (int height : {75, 123}) {
    Rig r;
    r.arm(height);
    const int direction = height == 75 ? -1 : 1;
    assert(!r.desk.hold(direction, r.now));
    assert(!r.desk.nudge(direction, r.now));
    r.silence_for(2000, height);
  }
  Rig r;
  r.arm();
  assert(r.desk.target(100, r.now));
  r.desk.tick(r.now, true);
  r.tick(50, 90);
  r.tick(50, 89);
  assert(!r.desk.active());
  r.silence_for(1000, 89);
}

void nudge_is_bounded() {
  Rig r;
  r.arm();
  r.desk.nudge(1, r.now);
  r.desk.tick(r.now, true);
  for (int i = 0; i < 4; ++i) r.tick();
  assert(!r.desk.active());
  assert((r.tx == std::vector<uint8_t>{0,2,2,2}));
  r.silence_for(1000, 90);
}

void presets_and_memory_save() {
  for (int slot = 1; slot <= 4; ++slot) {
    Rig r;
    r.arm();
    assert(r.desk.preset(slot, r.now));
    r.desk.tick(r.now, true);
    r.tick();
    assert(r.tx.back() == (1 << (slot + 1)));
    r.tick(50, 91);
    assert(r.tx.back() == 0);
    r.tick(400, 92);
    for (int i = 0; i < 60; ++i) r.tick(50, 92);
    assert(!r.desk.active());
    r.silence_for(1000, 92);
  }
  Rig r;
  r.arm();
  assert(r.desk.save(3, r.now));
  r.desk.tick(r.now, true);
  for (int i = 0; i < 71; ++i) r.tick();
  assert(!r.desk.active());
  assert(r.tx.size() == 71);
  assert(r.tx.front() == 0 && r.tx[69] == 0 && r.tx[70] == 16);
  for (size_t i = 1; i <= 68; ++i) assert(r.tx[i] == 64);
  r.silence_for(1000, 90);
}

void hard_timeout() {
  Rig r;
  r.arm(75);
  r.desk.start_hold(1, 9, r.now);
  for (int i = 1; i <= 300; ++i) {
    r.tick(100, 75 + (i - 1) / 10);
    if (i < 300) assert(r.desk.renew_hold(9, r.now));
  }
  assert(!r.desk.active());
  assert(std::strcmp(r.desk.status(), "Movement timeout") == 0);
  r.silence_for(1000, 104);
}

void clock_wrap_and_inches() {
  Rig r;
  r.now = UINT32_MAX - 100;
  r.arm();
  r.desk.target(95, r.now);
  r.desk.tick(r.now, true);
  r.tick(50, 91);
  r.tick(100, 92);
  assert(r.desk.active());
  r.tick(50, 95);
  assert(!r.desk.active());
  assert(std::fabs(Controller::inches(81) - 31.9f) < 0.01f);
  assert(r.desk.raw_from_inches(31.9f) == 81);
  assert(r.desk.raw_from_inches(29.5f) == 75);
  assert(r.desk.raw_from_inches(48.4f) == 123);
}

void release_after_disarm() {
  for (int mode = 0; mode < 3; ++mode) {
    Rig r;
    r.desk.set_manual_release_on_stop(true);
    r.arm();
    if (mode == 0) assert(r.desk.target(100, r.now));
    if (mode == 1) assert(r.desk.hold(1, r.now));
    if (mode == 2) assert(r.desk.nudge(1, r.now));
    r.desk.tick(r.now, true);
    r.tick();
    assert(r.tx.back() == 2);
    const auto count = r.tx.size();
    r.desk.set_enabled(false, r.now);
    r.desk.stop(r.now);  // Repeated Stop must preserve just one queued release.
    r.desk.tick(r.now, false);  // Also works during disconnect/shutdown.
    assert(r.tx.size() == count + 1 && r.tx.back() == 0);
    r.silence_for(5000, 90);
    assert(!r.desk.enabled());
  }
}

void release_on_target_and_lease_end() {
  Rig r;
  r.desk.set_manual_release_on_stop(true);
  r.arm();
  r.desk.target(95, r.now);
  r.desk.tick(r.now, true);
  r.tick();
  const auto count = r.tx.size();
  r.tick(50, 96);
  assert(!r.desk.active());
  assert(r.tx.size() == count + 1 && r.tx.back() == 0);
  r.silence_for(1000, 96);

  Rig held;
  held.desk.set_manual_release_on_stop(true);
  held.arm();
  held.desk.hold(-1, held.now);
  held.desk.tick(held.now, true);
  held.tick();
  held.tick(700);
  assert(!held.desk.active());
  const auto held_count = held.tx.size();
  held.tick(10);
  assert(held.tx.size() == held_count + 1 && held.tx.back() == 0);
  held.silence_for(1000, 90);
}

void release_never_transmits_at_boot_or_before_direction() {
  Rig r;
  r.desk.set_manual_release_on_stop(true);
  r.desk.set_enabled(false, r.now);
  r.desk.stop(r.now);
  r.silence_for(1000, 90);
  r.arm();
  r.desk.target(100, r.now);
  r.desk.tick(r.now, true);  // Initial neutral, no direction key sent yet.
  assert((r.tx == std::vector<uint8_t>{0}));
  r.desk.stop(r.now);
  r.silence_for(1000, 90);
  assert((r.tx == std::vector<uint8_t>{0}));
}

void release_does_not_keep_presets_or_saves_alive() {
  for (bool save : {false, true}) {
    Rig r;
    r.desk.set_manual_release_on_stop(true);
    r.arm();
    if (save) assert(r.desk.save(1, r.now));
    else assert(r.desk.preset(1, r.now));
    r.desk.tick(r.now, true);
    r.tick();
    const auto count = r.tx.size();
    r.desk.set_enabled(false, r.now);
    r.silence_for(5000, 90);
    assert(r.tx.size() == count);
  }
}

void release_cannot_resume_a_cancelled_hold_or_replacement() {
  Rig r;
  r.desk.set_manual_release_on_stop(true);
  r.arm();
  assert(r.desk.start_hold(1, 7, r.now));
  r.desk.tick(r.now, true);
  r.tick();
  r.desk.release_hold(7, r.now);
  assert(!r.desk.renew_hold(7, r.now));
  r.tick(10);
  assert(r.tx.back() == 0);
  r.silence_for(1000, 90);

  r.desk.target(100, r.now);
  r.desk.tick(r.now, true);
  r.tick();
  assert(r.tx.back() == 2);
  r.desk.target(80, r.now);  // Queue old-key release and new downward move.
  r.desk.set_enabled(false, r.now);  // Cancel replacement before it can start.
  r.tick(10);
  assert(r.tx.back() == 0);
  r.silence_for(5000, 90);
  for (auto key : r.tx) assert(key != 1);
}

void preset_slow_start_and_transient_height() {
  for (int direction : {-1, 1}) {
    Rig r;
    const int origin = direction > 0 ? 79 : 117;
    const int announcement = direction > 0 ? 117 : 79;
    r.arm(origin);
    assert(r.desk.preset(2, r.now));
    r.desk.tick(r.now, true);
    r.tick(50, origin);
    assert(r.tx.back() == 8);
    r.now += 50;
    const uint8_t transient[] = {0x98,0x98,3,3,
        static_cast<uint8_t>(announcement),static_cast<uint8_t>(announcement)};
    r.desk.feed(transient, sizeof(transient), r.now);
    assert(r.desk.height() == origin);
    assert(!r.desk.moving(r.now));
    for (int i = 0; i < 28; ++i) r.tick(50, origin);
    assert(r.desk.active()); // Revision 2 cancelled here before actual travel.
    assert(std::strcmp(r.desk.status(), "Starting preset") == 0);
    r.tick(50, origin + direction);
    assert(std::strcmp(r.desk.status(), "Starting preset") == 0);
    r.tick(400, origin + 2 * direction);
    assert(std::strcmp(r.desk.status(), "Moving to preset") == 0);
    for (int step = 3; step <= 6; ++step) {
      r.tick(400, origin + step * direction);
      assert(r.desk.active());
      assert(r.tx.back() == 0);
    }
    r.desk.stop(r.now);
    r.silence_for(5000, origin + 6 * direction);
  }
}

void preset_single_step_bounce_cannot_confirm_motion() {
  Rig r;
  r.arm();
  r.desk.preset(2, r.now);
  r.desk.tick(r.now, true);
  r.tick();
  for (int i = 0; i < 59; ++i) {
    r.tick(50, i % 2 == 0 ? 91 : 90);
    assert(r.desk.active());
    assert(std::strcmp(r.desk.status(), "Starting preset") == 0);
  }
  r.tick(50, 90);
  assert(!r.desk.active());
  assert(std::strcmp(r.desk.status(), "Preset travel unconfirmed") == 0);
  r.silence_for(1000, 90);
}

void preset_completion_requires_stationary_window() {
  Rig r;
  r.arm();
  r.desk.preset(2, r.now);
  r.desk.tick(r.now, true);
  r.tick();
  r.tick(400, 91);
  r.tick(400, 92);
  for (int i = 0; i < 59; ++i) {
    r.tick(50, 92);
    assert(r.desk.active());
  }
  r.tick(50, 92);
  assert(!r.desk.active());
  assert(std::strcmp(r.desk.status(), "Preset stationary") == 0);
  r.silence_for(5000, 92);
}

void preset_transient_frames_cannot_refresh_feedback() {
  Rig r;
  r.arm(79);
  r.desk.preset(2, r.now);
  r.desk.tick(r.now, true);
  r.tick(50, 79);
  for (int i = 0; i < 20; ++i) r.tick(50, 117);
  assert(!r.desk.active());
  assert(r.desk.height() == 79);
  assert(std::strcmp(r.desk.status(), "Height feedback lost") == 0);
  r.silence_for(1000, 79);
}

void preset_no_motion_has_bounded_startup() {
  Rig r;
  r.arm();
  r.desk.preset(2, r.now);
  r.desk.tick(r.now, true);
  r.tick();
  for (int i = 0; i < 59; ++i) r.tick();
  assert(r.desk.active());
  r.tick();
  assert(!r.desk.active());
  assert(std::strcmp(r.desk.status(), "Preset travel unconfirmed") == 0);
  r.silence_for(1000, 90);
}

void preset_startup_can_be_stopped_or_disconnected() {
  for (bool disconnect : {false, true}) {
    Rig r;
    r.desk.set_manual_release_on_stop(true);
    r.arm(79);
    r.desk.preset(2, r.now);
    r.desk.tick(r.now, true);
    r.tick(50, 79);
    const auto count = r.tx.size();
    if (disconnect) r.tick(50, 79, false);
    else r.desk.set_enabled(false, r.now);
    assert(!r.desk.active() && !r.desk.enabled());
    assert(r.tx.size() == count);
    r.silence_for(5000, 79);
  }
}

void preset_total_timeout_survives_continuing_progress() {
  Rig r;
  r.arm(90);
  r.desk.preset(2, r.now);
  r.desk.tick(r.now, true);
  r.tick();
  r.tick(50, 91);
  r.tick(50, 92);
  for (int i = 0; i < 596; ++i) {
    r.tick(50, (i / 10) % 2 == 0 ? 91 : 92);
    assert(r.desk.active());
  }
  r.tick(50, 92);
  assert(!r.desk.active());
  assert(std::strcmp(r.desk.status(), "Movement timeout") == 0);
  r.silence_for(1000, 92);
}

void preset_filter_and_startup_work_across_clock_wrap() {
  Rig r;
  r.now = UINT32_MAX - 500;
  r.arm(79);
  r.desk.preset(2, r.now);
  r.desk.tick(r.now, true);
  r.tick(50, 79);
  r.tick(50, 117);
  assert(r.desk.height() == 79);
  for (int i = 0; i < 25; ++i) r.tick(50, 79);
  assert(r.desk.active());
  r.tick(50, 80);
  r.tick(400, 81);
  assert(std::strcmp(r.desk.status(), "Moving to preset") == 0);
  // A replacement must not inherit the first preset's confirmed movement.
  r.desk.preset(1, r.now);
  for (int i = 0; i < 15; ++i) r.tick(50, 81);
  r.tick(50, 81);
  assert(std::strcmp(r.desk.status(), "Starting preset") == 0);
  r.desk.stop(r.now);
  r.silence_for(5000, 81);
}

int main() {
  preset_slow_start_and_transient_height();
  preset_single_step_bounce_cannot_confirm_motion();
  preset_completion_requires_stationary_window();
  preset_transient_frames_cannot_refresh_feedback();
  preset_no_motion_has_bounded_startup();
  preset_startup_can_be_stopped_or_disconnected();
  preset_total_timeout_survives_continuing_progress();
  preset_filter_and_startup_work_across_clock_wrap();
  boot_and_disabled();
  stream_parser();
  target_smooth_and_crossing();
  target_down_and_invalid();
  same_height_is_idle();
  missing_and_stale_feedback();
  stalled_target();
  cancel_every_mode();
  stop_during_run();
  replace_target_and_cancel_replacement();
  hold_lease_and_release();
  disconnect_and_reconnect();
  travel_limits_and_direction();
  nudge_is_bounded();
  presets_and_memory_save();
  hard_timeout();
  clock_wrap_and_inches();
  release_after_disarm();
  release_on_target_and_lease_end();
  release_never_transmits_at_boot_or_before_direction();
  release_does_not_keep_presets_or_saves_alive();
  release_cannot_resume_a_cancelled_hold_or_replacement();
  std::cout << "30 simulated test scenarios passed; no device connection.\n";
}
