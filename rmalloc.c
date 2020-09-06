#include "internal/gc.h"
#include "rmalloc.h"

#if USE_RMALLOC /* USE_RMALLOC */

#define RMALLOC_DEBUG 0
#define RMALLOC_DEBUG_INFINITE_BLOCK 0
#define RMALLOC_CHECK_MODE 0

// Note: using RUBY_ASSERT_WHEN() extend a macro in expr (info by nobu).
#define RMALLOC_ASSERT(expr) RUBY_ASSERT_MESG_WHEN(RMALLOC_CHECK_MODE > 0, expr, #expr)

#define RMALLOC_BLOCK_SIZE  (1024 * 16) /* 16KB */
#define RMALLOC_ARENA_SIZE  (1024 * 1024 * 64) /* 64MB */
#define RMALLOC_ARENA_BLOCK_COUNT RMALLOC_ARENA_SIZE/RMALLOC_BLOCK_SIZE
#define RMALLOC_SIZE_CLASSES 64


#if SIZEOF_VOID_P > 4
#define RMALLOC_ALIGNMENT              16               /* must be 2^N */
#define RMALLOC_ALIGNMENT_SHIFT         4
#else
#define RMALLOC_ALIGNMENT               8               /* must be 2^N */
#define RMALLOC_ALIGNMENT_SHIFT         3
#endif

#define CLASS_TO_SIZE(i) (((int32_t)(i) + 1) << RMALLOC_ALIGNMENT_SHIFT)
#define PTR_2_BLOCK(p) (void *)((intptr_t)p & ~(RMALLOC_BLOCK_SIZE-1))

struct rmalloc_block {
    struct rmalloc_block_header {
        int16_t size; /* sizeof(block) = TRANSIENT_HEAP_BLOCK_SIZE - sizeof(struct transient_heap_block_header) */
        int16_t index;
        int16_t live;
        uint64_t class;
        void **free;
        bool in_free;
        struct rmalloc_block *next_block;
    } info;
    char buff[RMALLOC_BLOCK_SIZE - sizeof(struct rmalloc_block_header)];
};

struct rmalloc_heap {
  struct rmalloc_block *free_blocks;
  struct rmalloc_block* allocating_blocks[RMALLOC_SIZE_CLASSES];

  struct rmalloc_block *arena;
  int arena_index; /* increment only */
};

static struct rmalloc_heap global_rmalloc_heap;

#include <sys/mman.h>
#include <errno.h>

static void
connect_to_free_blocks(struct rmalloc_block *block)
{
    block->info.next_block = global_rmalloc_heap.free_blocks;
    global_rmalloc_heap.free_blocks = block;
    block->info.in_free = TRUE;
}


static struct rmalloc_block *
heap_block_alloc()
{
#if RMALLOC_DEBUG_INFINITE_BLOCK
    struct rmalloc_block *block =  mmap(NULL, RMALLOC_BLOCK_SIZE, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS,
                 -1, 0);
    if (RMALLOC_DEBUG >= 2) fprintf(stderr, "heap_block_alloc: block allocated using mmap\n");
#else
    struct rmalloc_block *block = &global_rmalloc_heap.arena[global_rmalloc_heap.arena_index++];
    if (RMALLOC_DEBUG >= 2) fprintf(stderr, "heap_block_alloc: block allocated from arena\n");
#endif

    block->info.size = RMALLOC_BLOCK_SIZE - sizeof(struct rmalloc_block_header);
    block->info.index = 0;
    block->info.live = 0;
    return block;
}

static struct rmalloc_block *
heap_allocatable_block(uint class)
{
    struct rmalloc_block *block;
#if RMALLOC_DEBUG_INFINITE_BLOCK
    block = heap_block_alloc();
#else
    block = global_rmalloc_heap.free_blocks;
    if (block) {
        global_rmalloc_heap.free_blocks = block->info.next_block;
        block->info.next_block = NULL;
        block->info.free = NULL;
        block->info.class = class;
        block->info.in_free = FALSE;
        if (RMALLOC_DEBUG >= 2) fprintf(stderr, "heap_allocatable_block: free block grabbed\n");
    }
#endif
    return block;
}

void
Init_Rmalloc(void)
{
#if RMALLOC_DEBUG_INFINITE_BLOCK

#else
  global_rmalloc_heap.arena_index = 0;
  global_rmalloc_heap.arena = (void*) rb_aligned_malloc(RMALLOC_BLOCK_SIZE, RMALLOC_ARENA_SIZE);
  if (global_rmalloc_heap.arena == NULL) {
       rb_bug("rmalloc: arena allocation failed\n");
  }
  for (int i=0; i<RMALLOC_ARENA_BLOCK_COUNT; i++) {
      connect_to_free_blocks(heap_block_alloc());
  }
#endif
  if (RMALLOC_DEBUG >= 1) fprintf(stderr, "rmalloc initialized\n");
}

void* rmalloc(size_t size){
  if (size >= 512) return NULL;

  uint class = (uint)(size - 1) >> RMALLOC_ALIGNMENT_SHIFT;
  struct rmalloc_block *block = global_rmalloc_heap.allocating_blocks[class];

  if(block==NULL) {
    block = (global_rmalloc_heap.allocating_blocks[class] = heap_allocatable_block(class));
    if(block==NULL) return NULL;
  }

  while(block) {
    if (block->info.size - block->info.index >= CLASS_TO_SIZE(class)) {
        //bump
        void *alloc = (void *)&block->buff[block->info.index];
        block->info.index += CLASS_TO_SIZE(class);
        block->info.live++;
        if (RMALLOC_DEBUG >= 3) fprintf(stderr, "rmalloc bump %p size %zu\n", alloc, size);
        return alloc;
    } else if(block->info.free) {
      //pop
      void **alloc = block->info.free;
      block->info.free = *alloc;
      block->info.live++;
      if (RMALLOC_DEBUG >= 3) fprintf(stderr, "rmalloc pop %p size %zu\n", alloc, size);
      return (void*)alloc;
    }
    else {
      block = (global_rmalloc_heap.allocating_blocks[class] = heap_allocatable_block(class));
    }
  }

  return NULL;
}

void rfree(void* pointer) {
  struct rmalloc_block *block = PTR_2_BLOCK(pointer);
  *(void**)pointer = block->info.free;
  block->info.free = pointer;

  RMALLOC_ASSERT(block->info.live > 0);
  block->info.live--;
  if (block->info.live == 0) {
    block->info.free = NULL;
    block->info.index = 0;
  }
  if (block == global_rmalloc_heap.allocating_blocks[block->info.class]) return;
  if (!block->info.in_free) connect_to_free_blocks(block);
}

#endif
