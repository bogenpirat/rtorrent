#include "config.h"

#include "core/rate_tracker.h"

#include <algorithm>
#include <cmath>
#include <torrent/utils/chrono.h>

#include "control.h"
#include "core/manager.h"

namespace core {

RateTracker::rate_type
RateTracker::update(const void* source, int64_t total) {
  auto  timestamp = torrent::this_thread::cached_seconds().count();
  auto& sample    = m_samples[source];

  if (sample.timestamp == 0 || total < sample.total) {
    // Either the first time we see this counter, or it was reset by a session
    // restore. Record the baseline; we cannot say anything about the rate until
    // we have a second reading to compare it against.
    sample = sample_type{timestamp, total, 0, 0.0};
    return 0;
  }

  auto elapsed = timestamp - sample.timestamp;

  if (elapsed > 0) {
    auto   moved = total - sample.total;
    double speed = static_cast<double>(moved) / static_cast<double>(elapsed);

    // Deriving the weight from the elapsed time keeps the average converging at
    // the same pace whether we get a reading every second or, when nothing has
    // looked at the counter for a while, a lot less often.
    double weight = m_smoothing > 0 ? 1.0 - std::exp(-static_cast<double>(elapsed) / static_cast<double>(m_smoothing)) : 1.0;

    sample.value    += weight * (speed - sample.value);
    sample.idle      = moved != 0 ? 0 : sample.idle + elapsed;
    sample.total     = total;
    sample.timestamp = timestamp;

    if (sample.idle >= idle_snap)
      sample.value = 0.0;
  }

  // The tail of a transfer that stopped would otherwise linger as a fraction of
  // a byte per second for a very long time, and callers read a zero rate as
  // meaning idle.
  if (sample.value < 1.0)
    return 0;

  return static_cast<rate_type>(sample.value + 0.5);
}

void
RateTracker::set_smoothing(int64_t seconds) {
  m_smoothing = std::clamp<int64_t>(seconds, 0, max_smoothing);
}

void
RateTracker::prune() {
  auto timestamp = torrent::this_thread::cached_seconds().count();

  for (auto itr = m_samples.begin(); itr != m_samples.end(); ) {
    if (timestamp - itr->second.timestamp >= idle_timeout)
      itr = m_samples.erase(itr);
    else
      ++itr;
  }
}

RateTracker*
rate_tracker() {
  return control->core()->rate_tracker();
}

}
