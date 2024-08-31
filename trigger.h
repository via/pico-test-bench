#include <stdint.h>

struct trigger_block {
  uint32_t start_time;
  uint8_t count;
  uint16_t triggers[64];
};

static uint16_t trigger_block_get_time_offset(uint16_t value) {
  return (value >> 4);
}

static uint8_t trigger_block_get_triggers(uint16_t value) {
  return value & 0x3;
}
