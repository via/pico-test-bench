#pragma once
#include <stdint.h>
#include <stdbool.h>

#include "spsc.h"

struct change {
  uint32_t time;
  uint32_t value;
};

#define MAX_CHANGES_PER_BUF 128
struct changebuf {
  struct change changes[MAX_CHANGES_PER_BUF];
  size_t count;
  bool overflowed;
  uint32_t time_us;
};

extern struct spsc_queue change_buffer_queue;
extern struct changebuf change_buffers[32];

