#include "hardware/pio.h"
#include "triggergen.pio.h"

void setup_trigger_output(void) {
    /* initialize PIO0 with edgedetect */
    PIO pio = pio0;
    uint offset = pio_add_program(pio, &triggergen_program);
    uint sm = 2;
    pio_sm_claim(pio, sm);
    triggergen_program_init(pio, sm, offset, 27, 2);
}
