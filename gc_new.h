#ifndef RUBY_NEW_HEAP_H
#define RUBY_NEW_HEAP_H

/**********************************************************************

  new_gc.h

  Copyright (C) 2020 Jacob Matthews

**********************************************************************/

#include "internal.h"

#ifndef USE_NEW_HEAP
# define USE_NEW_HEAP 1
#endif

/* public API */
void *rb_new_heap_alloc(VALUE obj, size_t req_size);
void  rb_new_heap_mark(const void *ptr);
void rb_new_heap_start_marking(int full_marking);
void rb_new_heap_finish_marking(int full_marking);
#endif
