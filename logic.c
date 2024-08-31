#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"

#include "edgedetect.pio.h"
#include "capture.pio.h"
#include "triggergen.pio.h"

#include "spsc.h"
#include "logic.h"
#include "trigger.h"


/* Collect data 1 ms at a time. 
 * We use two PIO state machines, each using two DMA channels:
 *  - One (capture) captures 32 bit words at 16 Msps and stores them into a
 *    large buffer 
 *  - One (edgedetect) produces 32 bit values where each bit indicates that the
 *    inputs changed, thus 32 bits represents 32 time points.
 * Each PIO uses two DMA channels to double buffer. When the edgedetect DMA
 * completes, an interrupt handler processes the edges and hands them off to
 * be consumed by the IO loop */

#define SAMPLES_PER_SEC 16000000

#define CAPTURE_COUNT (SAMPLES_PER_SEC / 1000)
static uint32_t captures_buf1[CAPTURE_COUNT] = {0};
static uint32_t captures_buf2[CAPTURE_COUNT] = {0};

#define EDGE_COUNT (CAPTURE_COUNT / 32)
static uint32_t edges_buf1[EDGE_COUNT] = {0};
static uint32_t edges_buf2[EDGE_COUNT] = {0};

#define TRIGGERS_PER_MS 4000
#define TRIGGER_DMA_SIZE (TRIGGERS_PER_MS / 4)

uint8_t trigger_dma_block_1[TRIGGER_DMA_SIZE] = {0};
uint8_t trigger_dma_block_2[TRIGGER_DMA_SIZE] = {0};

#define CAPTURE_BUF1_DMA 0
#define CAPTURE_BUF2_DMA 1
#define EDGEDETECT_BUF1_DMA 2
#define EDGEDETECT_BUF2_DMA 3
#define TRIGGERGEN_BUF1_DMA 4
#define TRIGGERGEN_BUF2_DMA 5

#define CAPTURE_SM 0
#define EDGEDETECT_SM 1
#define TRIGGERGEN_SM 2

struct spsc_queue change_buffer_queue = {
  .size = 32,
};
struct changebuf change_buffers[32] = { 0 };

static void write_change(struct changebuf *dst, uint16_t time_offset, uint32_t value) {
  if (dst->count >= MAX_CHANGES_PER_BUF) {
    dst->overflowed = true;
    return;
  }
  dst->changes[dst->count] = (struct change){.time_offset = time_offset, .value = value};
  dst->count++;
}

static void collapse_buffer(struct changebuf *dst, uint32_t captures[CAPTURE_COUNT], uint32_t edges[EDGE_COUNT]) {
  for (int i = 0; i < EDGE_COUNT; i++) {
    uint32_t edge = edges[i];
    if (edge != 0) {
      for (int bit = 0; bit < 32; bit++) {
        if ((edge & 0x80000000) != 0) {
          uint32_t position = (32 * i) + bit;
          write_change(dst, position, captures[position]);
        }
        edge <<= 1;
      }
    }
  }
}

static uint32_t capture_time = 0;

void dma_handler(void) {
  if (dma_channel_get_irq0_status(EDGEDETECT_BUF1_DMA)) {
    if (!dma_channel_get_irq0_status(CAPTURE_BUF1_DMA)) {
      abort();
    }

    int index = spsc_allocate(&change_buffer_queue);
    if (index >= 0) {
      struct changebuf *buf = &change_buffers[index];
      buf->count = 0;
      buf->overflowed = false;
      buf->start_time = capture_time;
      collapse_buffer(buf, captures_buf1, edges_buf1);
      spsc_push(&change_buffer_queue);
    }

    dma_channel_set_write_addr(EDGEDETECT_BUF1_DMA, edges_buf1, false); 
    dma_channel_acknowledge_irq0(EDGEDETECT_BUF1_DMA);
    dma_channel_set_write_addr(CAPTURE_BUF1_DMA, captures_buf1, false); 
    dma_channel_acknowledge_irq0(CAPTURE_BUF1_DMA);

  } else if (dma_channel_get_irq0_status(EDGEDETECT_BUF2_DMA)) {
    if (!dma_channel_get_irq0_status(CAPTURE_BUF2_DMA)) {
      abort();
    }

    int index = spsc_allocate(&change_buffer_queue);
    if (index >= 0) {
      struct changebuf *buf = &change_buffers[index];
      buf->count = 0;
      buf->overflowed = false;
      buf->start_time = capture_time;
      collapse_buffer(buf, captures_buf2, edges_buf2);
      spsc_push(&change_buffer_queue);
    }

    dma_channel_set_write_addr(CAPTURE_BUF2_DMA, captures_buf2, false); 
    dma_channel_acknowledge_irq0(CAPTURE_BUF2_DMA);
    dma_channel_set_write_addr(EDGEDETECT_BUF2_DMA, edges_buf2, false); 
    dma_channel_acknowledge_irq0(EDGEDETECT_BUF2_DMA);
  }

  if (dma_channel_get_irq0_status(TRIGGERGEN_BUF1_DMA)) {
    dma_channel_set_read_addr(TRIGGERGEN_BUF1_DMA, trigger_dma_block_1, false); 
    dma_channel_acknowledge_irq0(TRIGGERGEN_BUF1_DMA);
  } else if (dma_channel_get_irq0_status(TRIGGERGEN_BUF2_DMA)) {
    dma_channel_set_read_addr(TRIGGERGEN_BUF2_DMA, trigger_dma_block_2, false); 
    dma_channel_acknowledge_irq0(TRIGGERGEN_BUF2_DMA);
  }

  capture_time += 16000;
}

