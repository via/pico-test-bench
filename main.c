#include <stdio.h>
#include "pico/stdlib.h"

#include "hardware/clocks.h"
#include "hardware/dma.h"

#include "logic.h"


void setup_input_capture(void);
void setup_trigger_output(void);

int main() {
    stdio_init_all();

    /* Set 128 MHz clock, this allows a /2 divider for the PIO0 SMs, giving a
     * clean 16 Msps */
    set_sys_clock_khz(128000, true);
    printf("Start!\n");
    setup_input_capture();
    setup_trigger_output();

    uint32_t count = 0;
    uint32_t overflows = 0;
    while (1) {
      if (!spsc_is_empty(&change_buffer_queue)) {
        int index = spsc_next(&change_buffer_queue);
        struct changebuf *buf = &change_buffers[index];
        count += 1;
        for (int i = 0; i < buf->count; i++) {
          printf("%u %x (%d)\n", buf->changes[i].time, buf->changes[i].value, atomic_load_explicit(&change_buffer_queue.overflows, memory_order_relaxed));
        }
        spsc_release(&change_buffer_queue);
      }
    }

    return 0;
}
