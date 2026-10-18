#pragma once
// Shunt controller - PI control of a 3-way return shunt with a motorized valve and a
// position sensor.
//
// Valve position 0 %   = shunt closed: all return water from the house goes to the boiler.
// Valve position 100 % = shunt fully open: boiler flow is mixed into the return (start-up).
//
// The motor is driven by two outputs: motor_on() powers the motor, dir_on() selects the
// direction. The direction is set before the motor starts and released only after it stops.
//
// Plain C++ without ESPHome dependencies. ventilregulator.yaml feeds it measurements and
// settings, writes the outputs and forwards events (next_event) to the log.

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace shunt {

inline float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }

struct Params {
  float sp = 55.0f;      // setpoint, °C
  float kp = 4.0f;       // % valve position per °C error
  float ti = 300.0f;     // integral time, s
  float travel = 5.0f;   // travel time 0-100 %, s (HH-Flo CR305: max 5 s)
  float db = 2.0f;       // deadband on valve position, %
  bool manual = false;
  float man_pos = 0.0f;
  bool invert_dir = false;        // swap direction if the valve turns the wrong way
  bool fault_open_shunt = false;  // sensor fault: false = hold position, true = open shunt
};

// Position sensor characteristics, in the sensor's own unit. The YAML selects one per
// hardware revision.
struct SensorCfg {
  const char* unit;
  float min_valid, max_valid;  // reading outside this range = sensor disconnected
  float min_span;              // minimum difference between 0 % and 100 %
  float stall_delta;           // change below this = shaft is not moving
  uint32_t stall_ms;           // this long without movement = end stop reached
  float learn_delta;           // store a learned end point only when it moved more than this
  float wrap;                  // the reading wraps around at this value (0 = no wrap)
};
// Rev 1: potentiometer on the ADC, mV. 1 MΩ to GND pulls a disconnected input to ~0 mV.
constexpr SensorCfg SENSOR_POT = {"mV", 100, 3150, 300, 15, 1500, 20, 0};
// Rev 2: AS5600 over I2C, degrees 0-360. The YAML passes NAN when the chip or the magnet
// is missing. 90° of valve travel is about 1000 steps, so the stall time can be short.
constexpr SensorCfg SENSOR_AS5600 = {"°", -1, 361, 20, 0.5f, 500, 1.0f, 360};

constexpr uint32_t MIN_PULSE_MS     = 150;   // shortest motor pulse (only without position sensor)
constexpr uint32_t DIR_SETTLE_MS    = 150;   // pause between direction output and motor output
constexpr uint32_t REST_MS          = 1000;  // rest between two motor pulses
constexpr float    ENDPOINT_OVERRUN = 0.25f; // extra run towards an end stop (fraction of travel time)
constexpr float    STOP_LEAD        = 0.5f;  // initial coast-down, %; learned continuously
constexpr float    MAX_LEAD         = 5.0f;  // largest coast-down accepted as learned
constexpr int      SENSOR_MAX_MISS  = 5;     // missing readings in a row before the sensor counts as gone
constexpr uint32_t CAL_MAX_MS       = 120000;
constexpr uint32_t BOOT_WAIT_MS     = 2000;  // wait for the position sensor before the first move

class Controller {
 public:
  Params p;
  SensorCfg cfg = SENSOR_POT;     // set by the YAML for the hardware revision
  float end0 = NAN, end100 = NAN; // sensor readings at 0 % and 100 %; stored in flash by the YAML
  bool loaded = false;            // set by the YAML once stored values are restored
  bool cal_dirty = false;         // end0/end100 changed -> YAML stores them
  float new_travel = NAN;         // travel time measured by calibration -> YAML stores it

  // Position sensor reading in cfg.unit; NAN = no reading
  void set_sensor(float v) {
    if (std::isnan(v)) {
      if (++misses_ >= SENSOR_MAX_MISS) sensor_ = NAN;  // ride out a single failed read
      return;
    }
    misses_ = 0;
    if (std::isnan(sensor_)) {
      sensor_ = v;
    } else {
      sensor_ += 0.3f * delta(v, sensor_);
      if (cfg.wrap > 0) sensor_ = std::fmod(sensor_ + cfg.wrap, cfg.wrap);
    }
  }

  bool sensor_connected() const {
    return !std::isnan(sensor_) && sensor_ > cfg.min_valid && sensor_ < cfg.max_valid;
  }
  bool sensor_calibrated() const {
    return !std::isnan(end0) && !std::isnan(end100) && std::fabs(delta(end100, end0)) >= cfg.min_span;
  }
  bool use_sensor() const { return sensor_connected() && sensor_calibrated(); }

