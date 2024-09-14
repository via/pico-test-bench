#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"

#include "capture.pio.h"
#include "triggergen.pio.h"
#include "spi_slave.pio.h"

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

#define SAMPLES_PER_SEC 4000000

#define CAPTURE_COUNT (SAMPLES_PER_SEC / 1000)
static uint32_t captures_buf1[CAPTURE_COUNT] = {0};
static uint32_t captures_buf2[CAPTURE_COUNT] = {0};

#define TRIGGERS_PER_MS 4000
#define TRIGGER_DMA_SIZE (TRIGGERS_PER_MS / 4)

uint8_t trigger_dma_block_1[TRIGGER_DMA_SIZE] = {0};
uint8_t trigger_dma_block_2[TRIGGER_DMA_SIZE] = {0};

#define SPI_SAMPLES 30
uint32_t spi_tx_dma_1[SPI_SAMPLES] = { 0 };
uint32_t spi_tx_dma_2[SPI_SAMPLES] = { 0 };
uint32_t spi_rx_dma_1[SPI_SAMPLES] = { 0 };
uint32_t spi_rx_dma_2[SPI_SAMPLES] = { 0 };

#define CAPTURE_BUF1_DMA 0
#define CAPTURE_BUF2_DMA 1
#define TRIGGERGEN_BUF1_DMA 4
#define TRIGGERGEN_BUF2_DMA 5
#define SPI_RX_BUF1_DMA 6
#define SPI_RX_BUF2_DMA 7
#define SPI_TX_BUF1_DMA 8
#define SPI_TX_BUF2_DMA 9

#define CAPTURE_SM 0
#define TRIGGERGEN_SM 2
#define SPI_SM 3

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

static uint32_t collapse_buffer(uint32_t previous, struct changebuf *dst, uint32_t captures[CAPTURE_COUNT]) {
  for (const uint32_t *sample = captures; sample - captures < CAPTURE_COUNT; sample++) {
    if (__builtin_expect((previous != *sample), 0)) {
      write_change(dst, sample - captures, *sample);
      previous = *sample;
    }
  }
  return previous;
}

bool next_trigger_before(uint32_t before, uint32_t *dest, uint8_t *trg);

static void populate_trigger_block(uint8_t dest[TRIGGER_DMA_SIZE], uint32_t start_time) {

  memset(dest, 0, TRIGGER_DMA_SIZE);
  uint32_t end_time = start_time + 4000;
  uint32_t time;
  uint8_t value;
  while (next_trigger_before(end_time, &time, &value)) {
    uint32_t time_offset = time - start_time;
    uint8_t shift = (time_offset & 0x3) * 2;
    dest[time_offset / 4] |= (value << shift);
  }
}


static uint32_t spi_index_plus_one(uint32_t val) {
  val += 1;
  if (val == SPI_SAMPLES) {
    return 0;
  } else {
    return val;
  }
}

#define TLV2553_SPI_REQ(pin) ((uint32_t)((pin << 12) | 0x0c00))
static uint32_t find_first_spi_index(uint32_t samples[SPI_SAMPLES]) {
  for (int i = 0; i < SPI_SAMPLES; i++) {
    if (samples[i] == TLV2553_SPI_REQ(11)) {
      return spi_index_plus_one(i);
    }
  }
  return 0;
}

uint32_t vals[SPI_SAMPLES] = {
  0, 0, 2063,
  0, 0, 0,
  0, 0, 0,
  0, 0, 0,
  0, 0, 0,
  0, 0, 0,
  0, 0, 0,
  0, 0, 0,
  0, 0, 0,
  0, 0, 2048,
};


static void populate_spi_tx(uint32_t tx[SPI_SAMPLES], uint32_t rx[SPI_SAMPLES]) {
  uint32_t idx = find_first_spi_index(rx);
  for (uint32_t src_idx = 0, dst_idx = idx; src_idx < SPI_SAMPLES;
       src_idx++,
       dst_idx = spi_index_plus_one(dst_idx)) {
    tx[spi_index_plus_one(dst_idx)] = vals[src_idx] << 20;
  }
}

