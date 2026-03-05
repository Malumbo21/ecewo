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

#ifndef ECEWO_ROUTE_TRIE_H
#define ECEWO_ROUTE_TRIE_H

#include "ecewo.h"
#include "llhttp.h"

#define MAX_PATH_SEGMENTS 128
#define METHOD_COUNT 7

#ifndef MAX_INLINE_PARAMS
#define MAX_INLINE_PARAMS 8
#endif

typedef enum {
  METHOD_INDEX_DELETE,
  METHOD_INDEX_GET,
  METHOD_INDEX_HEAD,
  METHOD_INDEX_POST,
  METHOD_INDEX_PUT,
  METHOD_INDEX_OPTIONS,
  METHOD_INDEX_PATCH
} http_method_index_t;

typedef struct
{
  const char *start;
  size_t len;
  bool is_param;
  bool is_wildcard;
} path_segment_t;

typedef struct
{
  path_segment_t *segments;
  uint8_t count;
} tokenized_path_t;

typedef struct trie_node {
  struct trie_node *children[128]; // ASCII characters
  struct trie_node *param_child; // For :param segments
  struct trie_node *wildcard_child; // For * wildcard
  char *param_name; // Name of parameter if this is a param node
  bool is_end; // Marks end of a route
  RequestHandler handlers[METHOD_COUNT]; // Handlers for different HTTP methods
  void *middleware_ctx[METHOD_COUNT]; // Middleware context for each method
} trie_node_t;

typedef struct
{
  trie_node_t *root;
  size_t route_count;
} route_trie_t;

extern route_trie_t *global_route_trie;

typedef struct
{
  const char *data;
  size_t len;
} string_view_t;

typedef struct
{
  string_view_t key;
  string_view_t value;
} param_match_t;

typedef struct
{
  RequestHandler handler;
  void *middleware_ctx;
  param_match_t inline_params[MAX_INLINE_PARAMS]; // On stack
  param_match_t *params; // On heap
  uint8_t param_count;
  uint8_t param_capacity; // For dynamic allocation
} route_match_t;

bool route_trie_match(route_trie_t *trie,
                      llhttp_t *parser,
                      const tokenized_path_t *tokenized_path,
                      route_match_t *match,
                      Arena *arena);

int route_trie_add(route_trie_t *trie,
                   llhttp_method_t method,
                   const char *path,
                   RequestHandler handler,
                   void *middleware_ctx);

int tokenize_path(Arena *arena, const char *path, size_t path_len, tokenized_path_t *result);
void route_trie_free(route_trie_t *trie);
route_trie_t *route_trie_create(void);

#endif
