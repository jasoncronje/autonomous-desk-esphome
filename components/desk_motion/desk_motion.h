#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>

// Protocol reference: https://github.com/Stefichen5/AutonomousControl#protocol
// Only tick() writes to UART. No delayed actions, threads, or queued commands.
namespace desk_draft {

class Controller {
 public:
  enum class Mode { IDLE, TARGET, HOLD, NUDGE, PRESET, SAVE };
  static constexpr uint32_t TX_MS = 50;
  static constexpr uint32_t FEEDBACK_MS = 1000;
  static constexpr uint32_t STALL_MS = 3000;
  static constexpr uint32_t MAX_MOVE_MS = 30000;
  static constexpr uint32_t HOLD_LEASE_MS = 750;
  static constexpr uint32_t QUIET_MS = 750;
  static constexpr uint32_t NUDGE_MS = 150;
  static constexpr uint32_t SAVE_MS = 3400;
  // Observed preset travel is roughly 2-5 raw steps/s. Allow 10 plus two
  // steps for quantization/batching, but reject the startup 79 -> 117 flash.
  static constexpr uint32_t PRESET_MAX_STEPS_PER_SECOND = 10;

  explicit Controller(int minimum = 75, int maximum = 123)
      : minimum_(minimum), maximum_(maximum) {}

  void set_sender(std::function<void(uint8_t)> sender) { sender_ = sender; }
  // Experimental revision: one neutral frame may release a manual key more
  // promptly than silence. Its physical braking behavior is not yet verified.
  void set_manual_release_on_stop(bool enabled) {
    manual_release_on_stop_ = enabled;
    if (!enabled) release_pending_ = false;
  }
  void set_limits(int minimum, int maximum) {
    if (minimum > 0 && minimum < maximum && maximum < 256) {
      minimum_ = minimum;
      maximum_ = maximum;
    }
  }
  bool enabled() const { return enabled_; }
  bool active() const { return mode_ != Mode::IDLE; }
  Mode mode() const { return mode_; }
  bool has_height() const { return have_height_; }
  int height() const { return height_; }
  const char *status() const { return status_; }
  bool fresh(uint32_t now) const {
    return have_height_ && elapsed(now, feedback_ms_) < FEEDBACK_MS;
  }
  bool moving(uint32_t now) const {
    return have_changed_ && elapsed(now, changed_ms_) < 750;
  }

  void set_enabled(bool enabled, uint32_t now) {
    if (!enabled) stop(now, "Disabled");
    enabled_ = enabled;
    if (enabled && !active()) status_ = "Ready";
  }

  // Always cancel immediately. Optionally queue ONE manual-key release frame,
  // then silence; never sustain neutral keepalives or invent a direction pulse.
  // Native preset/save stops remain silent because neutral may sustain presets.
  void stop(uint32_t now, const char *reason = "Stopped") {
    if (manual_release_on_stop_ && phase_ == Phase::RUN &&
        (mode_ == Mode::TARGET || mode_ == Mode::HOLD || mode_ == Mode::NUDGE)) {
      release_pending_ = true;
    }
    mode_ = Mode::IDLE;
    hold_token_ = 0;
    phase_ = Phase::QUIET;
    cooling_ = true;
    stopped_ms_ = now;
    status_ = reason;
  }

  bool target(float value, uint32_t now) {
    if (!std::isfinite(value) || value < minimum_ || value > maximum_ ||
        value != std::round(value)) {
      stop(now, "Invalid target");
      return false;
    }
    if (!ready(now)) return false;
    const int requested = static_cast<int>(value);
    if (requested == height_) {
      stop(now, "At target");
      return true;
    }
    if (mode_ == Mode::TARGET && target_ == requested) return true;
    begin(Mode::TARGET, requested > height_ ? 0x02 : 0x01, now);
    target_ = requested;
    status_ = "Moving to height";
    return true;
  }

  // Renew every 250 ms while held; release calls stop(). A lost release/client
  // stops renewal, so this lease expires even if another API client stays up.
  bool hold(int direction, uint32_t now) {
    if (!ready(now) || !valid_direction(direction, now)) return false;
    hold_token_ = 0;
    const uint8_t code = direction > 0 ? 0x02 : 0x01;
    if (mode_ != Mode::HOLD || command_ != code) {
      begin(Mode::HOLD, code, now);
      status_ = direction > 0 ? "Moving up (held)" : "Moving down (held)";
    }
    renewed_ms_ = now;
    return true;
  }

  // Sessioned API calls distinguish a new press from renewals. A renewal that
  // arrives after Stop/release must never start another move.
  bool start_hold(int direction, uint32_t token, uint32_t now) {
    if (token == 0 || token == last_hold_token_) return false;
    if (!hold(direction, now)) return false;
    hold_token_ = last_hold_token_ = token;
    return true;
  }
  bool renew_hold(uint32_t token, uint32_t now) {
    if (mode_ != Mode::HOLD || token == 0 || token != hold_token_) return false;
    if (elapsed(now, renewed_ms_) >= HOLD_LEASE_MS || !ready(now)) {
      stop(now, "Hold renewal expired");
      return false;
    }
    renewed_ms_ = now;
    return true;
  }
  void release_hold(uint32_t token, uint32_t now) {
    if (mode_ == Mode::HOLD && token != 0 && token == hold_token_) stop(now);
  }

