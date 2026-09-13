#pragma once

#include <Arduino.h>
#include "config.h"
#include "settings.h"

enum class ClockState : uint8_t {
  Acquiring = 0,
  Locked = 1,
  Degraded = 2,
  Holdover = 3,
  Unsynced = 4,
};

inline const char* clockStateLabel(ClockState s) {
  switch (s) {
    case ClockState::Locked:
      return "LCK";
    case ClockState::Degraded:
      return "DEG";
    case ClockState::Holdover:
      return "HLD";
    case ClockState::Unsynced:
      return "UNS";
    case ClockState::Acquiring:
    default:
      return "ACQ";
  }
}

// PPS-disciplined local UTC with NMEA cross-check. Used only from task-time.
class LocalClock {
 public:
  void reset();

  // Called from time task when a PPS edge is consumed (edgeUs from ISR).
  void onPpsEdge(uint64_t edgeUs, uint32_t ppsCount);

  // Called when a new NMEA second is committed.
  void onNmeaCommit(uint32_t epochSec, uint32_t ppsCountAtCommit, AnomalyPolicy policy,
                    uint16_t holdoverSec);

  // Periodic: NMEA stall / holdover timeout / policy change.
  void tick(bool nmeaFresh, bool ppsFresh, AnomalyPolicy policy, uint16_t holdoverSec);

  bool nowUtc(uint32_t& seconds, uint32_t& fraction) const;
  // Last PPS-aligned UTC second (fraction always 0). For NTP Reference Timestamp.
  bool referenceUtc(uint32_t& seconds, uint32_t& fraction) const;
  uint32_t qualityMs() const;

  ClockState state() const { return state_; }
  int32_t residualMs() const { return residualMs_; }
  float freqPpm() const { return freqPpm_; }
  bool ppsStable() const { return ppsStable_; }
  uint32_t holdoverElapsedMs() const;

 private:
  bool extrapolate(uint64_t atUs, uint32_t& sec, uint32_t& frac) const;
  void setAnchor(uint32_t utcSec, uint64_t edgeUs, uint32_t ppsCount);
  void enterHoldover();
  void enterUnsynced();
  void applyFail(AnomalyPolicy policy);
  void pushEdge(uint64_t edgeUs);

  static constexpr size_t kRing = CLK_PPS_EDGE_RING;

  uint64_t edges_[kRing] = {};
  uint8_t edgeHead_ = 0;
  uint8_t edgeCount_ = 0;

  float freqPpm_ = 0.0f;
  bool ppsStable_ = false;
  uint8_t ppsBadStreak_ = 0;

  bool haveAnchor_ = false;
  uint32_t anchorUtcSec_ = 0;
  uint64_t anchorEdgeUs_ = 0;

  ClockState state_ = ClockState::Acquiring;
  int32_t residualMs_ = 0;
  uint8_t okStreak_ = 0;

  AnomalyPolicy policy_ = AnomalyPolicy::Refuse;
  uint16_t holdoverSec_ = CLK_HOLDOVER_SHORT_SEC;
  uint32_t holdoverStartMs_ = 0;

  uint64_t lastEdgeUs_ = 0;
  uint32_t lastPpsCount_ = 0;
};
