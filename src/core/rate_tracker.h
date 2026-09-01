#ifndef RTORRENT_CORE_RATE_TRACKER_H
#define RTORRENT_CORE_RATE_TRACKER_H

#include <cstdint>
#include <unordered_map>

namespace core {

// Smoothed transfer rate estimation.
//
// LibTorrent's torrent::Rate reports the average over a fixed, minutes-long
// window. That both lags far behind what is actually going over the wire and
// keeps reporting traffic long after it stopped, so the numbers rtorrent used
// to print bore little resemblance to the current speed.
//
// RateTracker instead reads the monotonically increasing byte counters once a
// second and feeds the resulting per-second speeds into an exponential moving
// average. That follows a real change within a few seconds while still hiding
// the jitter of any single second.
//
// Counters are keyed on the address they are read from, so anything holding one
// can be tracked without knowing about this class. A counter is sampled at most
// once a second, by whoever reads it first; core::Manager reads the global,
// download and throttle counters on a timer so those keep ticking even when
// nothing is drawing or polling them.
//
// Main thread only.

class RateTracker {
public:
  typedef uint32_t rate_type;

  // Time constant of the moving average, in seconds; zero disables smoothing
  // and reports the raw per-second speed.
  static constexpr int64_t default_smoothing = 3;
  static constexpr int64_t max_smoothing     = 3600;

  // A moving average only ever approaches zero, so a transfer that stopped would
  // keep being reported for the better part of a minute. Once a counter has not
  // moved at all for this many seconds it is genuinely idle, and saying so is
  // both more useful and more honest than a decaying ghost.
  static constexpr int64_t idle_snap = 5;

  // Forget counters that have not been read for this long, so that peers and
  // downloads that went away do not accumulate.
  static constexpr int64_t idle_timeout = 60;

  // Anything with a monotonically increasing total() in bytes can be tracked;
  // the counter itself is only ever used as an identity and a reading, never
  // held on to.
  template <typename T>
  rate_type           rate(const T* source)              { return source != nullptr ? update(source, static_cast<int64_t>(source->total())) : 0; }

  int64_t             smoothing() const                  { return m_smoothing; }
  void                set_smoothing(int64_t seconds);

  void                prune();

private:
  rate_type           update(const void* source, int64_t total);

  struct sample_type {
    int64_t           timestamp{};  // Seconds since epoch of the last sample, zero if never sampled.
    int64_t           total{};      // Byte counter as of the last sample.
    int64_t           idle{};       // Consecutive seconds the counter has not moved.
    double            value{};      // Smoothed bytes per second.
  };

  std::unordered_map<const void*, sample_type> m_samples;

  int64_t             m_smoothing{default_smoothing};
};

// Shorthand for the single instance owned by core::Manager.
RateTracker*          rate_tracker();

}

#endif