  bool nudge(int direction, uint32_t now) {
    if (!ready(now) || !valid_direction(direction, now)) return false;
    begin(Mode::NUDGE, direction > 0 ? 0x02 : 0x01, now);
    status_ = "Nudging";
    return true;
  }

  bool preset(int slot, uint32_t now) {
    if (slot < 1 || slot > 4) {
      stop(now, "Invalid preset");
      return false;
    }
    if (!ready(now)) return false;
    begin(Mode::PRESET, static_cast<uint8_t>(1U << (slot + 1)), now);
    status_ = "Starting preset";
    return true;
  }

  bool save(int slot, uint32_t now) {
    if (!ready(now)) return false;
    if (slot < 1 || slot > 4 || active() || moving(now)) {
      stop(now, "Save rejected: moving or invalid slot");
      return false;
    }
    begin(Mode::SAVE, 0x40, now);
    save_slot_ = static_cast<uint8_t>(1U << (slot + 1));
    status_ = "Saving preset";
    return true;
  }

  // Sliding window accepts complete six-byte frames across arbitrary UART
  // chunks. All duplicate bytes and bounds must match; index 0 is included.
  void feed(const uint8_t *data, size_t size, uint32_t now) {
    for (size_t i = 0; i < size; ++i) {
      for (size_t j = 1; j < rx_.size(); ++j) rx_[j - 1] = rx_[j];
      rx_.back() = data[i];
      if (rx_count_ < rx_.size()) ++rx_count_;
      if (rx_count_ != rx_.size() || rx_[0] != 0x98 || rx_[1] != 0x98 ||
          (rx_[2] != 0x00 && rx_[2] != 0x03) || rx_[2] != rx_[3] ||
          rx_[4] != rx_[5] || rx_[4] < minimum_ || rx_[4] > maximum_) continue;
      const int next = rx_[4];
      if (mode_ == Mode::PRESET && have_height_) {
        const uint32_t age = elapsed(now, feedback_ms_);
        const uint32_t bounded_age = age < FEEDBACK_MS ? age : FEEDBACK_MS;
        const int allowed = 2 + PRESET_MAX_STEPS_PER_SECOND * bounded_age / 1000;
        if (std::abs(next - height_) > allowed) continue;
        // Do not refresh feedback/progress or publish an impossible jump. The
        // meaning of status 0x03 is unknown; apply the same check to both types.
      }
      if (have_height_ && next != height_) {
        have_changed_ = true;
        changed_ms_ = now;
        if (active()) {
          if (mode_ == Mode::PRESET) {
            if (phase_ == Phase::RUN) {
              progress_ms_ = now;
              const int direction = next > height_ ? 1 : -1;
              if (direction != preset_direction_) preset_step_count_ = 0;
              preset_direction_ = direction;
              if (preset_step_count_ < 2) ++preset_step_count_;
              if (preset_step_count_ >= 2) {
                saw_progress_ = true;
                status_ = "Moving to preset";
              }
            }
          } else {
            progress_ms_ = now;
            saw_progress_ = true;
          }
        }
        if (phase_ == Phase::RUN &&
            (mode_ == Mode::TARGET || mode_ == Mode::HOLD || mode_ == Mode::NUDGE) &&
            ((command_ == 0x02 && next < height_) ||
             (command_ == 0x01 && next > height_))) {
          stop(now, "Unexpected direction");
        }
      }
      height_ = next;
      have_height_ = true;
      feedback_ms_ = now;
      // Cancel immediately on a crossing; never reverse to chase an exact value.
      if (mode_ == Mode::TARGET && reached()) stop(now, "At target");
    }
  }

