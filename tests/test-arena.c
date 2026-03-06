// MIT License

// Copyright (c) 2026 savashn

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "arena.h"
#include "tester.h"
#include <stdint.h>
#include <string.h>


// TEST 1: arena_alloc: basic allocation
int test_arena_alloc_basic(void) {
  Arena a = {0};

  void *p = arena_alloc(&a, 64);
  ASSERT_NOT_NULL(p);

  // Write and read back to verify the memory is usable
  memset(p, 0xAB, 64);
  unsigned char *bytes = (unsigned char *)p;
  for (int i = 0; i < 64; i++) {
    ASSERT_EQ(0xAB, bytes[i]);
  }

  arena_free(&a);
  RETURN_OK();
}


// TEST 2: arena_alloc: multiple allocations do not overlap
int test_arena_alloc_no_overlap(void) {
  Arena a = {0};

  int *x = arena_alloc(&a, sizeof(int));
  int *y = arena_alloc(&a, sizeof(int));
  ASSERT_NOT_NULL(x);
  ASSERT_NOT_NULL(y);
  ASSERT_TRUE(x != y);

  *x = 42;
  *y = 99;
  ASSERT_EQ(42, *x);
  ASSERT_EQ(99, *y);

  arena_free(&a);
  RETURN_OK();
}


// TEST 3: arena_alloc: allocation larger than default region size forces a new region
int test_arena_alloc_large(void) {
  Arena a = {0};

  // 64 KiB is the default ARENA_REGION_SIZE
  size_t large = 64UL * 1024UL + 1;
  void *p = arena_alloc(&a, large);
  ASSERT_NOT_NULL(p);

  memset(p, 0, large);  // should not crash

  arena_free(&a);
  RETURN_OK();
}


// TEST 4: arena_realloc: newsz <= oldsz returns the same pointer
int test_arena_realloc_shrink(void) {
  Arena a = {0};

  void *p = arena_alloc(&a, 128);
  ASSERT_NOT_NULL(p);

  void *q = arena_realloc(&a, p, 128, 64);
  ASSERT_TRUE(q == p);

  void *r = arena_realloc(&a, p, 128, 128);
  ASSERT_TRUE(r == p);

  arena_free(&a);
  RETURN_OK();
}


// TEST 5: arena_realloc: grow copies existing data
int test_arena_realloc_grow(void) {
  Arena a = {0};

  char *p = arena_alloc(&a, 4);
  ASSERT_NOT_NULL(p);
  p[0] = 'h'; p[1] = 'i'; p[2] = '!'; p[3] = '\0';

  char *q = arena_realloc(&a, p, 4, 8);
  ASSERT_NOT_NULL(q);
  ASSERT_EQ_STR("hi!", q);

  arena_free(&a);
  RETURN_OK();
}


// TEST 6: arena_strdup: NULL input returns NULL
int test_arena_strdup_null(void) {
  Arena a = {0};

  char *p = arena_strdup(&a, NULL);
  ASSERT_NULL(p);

  arena_free(&a);
  RETURN_OK();
}


// TEST 7: arena_strdup: duplicates string correctly
int test_arena_strdup_basic(void) {
  Arena a = {0};

  const char *orig = "hello, world";
  char *dup = arena_strdup(&a, orig);
  ASSERT_NOT_NULL(dup);
  ASSERT_EQ_STR(orig, dup);

  // Must be a distinct pointer
  ASSERT_TRUE((void *)dup != (void *)orig);

  arena_free(&a);
  RETURN_OK();
}


// TEST 8: arena_strdup: empty string
int test_arena_strdup_empty(void) {
  Arena a = {0};

  char *dup = arena_strdup(&a, "");
  ASSERT_NOT_NULL(dup);
  ASSERT_EQ_STR("", dup);

  arena_free(&a);
  RETURN_OK();
}


