#include "ring_buffer.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>

/*
 * Initialize the ring
 * @param r A pointer to the ring
 * @return 0 on success, negative otherwise - this negative value will be
 * printed to output by the client program
 */
int init_ring(struct ring *r) {
    memset(r,0,sizeof(struct ring));
    return 0;
}

/*
 * Submit a new item - should be thread-safe
 * This call will block the calling thread if there's not enough space
 * @param r The shared ring
 * @param bd A pointer to a valid buffer_descriptor - This pointer is only
 * guaranteed to be valid during the invocation of the function
 */
void ring_submit(struct ring *r, struct buffer_descriptor *bd) {

    // Pointers to first find the spot in the ring
    // Attomically
    uint32_t p_head;
    uint32_t p_next;
    do {
        // Keeps repeating this until the current thread is able to 
        // atomically update the shared head
        p_head = r->p_head;
        p_next = (p_head + 1) % RING_SIZE;

        // If the ring is full, the next ptr
        // and the rings tail are the same, just yield
        while (p_next == r->c_tail) {
            sched_yield();
        }

    } while (!__atomic_compare_exchange(&r->p_head, &p_head, &p_next,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST));

    // An attomic update happened, which means that the stored
    // old p_head can be used to place values into
    // So copy the argument to this location
    memcpy(&r->buffer[p_head], bd, sizeof(struct buffer_descriptor));

    // Update the p_tail as well
    // I don't know why but need to use another variable
    // If you know plz tell me as well (;_;) -- been doin this for too long now
    uint32_t ph_c;
    do {
        ph_c = p_head;
        sched_yield();
    } while (!__atomic_compare_exchange(&r->p_tail, &ph_c, &p_next,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST));
}

/*
 * Get an item from the ring - should be thread-safe
 * This call will block the calling thread if the ring is empty
 * @param r A pointer to the shared ring
 * @param bd pointer to a valid buffer_descriptor to copy the data to
 * Note: This function is not used in the clinet program, so you can change
 * the signature.
 */
void ring_get(struct ring *r, struct buffer_descriptor *bd) {

    // Same thing as the producer,
    // c_head and next to keep track of the pointers
    uint32_t c_head;
    uint32_t c_next;
    do {
        // Assign the relevant values
        c_head = r->c_head;
        c_next = (c_head + 1) % RING_SIZE;

        // If the c_head is the same as producer tail, 
        // you have nothing to consume, so just yield() the cpu
        while (c_head == r->p_tail) {
            sched_yield();
        }

    } while (!__atomic_compare_exchange(&r->c_head, &c_head, &c_next,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST));

    // Serve the request by copying the buffer
    memcpy(bd, &r->buffer[c_head], sizeof(struct buffer_descriptor));

    // Update the consumer tail
    uint32_t ch_c;
    do {
        ch_c = c_head;
    } while (!__atomic_compare_exchange(&r->c_tail, &ch_c, &c_next,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST));
}

