#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"

#include "hardware/clocks.h"
#include "hardware/dma.h"

#include "logic.h"


void setup_input_capture(void);
void setup_trigger_output(void);

static void transmit_change_buffer(struct changebuf *b) {}

#if 0
/* Send a changebuf over usb. Changebufs contain up to 128 change entries
 * spanning 16000 possible times. We encode a time, status fields, a count, and
 * then an array of tuples of time offset and pin values:
 * time - uint32 - time of first slot in buffer. If this is not 16000 past the
 *                 last received buffer, a buffer was dropped
 * count - uint8 - count of values in buffer. >128 means the buffer overflowed
 *                 and thus events were dropped
 * array of [count] tuples:
 *   time offset - uint16 - time offset from start time
 *   data - uint32 - pin values
 */
static uint32_t bleh = 0x01020304;
static uint8_t transmit_buffer[4 + 1 + 128 * 6 + 1];
static void transmit_change_buffer(struct changebuf *b) {
//  memcpy(&transmit_buffer[0], &b->start_time, 4);
  memcpy(&transmit_buffer[0], &bleh, 4);
  
  b->overflowed = false;
  b->count = 0;
  if (b->overflowed) {
    transmit_buffer[4] = 128 + 1;
  } else {
    transmit_buffer[4] = b->count;
  }
#if 0
  size_t position = 5;
  for (size_t i = 0; i < b->count; i++) {
    memcpy(&transmit_buffer[position], &b->changes[i].time_offset, 2);
    memcpy(&transmit_buffer[position + 2], &b->changes[i].value, 4);
    position += 6;
  }
#endif
  size_t len = 4 + 1 + 6 * b->count;
  if (5 != fwrite(&transmit_buffer[0], 1, writelen, stdout)) {
    while (true) printf("something bad2\n");
  }

}
#endif

int main() {
    stdio_init_all();

    /* Set 128 MHz clock, this allows a /2 divider for the PIO0 SMs, giving a
     * clean 16 Msps */
    set_sys_clock_khz(128000, true);

    setup_input_capture();
    setup_trigger_output();

    while (1) {
      if (!spsc_is_empty(&change_buffer_queue)) {
        int index = spsc_next(&change_buffer_queue);
        struct changebuf *buf = &change_buffers[index];
        transmit_change_buffer(buf);
        spsc_release(&change_buffer_queue);
      }
    }

    return 0;
}
