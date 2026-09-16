#include "motion_segment_queue.h"

#include <string.h>

/* On Cortex-M4 a compiler barrier plus volatile indices is sufficient
 * between an interrupt and the thread it preempts. A DMB is used so the
 * code stays correct if the producer ever moves to a context that is not
 * strictly ordered against the consumer. */
#if defined(__GNUC__) && (defined(__ARM_ARCH_7EM__) || defined(__ARM_ARCH_7M__))
#  define MSQ_BARRIER() __asm volatile ("dmb 0xF" ::: "memory")
#else
#  define MSQ_BARRIER() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#endif

#define MSQ_MASK (MOTION_SEGMENT_QUEUE_DEPTH - 1u)

void msq_init(motion_segment_queue_t *q)
{
    memset(q, 0, sizeof(*q));
}

bool msq_push(motion_segment_queue_t *q, const motion_segment_t *seg)
{
    const uint32_t head = q->head;
    const uint32_t tail = q->tail;

    if ((uint32_t)(head - tail) >= MOTION_SEGMENT_QUEUE_DEPTH) {
        return false;                       /* full - ADR-008 backpressure */
    }
    q->buf[head & MSQ_MASK] = *seg;
    MSQ_BARRIER();                          /* publish payload before index */
    q->head = head + 1u;
    return true;
}

bool msq_pop(motion_segment_queue_t *q, motion_segment_t *out)
{
    const uint32_t tail = q->tail;
    const uint32_t head = q->head;

    if (head == tail) {
        return false;                       /* empty */
    }
    MSQ_BARRIER();                          /* read index before payload */
    *out = q->buf[tail & MSQ_MASK];
    MSQ_BARRIER();
    q->tail = tail + 1u;
    return true;
}

uint32_t msq_count(const motion_segment_queue_t *q)
{
    return (uint32_t)(q->head - q->tail);
}

uint32_t msq_free(const motion_segment_queue_t *q)
{
    return MOTION_SEGMENT_QUEUE_DEPTH - msq_count(q);
}

void msq_flush(motion_segment_queue_t *q)
{
    q->tail = q->head;
}

_Static_assert((MOTION_SEGMENT_QUEUE_DEPTH & (MOTION_SEGMENT_QUEUE_DEPTH - 1u)) == 0u,
               "queue depth must be a power of two");
