#pragma once
#include <stdbool.h>
#include "common.h"

#define RING_SIZE 1024

enum REQUEST_TYPE {
  PUT = 0,
  GET
};

/* Client sends requests using this format - Each element of the ring is 
 * a buffer_descriptor */
struct buffer_descriptor {
  	enum REQUEST_TYPE req_type;
  	key_type k;
	value_type v;
	/* Result offset (in bytes) - this is where the client program expects to see the 
	 * result of its query - The kv_store program should write the result 
	 * at this address (assuming shared_mem_start is a char * and points
	 * to the beginning of the shared memory region):
	 * struct buffer_descriptor *result = shared_mem_start + res_off;
	 * memcpy(result, ..., sizeof(struct buffer_descriptor); */
  	int res_off;
	/* The client program polls predefined locations for request completions -
	 * It considers a request as completed when this flag is set to 1 - So, after
	 * doing memcpy above, the kv_store should set the ready flag:
	 * result->ready = 1;
	 * The client program will reset the flag to 0 before using the same 
	 * location for completion */
  	int ready;
};

/* This structure is laid out at the beginning of the shared memory region
 * You can add new fields to the structure (It's very unlikely that you need to) */
struct __attribute__((packed, aligned(64))) ring {
	/* Producer tail - where the last valid item is */
	uint32_t p_tail; 
	char pad1[60];
	/* Producer head - where producers are putting new elements
	 * It should be always ahead of p_tail - elements between p_tail and
	 * p_head may not be valid yet (in process of copying data?) */
	uint32_t p_head; 
	char pad2[60];
	/* Consumer tail - first item to be consumed - producers can't write
	 * any data here - producers can only write before c_tail */
	uint32_t c_tail;
	char pad3[60];
	/* Consumer head - next consumer will consume the data pointed by c_head */
	uint32_t c_head;
	char pad4[60];
	/* An array of structs - This is the actual ring */
	struct buffer_descriptor buffer[RING_SIZE];
};

/*
 * Initialize the ring
 * @param r A pointer to the ring
 * @return 0 on success, negative otherwise - this negative value will be
 * printed to output by the client program
*/
int init_ring(struct ring *r);

/*
 * Submit a new item - should be thread-safe
 * This call will block the calling thread if there's not enough space
 * @param r The shared ring
 * @param bd A pointer to a valid buffer_descriptor - This pointer is only
 * guaranteed to be valid during the invocation of the function
*/
void ring_submit(struct ring *r, struct buffer_descriptor *bd); 

/*
 * Get an item from the ring - should be thread-safe
 * This call will block the calling thread if the ring is empty
 * @param r A pointer to the shared ring 
 * @param bd pointer to a valid buffer_descriptor to copy the data to
 * Note: This function is not used in the clinet program, so you can change
 * the signature.
*/
void ring_get(struct ring *r, struct buffer_descriptor *bd); 

