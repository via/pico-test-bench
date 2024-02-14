#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"

#include "edgedetect.pio.h"


uint32_t captures[500 * 32];
uint32_t edges[500];


int main() {
    stdio_init_all();
//    set_sys_clock_khz(125000, true);
    printf("Start!\n");
    PIO pio = pio0;
    uint offset = pio_add_program(pio, &edgedetect_program);
    uint sm = pio_claim_unused_sm(pio, true);
    edgedetect_program_init(pio, sm, offset, 0, 4);
    uint32_t last_time = 0;
    while (1) {
      int n_captures = 0;
      while (n_captures < 500) {
        uint32_t value = pio_sm_get_blocking(pio, sm);
        edges[n_captures] = value;
        n_captures++;
      }
      int n_edges = 0;
      uint32_t before = time_us_32();
      for (int i = 0; i < n_captures; i++) {
        uint32_t edge = edges[i];
        if (edge != 0) {
          for (int j = 0; j < 32; j++) {
            if ((edge & 1) == 1) {
              n_edges++;
            }
            edge = edge >> 1;
          }
        }
      }
      uint32_t after = time_us_32();
      if (n_edges > 0) {
        printf("edges: %d, time: %u\n", n_edges, after - before);
      }
    }
    return 0;
}