  bool start_calibration() {
    if (!sensor_connected()) {
      cal_msg_ = "Position sensor not connected";
      return false;
    }
    cal_ = CAL_CLOSE;
    cal_msg_ = "Calibrating...";
    return true;
  }

  // Controller - called every dt_s seconds with the latest temperature (NAN = no reading)
  void tick(float temp, float dt_s) {
    bool ok = !std::isnan(temp) && temp > -50.0f && temp < 125.0f &&
              // 85.0 is the DS18B20 power-on value; reject it when it appears out of the blue
              !(temp == 85.0f && (std::isnan(temp_) || std::fabs(temp_ - 85.0f) > 5.0f));
    if (ok) {
      if (sensor_fault_) log_event("Temperature sensor OK again: %.2f °C", temp);
      temp_ = temp;
      bad_reads_ = 0;
      sensor_fault_ = false;
    } else if (++bad_reads_ >= 3 && !sensor_fault_) {
      sensor_fault_ = true;
      log_event("SENSOR FAULT: no valid temperature 3 times in a row");
    }

    if (was_manual_ && !p.manual && !std::isnan(temp_)) {
      // Bumpless transfer from manual to auto
      p_term_ = p.kp * (p.sp - temp_);
      integ_ = clampf(out_ - p_term_, 0.0f, 100.0f);
    }
    was_manual_ = p.manual;

    if (p.manual || !init_) return;
    if (sensor_fault_ || std::isnan(temp_)) {
      if (p.fault_open_shunt) out_ = target_ = 100.0f;
      return;
    }

    float e = p.sp - temp_;  // too cold -> positive -> open the shunt more
    p_term_ = p.kp * e;
    float di = p.kp * dt_s / p.ti * e;
    float u = p_term_ + integ_ + di;
    // Anti-windup: stop integrating while the output is saturated in the same direction
    if (!((u > 100.0f && di > 0) || (u < 0.0f && di < 0))) integ_ += di;
    integ_ = clampf(integ_, 0.0f, 100.0f);
    out_ = clampf(p_term_ + integ_, 0.0f, 100.0f);
    target_ = out_;
  }

