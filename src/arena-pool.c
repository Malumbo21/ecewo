// Copyright 2025-2026 Savas Sahin <savashn@proton.me>

// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:

// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "arena-internal.h"
#include "logger.h"
#include <stdlib.h>
#include <stdint.h>

// Soft cap on the LIFO recycle cache. Arenas beyond this point are freed at
// return rather than retained. Does NOT limit the number of live arenas;
// borrow always tries malloc and only fails on OS allocation failure.
#ifndef ARENA_POOL_CAP
#define ARENA_POOL_CAP 1024
#endif

#ifndef PREALLOCATED_ARENA
#define PREALLOCATED_ARENA 32
#endif

#ifndef ARENA_POOL_LOW_WATERMARK
#define ARENA_POOL_LOW_WATERMARK 8 /* Grow when <= 8 available */
#endif

#ifndef ARENA_POOL_HIGH_WATERMARK
#define ARENA_POOL_HIGH_WATERMARK 64 /* Shrink when >= 64 available */
#endif

#ifndef ARENA_POOL_GROW_BATCH
#define ARENA_POOL_GROW_BATCH 8 /* Allocate 8 at a time */
#endif

typedef struct {
  ecewo_arena_t **arenas; // heap-allocated LIFO of size pool_capacity
  uint32_t pool_capacity; // size of the arenas[] array (recycle cache size)
  uint32_t head;
  uint32_t total_allocated; // live arenas (cached + in flight)

#ifdef ECEWO_DEBUG
  uint32_t peak_usage;
  uint32_t grow_count;
  uint32_t shrink_count;
#endif
  bool initialized;
} arena_pool_t;

static arena_pool_t arena_pool = { 0 };

// Called when acquiring
static void arena_pool_try_grow(void) {
  if (arena_pool.head > ARENA_POOL_LOW_WATERMARK)
    return;

  if (arena_pool.head >= arena_pool.pool_capacity)
    return;

  uint32_t space_available = arena_pool.pool_capacity - arena_pool.head;
  uint32_t to_allocate = ARENA_POOL_GROW_BATCH;
  if (to_allocate > space_available)
    to_allocate = space_available;

#ifdef ECEWO_DEBUG
  uint32_t allocated = 0;
#endif

  for (uint32_t i = 0; i < to_allocate; i++) {
    ecewo_arena_t *arena = malloc(sizeof(ecewo_arena_t));
    if (!arena)
      break;

    if (!new_region_to(&arena->begin, &arena->end, ARENA_REGION_SIZE)) {
      free(arena);
      break;
    }

    arena_pool.arenas[arena_pool.head++] = arena;
    arena_pool.total_allocated++;

#ifdef ECEWO_DEBUG
    allocated++;
  }

  if (allocated > 0) {
    arena_pool.grow_count++;
    LOG_DEBUG("Arena pool grew: +%u arenas (now %u/%u cached)",
              allocated,
              arena_pool.head,
              arena_pool.pool_capacity);
#endif
  }
}

// Called when releasing
static void arena_pool_try_shrink(void) {
  if (arena_pool.head < ARENA_POOL_HIGH_WATERMARK)
    return;

  // Keep some reserve, don't shrink below initial size
  uint32_t target = PREALLOCATED_ARENA + ARENA_POOL_GROW_BATCH;
  if (arena_pool.head <= target)
    return;

  // Shrink by half of excess
  uint32_t excess = arena_pool.head - target;
  uint32_t to_free = excess / 2;
  if (to_free < ARENA_POOL_GROW_BATCH)
    to_free = ARENA_POOL_GROW_BATCH;

#ifdef ECEWO_DEBUG
  uint32_t freed = 0;
#endif

  while (to_free > 0 && arena_pool.head > target) {
    ecewo_arena_t *arena = arena_pool.arenas[--arena_pool.head];
    arena_pool.arenas[arena_pool.head] = NULL;

    if (arena) {
      arena_free(arena);
      free(arena);
      if (arena_pool.total_allocated > 0)
        arena_pool.total_allocated--;

#ifdef ECEWO_DEBUG
      freed++;
#endif
    }

    to_free--;
  }

#ifdef ECEWO_DEBUG
  if (freed > 0) {
    arena_pool.shrink_count++;
    LOG_DEBUG("Arena pool shrunk: -%u arenas (now %u/%u cached)",
              freed, arena_pool.head, arena_pool.pool_capacity);
  }
#endif
}