static void configure_dma(void) {
  /* Configure Channel 0 to copy PIO0 SM0's RX, 32 bits at a time, to
   * capture_ch0, and to fire our dma handler interrupt when completed */
  {
    dma_channel_claim(CAPTURE_BUF1_DMA);
    dma_channel_config config = dma_channel_get_default_config(CAPTURE_BUF1_DMA);
    channel_config_set_read_increment(&config, false);
    channel_config_set_write_increment(&config, true);
    channel_config_set_dreq(&config, DREQ_PIO0_RX0);
    channel_config_set_chain_to(&config, CAPTURE_BUF2_DMA); /* Chain to buf 2*/
    dma_channel_configure(CAPTURE_BUF1_DMA, &config, captures_buf1, &pio0_hw->rxf[CAPTURE_SM], CAPTURE_COUNT, true);
    dma_channel_set_irq0_enabled(CAPTURE_BUF1_DMA, true);
  }

  /* Channel 1 same as 0, except use 2nd buffer and do not enable it now */
  {
    dma_channel_claim(CAPTURE_BUF2_DMA);
    dma_channel_config config = dma_channel_get_default_config(CAPTURE_BUF2_DMA);
    channel_config_set_read_increment(&config, false);
    channel_config_set_write_increment(&config, true);
    channel_config_set_dreq(&config, DREQ_PIO0_RX0);
    channel_config_set_chain_to(&config, CAPTURE_BUF1_DMA);
    dma_channel_configure(CAPTURE_BUF2_DMA, &config, captures_buf2, &pio0_hw->rxf[CAPTURE_SM], CAPTURE_COUNT, false);
    dma_channel_set_irq0_enabled(CAPTURE_BUF2_DMA, true);
  }

  /* Channel 2 is to copy PIO0's SM1 (edgedetect) 32 bits at a time to edge
   * buf1, and fire interrupt when complete */
  {
    dma_channel_claim(EDGEDETECT_BUF1_DMA);
    dma_channel_config config = dma_channel_get_default_config(EDGEDETECT_BUF1_DMA);
    channel_config_set_read_increment(&config, false);
    channel_config_set_write_increment(&config, true);
    channel_config_set_dreq(&config, DREQ_PIO0_RX1);
    channel_config_set_chain_to(&config, EDGEDETECT_BUF2_DMA);
    dma_channel_configure(EDGEDETECT_BUF1_DMA, &config, edges_buf1, &pio0_hw->rxf[EDGEDETECT_SM], EDGE_COUNT, true);
    dma_channel_set_irq0_enabled(EDGEDETECT_BUF1_DMA, true);
  }

  /* Channel 3 is same as 2, but buf2 (and don't start it yet)
   * buf1, and fire interrupt when complete */
  {
    dma_channel_claim(EDGEDETECT_BUF2_DMA);
    dma_channel_config config = dma_channel_get_default_config(EDGEDETECT_BUF2_DMA);
    channel_config_set_read_increment(&config, false);
    channel_config_set_write_increment(&config, true);
    channel_config_set_dreq(&config, DREQ_PIO0_RX1);
    channel_config_set_chain_to(&config, EDGEDETECT_BUF1_DMA);
    dma_channel_configure(EDGEDETECT_BUF2_DMA, &config, edges_buf2, &pio0_hw->rxf[EDGEDETECT_SM], EDGE_COUNT, false);
    dma_channel_set_irq0_enabled(EDGEDETECT_BUF2_DMA, true);
  }

  /* Channel 4 is for trigger outputs buf 1*/
  {
    dma_channel_claim(TRIGGERGEN_BUF1_DMA);
    dma_channel_config config = dma_channel_get_default_config(TRIGGERGEN_BUF1_DMA);
    channel_config_set_read_increment(&config, true);
    channel_config_set_write_increment(&config, false);
    channel_config_set_dreq(&config, DREQ_PIO0_TX2);
    channel_config_set_chain_to(&config, TRIGGERGEN_BUF2_DMA);
    dma_channel_configure(TRIGGERGEN_BUF1_DMA, &config, &pio0_hw->txf[TRIGGERGEN_SM], trigger_dma_block_1, TRIGGER_DMA_SIZE / 4, true);
    dma_channel_set_irq0_enabled(TRIGGERGEN_BUF1_DMA, true);
  }

  /* Channel 5 is for trigger outputs buf 2*/
  {
    dma_channel_claim(TRIGGERGEN_BUF2_DMA);
    dma_channel_config config = dma_channel_get_default_config(TRIGGERGEN_BUF2_DMA);
    channel_config_set_read_increment(&config, true);
    channel_config_set_write_increment(&config, false);
    channel_config_set_dreq(&config, DREQ_PIO0_TX2);
    channel_config_set_chain_to(&config, TRIGGERGEN_BUF1_DMA);
    dma_channel_configure(TRIGGERGEN_BUF2_DMA, &config, &pio0_hw->txf[TRIGGERGEN_SM], trigger_dma_block_2, TRIGGER_DMA_SIZE / 4, false);
    dma_channel_set_irq0_enabled(TRIGGERGEN_BUF2_DMA, true);
  }

  irq_set_exclusive_handler(DMA_IRQ_0, dma_handler);
  irq_set_enabled(DMA_IRQ_0, true);

}


