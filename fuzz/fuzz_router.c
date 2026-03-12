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
//
// The fuzzer exercises two code paths on every input:
//
//   A. Path matching  — the first byte selects an HTTP method (0-6), the
//      remainder is treated as a raw request URL and matched against a fixed
//      set of routes that was registered during initialisation.
//
//   B. Route registration — the remainder bytes are tried as a route pattern
//      in a throw-away trie.  Arbitrary byte sequences (including embedded
//      NULs, very long strings, unusual characters) must not crash or corrupt
//      memory during registration or teardown.

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "ecewo.h"
#include "route-table.h"
#include "llhttp.h"

// Only its address is stored; it is never invoked.
static void noop(Req *req, Res *res) {
  (void)req;
  (void)res;
}

static route_table_t *route_table;

static const uint8_t method_map[7] = {
  HTTP_DELETE,
  HTTP_GET,
  HTTP_HEAD,
  HTTP_POST,
  HTTP_PUT,
  HTTP_OPTIONS,
  HTTP_PATCH,
};

int LLVMFuzzerInitialize(int *argc, char ***argv) {
  (void)argc;
  (void)argv;

  route_table = route_table_create();
  if (!route_table)
    return 0;

  // Static routes
  route_table_add(route_table, HTTP_GET, "/", noop, NULL);
  route_table_add(route_table, HTTP_GET, "/users", noop, NULL);
  route_table_add(route_table, HTTP_GET, "/users/admin", noop, NULL);
  route_table_add(route_table, HTTP_POST, "/users", noop, NULL);
  route_table_add(route_table, HTTP_GET, "/api/v1/status", noop, NULL);

  // Dynamic routes
  route_table_add(route_table, HTTP_GET, "/users/:id", noop, NULL);
  route_table_add(route_table, HTTP_PUT, "/users/:id", noop, NULL);
  route_table_add(route_table, HTTP_DELETE, "/users/:id", noop, NULL);
  route_table_add(route_table, HTTP_GET, "/users/:id/posts", noop, NULL);
  route_table_add(route_table, HTTP_POST, "/users/:userId/posts/:postId", noop, NULL);
  route_table_add(route_table, HTTP_GET, "/api/:version/:resource", noop, NULL);
  route_table_add(route_table, HTTP_GET, "/api/:version/:resource/:id", noop, NULL);

  // Wildcard routes
  route_table_add(route_table, HTTP_GET, "/files/*", noop, NULL);
  route_table_add(route_table, HTTP_PUT, "/static/*", noop, NULL);

  return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 2)
    return 0;

  uint8_t method_byte = data[0] % 7;
  const char *path = (const char *)(data + 1);
  size_t path_len = size - 1;

  // A. Path matching against the fixed trie
  {
    Arena arena = {0};
    tokenized_path_t tok = {0};

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

  // B. Route registration — arbitrary pattern in a temporary trie
  // Limit pattern length to avoid burning time on unrealistically long
  // inputs; real route strings are always short source-code literals.
  if (path_len > 0 && path_len <= 512) {
    char pattern[513];
    memcpy(pattern, path, path_len);
    pattern[path_len] = '\0';

    for (int m = 0; m < 7; m++) {
      route_table_t *tmp = route_table_create();
      if (!tmp)
        continue;

      static const llhttp_method_t methods[7] = {
        HTTP_DELETE, HTTP_GET, HTTP_HEAD, HTTP_POST,
        HTTP_PUT,    HTTP_OPTIONS, HTTP_PATCH,
      };

      route_table_add(tmp, methods[m], pattern, noop, NULL);

      // Duplicate registration must not double-free or leak
      route_table_add(tmp, methods[m], pattern, noop, NULL);

      route_table_free(tmp);
    }
  }

  return 0;
}
