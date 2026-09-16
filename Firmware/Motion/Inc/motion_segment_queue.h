/**
 * @file    motion_segment_queue.h
 * @brief   Single-producer / single-consumer lock-free motion segment queue.
 *
 * Producer: the Phase 3 protocol layer, at Ethernet priority.
 * Consumer: the STEP ring-boundary handler, at motion priority.
 *
 * The only coupling point between the network side and the hard real-time
 * side (Docs/FIRMWARE-ARCHITECTURE.md §11, §18). Wait-free in both
 * directions: neither side can block the other.
 */
#ifndef MOTION_SEGMENT_QUEUE_H
#define MOTION_SEGMENT_QUEUE_H

#include "motion_types.h"

typedef struct {
    motion_segment_t  buf[MOTION_SEGMENT_QUEUE_DEPTH];
    volatile uint32_t head;   /**< producer index, free-running */
    volatile uint32_t tail;   /**< consumer index, free-running */
} motion_segment_queue_t;

void     msq_init(motion_segment_queue_t *q);
bool     msq_push(motion_segment_queue_t *q, const motion_segment_t *seg);
bool     msq_pop (motion_segment_queue_t *q, motion_segment_t *out);
uint32_t msq_count(const motion_segment_queue_t *q);
uint32_t msq_free (const motion_segment_queue_t *q);
void     msq_flush(motion_segment_queue_t *q);   /**< consumer side only */

#endif /* MOTION_SEGMENT_QUEUE_H */
