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
  bool manual = false;   // no PI control (manual position or manual motor)
  float man_pos = 0.0f;
  bool direct = false;   // manual motor: the motor follows jog_on / jog_open directly
  bool jog_on = false;   // manual motor: run; cleared at an end stop or after the time limit
  bool jog_open = false; // manual motor: true = open the shunt
  bool invert_dir = false;        // swap direction if the valve turns the wrong way
  // Sensor fault: false = close the shunt (safe: the boiler can always get rid of its heat),
  // true = hold the position
  bool fault_hold_position = false;
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
constexpr float    REVERSE_FACTOR   = 1.5f;  // step controller: a reversal needs 1.5 x the deadband
constexpr float    MAX_JUMP_C       = 5.0f;  // larger temperature change in one tick must be confirmed
constexpr float    WRONG_WAY_PCT    = 3.0f;  // moving this far against the command = wrong direction
constexpr float    END_TOL_PCT      = 10.0f; // an end stop must be within this of the calibrated one
constexpr uint32_t FAULT_RETRY_MS   = 60000; // rest after a valve fault before the next try
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
    cal_ = CAL_OPEN;  // open first, so the calibration ends with the shunt closed
    cal_msg_ = "Calibrating...";
    return true;
  }

  // Controller - called every dt_s seconds with the latest temperature (NAN = no reading)
  void tick(float temp, float dt_s) {
    bool ok = !std::isnan(temp) && temp > -50.0f && temp < 125.0f &&
              // 85.0 is the DS18B20 power-on value, and 0.0 is what an all-zero read gives
              // (its CRC is valid) when the bus is disturbed; reject them out of the blue
              !(temp == 85.0f && (std::isnan(temp_) || std::fabs(temp_ - 85.0f) > 5.0f)) &&
              !(temp == 0.0f && (std::isnan(temp_) || std::fabs(temp_) > 5.0f));
    if (ok && !std::isnan(temp_) && std::fabs(temp - temp_) > MAX_JUMP_C) {
      // A big jump in one tick: accept it only when the next reading confirms it
      bool confirmed = !std::isnan(jump_) && std::fabs(temp - jump_) <= MAX_JUMP_C;
      if (!confirmed) {
        log_event("Temperature jump %.2f -> %.2f °C ignored until confirmed", temp_, temp);
        jump_ = temp;
        ok = false;
      }
    }
    if (ok) {
      jump_ = NAN;
      if (sensor_fault_) log_event("Temperature sensor OK again: %.2f °C", temp);
      temp_ = temp;
      bad_reads_ = 0;
      sensor_fault_ = false;
    } else if (++bad_reads_ >= 3 && !sensor_fault_) {
      sensor_fault_ = true;
      log_event("SENSOR FAULT: no valid temperature 3 times in a row");
    }

    // Step controller (incremental PI): every tick adds the wanted change of the valve
    // position to acc_. The motor control runs the valve when acc_ reaches the deadband and
    // then subtracts the movement actually measured, so a move that goes too far is not
    // corrected by itself - only if the temperature asks for it.
    if (p.manual || !init_ || sensor_fault_ || std::isnan(temp_)) {
      // Start again without a kick when the controller takes over
      acc_ = 0;
      e_prev_ = NAN;
      if (sensor_fault_ && !p.manual && init_ && !p.fault_hold_position) {
        out_ = target_ = 0.0f;  // close the shunt (position mode, see step_mode())
      }
      return;
    }

    float e = p.sp - temp_;  // too cold -> positive -> open the shunt more
    p_term_ = p.kp * e;
    if (std::isnan(e_prev_)) e_prev_ = e;
    float d = p.kp * (e - e_prev_) + p.kp * dt_s / p.ti * e;
    e_prev_ = e;
    // The valve cannot go beyond its ends; this is also the anti-windup
    acc_ = clampf(acc_ + d, -pos_, 100.0f - pos_);
    out_ = target_ = clampf(pos_ + acc_, 0.0f, 100.0f);
    integ_ = out_ - p_term_;  // for display only: the part of the output not explained by P
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
        acc_ = 0;
        log_event("Start-up: position sensor OK (%.1f %s), continuing from %.1f %%", sensor_, cfg.unit, pos_);
      } else {
        // Position unknown: close the shunt fully (safe: the boiler can always get rid of its heat)
        pos_ = 100.0f;
        out_ = integ_ = target_ = 0.0f;
        homing_ = true;  // position mode until the end stop is reached
        log_event("Start-up: position sensor %s (%.1f %s) - closing the shunt fully",
                  sensor_connected() ? "not calibrated" : "not connected", sensor_, cfg.unit);
      }
    }
    if (use_sensor()) pos_ = sensor_pos();
    if (!p.direct) p.jog_on = false;
    if (p.manual) out_ = target_ = p.direct ? pos_ : p.man_pos;

    switch (state_) {
      case A_IDLE: {
        if (cal_ == CAL_OPEN)  { start_move(+1, RM_END, 100.0f, CAL_MAX_MS, now); break; }
        if (cal_ == CAL_CLOSE) { start_move(-1, RM_END, 0.0f, CAL_MAX_MS, now); break; }
        if (p.direct) {
          if (p.jog_on)
            start_move(p.jog_open ? 1 : -1, RM_JOG, p.jog_open ? 100.0f : 0.0f,
                       (uint32_t)(p.travel * 1000.0f * 1.5f) + 2000, now);
          break;
        }
        if (step_mode()) {
          // Step controller: run when the accumulated demand reaches the deadband; a
          // reversal needs a bit more, so noise does not make the valve go back and forth
          acc_ = clampf(acc_, -pos_, 100.0f - pos_);
          out_ = target_ = clampf(pos_ + acc_, 0.0f, 100.0f);
          int dir = acc_ > 0 ? 1 : -1;
          float need = p.db * (last_dir_ == 0 || dir == last_dir_ ? 1.0f : REVERSE_FACTOR);
          if (std::fabs(acc_) < need) break;
          float t = pos_ + acc_;
          float ms = std::fabs(acc_) / 100.0f * p.travel * 1000.0f;
          step_move_ = true;
          if (t <= 0.5f || t >= 99.5f) {
            t = t <= 0.5f ? 0.0f : 100.0f;
            if (std::fabs(t - pos_) < (use_sensor() ? 1.5f : 0.01f)) { step_move_ = false; acc_ = 0; break; }
            start_move(dir, RM_END, t, (uint32_t)(ms + ENDPOINT_OVERRUN * p.travel * 1000.0f), now);
          } else if (use_sensor()) {
            start_move(dir, RM_TARGET, t, (uint32_t)(ms * 1.5f + 3000 + dead_ms_), now);
          } else if (ms >= MIN_PULSE_MS) {
            start_move(dir, RM_TARGET, t, (uint32_t)(ms + dead_ms_), now);
          } else {
            step_move_ = false;  // too short to run without a position sensor: keep accumulating
          }
          break;
        }

        // Position mode (manual position, fault, homing): run to target_ and correct misses
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
        // Turning the wrong way (lost direction signal, swapped wires): stop at once
        bool wrong_way = use_sensor() && (pos_ - move_start_pos_) * dir_ < -WRONG_WAY_PCT;

        if (wrong_way) {
          stop = true;
          valve_fault_ = true;
          why = "VALVE FAULT: turning the wrong way (direction signal?)";
          if (mode_ == RM_JOG) p.jog_on = false;
          if (cal_ != CAL_NONE) { cal_ = CAL_NONE; cal_msg_ = "Calibration failed: valve turned the wrong way"; }
        } else if (mode_ == RM_END) {
          stop = stalled || timeout;
          why = stalled ? "end stop (not moving)" : "time elapsed";
        } else if (mode_ == RM_JOG) {
          bool wanted = p.direct && p.jog_on && (p.jog_open ? 1 : -1) == dir_;
          stop = !wanted || stalled || timeout;
          why = !wanted ? "manual motor off" : stalled ? "end stop (not moving)" : "manual motor time limit";
          if (stalled || timeout) p.jog_on = false;
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
          if (mode_ == RM_END && !wrong_way) finish_end_move(stalled);
          // After a fault, wait before trying again instead of starting the motor every few seconds
          rest_ms_ = valve_fault_ ? FAULT_RETRY_MS : REST_MS;
          state_ = A_REST;
          t_state_ = now;
        }
        break;
      }
      case A_REST:
        if (now - t_state_ >= DIR_SETTLE_MS) dir_on_ = false;
        if (now - t_state_ >= rest_ms_) {
          if (learn_lead_ && use_sensor()) {
            // Coast-down: how far the valve kept moving after the motor was switched off
            float over = (pos_ - pos_stop_) * dir_;
            if (over > -1.0f && over < MAX_LEAD * 2)
              lead_ = clampf(0.7f * lead_ + 0.3f * over, 0.0f, MAX_LEAD);
            log_event("Coast-down %.2f %% -> now stopping %.2f %% early. Dead time approx. %u ms. Final position %.1f %%",
                      over, lead_, (unsigned) dead_ms_, pos_);
          }
          learn_lead_ = false;
          if (step_move_) {
            // Subtract the movement actually made, coast-down included; what is left over
            // (too short or too far) stays in acc_ for the controller to decide on
            float moved = pos_ - move_start_pos_;
            acc_ = clampf(acc_ - moved, -pos_, 100.0f - pos_);
            last_dir_ = dir_;
            step_move_ = false;
            log_event("Step done: moved %+.1f %%, demand left %+.1f %%", moved, acc_);
          }
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
  bool calibrating() const { return cal_ != CAL_NONE; }
  const char* cal_msg() const { return cal_msg_; }
  const char* state_text() const {
    if (cal_ != CAL_NONE) return "CALIBRATING";
    if (sensor_fault_)    return "SENSOR FAULT";
    if (valve_fault_)     return "VALVE FAULT";
    if (p.direct)         return "MANUAL MOTOR";
    if (p.manual)         return "MANUAL";
    return "AUTO";
  }

 private:
  enum ActState { A_IDLE, A_PREP, A_RUN, A_REST };
  enum RunMode  { RM_TARGET, RM_END, RM_JOG };
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

  // Auto control runs the step controller; manual position, a temperature sensor fault that
  // closes the shunt, and homing after start-up run to a fixed position instead
  bool step_mode() const {
    return !p.manual && !homing_ && !(sensor_fault_ && !p.fault_hold_position);
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
              mode == RM_END ? "end stop" : mode == RM_JOG ? "end (manual motor)" : "target",
              pos_, run_target, (unsigned) max_ms);
    dir_ = dir;
    mode_ = mode;
    move_start_pos_ = pos_;
    run_target_ = run_target;
    max_ms_ = max_ms;
    dir_on_ = (dir > 0) != p.invert_dir;
    state_ = A_PREP;
    t_state_ = now;
  }

  void finish_end_move(bool stalled) {
    bool opening = dir_ > 0;
    if (stalled && cal_ == CAL_NONE && sensor_calibrated()) {
      // In operation an end stop must be where calibration found it. Stopping elsewhere
      // means the valve is blocked or turned the wrong way: do not learn it
      float at = delta(sensor_, end0) / delta(end100, end0) * 100.0f;
      if (opening ? at < 100.0f - END_TOL_PCT : at > END_TOL_PCT) {
        valve_fault_ = true;
        log_event("VALVE FAULT: end stop found at %.1f %% instead of %s - not learned", at,
                  opening ? "100 %" : "0 %");
        stalled = false;
      }
    }
    if (stalled) {
      if (cal_ == CAL_NONE) valve_fault_ = false;  // the end stop is where it should be
      // Learn the sensor end point; store only on a noticeable change (saves flash wear)
      float& e = opening ? end100 : end0;
      if (std::isnan(e) || std::fabs(delta(e, sensor_)) > cfg.learn_delta || cal_ != CAL_NONE) {
        e = sensor_;
        cal_dirty = true;
        log_event("End point %s learned: %.1f %s", opening ? "100 %" : "0 %", e, cfg.unit);
      }
      if (cal_ == CAL_CLOSE) {
        // The closing run goes the full way, so it gives the travel time
        new_travel = std::max(2.0f, (t_last_move_ - t_state_) / 1000.0f);
        cal_msg_ = sensor_calibrated() ? "Calibration OK" : "Calibration failed: too little travel measured";
        log_event("Calibration OK: travel time %.2f s, 0 %% = %.1f %s, 100 %% = %.1f %s",
                  new_travel, end0, cfg.unit, end100, cfg.unit);
      }
    }
    if (!use_sensor()) pos_ = opening ? 100.0f : 0.0f;
    if (cal_ == CAL_NONE) homing_ = false;

    if (cal_ == CAL_OPEN) {
      cal_ = stalled ? CAL_CLOSE : CAL_NONE;
      if (!stalled) cal_msg_ = "Calibration failed: no movement detected";
    } else if (cal_ == CAL_CLOSE) {
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
  float p_term_ = 0, integ_ = 0, out_ = 0;
  float jump_ = NAN;        // a big temperature jump waiting for confirmation
  uint32_t rest_ms_ = REST_MS;
  float acc_ = 0;           // step controller: wanted change of the valve position, %
  float e_prev_ = NAN;      // previous control error, for the P part of the step
  bool homing_ = false;     // start-up without position: closing to the end stop first
  bool step_move_ = false;  // the current move was started by the step controller
  float move_start_pos_ = 0;
  int last_dir_ = 0;        // direction of the last step move

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
  float pos_ = 0, target_ = 0, run_target_ = 0;
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
