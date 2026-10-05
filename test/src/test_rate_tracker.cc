#include "config.h"

#include "test/src/test_rate_tracker.h"

#include <chrono>
#include <cstdlib>

#include "core/rate_tracker.h"

CPPUNIT_TEST_SUITE_REGISTRATION(TestRateTracker);

namespace {

// Stands in for torrent::Rate, of which the tracker only ever reads the
// cumulative byte count.
struct test_counter {
  int64_t             total() const                      { return m_total; }
  int64_t             m_total{};
};

// Advance the clock the tracker reads and move the counter along at the given
// number of bytes per second.
void
transfer(TestMainThread* thread, test_counter& counter, int64_t seconds, int64_t bytes_per_second) {
  thread->test_add_cached_time(std::chrono::seconds(seconds));
  counter.m_total += seconds * bytes_per_second;
}

} // namespace

void
TestRateTracker::setUp() {
  TestFixtureWithMainThread::setUp();

  m_main_thread->test_set_cached_time(std::chrono::seconds(0));
}

void
TestRateTracker::tearDown() {
  TestFixtureWithMainThread::tearDown();
}

void
TestRateTracker::test_first_reading_only_sets_a_baseline() {
  core::RateTracker tracker;
  test_counter      counter;

  CPPUNIT_ASSERT(tracker.rate(&counter) == 0);

  // A second reading within the same second has nothing new to say either.
  counter.m_total += 1000000;
  CPPUNIT_ASSERT(tracker.rate(&counter) == 0);

  CPPUNIT_ASSERT(tracker.rate(static_cast<test_counter*>(nullptr)) == 0);
}

void
TestRateTracker::test_follows_a_steady_transfer() {
  core::RateTracker tracker;
  test_counter      counter;

  tracker.rate(&counter);

  // The window based estimate this replaced needed minutes to get here.
  transfer(m_main_thread.get(), counter, 3, 1000000);
  CPPUNIT_ASSERT(tracker.rate(&counter) > 600000);

  transfer(m_main_thread.get(), counter, 7, 1000000);
  CPPUNIT_ASSERT(tracker.rate(&counter) > 950000);
  CPPUNIT_ASSERT(tracker.rate(&counter) <= 1000000);
}

void
TestRateTracker::test_evens_out_bursty_seconds() {
  core::RateTracker tracker;
  test_counter      counter;

  tracker.rate(&counter);

  // Two megabytes every other second is a megabyte per second, and that is what
  // should be reported rather than the raw seesaw between nothing and double.
  for (int i = 1; i <= 24; i++) {
    transfer(m_main_thread.get(), counter, 1, (i % 2) != 0 ? 2000000 : 0);

    auto rate = tracker.rate(&counter);

    if (i > 12) {
      CPPUNIT_ASSERT(rate > 700000);
      CPPUNIT_ASSERT(rate < 1300000);
    }
  }
}

void
TestRateTracker::test_stopped_transfer_reads_zero() {
  core::RateTracker tracker;
  test_counter      counter;

  tracker.rate(&counter);
  transfer(m_main_thread.get(), counter, 10, 1000000);
  CPPUNIT_ASSERT(tracker.rate(&counter) > 900000);

  // Decaying towards zero, but not there yet.
  for (int64_t i = 1; i < core::RateTracker::idle_snap; i++) {
    transfer(m_main_thread.get(), counter, 1, 0);
    CPPUNIT_ASSERT(tracker.rate(&counter) != 0);
  }

  // Nothing has moved for long enough that the transfer really has stopped.
  transfer(m_main_thread.get(), counter, 1, 0);
  CPPUNIT_ASSERT(tracker.rate(&counter) == 0);

  transfer(m_main_thread.get(), counter, 30, 0);
  CPPUNIT_ASSERT(tracker.rate(&counter) == 0);
}

void
TestRateTracker::test_uneven_sampling_converges_alike() {
  core::RateTracker every_second;
  core::RateTracker every_third_second;
  test_counter      seconds_counter;
  test_counter      thirds_counter;

  every_second.rate(&seconds_counter);
  every_third_second.rate(&thirds_counter);

  // Whoever reads a counter is what samples it, so one that nothing looked at
  // for a while must not end up with a different average than a busy one.
  for (int i = 1; i <= 12; i++) {
    transfer(m_main_thread.get(), seconds_counter, 1, 1000000);
    every_second.rate(&seconds_counter);

    if (i % 3 != 0)
      continue;

    thirds_counter.m_total = seconds_counter.m_total;

    auto sampled_often   = every_second.rate(&seconds_counter);
    auto sampled_seldom  = every_third_second.rate(&thirds_counter);

    CPPUNIT_ASSERT(std::abs((int64_t)sampled_often - (int64_t)sampled_seldom) < 1000);
  }
}

void
TestRateTracker::test_reset_counter_starts_over() {
  core::RateTracker tracker;
  test_counter      counter;

  tracker.rate(&counter);
  transfer(m_main_thread.get(), counter, 10, 1000000);
  CPPUNIT_ASSERT(tracker.rate(&counter) > 900000);

  // Restoring the totals from a session file winds the counter back.
  m_main_thread->test_add_cached_time(std::chrono::seconds(1));
  counter.m_total = 4096;
  CPPUNIT_ASSERT(tracker.rate(&counter) == 0);

  transfer(m_main_thread.get(), counter, 10, 1000000);
  CPPUNIT_ASSERT(tracker.rate(&counter) > 900000);
}

void
TestRateTracker::test_smoothing_is_configurable() {
  core::RateTracker tracker;
  test_counter      counter;

  CPPUNIT_ASSERT(tracker.smoothing() == core::RateTracker::default_smoothing);

  tracker.set_smoothing(-1);
  CPPUNIT_ASSERT(tracker.smoothing() == 0);

  tracker.set_smoothing(core::RateTracker::max_smoothing + 1);
  CPPUNIT_ASSERT(tracker.smoothing() == core::RateTracker::max_smoothing);

  // Without smoothing the raw per-second speed is reported as it is.
  tracker.set_smoothing(0);
  tracker.rate(&counter);

  transfer(m_main_thread.get(), counter, 1, 1234567);
  CPPUNIT_ASSERT(tracker.rate(&counter) == 1234567);

  transfer(m_main_thread.get(), counter, 2, 2000000);
  CPPUNIT_ASSERT(tracker.rate(&counter) == 2000000);
}

void
TestRateTracker::test_prune_forgets_unread_counters() {
  core::RateTracker pruned;
  core::RateTracker kept;
  test_counter      pruned_counter;
  test_counter      kept_counter;

  pruned.rate(&pruned_counter);
  kept.rate(&kept_counter);

  transfer(m_main_thread.get(), pruned_counter, core::RateTracker::idle_timeout + 1, 1000000);
  kept_counter.m_total = pruned_counter.m_total;

  pruned.prune();

  // The counter could since have been freed and its address handed out to
  // something else, so after pruning the next reading has to be a baseline.
  CPPUNIT_ASSERT(pruned.rate(&pruned_counter) == 0);
  CPPUNIT_ASSERT(kept.rate(&kept_counter) != 0);
}