  // Motor control - called often (every 20 ms)
  void update(uint32_t now) {
    if (!loaded) return;
    if (!init_) {
      if (!boot_started_) { boot_started_ = true; boot_ms_ = now; }
      if (!sensor_connected() && now - boot_ms_ < BOOT_WAIT_MS) return;
      init_ = true;
      if (use_sensor()) {
        // Restart: continue from the current position
        pos_ = sensor_pos();
        out_ = integ_ = target_ = pos_;
        log_event("Start-up: position sensor OK (%.1f %s), continuing from %.1f %%", sensor_, cfg.unit, pos_);
      } else {
        // Position unknown: open the shunt fully (safe start-up for the boiler)
        pos_ = 0.0f;
        out_ = integ_ = target_ = 100.0f;
        log_event("Start-up: position sensor %s (%.1f %s) - opening the shunt fully",
                  sensor_connected() ? "not calibrated" : "not connected", sensor_, cfg.unit);
      }
    }
    if (use_sensor()) pos_ = sensor_pos();
    if (p.manual) out_ = target_ = p.man_pos;

    switch (state_) {
      case A_IDLE: {
        if (cal_ == CAL_CLOSE) { start_move(-1, RM_END, 0.0f, CAL_MAX_MS, now); break; }
        if (cal_ == CAL_OPEN)  { start_move(+1, RM_END, 100.0f, CAL_MAX_MS, now); break; }

        float t = target_;
        bool end = false;
        if (t <= 0.5f)       { t = 0.0f;   end = true; }
        else if (t >= 99.5f) { t = 100.0f; end = true; }
        float diff = t - pos_;
        float ms = std::fabs(diff) / 100.0f * p.travel * 1000.0f;
        int dir = diff > 0 ? 1 : -1;
        if (end) {
          if (std::fabs(diff) < (use_sensor() ? 1.5f : 0.01f)) break;
          start_move(dir, RM_END, t, (uint32_t)(ms + ENDPOINT_OVERRUN * p.travel * 1000.0f), now);
        } else if (std::fabs(diff) >= p.db) {
          if (use_sensor())            start_move(dir, RM_TARGET, t, (uint32_t)(ms * 1.5f + 3000 + dead_ms_), now);
          else if (ms >= MIN_PULSE_MS) start_move(dir, RM_TARGET, t, (uint32_t)(ms + dead_ms_), now);
        }
        break;
      }
      case A_PREP:
        if (now - t_state_ >= DIR_SETTLE_MS) {
          motor_ = true;
          starts_++;
          state_ = A_RUN;
          t_state_ = t_last_ = t_last_move_ = now;
          last_move_val_ = sensor_;
          moved_ = false;
        }
        break;
      case A_RUN: {
        uint32_t dt = now - t_last_;
        t_last_ = now;
        if (!use_sensor()) pos_ = clampf(pos_ + dir_ * dt / (p.travel * 10.0f), 0.0f, 100.0f);
        if (sensor_connected() && std::isnan(last_move_val_)) last_move_val_ = sensor_;
        if (sensor_connected() && std::fabs(delta(sensor_, last_move_val_)) > cfg.stall_delta) {
          last_move_val_ = sensor_;
          t_last_move_ = now;
          if (!moved_) {
            // Dead time: from motor power-on until the shaft starts turning
            moved_ = true;
            uint32_t d = now - t_state_;
            dead_ms_ = dead_ms_ == 0 ? d : (uint32_t)(0.7f * dead_ms_ + 0.3f * d);
          }
        }
        uint32_t elapsed = now - t_state_;
        bool stalled = sensor_connected() && elapsed > 1000 + dead_ms_ && now - t_last_move_ > cfg.stall_ms;
        bool timeout = elapsed >= max_ms_;
        bool stop = false;
        const char* why = "";

        if (mode_ == RM_END) {
          stop = stalled || timeout;
          why = stalled ? "end stop (not moving)" : "time elapsed";
        } else if (use_sensor()) {
          bool reached = dir_ > 0 ? pos_ >= run_target_ - lead_ : pos_ <= run_target_ + lead_;
          if (reached) { stop = true; valve_fault_ = false; why = "target reached"; }
          else if (stalled || timeout) {
            stop = true;
            valve_fault_ = true;
            why = stalled ? "VALVE FAULT: shaft not moving" : "VALVE FAULT: target not reached in time";
          }
        } else {
          stop = timeout;
          why = "time elapsed (no position sensor)";
        }

        if (stop) {
          motor_ = false;
          log_event("Stop after %u ms (%s): position %.1f %%, sensor %.1f %s",
                    (unsigned) elapsed, why, pos_, sensor_, cfg.unit);
          pos_stop_ = pos_;
          learn_lead_ = mode_ == RM_TARGET && use_sensor() && !valve_fault_;
          if (mode_ == RM_END) finish_end_move(stalled);
          state_ = A_REST;
          t_state_ = now;
        }
        break;
      }
      case A_REST:
        if (now - t_state_ >= DIR_SETTLE_MS) dir_on_ = false;
        if (now - t_state_ >= REST_MS) {
          if (learn_lead_ && use_sensor()) {
            // Coast-down: how far the valve kept moving after the motor was switched off
            float over = (pos_ - pos_stop_) * dir_;
            if (over > -1.0f && over < MAX_LEAD * 2)
              lead_ = clampf(0.7f * lead_ + 0.3f * over, 0.0f, MAX_LEAD);
            log_event("Coast-down %.2f %% -> now stopping %.2f %% early. Dead time approx. %u ms. Final position %.1f %%",
                      over, lead_, (unsigned) dead_ms_, pos_);
          }
          learn_lead_ = false;
          state_ = A_IDLE;
        }
        break;
    }
  }

  bool motor_on() const { return motor_; }
  bool dir_on() const { return dir_on_; }

  float temp() const { return sensor_fault_ ? NAN : temp_; }
  float out() const { return out_; }
  float pos() const { return pos_; }
  float p_term() const { return p_term_; }
  float integ() const { return integ_; }
  float sensor() const { return sensor_; }        // position sensor reading, cfg.unit
  uint32_t starts() const { return starts_; }     // motor starts since boot
  float lead() const { return lead_; }            // learned coast-down, %
  uint32_t dead_ms() const { return dead_ms_; }   // learned dead time, ms

  // Fetches the next event for the log; false when the queue is empty
  bool next_event(char* out, size_t n) {
    if (ev_tail_ == ev_head_) return false;
    strncpy(out, ev_[ev_tail_], n - 1);
    out[n - 1] = '\0';
    ev_tail_ = (ev_tail_ + 1) % EV_COUNT;
    return true;
  }
  bool sensor_fault() const { return sensor_fault_; }
  bool valve_fault() const { return valve_fault_; }
  bool any_fault() const { return sensor_fault_ || valve_fault_; }
  const char* cal_msg() const { return cal_msg_; }
  const char* state_text() const {
    if (cal_ != CAL_NONE) return "CALIBRATING";
    if (sensor_fault_)    return "SENSOR FAULT";
    if (valve_fault_)     return "VALVE FAULT";
    if (p.manual)         return "MANUAL";
    return "AUTO";
  }

