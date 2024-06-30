#include "spsc.h"

static void spsc_overflow(struct spsc_queue *q) {
  q->overflows = q->overflows + 1;
}

static uint32_t spsc_advance(uint32_t idx, uint32_t size) {
  return (idx == size - 1) ? 0 : idx + 1;
}

bool spsc_is_empty(const struct spsc_queue *q) {
  return (q->read == q->write);
}

bool spsc_is_full(const struct spsc_queue *q) {
  return (q->read == spsc_advance(q->write, q->size));
}


/* Provide index of entry that can be populated and pushed next.  Returns -1 if
 * there is no space, and increments the overrun count */
int spsc_allocate(struct spsc_queue *q) {
  if (spsc_is_full(q)) {
    spsc_overflow(q);
    return -1;
  }
  return q->write;
}

/* Increment queue write pointer.  
 * This assumes a queue index has been allocated and then written */
void spsc_push(struct spsc_queue *q) {
  q->write = spsc_advance(q->write, q->size);
}

/* Return index of next queue entry that is ready to be read.  Returns -1 if
 * queue is empty */
int spsc_next(struct spsc_queue *q) {
  if (spsc_is_empty(q)) {
    return -1;
  }
  return q->read;
}

/* Increment read pointer. This assumes the queue is not empty, and any pointer 
 * obtained from a previous spsc_next call is invalidated */
void spsc_release(struct spsc_queue *q) {
  q->read = spsc_advance(q->read, q->size);
}