static uint32_t capture_time = 0;
static uint32_t trigger_time = 0;
static uint32_t last_capture = 0;

void dma0_handler(void) {
  if (dma_channel_get_irq0_status(CAPTURE_BUF1_DMA)) {

    int index = spsc_allocate(&change_buffer_queue);
    if (index >= 0) {
      struct changebuf *buf = &change_buffers[index];
      buf->count = 0;
      buf->overflowed = false;
      buf->start_time = capture_time;
      last_capture = collapse_buffer(last_capture, buf, captures_buf1);
      spsc_push(&change_buffer_queue);
    }

    dma_channel_set_write_addr(CAPTURE_BUF1_DMA, captures_buf1, false); 
    dma_channel_acknowledge_irq0(CAPTURE_BUF1_DMA);

    capture_time += 4000;

  } else if (dma_channel_get_irq0_status(CAPTURE_BUF2_DMA)) {
    int index = spsc_allocate(&change_buffer_queue);
    if (index >= 0) {
      struct changebuf *buf = &change_buffers[index];
      buf->count = 0;
      buf->overflowed = false;
      buf->start_time = capture_time;
      last_capture = collapse_buffer(last_capture, buf, captures_buf2);
      spsc_push(&change_buffer_queue);
    }

    dma_channel_set_write_addr(CAPTURE_BUF2_DMA, captures_buf2, false); 
    dma_channel_acknowledge_irq0(CAPTURE_BUF2_DMA);

    capture_time += 4000;
  }

  if (dma_channel_get_irq0_status(TRIGGERGEN_BUF1_DMA)) {
    populate_trigger_block(trigger_dma_block_1, 8000 + trigger_time);
    dma_channel_set_read_addr(TRIGGERGEN_BUF1_DMA, trigger_dma_block_1, false); 
    dma_channel_acknowledge_irq0(TRIGGERGEN_BUF1_DMA);
    trigger_time += 4000;
  } else if (dma_channel_get_irq0_status(TRIGGERGEN_BUF2_DMA)) {
    populate_trigger_block(trigger_dma_block_2, 8000 + trigger_time);
    dma_channel_set_read_addr(TRIGGERGEN_BUF2_DMA, trigger_dma_block_2, false); 
    dma_channel_acknowledge_irq0(TRIGGERGEN_BUF2_DMA);
    trigger_time += 4000;
  }

  if (dma_channel_get_irq0_status(SPI_RX_BUF1_DMA)) {
    populate_spi_tx(spi_tx_dma_1, spi_rx_dma_1);
    dma_channel_set_write_addr(SPI_RX_BUF1_DMA, spi_rx_dma_1, false); 
    dma_channel_acknowledge_irq0(SPI_RX_BUF1_DMA);
  }
  if (dma_channel_get_irq0_status(SPI_RX_BUF2_DMA)) {
    populate_spi_tx(spi_tx_dma_2, spi_rx_dma_2);
    dma_channel_set_write_addr(SPI_RX_BUF2_DMA, spi_rx_dma_2, false); 
    dma_channel_acknowledge_irq0(SPI_RX_BUF2_DMA);
  }
  if (dma_channel_get_irq0_status(SPI_TX_BUF1_DMA)) {
    dma_channel_set_read_addr(SPI_TX_BUF1_DMA, spi_tx_dma_1, false); 
    dma_channel_acknowledge_irq0(SPI_TX_BUF1_DMA);
  }
  if (dma_channel_get_irq0_status(SPI_TX_BUF2_DMA)) {
    dma_channel_set_read_addr(SPI_TX_BUF2_DMA, spi_tx_dma_2, false); 
    dma_channel_acknowledge_irq0(SPI_TX_BUF2_DMA);
  }

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

  /* Channel 6 and 7 are for SPI RX */
  {
    dma_channel_claim(SPI_RX_BUF1_DMA);
    dma_channel_config config = dma_channel_get_default_config(SPI_RX_BUF1_DMA);
    channel_config_set_read_increment(&config, false);
    channel_config_set_write_increment(&config, true);
    channel_config_set_dreq(&config, DREQ_PIO0_RX3);
    channel_config_set_chain_to(&config, SPI_RX_BUF2_DMA);
    dma_channel_configure(SPI_RX_BUF1_DMA, &config, spi_rx_dma_1, &pio0_hw->rxf[SPI_SM], SPI_SAMPLES, true);
    dma_channel_set_irq0_enabled(SPI_RX_BUF1_DMA, true);
  }
  {
    dma_channel_claim(SPI_RX_BUF2_DMA);
    dma_channel_config config = dma_channel_get_default_config(SPI_RX_BUF2_DMA);
    channel_config_set_read_increment(&config, false);
    channel_config_set_write_increment(&config, true);
    channel_config_set_dreq(&config, DREQ_PIO0_RX3);
    channel_config_set_chain_to(&config, SPI_RX_BUF1_DMA);
    dma_channel_configure(SPI_RX_BUF2_DMA, &config, spi_rx_dma_2, &pio0_hw->rxf[SPI_SM], SPI_SAMPLES, false);
    dma_channel_set_irq0_enabled(SPI_RX_BUF2_DMA, true);
  }

  /* Channel 8 and 9 are for SPI TX */
  {
    dma_channel_claim(SPI_TX_BUF1_DMA);
    dma_channel_config config = dma_channel_get_default_config(SPI_TX_BUF1_DMA);
    channel_config_set_read_increment(&config, true);
    channel_config_set_write_increment(&config, false);
    channel_config_set_dreq(&config, DREQ_PIO0_TX3);
    channel_config_set_chain_to(&config, SPI_TX_BUF2_DMA);
    dma_channel_configure(SPI_TX_BUF1_DMA, &config, &pio0_hw->txf[SPI_SM], spi_tx_dma_1, SPI_SAMPLES, true);
    dma_channel_set_irq0_enabled(SPI_TX_BUF1_DMA, true);
  }
  {
    dma_channel_claim(SPI_TX_BUF2_DMA);
    dma_channel_config config = dma_channel_get_default_config(SPI_TX_BUF2_DMA);
    channel_config_set_read_increment(&config, true);
    channel_config_set_write_increment(&config, false);
    channel_config_set_dreq(&config, DREQ_PIO0_TX3);
    channel_config_set_chain_to(&config, SPI_TX_BUF1_DMA);
    dma_channel_configure(SPI_TX_BUF2_DMA, &config, &pio0_hw->txf[SPI_SM], spi_tx_dma_2, SPI_SAMPLES, false);
    dma_channel_set_irq0_enabled(SPI_TX_BUF2_DMA, true);
  }

  irq_set_exclusive_handler(DMA_IRQ_0, dma0_handler);
  irq_set_enabled(DMA_IRQ_0, true);

}

void setup_input_output_pio(void) {

    /* initialize PIO0 with edgedetect */
    PIO pio = pio0;
    {
      uint offset = pio_add_program(pio, &capture_program);
      uint sm = CAPTURE_SM;
      pio_sm_claim(pio, sm);
      capture_program_init(pio, sm, offset, 4, 18);
    }
    {
      uint offset = pio_add_program(pio, &triggergen_program);
      uint sm = TRIGGERGEN_SM;
      pio_sm_claim(pio, sm);
      triggergen_program_init(pio, sm, offset, 4, 2);
    }
    {
      uint offset = pio_add_program(pio, &spi_slave_program);
      uint sm = SPI_SM;
      pio_sm_claim(pio, sm);
      spi_slave_program_init(pio, sm, offset, 0);
    }

    populate_trigger_block(trigger_dma_block_1, 0);
    populate_trigger_block(trigger_dma_block_2, 4000);

    configure_dma();

    /* Enable all SMs similtaneously */
    pio_enable_sm_mask_in_sync(pio, 
//        (1 << CAPTURE_SM) | 
        (1 << TRIGGERGEN_SM) |
        (1 << SPI_SM));


}
