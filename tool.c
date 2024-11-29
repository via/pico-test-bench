
#include <stdio.h>
#include <stdbool.h>
#include <libusb-1.0/libusb.h>

static struct libusb_device_handle *devh;

struct transfer {
  struct libusb_transfer *xfer;
  uint8_t buffer[4096];
};

static void write_cb(struct libusb_transfer *t);
static void fill_and_submit_write(struct transfer *t) {
  int len = fread(t->buffer, 1, 4096, stdin);
  libusb_fill_bulk_transfer(t->xfer, devh, 0x2, t->buffer, len, write_cb, t, 1000);
  libusb_submit_transfer(t->xfer);
}

static void read_cb(struct libusb_transfer *t);
static void fill_and_submit_read(struct transfer *t) {
  fwrite(t->buffer, 1, t->xfer->actual_length, stdout);

  libusb_fill_bulk_transfer(t->xfer, devh, 0x82, t->buffer, 4096, read_cb, t, 1000);
  libusb_submit_transfer(t->xfer);
}

static void read_cb(struct libusb_transfer *t) {
  fill_and_submit_read(t->user_data);
}

static void write_cb(struct libusb_transfer *t) {
  fill_and_submit_write(t->user_data);
}



int main(void) {
  const uint16_t vid = 0xcafe;
  const uint16_t pid = 0x4010;

  int rc = libusb_init(NULL);
  if (rc < 0) {
    return 1;
  }

  devh = libusb_open_device_with_vid_pid(NULL, vid, pid);
  struct transfer transfers[8];


  for (int i = 0; i < 4; i++) {
    transfers[i].xfer = libusb_alloc_transfer(0);
    fill_and_submit_read(&transfers[i]);
  }

  for (int i = 4; i < 8; i++) {
    transfers[i].xfer = libusb_alloc_transfer(0);
    fill_and_submit_write(&transfers[i]);
  }

  while(true) {
    libusb_handle_events(NULL);
  }


}
