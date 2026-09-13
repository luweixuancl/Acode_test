#include "local_clock.h"
#include <esp_timer.h>
#include <cmath>

void LocalClock::reset() {
  edgeHead_ = 0;
  edgeCount_ = 0;
  freqPpm_ = 0.0f;
  ppsStable_ = false;
  ppsBadStreak_ = 0;
  haveAnchor_ = false;
  anchorUtcSec_ = 0;
  anchorEdgeUs_ = 0;
  state_ = ClockState::Acquiring;
  residualMs_ = 0;
  okStreak_ = 0;
  holdoverStartMs_ = 0;
  lastEdgeUs_ = 0;
  lastPpsCount_ = 0;
}

void LocalClock::pushEdge(uint64_t edgeUs) {
  edges_[edgeHead_] = edgeUs;
  edgeHead_ = static_cast<uint8_t>((edgeHead_ + 1) % kRing);
  if (edgeCount_ < kRing) {
    edgeCount_++;
  }
}

void LocalClock::onPpsEdge(uint64_t edgeUs, uint32_t ppsCount) {
  uint32_t delta = 1;
  if (lastPpsCount_ != 0 && ppsCount > lastPpsCount_) {
    delta = ppsCount - lastPpsCount_;
  }

  if (edgeCount_ > 0 && delta == 1) {
    const uint8_t prevIdx = static_cast<uint8_t>((edgeHead_ + kRing - 1) % kRing);
    const uint64_t prev = edges_[prevIdx];
    if (edgeUs > prev) {
      const int64_t interval = static_cast<int64_t>(edgeUs - prev);
      const int64_t err = interval - 1000000LL;
      if (llabs(err) > CLK_PPS_INTERVAL_MAX_ERR_US) {
        if (ppsBadStreak_ < 255) {
          ppsBadStreak_++;
        }
      } else {
        ppsBadStreak_ = 0;
        const float samplePpm = static_cast<float>(err) / 1000000.0f * 1.0e6f;
        freqPpm_ = freqPpm_ * (1.0f - CLK_PPM_EMA_ALPHA) + samplePpm * CLK_PPM_EMA_ALPHA;
      }
    }
  } else if (delta > 1) {
    // Missed ISR deliveries / queue overflow — interval sample is not 1s.
    if (ppsBadStreak_ < 250) {
      ppsBadStreak_ = static_cast<uint8_t>(ppsBadStreak_ + delta);
    }
  }

  pushEdge(edgeUs);
  lastEdgeUs_ = edgeUs;
  lastPpsCount_ = ppsCount;
  ppsStable_ = (ppsBadStreak_ < CLK_PPS_UNSTABLE_COUNT) && (edgeCount_ >= 2);

  // Once disciplined, walk UTC with PPS. Catch up by delta if edges were coalesced.
  if (haveAnchor_ &&
      (state_ == ClockState::Locked || state_ == ClockState::Degraded ||
       state_ == ClockState::Holdover)) {
    if (delta >= CLK_PPS_MISS_UNSYNC) {
      Serial.printf("[clk] PPS miss delta=%u → unsync\n", static_cast<unsigned>(delta));
      enterUnsynced();
    } else if (ppsStable_ && delta == 1) {
      anchorUtcSec_ += 1;
      anchorEdgeUs_ = edgeUs;
    } else if (delta > 1) {
      // Coalesced edges without a stable 1s history: catch up but mark unstable.
      anchorUtcSec_ += delta;
      anchorEdgeUs_ = edgeUs;
      Serial.printf("[clk] PPS catch-up +%u s (unstable)\n", static_cast<unsigned>(delta));
      if (ppsBadStreak_ < 250) {
        ppsBadStreak_ = static_cast<uint8_t>(ppsBadStreak_ + 1);
      }
      ppsStable_ = false;
    }
  }

  if (!ppsStable_ &&
      (state_ == ClockState::Locked || state_ == ClockState::Degraded ||
       state_ == ClockState::Holdover)) {
    enterUnsynced();
  }
}

void LocalClock::setAnchor(uint32_t utcSec, uint64_t edgeUs, uint32_t /*ppsCount*/) {
  haveAnchor_ = true;
  anchorUtcSec_ = utcSec;
  anchorEdgeUs_ = edgeUs;
}

bool LocalClock::extrapolate(uint64_t atUs, uint32_t& sec, uint32_t& frac) const {
  if (!haveAnchor_ || atUs < anchorEdgeUs_) {
    return false;
  }
  const double scale = 1.0 - static_cast<double>(freqPpm_) * 1.0e-6;
  const double elapsedUs = static_cast<double>(atUs - anchorEdgeUs_) * scale;
  if (elapsedUs < 0) {
    return false;
  }
  const uint64_t whole = static_cast<uint64_t>(elapsedUs / 1000000.0);
  const double remUs = elapsedUs - static_cast<double>(whole) * 1000000.0;
  sec = anchorUtcSec_ + static_cast<uint32_t>(whole);
  frac = static_cast<uint32_t>((remUs / 1000000.0) * 4294967296.0);
  return true;
}

