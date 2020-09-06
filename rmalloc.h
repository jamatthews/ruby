#ifndef RUBY_RMALLOC_H
#define RUBY_RMALLOC_H
#endif
/**********************************************************************

  transient_heap.h - declarations of transient_heap related APIs.

  Copyright (C) 2018 Koichi Sasada

**********************************************************************/

#include "internal.h"

#if USE_RMALLOC

/* public API */

/* Allocate req_size bytes */
void *rmalloc(size_t req_size);

/* Deallocate the allocation starting at ptr  */
void rfree(void *ptr);

/* Return empty blocks for fresh allocations  */
void rmalloc_flush();

#endif
