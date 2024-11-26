#include <stdlib.h>
#include <string.h>
#include "class/cdc/cdc_device.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"

#include "bsp/board_api.h"
#include "tusb.h"
#include "usb_descriptors.h"

#include "test-bench-interfaces.pb.h"
#include "pb_common.h"
#include "pb_encode.h"
#include "pb_decode.h"

#include "cobs.h"

#include "logic.h"


void setup_input_output_pio(void);

_Atomic uint32_t rxcount = 0;

static void transmit_change_buffer(struct changebuf *b) {
  uint32_t time = time_us_32();
  Status msg = Status_init_default;
  msg.cputime = time;
  msg.state = State_Stopped;
  msg.command_count = rxcount;

  uint8_t buffer[Status_size];
  pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));

  pb_encode(&stream, Status_fields, &msg);

  uint8_t cobsbuffer[COBS_ENCODE_MAX(Status_size)];
  unsigned int cobslen = 0;
  cobs_encode(buffer, stream.bytes_written, cobsbuffer, sizeof(cobsbuffer), &cobslen); 
  if (tud_cdc_write_available() < cobslen) {
    return false;
  }
  tud_cdc_write(cobsbuffer, cobslen);
  tud_cdc_write_flush();
  return true;

}

static void handle_command(uint8_t *buf, size_t size) {
  if (size > Command_size) {
    return;
  }

  pb_istream_t stream = pb_istream_from_buffer(buf, size);

  Command cmd = Command_init_default;
  bool status = pb_decode(&stream, Command_fields, &cmd);
  if (!status) {
    return;
  }
  rxcount += 1;
}

static void try_input(void) {
  static uint8_t buffer[COBS_ENCODE_MAX(Command_size)];
  static size_t size = 0;
  uint8_t next = 1; // getchar_timeout_us(10);
  if (next == '\0') {
    unsigned int decoded_len;
    buffer[size] = next;
    size += 1;
    cobs_decode(buffer, size, buffer, sizeof(buffer), &decoded_len); 
    handle_command(buffer, decoded_len);
    size = 0;
  } else if (size >= sizeof(buffer)) {
    size = 0;
  } else {
    buffer[size] = next;
    size += 1;
  }
}


void main_cpu1(void) {
}

static uint32_t count = 0;
static uint8_t buffer[128];
void tud_cdc_rx_cb(uint8_t itf) {
  rxcount += tud_cdc_n_read(itf, buffer, 128);
}

int main() {

    set_sys_clock_khz(128000, true);


    tud_init(BOARD_TUD_RHPORT);
    if (board_init_after_tusb) {
      board_init_after_tusb();
    }

    setup_input_output_pio();
    
//    multicore_launch_core1(main_cpu1);

    while (true) {
      tud_task();
      if (!spsc_is_empty(&change_buffer_queue)) {
        int index = spsc_next(&change_buffer_queue);
        struct changebuf *buf = &change_buffers[index];
        if (transmit_change_buffer(buf)) {
          spsc_release(&change_buffer_queue);
        }
      }
    }
    return 0;
}
