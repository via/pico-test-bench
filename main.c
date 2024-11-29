#include <stdlib.h>
#include <string.h>
#include "class/vendor/vendor_device.h"
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

static bool transmit_change_buffer(struct changebuf *b, uint32_t overflows) {
  if (!tud_vendor_mounted()) return false;

  uint32_t time = time_us_32();
  static Status msg = Status_init_default;
  msg.cputime = time;
  msg.state = State_Stopped;
  msg.command_count = rxcount;
  for (int i = 0; i < b->count; i++) {
    msg.changes[i].offset = b->changes[i].time_offset;
    msg.changes[i].value = b->changes[i].value;
  }
  msg.changes_count = b->count;
  msg.start_time = b->start_time;
  msg.overflows = overflows;

  static uint8_t buffer[Status_size];
  pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));

  pb_encode(&stream, Status_fields, &msg);

  static uint8_t cobsbuffer[COBS_ENCODE_MAX(Status_size)];
  unsigned int cobslen = 0;
  cobs_encode(buffer, stream.bytes_written, cobsbuffer, sizeof(cobsbuffer), &cobslen); 
  if (tud_vendor_write_available() < cobslen) {
    return false;
  }
  tud_vendor_write(cobsbuffer, cobslen);
  tud_vendor_write_flush();
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

static void add_input(const uint8_t next) {
  static uint8_t buffer[COBS_ENCODE_MAX(Command_size)];
  static size_t size = 0;

  buffer[size] = next;
  size += 1;

  if (next == '\0') {
    unsigned int decoded_len;
    cobs_decode(buffer, size, buffer, sizeof(buffer), &decoded_len); 
    handle_command(buffer, decoded_len);
    size = 0;
  } else if (size == sizeof(buffer)) {
    size = 0;
  }
}


void main_cpu1(void) {
    setup_input_output_pio();
    while (true) {
      sleep_ms(1000);
    }
}

static uint32_t count = 0;
void tud_vendor_rx_cb(uint8_t itf, uint8_t const *buf, uint16_t _len) {
  while (tud_vendor_available() > 0) {
    static uint8_t buffer[512];
    size_t len = tud_vendor_n_read(itf, buffer, 512);
    for (int i = 0; i < len; i++) {
      add_input(buffer[i]);
    }
  }
}

int main() {

    set_sys_clock_khz(128000, true);

    multicore_launch_core1(main_cpu1);

    tud_init(BOARD_TUD_RHPORT);
    if (board_init_after_tusb) {
      board_init_after_tusb();
    }

    

    while (true) {
      tud_task();
      if (!spsc_is_empty(&change_buffer_queue)) {
        int index = spsc_next(&change_buffer_queue);
        struct changebuf *buf = &change_buffers[index];
        if (transmit_change_buffer(buf, change_buffer_queue.overflows)) {
          spsc_release(&change_buffer_queue);
        }
      }
    }
    return 0;
}