static inline uint32_t get_arena_preallocation(uint32_t cap) {
  uint32_t preallocate = PREALLOCATED_ARENA;

  if (preallocate > cap) {
    LOG_DEBUG("%u exceeds maximum %u, capping to %u",
              preallocate, cap, cap);
    preallocate = cap;
  }

  const char *env_prealloc = getenv("ECEWO_ARENA_PREALLOC");
  if (!env_prealloc)
    return preallocate;

  char *endptr;
  long val = strtol(env_prealloc, &endptr, 10);

  if (endptr == env_prealloc || *endptr != '\0' || val <= 0 || val > UINT32_MAX) {
    LOG_DEBUG("Invalid ECEWO_ARENA_PREALLOC='%s', using default: %u",
              env_prealloc, preallocate);
    return preallocate;
  }

  uint32_t env_val = (uint32_t)val;

  if (env_val > cap) {
    LOG_DEBUG("ECEWO_ARENA_PREALLOC=%u exceeds maximum %u, capping to %u",
              env_val, cap, cap);
    return cap;
  } else {
    LOG_DEBUG("Using ECEWO_ARENA_PREALLOC=%u from environment", env_val);
    return env_val;
  }
}

void arena_pool_init(void) {
  if (arena_pool.initialized)
    return;

  arena_pool.pool_capacity = ARENA_POOL_CAP;
  arena_pool.arenas = calloc(arena_pool.pool_capacity, sizeof(ecewo_arena_t *));
  if (!arena_pool.arenas) {
    LOG_ERROR("Failed to allocate arena pool LIFO (%u slots)",
              arena_pool.pool_capacity);
    arena_pool.pool_capacity = 0;
    return;
  }

  arena_pool.head = 0;
  arena_pool.total_allocated = 0;

#ifdef ECEWO_DEBUG
  arena_pool.peak_usage = 0;
  arena_pool.grow_count = 0;
  arena_pool.shrink_count = 0;
#endif

  const uint32_t preallocate = get_arena_preallocation(arena_pool.pool_capacity);

// Pre-allocate arenas
#ifdef ECEWO_DEBUG
  uint32_t allocated = 0;
#endif

  for (uint32_t i = 0; i < preallocate; i++) {
    ecewo_arena_t *arena = malloc(sizeof(ecewo_arena_t));
    if (!arena) {
      LOG_DEBUG("Failed to allocate arena %u/%u, stopping pre-allocation",
                i + 1, preallocate);
      break;
    }

    // Pre-allocate first region
    if (!new_region_to(&arena->begin, &arena->end, ARENA_REGION_SIZE)) {
      free(arena);
      LOG_DEBUG("Failed to allocate region for arena %u/%u, stopping",
                i + 1, preallocate);
      break;
    }

    arena_pool.arenas[arena_pool.head++] = arena;
    arena_pool.total_allocated++;

#ifdef ECEWO_DEBUG
    allocated++;
#endif
  }

  arena_pool.initialized = true;

#ifdef ECEWO_DEBUG
  double allocated_mb = (allocated * ARENA_REGION_SIZE) / (1024.0 * 1024.0);
  LOG_DEBUG("Arena pool initialized: %u/%u arenas (%.2f MB)",
            allocated,
            arena_pool.pool_capacity,
            allocated_mb);
#endif
}

void arena_pool_destroy(void) {
  if (!arena_pool.initialized)
    return;

#ifdef ECEWO_DEBUG
  // Statistics before destruction
  if (arena_pool.grow_count > 0 || arena_pool.shrink_count > 0) {
    LOG_DEBUG("Arena pool statistics:");
    LOG_DEBUG("  Total allocated: %u arenas", arena_pool.total_allocated);
    LOG_DEBUG("  Peak usage: %u arenas", arena_pool.peak_usage);
    LOG_DEBUG("  Grow operations: %u", arena_pool.grow_count);
    LOG_DEBUG("  Shrink operations: %u", arena_pool.shrink_count);
  }
#endif

  if (arena_pool.arenas) {
    for (uint32_t i = 0; i < arena_pool.head; i++) {
      if (arena_pool.arenas[i]) {
        arena_free(arena_pool.arenas[i]);
        free(arena_pool.arenas[i]);
        arena_pool.arenas[i] = NULL;
      }
    }
    free(arena_pool.arenas);
    arena_pool.arenas = NULL;
  }

  arena_pool.head = 0;
  arena_pool.pool_capacity = 0;
  arena_pool.initialized = false;

  LOG_DEBUG("Arena pool destroyed");
}

