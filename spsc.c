#include "spsc.h"

bool spsc_is_empty(const struct spsc_queue *q) {
  const int read = atomic_load_explicit(&q->read, memory_order_relaxed);
  const int write = atomic_load_explicit(&q->write, memory_order_relaxed);
  return (read == write);
}

bool spsc_is_full(const struct spsc_queue *q) {
  const int read = atomic_load_explicit(&q->read, memory_order_relaxed);
  const int write = atomic_load_explicit(&q->write, memory_order_relaxed);
  return (read == ((write + 1) & q->mask));
}

static void spsc_overflow(struct spsc_queue *q) {
  q->overflows = q->overflows + 1;
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
  int write = q->write;
  write = (write + 1) % q->size;
  q->write = write;
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
  int read = q->read;
  read = (read + 1) % q->size;
  q->read = read;
}
