#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

struct spsc_queue {
  uint32_t size;
  uint32_t read;
  uint32_t write;
  int overflows;
};

bool spsc_is_empty(const struct spsc_queue *q);
bool spsc_is_full(const struct spsc_queue *q);
int spsc_allocate(struct spsc_queue *q);
void spsc_push(struct spsc_queue *q);
int spsc_next(struct spsc_queue *q);
void spsc_release(struct spsc_queue *q);