 private:
  enum ActState { A_IDLE, A_PREP, A_RUN, A_REST };
  enum RunMode  { RM_TARGET, RM_END };
  enum CalState { CAL_NONE, CAL_CLOSE, CAL_OPEN };

  // a - b; the shortest way round when the reading wraps (the valve turns only 90°)
  float delta(float a, float b) const {
    float d = a - b;
    if (cfg.wrap > 0) {
      d = std::fmod(d, cfg.wrap);
      if (d > cfg.wrap / 2) d -= cfg.wrap;
      else if (d < -cfg.wrap / 2) d += cfg.wrap;
    }
    return d;
  }

  float sensor_pos() const { return clampf(delta(sensor_, end0) / delta(end100, end0) * 100.0f, 0.0f, 100.0f); }

  void log_event(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(ev_[ev_head_], EV_LEN, fmt, args);
    va_end(args);
    ev_head_ = (ev_head_ + 1) % EV_COUNT;
    if (ev_head_ == ev_tail_) ev_tail_ = (ev_tail_ + 1) % EV_COUNT;  // full: drop the oldest
  }

  void start_move(int dir, RunMode mode, float run_target, uint32_t max_ms, uint32_t now) {
    log_event("Start %s towards %s: %.1f %% -> %.1f %% (max %u ms)", dir > 0 ? "OPEN" : "CLOSE",
              mode == RM_END ? "end stop" : "target", pos_, run_target, (unsigned) max_ms);
    dir_ = dir;
    mode_ = mode;
    run_target_ = run_target;
    max_ms_ = max_ms;
    dir_on_ = (dir > 0) != p.invert_dir;
    state_ = A_PREP;
    t_state_ = now;
  }

  void finish_end_move(bool stalled) {
    bool opening = dir_ > 0;
    if (stalled) {
      // Learn the sensor end point; store only on a noticeable change (saves flash wear)
      float& e = opening ? end100 : end0;
      if (std::isnan(e) || std::fabs(delta(e, sensor_)) > cfg.learn_delta || cal_ != CAL_NONE) {
        e = sensor_;
        cal_dirty = true;
        log_event("End point %s learned: %.1f %s", opening ? "100 %" : "0 %", e, cfg.unit);
      }
      if (cal_ == CAL_OPEN) {
        new_travel = std::max(2.0f, (t_last_move_ - t_state_) / 1000.0f);
        cal_msg_ = "Calibration OK";
        log_event("Calibration OK: travel time %.2f s, 0 %% = %.1f %s, 100 %% = %.1f %s",
                  new_travel, end0, cfg.unit, end100, cfg.unit);
      }
    }
    if (!use_sensor()) pos_ = opening ? 100.0f : 0.0f;

    if (cal_ == CAL_CLOSE) {
      cal_ = stalled ? CAL_OPEN : CAL_NONE;
      if (!stalled) cal_msg_ = "Calibration failed: no movement detected";
    } else if (cal_ == CAL_OPEN) {
      if (!stalled) cal_msg_ = "Calibration failed: end stop not found";
      cal_ = CAL_NONE;
    } else {
      return;
    }
    if (!stalled) log_event("%s", cal_msg_);
  }

  // Controller
  float temp_ = NAN;
  int bad_reads_ = 0;
  bool sensor_fault_ = false;
  bool was_manual_ = false;
  float p_term_ = 0, integ_ = 100.0f, out_ = 100.0f;

  // Actuator
  bool init_ = false, boot_started_ = false;
  uint32_t boot_ms_ = 0;
  ActState state_ = A_IDLE;
  RunMode mode_ = RM_TARGET;
  CalState cal_ = CAL_NONE;
  const char* cal_msg_ = "";
  int dir_ = 0;
  bool motor_ = false, dir_on_ = false;
  bool valve_fault_ = false;
  uint32_t starts_ = 0;
  float lead_ = STOP_LEAD, pos_stop_ = 0;
  uint32_t dead_ms_ = 0;
  bool moved_ = false, learn_lead_ = false;
  float pos_ = 0, target_ = 100.0f, run_target_ = 0;
  float sensor_ = NAN, last_move_val_ = NAN;
  int misses_ = 0;
  uint32_t t_state_ = 0, t_last_ = 0, t_last_move_ = 0, max_ms_ = 0;

  // Event queue for the log
  static constexpr int EV_COUNT = 8, EV_LEN = 112;
  char ev_[EV_COUNT][EV_LEN] = {};
  int ev_head_ = 0, ev_tail_ = 0;
};

inline Controller ctl;

}  // namespace shunt
