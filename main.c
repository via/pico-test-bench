#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "device/usbd.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"

#include "test-bench-interfaces.pb.h"
#include "pb_common.h"
#include "pb_encode.h"
#include "pb_decode.h"

#include "cobs.h"

#include "logic.h"


void setup_input_output_pio(void);

static void send_uint32(uint32_t val) {
  putchar_raw(val & 0xFF);
  putchar_raw((val >> 8) & 0xFF);
  putchar_raw((val >> 16) & 0xFF);
  putchar_raw((val >> 24) & 0xFF);
}

static uint32_t recv_uint32() {
  uint32_t result = 0;
  result |= (getchar() & 0xff);
  result |= (getchar() & 0xff) << 8;
  result |= (getchar() & 0xff) << 16;
  result |= (getchar() & 0xff) << 24;
  return result;
}

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
  for (int i = 0; i < cobslen; i++) {
    putchar_raw(cobsbuffer[i]);
  }

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
  uint8_t next = getchar_timeout_us(10);
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

int main() {

    set_sys_clock_khz(128000, true);

    stdio_init_all();

    setup_input_output_pio();
    
//    multicore_launch_core1(main_cpu1);

    while (true) {
      if (!spsc_is_empty(&change_buffer_queue)) {
        int index = spsc_next(&change_buffer_queue);
        struct changebuf *buf = &change_buffers[index];
        transmit_change_buffer(buf);
        spsc_release(&change_buffer_queue);
      } else {
        try_input();
      }
    }
    return 0;
}
