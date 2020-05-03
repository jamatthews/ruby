/**********************************************************************

  new_gc.c - Implement New GC

  Copyright (C) 2020 Jacob Matthews

**********************************************************************/

#include "debug_counter.h"
#include "gc.h"
#include "internal.h"
#include "internal/gc.h"
#include "internal/sanitizers.h"
#include "internal/static_assert.h"
#include "internal/variable.h"
#include "ruby/debug.h"
#include "ruby/ruby.h"
#include "ruby_assert.h"
#include "vm_debug.h"

#include <sys/mman.h>
#include <errno.h>

#define USE_NEW_HEAP 1

#define NEW_HEAP_ALLOC_MAX 512 /* 512 B */
#define NEW_HEAP_ALLOC_ALIGN RUBY_ALIGNOF(void *)
#define NEW_HEAP_BLOCK_SIZE (1024 * 16) /* 16KB int16_t */
#define NEW_HEAP_SIZE_CLASSES 64

#define NEW_HEAP_DEBUG 0
#define NEW_HEAP_DEBUG_INFINITE_BLOCK 1

#define ROUND_UP(v, a)  (((size_t)(v) + (a) - 1) & ~((a) - 1))
#define NEW_GC_ASSERT(expr) RUBY_ASSERT_MESG_WHEN(NEW_HEAP_DEBUG > 0, expr, #expr)

typedef int64_t size_class_t;

struct block {
  struct header {
    size_class_t size_class;
    int16_t size;
    struct block* next_block;
    int16_t index;
  } header;

  char buff[NEW_HEAP_BLOCK_SIZE - sizeof(struct header)];
};


struct new_heap {
  int total_blocks;
  struct block* used_blocks[NEW_HEAP_SIZE_CLASSES];
  struct block* empty_blocks;
};

static struct new_heap global_new_heap;

static struct new_heap*
new_heap_get(void)
{
    struct new_heap* heap = &global_new_heap;
    //new_heap_verify(heap);
    return heap;
}

static void
reset_block(struct block *block)
{
    __msan_allocated_memory(block, sizeof block);
    block->header.size = NEW_HEAP_BLOCK_SIZE - sizeof(struct header);
    block->header.index = 0;
    __asan_poison_memory_region(&block->buff, sizeof block->buff);
}

static struct block *
new_heap_block_alloc(struct new_heap* heap)
{
    struct block *block;
//#if NEW_HEAP_DEBUG_INFINITE_BLOCK
    block = mmap(NULL, NEW_HEAP_BLOCK_SIZE, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS,
                 -1, 0);
    if (block == MAP_FAILED) rb_bug("new_heap_block_alloc: err:%d\n", errno);
//#endif
    reset_block(block);
    return block;
}

static struct block *
new_heap_allocatable_block(struct new_heap* heap, size_class_t size_class)
{
    struct block *block;

//#if NEW_HEAP_DEBUG_INFINITE_BLOCK
    block = new_heap_block_alloc(heap);
    block->header.size_class = size_class;
    heap->total_blocks++;
//#endif


    return block;
}

static void
connect_to_using_blocks(struct new_heap *heap, struct block *block)
{
    size_class_t size_class = block->header.size_class;
    block->header.next_block = heap->used_blocks[size_class];
    heap->used_blocks[size_class] = block;
}

void
Init_NewHeap(void)
{
    struct new_heap* heap = new_heap_get();
    for(size_class_t size_class = 0; size_class < NEW_HEAP_SIZE_CLASSES; size_class++) {
      struct block* block = new_heap_allocatable_block(heap, size_class);
      connect_to_using_blocks(heap, block);
    }
}

void *
rb_new_heap_alloc(VALUE obj, size_t req_size)
{
    struct new_heap* heap = new_heap_get();
    size_t size = ROUND_UP(req_size, NEW_HEAP_ALLOC_ALIGN);
    void* ptr;

    NEW_GC_ASSERT(RB_TYPE_P(obj, T_ARRAY)); /* supported types */

    if (size > NEW_HEAP_ALLOC_MAX) {
        if (NEW_HEAP_DEBUG >= 3) fprintf(stderr, "rb_new_heap_alloc: [too big: %ld] %s\n", (long)size, rb_obj_info(obj));
        return NULL;
    }

    size_class_t size_class = ROUND_UP(req_size, 8) / 8;
    struct block *block = heap->used_blocks[size_class];

    while (block) {
        if (block->header.size - block->header.index >= (int32_t)size) {
            ptr = (void *)&block->buff[block->header.index];
            block->header.index += size;
            return ptr;
        }
        else {
            block = new_heap_allocatable_block(heap, size_class);
            if (block) connect_to_using_blocks(heap, block);
        }
    }

    return NULL;
}

void
rb_new_heap_mark(const void *ptr)
{
}
