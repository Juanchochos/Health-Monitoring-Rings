#include "ring_buffer.h"

#include <stddef.h>

// Orders the payload access against the index publish. On Cortex-M4 this is a
// DMB, identical to CMSIS __DMB(), without pulling in device headers.
#if defined(__arm__)
#define RB_BARRIER() __asm volatile("dmb" ::: "memory")
#else
#define RB_BARRIER() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#endif

bool rb_init(ring_buffer_t *rb, sample_t *storage, uint32_t capacity)
{
  if (rb == NULL || storage == NULL || capacity == 0 || (capacity & (capacity - 1)) != 0) {
    return false;
  }
  rb->storage = storage;
  rb->mask = capacity - 1;
  rb->head = 0;
  rb->tail = 0;
  rb->overflow_count = 0;
  return true;
}

bool rb_push(ring_buffer_t *rb, const sample_t *sample)
{
  const uint32_t head = rb->head;
  const uint32_t tail = rb->tail;
  if (head - tail > rb->mask) {
    rb->overflow_count++;
    return false;
  }
  rb->storage[head & rb->mask] = *sample;
  RB_BARRIER();
  rb->head = head + 1;
  return true;
}

bool rb_peek(const ring_buffer_t *rb, sample_t *out)
{
  const uint32_t tail = rb->tail;
  if (rb->head == tail) {
    return false;
  }
  RB_BARRIER();
  *out = rb->storage[tail & rb->mask];
  return true;
}

bool rb_pop(ring_buffer_t *rb, sample_t *out)
{
  const uint32_t tail = rb->tail;
  if (rb->head == tail) {
    return false;
  }
  RB_BARRIER();
  *out = rb->storage[tail & rb->mask];
  RB_BARRIER();
  rb->tail = tail + 1;
  return true;
}

uint32_t rb_pop_bulk(ring_buffer_t *rb, sample_t *out, uint32_t max_count)
{
  const uint32_t tail = rb->tail;
  uint32_t available = rb->head - tail;
  if (available > max_count) {
    available = max_count;
  }
  if (available == 0) {
    return 0;
  }
  RB_BARRIER();
  for (uint32_t i = 0; i < available; i++) {
    out[i] = rb->storage[(tail + i) & rb->mask];
  }
  RB_BARRIER();
  rb->tail = tail + available;
  return available;
}

uint32_t rb_count(const ring_buffer_t *rb)
{
  return rb->head - rb->tail;
}

uint32_t rb_capacity(const ring_buffer_t *rb)
{
  return rb->mask + 1;
}

uint32_t rb_overflows(const ring_buffer_t *rb)
{
  return rb->overflow_count;
}