// TEST 9: arena_memdup: NULL data or zero size returns NULL
int test_arena_memdup_null(void) {
  Arena a = {0};

  ASSERT_NULL(arena_memdup(&a, NULL, 8));
  ASSERT_NULL(arena_memdup(&a, "x", 0));

  arena_free(&a);
  RETURN_OK();
}


// TEST 10: arena_memdup: duplicates binary data correctly
int test_arena_memdup_basic(void) {
  Arena a = {0};

  uint8_t src[4] = {0xDE, 0xAD, 0xBE, 0xEF};
  uint8_t *dst = arena_memdup(&a, src, sizeof(src));
  ASSERT_NOT_NULL(dst);
  ASSERT_TRUE((void *)dst != (void *)src);
  ASSERT_EQ(0, memcmp(src, dst, sizeof(src)));

  arena_free(&a);
  RETURN_OK();
}


// TEST 11: arena_sprintf: multi-argument format
int test_arena_sprintf_multi(void) {
  Arena a = {0};

  char *s = arena_sprintf(&a, "%s:%d", "port", 8080);
  ASSERT_NOT_NULL(s);
  ASSERT_EQ_STR("port:8080", s);

  arena_free(&a);
  RETURN_OK();
}


// TEST 12: arena_free: zeroes begin and end pointers
int test_arena_free(void) {
  Arena a = {0};

  void *p = arena_alloc(&a, 32);
  ASSERT_NOT_NULL(p);
  ASSERT_NOT_NULL(a.begin);

  arena_free(&a);
  ASSERT_NULL(a.begin);
  ASSERT_NULL(a.end);

  RETURN_OK();
}


// TEST 13: arena_da_append: capacity doubles on overflow (forces realloc past ARENA_DA_INIT_CAP=256)
typedef struct {
  int *items;
  size_t count;
  size_t capacity;
} IntArray;

int test_arena_da_append_growth(void) {
  Arena a = {0};
  IntArray arr = {0};

  // Fill past initial capacity (ARENA_DA_INIT_CAP = 256) to force a realloc
  for (int i = 0; i < 300; i++) {
    arena_da_append(&a, &arr, i);
  }

  ASSERT_EQ(300, (int64_t)arr.count);
  ASSERT_TRUE(arr.capacity >= 300);

  for (int i = 0; i < 300; i++) {
    ASSERT_EQ(i, arr.items[i]);
  }

  arena_free(&a);
  RETURN_OK();
}


// TEST 14: arena_da_append_many: bulk append
int test_arena_da_append_many(void) {
  Arena a = {0};
  IntArray arr = {0};

  int batch[] = {10, 20, 30, 40, 50};
  arena_da_append_many(&a, &arr, batch, 5);

  ASSERT_EQ(5, (int64_t)arr.count);
  for (int i = 0; i < 5; i++) {
    ASSERT_EQ((i + 1) * 10, arr.items[i]);
  }

  // Append a second batch
  int batch2[] = {60, 70};
  arena_da_append_many(&a, &arr, batch2, 2);
  ASSERT_EQ(7, (int64_t)arr.count);
  ASSERT_EQ(60, arr.items[5]);
  ASSERT_EQ(70, arr.items[6]);

  arena_free(&a);
  RETURN_OK();
}


int main(void) {
  RUN_TEST(test_arena_alloc_basic);
  RUN_TEST(test_arena_alloc_no_overlap);
  RUN_TEST(test_arena_alloc_large);
  RUN_TEST(test_arena_realloc_shrink);
  RUN_TEST(test_arena_realloc_grow);
  RUN_TEST(test_arena_strdup_null);
  RUN_TEST(test_arena_strdup_basic);
  RUN_TEST(test_arena_strdup_empty);
  RUN_TEST(test_arena_memdup_null);
  RUN_TEST(test_arena_memdup_basic);
  RUN_TEST(test_arena_sprintf_multi);
  RUN_TEST(test_arena_free);
  RUN_TEST(test_arena_da_append_growth);
  RUN_TEST(test_arena_da_append_many);

  return 0;
}