static void populate_dma_block(uint8_t dest[TRIGGER_DMA_SIZE], struct trigger_block *src) {
  for (int i = 0; i < src->count; i++) {
    uint16_t time_offset = trigger_block_get_time_offset(src->triggers[i]);
    uint8_t trigger = trigger_block_get_triggers(src->triggers[i]);
    uint8_t shift = (time_offset & 0x3) * 2;
    dest[time_offset / 4] |= (trigger << shift);
  }
}

static void depopulate_dma_block(uint8_t dest[TRIGGER_DMA_SIZE], struct trigger_block *src) {
  for (int i = 0; i < src->count; i++) {
    uint16_t time_offset = trigger_block_get_time_offset(src->triggers[i]);
    dest[time_offset / 4] = 0;
  }
}

void setup_input_output_pio(void) {

    /* initialize PIO0 with edgedetect */
    PIO pio = pio0;
    {
      uint offset = pio_add_program(pio, &capture_program);
      uint sm = CAPTURE_SM;
      pio_sm_claim(pio, sm);
      capture_program_init(pio, sm, offset, 0, 24);
    }
    {
      uint offset = pio_add_program(pio, &edgedetect_program);
      uint sm = EDGEDETECT_SM;
      pio_sm_claim(pio, sm);
      edgedetect_program_init(pio, sm, offset, 0, 24);
    }
    {
      uint offset = pio_add_program(pio, &triggergen_program);
      uint sm = TRIGGERGEN_SM;
      pio_sm_claim(pio, sm);
      triggergen_program_init(pio, sm, offset, 27, 2);
    }

    /*XXX*/
    trigger_dma_block_1[0] = 0x3;
    trigger_dma_block_1[10] = 0x1;
    trigger_dma_block_1[11] = 0x2;

    trigger_dma_block_2[0] = 0x3;
    trigger_dma_block_2[20] = 0x1;
    trigger_dma_block_2[21] = 0x2;

    configure_dma();

    /* Enable all SMs similtaneously */
    pio_enable_sm_mask_in_sync(pio, 
        (1 << CAPTURE_SM) | 
        (1 << EDGEDETECT_SM) |
        (1 << TRIGGERGEN_SM));


}