void LocalClock::enterHoldover() {
  if (state_ != ClockState::Holdover) {
    holdoverStartMs_ = millis();
  }
  state_ = ClockState::Holdover;
  okStreak_ = 0;
}

void LocalClock::enterUnsynced() {
  state_ = ClockState::Unsynced;
  okStreak_ = 0;
  holdoverStartMs_ = 0;
  haveAnchor_ = false;
  ppsStable_ = false;
  ppsBadStreak_ = 0;
  edgeCount_ = 0;
  edgeHead_ = 0;
}

void LocalClock::applyFail(AnomalyPolicy policy) {
  policy_ = policy;
  if (policy == AnomalyPolicy::Refuse || holdoverSec_ == 0) {
    enterUnsynced();
    return;
  }
  if (ppsStable_ && haveAnchor_) {
    enterHoldover();
  } else {
    enterUnsynced();
  }
}

void LocalClock::onNmeaCommit(uint32_t epochSec, uint32_t ppsCountAtCommit, AnomalyPolicy policy,
                             uint16_t holdoverSec) {
  policy_ = policy;
  holdoverSec_ = holdoverSec;

  if (lastEdgeUs_ == 0) {
    state_ = ClockState::Acquiring;
    return;
  }

  // NMEA often trails the PPS that opened this second. Align label to last edge.
  const int32_t lag = static_cast<int32_t>(lastPpsCount_ - ppsCountAtCommit);
  const uint32_t utcAtLastEdge =
      epochSec + (lag > 0 ? static_cast<uint32_t>(lag) : 0);

  // Bootstrap anchor on first good PPS + NMEA.
  if (!haveAnchor_) {
    if (ppsStable_ || edgeCount_ >= 1) {
      setAnchor(utcAtLastEdge, lastEdgeUs_, ppsCountAtCommit);
      residualMs_ = 0;
      state_ = ClockState::Acquiring;
      okStreak_ = 1;
    }
    return;
  }

  // Cross-check at last PPS edge (second boundary), not at parse completion time.
  uint32_t localSec = 0;
  uint32_t localFrac = 0;
  if (!extrapolate(lastEdgeUs_, localSec, localFrac)) {
    state_ = ClockState::Acquiring;
    return;
  }

  const double localMs =
      static_cast<double>(localSec) * 1000.0 + (static_cast<double>(localFrac) / 4294967296.0) * 1000.0;
  const double nmeaMs = static_cast<double>(utcAtLastEdge) * 1000.0;
  residualMs_ = static_cast<int32_t>(lround(nmeaMs - localMs));

  const int32_t absR = residualMs_ >= 0 ? residualMs_ : -residualMs_;
  const bool lockedLike =
      state_ == ClockState::Locked || state_ == ClockState::Degraded || state_ == ClockState::Holdover;

  if (absR <= CLK_RESIDUAL_RELOCK_MS) {
    if (okStreak_ < 255) {
      okStreak_++;
    }

    if (!lockedLike) {
      // Acquiring: allow NMEA to set phase until we trust PPS walk.
      setAnchor(utcAtLastEdge, lastEdgeUs_, ppsCountAtCommit);
    } else if (absR >= CLK_LOCKED_SLEW_MS) {
      // Small correction only — avoid stepping every second from NMEA jitter.
      setAnchor(utcAtLastEdge, lastEdgeUs_, ppsCountAtCommit);
    }
    // else: Locked and |r| < slew band — PPS advance owns the phase.

    if (okStreak_ >= CLK_RELOCK_COUNT && ppsStable_) {
      state_ = ClockState::Locked;
      holdoverStartMs_ = 0;
    } else if (state_ == ClockState::Unsynced || state_ == ClockState::Acquiring) {
      state_ = ClockState::Acquiring;
    }
    return;
  }

  okStreak_ = 0;

  // WARN (RELOCK < |r| <= WARN): Degraded, do not re-anchor — residual feeds dispersion.
  if (absR <= CLK_RESIDUAL_WARN_MS) {
    if (ppsStable_ && haveAnchor_) {
      state_ = ClockState::Degraded;
    } else {
      enterUnsynced();
    }
    return;
  }

  // Soft-fail band before FAIL: still Degraded, keep prior anchor.
  if (absR < CLK_RESIDUAL_FAIL_MS) {
    if (ppsStable_ && haveAnchor_) {
      state_ = ClockState::Degraded;
    } else {
      enterUnsynced();
    }
    return;
  }

  // FAIL
  applyFail(policy);
}

