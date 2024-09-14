#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"

#include "logic.h"


void setup_input_output_pio(void);

#if 0
static void transmit_change_buffer(struct changebuf *b) {}
#endif

/* Send a changebuf over usb. Changebufs contain up to 128 change entries
 * spanning 4000 possible times. We encode a time, status fields, a count, and
 * then an array of tuples of time offset and pin values:
 * time - uint32 - time of first slot in buffer. If this is not 16000 past the
 *                 last received buffer, a buffer was dropped
 * count - uint8 - count of values in buffer. >128 means the buffer overflowed
 *                 and thus events were dropped
 * array of [count] tuples:
 *   time offset - uint16 - time offset from start time
 *   data - uint32 - pin values
 */
static void send_uint32(uint32_t val) {
  putchar_raw((val >> 24) & 0xFF);
  putchar_raw((val >> 16) & 0xFF);
  putchar_raw((val >> 8) & 0xFF);
  putchar_raw(val & 0xFF);
}

static void send_uint16(uint16_t val) {
  putchar_raw((val >> 8) & 0xFF);
  putchar_raw(val & 0xFF);
}

const char *to_parse = "a 0 0 0 0 10 0 0 0 0 52 0 0 0 0 0 0";

static uint8_t bleh[] = "HELO";
static void transmit_change_buffer(struct changebuf *b) {
  
  putchar_raw(bleh[0]);
  putchar_raw(bleh[1]);
  putchar_raw(bleh[2]);
  putchar_raw(bleh[3]);

  send_uint32(b->start_time);
  send_uint32(b->count);

  for (size_t i = 0; i < b->count; i++) {
   send_uint16(b->changes[i].time_offset);
   send_uint32(b->changes[i].value);
  }

}

static void cobs_encode(char buffer[static 1024], size_t n) {

}

_Atomic uint32_t started = 0;

void main_cpu1(void) {
  #if 0
  while (true) {
    uint32_t delay;
    uint8_t trigger;
    uint8_t blah;
    int ret = fread(&blah, 1, 1, stdin);
    putchar_raw('.');
    switch (blah) {
      case 'T': {
                  fread(&delay, sizeof(delay), 1, stdin);
                  putchar_raw(',');
                  fread(&trigger, sizeof(trigger), 1, stdin);
                  putchar_raw(',');
                  started = 1;
                  break;
                }
      default:
                putchar_raw(blah);
                break;
    }
  }
#endif
    setup_input_output_pio();

    while (true) {
      getchar();
    }
}

int main() {

    set_sys_clock_khz(128000, true);

    stdio_init_all();

    getchar();
    
    multicore_launch_core1(main_cpu1);

    while (true) {
      if (!spsc_is_empty(&change_buffer_queue)) {
        int index = spsc_next(&change_buffer_queue);
        struct changebuf *buf = &change_buffers[index];
        transmit_change_buffer(buf);
        spsc_release(&change_buffer_queue);
      }
    }
    return 0;
}