ecewo_arena_t *ecewo_arena_borrow(void) {
  ecewo_arena_t *arena;

  if (arena_pool.head > 0) {
    // Take from pool
    arena = arena_pool.arenas[--arena_pool.head];
    arena_pool.arenas[arena_pool.head] = NULL;

#ifdef ECEWO_DEBUG
    uint32_t in_use = arena_pool.total_allocated - arena_pool.head;
    if (in_use > arena_pool.peak_usage)
      arena_pool.peak_usage = in_use;
#endif

    arena_pool_try_grow();
    arena_reset(arena);
    return arena;
  }

  // Pool's recycle cache is empty. Always try to allocate a fresh arena.
  // Live arena count is bounded only by app->max_connections (and OS memory).
  arena = malloc(sizeof(ecewo_arena_t));
  if (!arena)
    return NULL;

  if (!new_region_to(&arena->begin, &arena->end, ARENA_REGION_SIZE)) {
    free(arena);
    return NULL;
  }

  arena_pool.total_allocated++;

#ifdef ECEWO_DEBUG
  uint32_t in_use = arena_pool.total_allocated - arena_pool.head;
  if (in_use > arena_pool.peak_usage)
    arena_pool.peak_usage = in_use;

  LOG_DEBUG("Arena pool: allocated new arena (live=%u, cached=%u/%u)",
            arena_pool.total_allocated, arena_pool.head, arena_pool.pool_capacity);
#endif

  return arena;
}

void ecewo_arena_return(ecewo_arena_t *arena) {
  if (!arena)
    return;

  if (!arena_pool.initialized) {
    arena_free(arena);
    free(arena);
    return;
  }

  // Keep only the first region, free the rest
  if (arena->begin && arena->begin->next) {
    arena_region_t *to_free = arena->begin->next;
    arena->begin->next = NULL;

    while (to_free) {
      arena_region_t *next = to_free->next;
      free(to_free);
      to_free = next;
    }
  }

  // Reset the first region
  if (arena->begin) {
    arena->begin->count = 0;
    arena->end = arena->begin;
  }

  if (arena_pool.arenas && arena_pool.head < arena_pool.pool_capacity) {
    arena_pool.arenas[arena_pool.head++] = arena;
    arena_pool_try_shrink();
  } else {
    // Recycle cache is full or unallocated; free directly so live count
    // does not climb without bound.
    arena_free(arena);
    free(arena);
    if (arena_pool.total_allocated > 0)
      arena_pool.total_allocated--;
  }
}

#ifdef ECEWO_DEBUG
void ecewo_arena_pool_stats(void) {
  if (!arena_pool.initialized) {
    LOG_DEBUG("Arena pool not initialized");
    return;
  }

  uint32_t available = arena_pool.head;
  uint32_t in_use = arena_pool.total_allocated - available;
  double available_mb = (available * ARENA_REGION_SIZE) / (1024.0 * 1024.0);
  double total_mb = (arena_pool.total_allocated * ARENA_REGION_SIZE) / (1024.0 * 1024.0);

  LOG_DEBUG("Arena Pool Statistics:");
  LOG_DEBUG("  Cached: %u/%u arenas (%.2f MB)",
            available, arena_pool.pool_capacity, available_mb);
  LOG_DEBUG("  In use: %u arenas", in_use);
  LOG_DEBUG("  Peak usage: %u arenas", arena_pool.peak_usage);
  LOG_DEBUG("  Total allocated: %.2f MB", total_mb);
  LOG_DEBUG("  Grow operations: %u", arena_pool.grow_count);
  LOG_DEBUG("  Shrink operations: %u", arena_pool.shrink_count);
}

#endif

bool arena_pool_is_initialized(void) {
  return arena_pool.initialized;
}