  void tick(uint32_t now, bool connected) {
    // A queued release survives disarming/disconnect, but can emit only once.
    // An idle/boot Stop never queues this frame. Direction frames still require
    // the normal connection, enable, freshness, lease, and travel checks below.
    if (release_pending_) {
      release_pending_ = false;
      if (sender_) {
        sender_(0x00);
        last_tx_ms_ = now;
      }
      return;
    }
    if (!connected) {
      if (enabled_ || active()) set_enabled(false, now);
      return;
    }
    if (!active()) return;
    if (!enabled_ || !sender_) {
      stop(now, "Disabled");
      return;
    }
    if (!fresh(now)) {
      stop(now, "Height feedback lost");
      return;
    }
    if (elapsed(now, started_ms_) >= MAX_MOVE_MS) {
      stop(now, "Movement timeout");
      return;
    }
    if (mode_ == Mode::HOLD && elapsed(now, renewed_ms_) >= HOLD_LEASE_MS) {
      stop(now, "Hold released or renewal lost");
      return;
    }
    if (mode_ != Mode::SAVE && mode_ != Mode::PRESET &&
        elapsed(now, progress_ms_) >= STALL_MS) {
      stop(now, "No height progress");
      return;
    }
    if (mode_ == Mode::PRESET && phase_ == Phase::RUN) {
      if (!saw_progress_ && elapsed(now, key_ms_) >= STALL_MS) {
        stop(now, "Preset travel unconfirmed");
        return;
      }
      if (saw_progress_ && elapsed(now, progress_ms_) >= STALL_MS) {
        // The native controller knows the stored target; we can only observe
        // stationarity. This is not proof that its preset height was reached.
        stop(now, "Preset stationary");
        return;
      }
    }
    if ((mode_ == Mode::TARGET && reached()) ||
        ((mode_ == Mode::HOLD || mode_ == Mode::NUDGE) &&
         ((command_ == 0x02 && height_ >= maximum_) ||
          (command_ == 0x01 && height_ <= minimum_)))) {
      stop(now, "Height limit or target reached");
      return;
    }
    if (phase_ == Phase::QUIET) {
      if (cooling_ && elapsed(now, stopped_ms_) < QUIET_MS) return;
      sender_(0x00);  // New key sequence begins with neutral, exactly once.
      last_tx_ms_ = now;
      phase_ = Phase::KEY;
      return;
    }
    if (elapsed(now, last_tx_ms_) < TX_MS) return;
    if (phase_ == Phase::KEY) {
      sender_(command_);
      last_tx_ms_ = now;
      key_ms_ = now;
      if (mode_ == Mode::PRESET) progress_ms_ = now;
      phase_ = Phase::RUN;
      return;
    }
    if (mode_ == Mode::NUDGE && elapsed(now, key_ms_) >= NUDGE_MS) {
      stop(now, "Nudge finished");
      return;
    }
    if (mode_ == Mode::SAVE && elapsed(now, key_ms_) >= SAVE_MS) {
      if (!save_neutral_sent_) {
        sender_(0x00);
        last_tx_ms_ = now;
        save_neutral_sent_ = true;
      } else {
        sender_(save_slot_);
        stop(now, "Preset save sent");
      }
      return;
    }
    sender_(mode_ == Mode::PRESET ? 0x00 : command_);
    last_tx_ms_ = now;
  }

  static float inches(int raw) {
    return std::round((647614.5 - 647614.3026814 /
        (1 + std::pow(static_cast<double>(raw) / 1548255, 1.006855))) * 10) / 10;
  }
  int raw_from_inches(float value) const {
    int best = minimum_;
    for (int raw = minimum_ + 1; raw <= maximum_; ++raw) {
      if (std::fabs(inches(raw) - value) < std::fabs(inches(best) - value)) best = raw;
    }
    return best;
  }

 private:
  enum class Phase { QUIET, KEY, RUN };
  static uint32_t elapsed(uint32_t now, uint32_t then) { return now - then; }
  bool reached() const {
    return command_ == 0x02 ? height_ >= target_ : height_ <= target_;
  }
  bool ready(uint32_t now) {
    if (!enabled_) {
      stop(now, "Disabled");
      return false;
    }
    if (!fresh(now)) {
      stop(now, "Fresh height required");
      return false;
    }
    return true;
  }
  bool valid_direction(int direction, uint32_t now) {
    if ((direction != 1 && direction != -1) ||
        (direction > 0 && height_ >= maximum_) ||
        (direction < 0 && height_ <= minimum_)) {
      stop(now, "Invalid direction or height limit");
      return false;
    }
    return true;
  }
  void begin(Mode mode, uint8_t command, uint32_t now) {
    if (active()) stop(now, "Replaced");
    mode_ = mode;
    command_ = command;
    started_ms_ = progress_ms_ = renewed_ms_ = now;
    phase_ = Phase::QUIET;
    saw_progress_ = false;
    preset_direction_ = 0;
    preset_step_count_ = 0;
    save_neutral_sent_ = false;
  }

  int minimum_, maximum_;
  bool enabled_{false}, have_height_{false}, have_changed_{false};
  bool cooling_{false}, saw_progress_{false}, save_neutral_sent_{false};
  bool manual_release_on_stop_{false}, release_pending_{false};
  int height_{0}, target_{0};
  int preset_direction_{0};
  uint8_t preset_step_count_{0};
  uint8_t command_{0}, save_slot_{0};
  Mode mode_{Mode::IDLE};
  Phase phase_{Phase::QUIET};
  const char *status_{"Disabled"};
  uint32_t feedback_ms_{0}, changed_ms_{0}, progress_ms_{0}, started_ms_{0};
  uint32_t stopped_ms_{0}, last_tx_ms_{0}, key_ms_{0}, renewed_ms_{0};
  uint32_t hold_token_{0}, last_hold_token_{0};
  std::array<uint8_t, 6> rx_{};
  size_t rx_count_{0};
  std::function<void(uint8_t)> sender_;
};

// ESPHome 2026.7 emits typed globals before user includes. Keep ownership here
// so the type only needs to be visible in lambda bodies, after the include.
inline Controller &controller() {
  static Controller instance;
  return instance;
}

}  // namespace desk_draft
