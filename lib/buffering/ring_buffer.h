#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "sample.h"

#ifdef __cplusplus
extern "C" {
#endif

// Lock-free single-producer / single-consumer ring buffer of sample_t.
//
// head and tail are free-running 32-bit counters (index = counter & mask), so
// count = head - tail stays correct across 2^32 wrap. Only the producer writes
// head and overflow_count; only the consumer writes tail. A full buffer drops
// the newest sample, because the producer must never move tail.
typedef struct {
  sample_t *storage;
  uint32_t mask;
  volatile uint32_t head;
  volatile uint32_t tail;
  volatile uint32_t overflow_count;
} ring_buffer_t;

// capacity must be a power of two. Returns false otherwise.
bool rb_init(ring_buffer_t *rb, sample_t *storage, uint32_t capacity);

// Producer side.
bool rb_push(ring_buffer_t *rb, const sample_t *sample);

// Consumer side.
bool rb_pop(ring_buffer_t *rb, sample_t *out);
bool rb_peek(const ring_buffer_t *rb, sample_t *out);
uint32_t rb_pop_bulk(ring_buffer_t *rb, sample_t *out, uint32_t max_count);

// Either side.
uint32_t rb_count(const ring_buffer_t *rb);
uint32_t rb_capacity(const ring_buffer_t *rb);
uint32_t rb_overflows(const ring_buffer_t *rb);

#ifdef __cplusplus
}
#endif
