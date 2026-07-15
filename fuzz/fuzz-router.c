// Copyright 2026 Savas Sahin <savashn@proton.me>

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
//
//
// ---------------------------------------------------------------------------
// libFuzzer target for the ecewo routing layer. Requires Clang
//
// Usage:
//   cmake -B build-fuzz \
//     -DECEWO_BUILD_FUZZ=ON \
//     -DCMAKE_C_COMPILER=clang \
//     -DCMAKE_BUILD_TYPE=Debug
//   cmake --build build-fuzz --target fuzz-router
//   mkdir -p fuzz/corpus && ./build-fuzz/fuzz-router fuzz/corpus -max_len=4096
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// FUZZING STRATEGY:
// 1 - Path Matching: Use fixed routes to see if random URLs cause crashes.
// 2 - Route Registration: Try creating temporary routes with "garbage" patterns
//    to ensure the trie/memory management is robust against malformed input.
// ---------------------------------------------------------------------------

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "ecewo.h"
#include "arena-internal.h"
#include "route-table.h"
#include "http-methods.h"
#include "llhttp.h"

static void noop(ecewo_request_t *req, ecewo_response_t *res) {
  (void)req;
  (void)res;
}

static route_table_t *route_table;
// Backs the persistent table; lives for the whole fuzzing run.
static ecewo_arena_t table_arena;

// Wire methods for route_table_match, generated from ECEWO_METHOD_TABLE so
// every routable method (and its Allow-bitmask bit) gets fuzzed.
static const uint8_t method_map[ECEWO__METHOD_COUNT] = {
#define X(suffix, http_method) http_method,
  ECEWO_METHOD_TABLE(X)
#undef X
};

int LLVMFuzzerInitialize(int *argc, char ***argv) {
  (void)argc;
  (void)argv;

  route_table = route_table_create(&table_arena);
  if (!route_table)
    return 0;

  // Static routes
  route_table_add(route_table, &table_arena, ECEWO_METHOD_GET, "/", noop, NULL);
  route_table_add(route_table, &table_arena, ECEWO_METHOD_GET, "/users", noop, NULL);
  route_table_add(route_table, &table_arena, ECEWO_METHOD_GET, "/users/admin", noop, NULL);
  route_table_add(route_table, &table_arena, ECEWO_METHOD_POST, "/users", noop, NULL);
  route_table_add(route_table, &table_arena, ECEWO_METHOD_GET, "/api/v1/status", noop, NULL);

  // Dynamic routes
  route_table_add(route_table, &table_arena, ECEWO_METHOD_GET, "/users/:id", noop, NULL);
  route_table_add(route_table, &table_arena, ECEWO_METHOD_PUT, "/users/:id", noop, NULL);
  route_table_add(route_table, &table_arena, ECEWO_METHOD_DELETE, "/users/:id", noop, NULL);
  route_table_add(route_table, &table_arena, ECEWO_METHOD_GET, "/users/:id/posts", noop, NULL);
  route_table_add(route_table, &table_arena, ECEWO_METHOD_POST, "/users/:userId/posts/:postId", noop, NULL);
  route_table_add(route_table, &table_arena, ECEWO_METHOD_GET, "/api/:version/:resource", noop, NULL);
  route_table_add(route_table, &table_arena, ECEWO_METHOD_GET, "/api/:version/:resource/:id", noop, NULL);

  // Wildcard routes
  route_table_add(route_table, &table_arena, ECEWO_METHOD_GET, "/files/*", noop, NULL);
  route_table_add(route_table, &table_arena, ECEWO_METHOD_PUT, "/static/*", noop, NULL);

  return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 2)
    return 0;

  uint8_t method_byte = data[0] % ECEWO__METHOD_COUNT;
  const char *path = (const char *)(data + 1);
  size_t path_len = size - 1;

  // 1 - Request Matching (Testing existing routes):
  // The fuzzer takes the first byte of the random input to pick an HTTP
  // method. The rest of the input is treated as a URL.
  // We check if the router can handle weird, broken,
  // or extremely long URLs without crashing when comparing them
  // against the real routes we defined during startup.
  {
    ecewo_arena_t arena = { 0 };
    tokenized_path_t tok = { 0 };

    tokenize_path(&arena, path, path_len, &tok);

    llhttp_settings_t settings;
    llhttp_settings_init(&settings);
    llhttp_t parser;
    llhttp_init(&parser, HTTP_REQUEST, &settings);
    parser.method = method_map[method_byte];

    route_match_t match;
    route_table_match(route_table, &parser, &tok, &match, &arena);

    arena_free(&arena);
  }

  // 2 - Dynamic Registration (Testing the route creator):
  // The fuzzer tries to create a brand new, temporary route using the
  // random input as the "pattern". This tests if the system stays stable
  // when we feed it "garbage" data (like hidden null characters, massive
  // strings, or symbols). We must ensure that creating and then
  // immediately deleting these "trash" routes doesn't leak memory or
  // corrupt the internal data structures.
  if (path_len > 0 && path_len <= 512) {
    char pattern[513];
    memcpy(pattern, path, path_len);
    pattern[path_len] = '\0';

    for (int m = 0; m < ECEWO__METHOD_COUNT; m++) {
      ecewo_arena_t tmp_arena = { 0 };
      route_table_t *tmp = route_table_create(&tmp_arena);
      if (!tmp) {
        arena_free(&tmp_arena);
        continue;
      }

      route_table_add(tmp, &tmp_arena, (ecewo_method_t)m, pattern, noop, NULL);

      // Duplicate registration must not double-free or leak
      route_table_add(tmp, &tmp_arena, (ecewo_method_t)m, pattern, noop, NULL);

      route_table_free(tmp);
      arena_free(&tmp_arena);
    }
  }

  return 0;
}