void LocalClock::tick(bool nmeaFresh, bool ppsFresh, AnomalyPolicy policy, uint16_t holdoverSec) {
  policy_ = policy;
  holdoverSec_ = holdoverSec;

  // Policy switched to Refuse while holding over.
  if (policy == AnomalyPolicy::Refuse && state_ == ClockState::Holdover) {
    enterUnsynced();
    return;
  }

  // PPS lost: LocalUtc can still extrapolate via esp_timer. Prefer Holdover over
  // immediate Unsynced when policy allows (covers antenna disconnect).
  if (!ppsFresh) {
    if (state_ == ClockState::Locked || state_ == ClockState::Degraded) {
      if (policy == AnomalyPolicy::Refuse || holdoverSec_ == 0 || !haveAnchor_) {
        enterUnsynced();
      } else {
        enterHoldover();
      }
    }
    if (state_ == ClockState::Holdover) {
      if (holdoverElapsedMs() >= static_cast<uint32_t>(holdoverSec_) * 1000UL ||
          qualityMs() >= CLK_HOLDOVER_MAX_QUALITY_MS) {
        enterUnsynced();
      }
    }
    return;
  }

  if (!nmeaFresh) {
    if (state_ == ClockState::Locked || state_ == ClockState::Degraded) {
      if (policy == AnomalyPolicy::Refuse || holdoverSec_ == 0) {
        enterUnsynced();
      } else if (ppsStable_ && haveAnchor_) {
        enterHoldover();
      } else {
        enterUnsynced();
      }
    }
  }

  if (state_ == ClockState::Holdover) {
    if (holdoverElapsedMs() >= static_cast<uint32_t>(holdoverSec_) * 1000UL ||
        qualityMs() >= CLK_HOLDOVER_MAX_QUALITY_MS) {
      enterUnsynced();
    }
  }

  if (state_ == ClockState::Unsynced && nmeaFresh && ppsFresh) {
    state_ = ClockState::Acquiring;
  }
}

uint32_t LocalClock::holdoverElapsedMs() const {
  if (state_ != ClockState::Holdover || holdoverStartMs_ == 0) {
    return 0;
  }
  return millis() - holdoverStartMs_;
}

bool LocalClock::nowUtc(uint32_t& seconds, uint32_t& fraction) const {
  if (state_ != ClockState::Locked && state_ != ClockState::Degraded &&
      state_ != ClockState::Holdover) {
    return false;
  }
  return extrapolate(esp_timer_get_time(), seconds, fraction);
}

bool LocalClock::referenceUtc(uint32_t& seconds, uint32_t& fraction) const {
  if (!haveAnchor_ || (state_ != ClockState::Locked && state_ != ClockState::Degraded &&
                       state_ != ClockState::Holdover)) {
    return false;
  }
  // Anchor is the last PPS-disciplined whole second (phase at edge).
  seconds = anchorUtcSec_;
  fraction = 0;
  return true;
}

uint32_t LocalClock::qualityMs() const {
  if (state_ == ClockState::Acquiring || state_ == ClockState::Unsynced || !haveAnchor_) {
    return 0xFFFFFFFF;
  }
  uint32_t q = 5;
  const int32_t absR = residualMs_ >= 0 ? residualMs_ : -residualMs_;
  if (static_cast<uint32_t>(absR) > q) {
    q = static_cast<uint32_t>(absR);
  }
  if (state_ == ClockState::Degraded) {
    q = q < 50 ? 50 : q;
  }
  if (state_ == ClockState::Holdover) {
    // Free-run bound: max(|EMA|, crystal floor, PHI) × age, plus entry uncertainty.
    const float ap = fabsf(freqPpm_);
    float usePpm = ap > CLK_HOLDOVER_PPM_FLOOR ? ap : CLK_HOLDOVER_PPM_FLOOR;
    if (usePpm < CLK_HOLDOVER_PHI_PPM) {
      usePpm = CLK_HOLDOVER_PHI_PPM;
    }
    const float elapsedSec = static_cast<float>(holdoverElapsedMs()) / 1000.0f;
    // error_s = ppm * 1e-6 * t_s  →  error_ms = ppm * t_s / 1000
    const uint32_t growMs = static_cast<uint32_t>(usePpm * elapsedSec / 1000.0f);
    q = q + CLK_HOLDOVER_ENTRY_MS + growMs;
  }
  const float ap = fabsf(freqPpm_);
  if (ap > 1.0f) {
    q += static_cast<uint32_t>(ap);
  }
  return q;
}
