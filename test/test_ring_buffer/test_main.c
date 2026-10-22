#include <pthread.h>
#include <stdint.h>
#include <unity.h>

#include "ring_buffer.h"

#define CAPACITY 8

static ring_buffer_t rb;
static sample_t storage[CAPACITY];

void setUp(void)
{
  TEST_ASSERT_TRUE(rb_init(&rb, storage, CAPACITY));
}

void tearDown(void) {}

static sample_t make(uint32_t i)
{
  sample_t s = {i * 1000u, (int32_t)i - 100};
  return s;
}

static void test_init_rejects_non_power_of_two(void)
{
  ring_buffer_t other;
  sample_t buf[6];
  TEST_ASSERT_FALSE(rb_init(&other, buf, 6));
  TEST_ASSERT_FALSE(rb_init(&other, buf, 0));
}

static void test_empty_pop_fails(void)
{
  sample_t out;
  TEST_ASSERT_EQUAL_UINT32(0, rb_count(&rb));
  TEST_ASSERT_FALSE(rb_pop(&rb, &out));
  TEST_ASSERT_FALSE(rb_peek(&rb, &out));
  TEST_ASSERT_EQUAL_UINT32(0, rb_pop_bulk(&rb, &out, 1));
}

static void test_fifo_order_and_peek(void)
{
  for (uint32_t i = 0; i < 5; i++) {
    sample_t s = make(i);
    TEST_ASSERT_TRUE(rb_push(&rb, &s));
  }
  sample_t out;
  TEST_ASSERT_TRUE(rb_peek(&rb, &out));
  TEST_ASSERT_EQUAL_UINT32(0, out.timestamp_us);
  TEST_ASSERT_EQUAL_UINT32(5, rb_count(&rb));
  for (uint32_t i = 0; i < 5; i++) {
    TEST_ASSERT_TRUE(rb_pop(&rb, &out));
    TEST_ASSERT_EQUAL_UINT32(i * 1000u, out.timestamp_us);
    TEST_ASSERT_EQUAL_INT32((int32_t)i - 100, out.raw_sample);
  }
}

static void test_full_drops_newest_and_counts(void)
{
  for (uint32_t i = 0; i < CAPACITY; i++) {
    sample_t s = make(i);
    TEST_ASSERT_TRUE(rb_push(&rb, &s));
  }
  sample_t extra = make(99);
  TEST_ASSERT_FALSE(rb_push(&rb, &extra));
  TEST_ASSERT_FALSE(rb_push(&rb, &extra));
  TEST_ASSERT_EQUAL_UINT32(2, rb_overflows(&rb));
  TEST_ASSERT_EQUAL_UINT32(CAPACITY, rb_count(&rb));

  sample_t out[CAPACITY];
  TEST_ASSERT_EQUAL_UINT32(CAPACITY, rb_pop_bulk(&rb, out, CAPACITY));
  TEST_ASSERT_EQUAL_UINT32((CAPACITY - 1) * 1000u, out[CAPACITY - 1].timestamp_us);
}

static void test_wrap_at_2_pow_32(void)
{
  rb.head = UINT32_MAX - 2;
  rb.tail = UINT32_MAX - 2;
  for (uint32_t i = 0; i < 6; i++) {
    sample_t s = make(i);
    TEST_ASSERT_TRUE(rb_push(&rb, &s));
  }
  TEST_ASSERT_EQUAL_UINT32(6, rb_count(&rb));
  sample_t out;
  for (uint32_t i = 0; i < 6; i++) {
    TEST_ASSERT_TRUE(rb_pop(&rb, &out));
    TEST_ASSERT_EQUAL_UINT32(i * 1000u, out.timestamp_us);
  }
  TEST_ASSERT_EQUAL_UINT32(0, rb_count(&rb));
}

static void test_pop_bulk_partial(void)
{
  for (uint32_t i = 0; i < 3; i++) {
    sample_t s = make(i);
    rb_push(&rb, &s);
  }
  sample_t out[8];
  TEST_ASSERT_EQUAL_UINT32(2, rb_pop_bulk(&rb, out, 2));
  TEST_ASSERT_EQUAL_UINT32(1, rb_count(&rb));
  TEST_ASSERT_EQUAL_UINT32(1, rb_pop_bulk(&rb, out, 8));
  TEST_ASSERT_EQUAL_UINT32(2000u, out[0].timestamp_us);
}

#define STRESS_COUNT 2000000u

static void *producer(void *arg)
{
  (void)arg;
  for (uint32_t i = 0; i < STRESS_COUNT;) {
    sample_t s = {i, (int32_t)i};
    if (rb_push(&rb, &s)) {
      i++;
    }
  }
  return NULL;
}

static void test_spsc_threads_preserve_order(void)
{
  pthread_t thread;
  pthread_create(&thread, NULL, producer, NULL);

  uint32_t expected = 0;
  uint32_t errors = 0;
  sample_t out[4];
  while (expected < STRESS_COUNT) {
    uint32_t n = rb_pop_bulk(&rb, out, 4);
    for (uint32_t i = 0; i < n; i++) {
      if (out[i].timestamp_us != expected || out[i].raw_sample != (int32_t)expected) {
        errors++;
      }
      expected++;
    }
  }
  pthread_join(thread, NULL);
  TEST_ASSERT_EQUAL_UINT32(0, errors);
  TEST_ASSERT_EQUAL_UINT32(0, rb_count(&rb));
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_init_rejects_non_power_of_two);
  RUN_TEST(test_empty_pop_fails);
  RUN_TEST(test_fifo_order_and_peek);
  RUN_TEST(test_full_drops_newest_and_counts);
  RUN_TEST(test_wrap_at_2_pow_32);
  RUN_TEST(test_pop_bulk_partial);
  RUN_TEST(test_spsc_threads_preserve_order);
  return UNITY_END();
}
