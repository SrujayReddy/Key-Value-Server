#pragma once
#include <stdint.h> 

typedef uint32_t key_type;
typedef uint32_t value_type;
typedef uint32_t index_t;

static index_t hash_function(key_type k, int table_size) {
	return k % table_size;
}
