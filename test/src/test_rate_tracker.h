#include "test/helpers/test_main_thread.h"

class TestRateTracker : public TestFixtureWithMainThread {
  CPPUNIT_TEST_SUITE(TestRateTracker);

  CPPUNIT_TEST(test_first_reading_only_sets_a_baseline);
  CPPUNIT_TEST(test_follows_a_steady_transfer);
  CPPUNIT_TEST(test_evens_out_bursty_seconds);
  CPPUNIT_TEST(test_stopped_transfer_reads_zero);
  CPPUNIT_TEST(test_uneven_sampling_converges_alike);
  CPPUNIT_TEST(test_reset_counter_starts_over);
  CPPUNIT_TEST(test_smoothing_is_configurable);
  CPPUNIT_TEST(test_prune_forgets_unread_counters);

  CPPUNIT_TEST_SUITE_END();

public:
  void setUp();
  void tearDown();

  void test_first_reading_only_sets_a_baseline();
  void test_follows_a_steady_transfer();
  void test_evens_out_bursty_seconds();
  void test_stopped_transfer_reads_zero();
  void test_uneven_sampling_converges_alike();
  void test_reset_counter_starts_over();
  void test_smoothing_is_configurable();
  void test_prune_forgets_unread_counters();
};
