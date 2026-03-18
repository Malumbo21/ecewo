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

#include <stdlib.h>
#include <inttypes.h>
#include "route-table.h"
#include "middleware.h"
#include "logger.h"
#include "rax.h"

#define MAX_PATH_SEGMENTS 128
// MAX_PATH_BUF must hold the canonical path plus 2 extra bytes:
// a NULL separator and the method-index byte used as the rax key suffix.
#define MAX_PATH_BUF 2050
#define METHOD_COUNT 7
#define INITIAL_DYNAMIC_CAP 8

typedef enum {
  METHOD_INDEX_DELETE,
  METHOD_INDEX_GET,
  METHOD_INDEX_HEAD,
  METHOD_INDEX_POST,
  METHOD_INDEX_PUT,
  METHOD_INDEX_OPTIONS,
  METHOD_INDEX_PATCH
} http_method_index_t;

static int method_to_index(llhttp_method_t method) {
  switch (method) {
  case HTTP_DELETE:
    return METHOD_INDEX_DELETE;
  case HTTP_GET:
    return METHOD_INDEX_GET;
  case HTTP_HEAD:
    return METHOD_INDEX_HEAD;
  case HTTP_POST:
    return METHOD_INDEX_POST;
  case HTTP_PUT:
    return METHOD_INDEX_PUT;
  case HTTP_OPTIONS:
    return METHOD_INDEX_OPTIONS;
  case HTTP_PATCH:
    return METHOD_INDEX_PATCH;
  default:
    return -1;
  }
}

typedef struct {
  RequestHandler handler;
  void *middleware_ctx;
} static_val_t;

typedef struct {
  RequestHandler handler;
  void *middleware_ctx;
  path_segment_t *segs; // parsed pattern segments
  uint8_t seg_count;
  char *pattern_buf; // strdup'd pattern (owns segment memory)
} dynamic_entry_t;

// Static routes live in one rax tree. The key is:
//
//   canonical_path  +  '\0'  +  method_index_byte
//
// Using a single tree (rather than one per method) lets routes that share a
// URL prefix across methods share rax nodes, reducing memory and keeping the
// structure simpler.
//
// Dynamic routes (those containing ':' or '*') are split into two sorted
// arrays per method:
//
//   dyn_fixed:
//      patterns without wildcards, kept in ascending seg_count
//      order so the match loop can break early once
//      seg_count > request depth.
//
//   dyn_wildcard:
//      patterns that contain a '*' segment; these may match
//      requests of any depth and are checked separately.

struct route_table {
  rax *routes;

  dynamic_entry_t **dyn_fixed[METHOD_COUNT];
  size_t dyn_fixed_count[METHOD_COUNT];
  size_t dyn_fixed_cap[METHOD_COUNT];

  dynamic_entry_t **dyn_wildcard[METHOD_COUNT];
  size_t dyn_wildcard_count[METHOD_COUNT];
  size_t dyn_wildcard_cap[METHOD_COUNT];

  size_t route_count;
};

// Called by router.c before route_table_match)
int tokenize_path(Arena *arena, const char *path, size_t path_len, tokenized_path_t *result) {
  if (!path || !result)
    return -1;

  memset(result, 0, sizeof(tokenized_path_t));

  if (path_len > 0 && *path == '/') {
    path++;
    path_len--;
  }

  if (path_len == 0)
    return 0;

  uint8_t segment_count = 0;
  const char *p = path;
  const char *end = path + path_len;

  path_segment_t segments[MAX_PATH_SEGMENTS];

  while (p < end) {
    while (p < end && *p == '/')
      p++;

    if (p >= end)
      break;

    const char *start = p;

    while (p < end && *p != '/')
      p++;

    size_t len = (size_t)(p - start);
    if (len == 0)
      continue;

    if (segment_count >= MAX_PATH_SEGMENTS) {
      LOG_DEBUG("Path too deep: %" PRIu8 " segments (max %d)",
                segment_count, MAX_PATH_SEGMENTS);
      return -1;
    }

    segments[segment_count].start = start;
    segments[segment_count].len = len;
    segments[segment_count].is_param = (start[0] == ':');
    segments[segment_count].is_wildcard = (start[0] == '*');

    segment_count++;
  }

  if (segment_count == 0)
    return 0;

  result->count = segment_count;
  result->segments = arena_alloc(arena, sizeof(path_segment_t) * segment_count);
  if (!result->segments)
    return -1;

  memcpy(result->segments, segments,
         segment_count * sizeof(path_segment_t));

  return 0;
}

// Build a canonical path string from tokenized segments:
//   ["users", "123"]  ->  "/users/123"
//   []                ->  "/"
//
// Returns the number of path bytes written (without the trailing NULL)
// buf must have at least buf_size bytes; the result is always NULL-terminated
static size_t segs_to_path(const path_segment_t *segs, uint8_t count, char *buf, size_t buf_size) {
  if (count == 0) {
    buf[0] = '/';
    buf[1] = '\0';
    return 1;
  }
  size_t pos = 0;
  for (uint8_t i = 0; i < count; i++) {
    if (pos + 1 >= buf_size)
      break;
    buf[pos++] = '/';
    size_t len = segs[i].len;
    if (pos + len >= buf_size)
      len = buf_size - pos - 1;
    memcpy(buf + pos, segs[i].start, len);
    pos += len;
  }
  buf[pos] = '\0';
  return pos;
}

// Build the rax lookup key from tokenized request segments and a method index.
//
// Key layout:  canonical_path + '\0' + method_idx_byte
//
// Returns total key length (path_len + 2), or 0 if the canonical path would
// exceed buf_size (the path is too long to match any registered route)
static size_t build_static_key(const tokenized_path_t *tok, int method_idx, char *buf, size_t buf_size) {
  size_t required = (tok->count == 0) ? 1 : 0;

  for (uint8_t i = 0; i < tok->count; i++)
    required += 1 + tok->segments[i].len;

  // +2 for the NULL separator and method byte
  if (required + 2 > buf_size)
    return 0;

  size_t path_len = segs_to_path(tok->segments, tok->count, buf, buf_size - 2);
  buf[path_len] = '\0';
  buf[path_len + 1] = (char)(unsigned char)method_idx;
  return path_len + 2;
}

// Tokenize a route pattern at registration time
// The caller owns *segs_out and *buf_out and must free both
static int tokenize_pattern(const char *path,
                            path_segment_t **segs_out,
                            uint8_t *count_out,
                            char **buf_out) {
  char *buf = strdup(path);
  if (!buf)
    return -1;

  const char *p = buf;
  const char *end = buf + strlen(buf);

  if (p < end && *p == '/')
    p++;

  path_segment_t segs[MAX_PATH_SEGMENTS];
  uint8_t count = 0;

  while (p < end) {
    while (p < end && *p == '/')
      p++;
    if (p >= end)
      break;

    const char *start = p;
    while (p < end && *p != '/')
      p++;

    size_t len = (size_t)(p - start);
    if (len == 0)
      continue;

    if (count >= MAX_PATH_SEGMENTS) {
      free(buf);
      return -1;
    }

    segs[count].start = start;
    segs[count].len = len;
    segs[count].is_param = (start[0] == ':');
    segs[count].is_wildcard = (start[0] == '*');
    count++;
  }

  path_segment_t *result = malloc(sizeof(path_segment_t) * (count > 0 ? count : 1));
  if (!result) {
    free(buf);
    return -1;
  }
  memcpy(result, segs, sizeof(path_segment_t) * count);

  *segs_out = result;
  *count_out = count;
  *buf_out = buf;
  return 0;
}

static bool pattern_has_wildcard(const path_segment_t *segs, uint8_t count) {
  for (uint8_t i = 0; i < count; i++) {
    if (segs[i].is_wildcard)
      return true;
  }
  return false;
}

// -------------------------------------------------------------------------
// Dynamic array helpers
// -------------------------------------------------------------------------

// TODO: When we have a server-level arena, we might think to use arena_da_ macros here

// Look for an existing entry whose pattern_buf matches the given path string.
// If found, replace its handler and middleware_ctx (freeing the old middleware),
// free the candidate entry, and return true. Used to deduplicate dynamic
// route registrations the same way raxInsert deduplicates static ones.
static bool dyn_array_try_update(dynamic_entry_t **arr, size_t count, dynamic_entry_t *candidate) {
  for (size_t i = 0; i < count; i++) {
    if (strcmp(arr[i]->pattern_buf, candidate->pattern_buf) == 0) {
      if (arr[i]->middleware_ctx)
        free_middleware_info((MiddlewareInfo *)arr[i]->middleware_ctx);
      arr[i]->handler = candidate->handler;
      arr[i]->middleware_ctx = candidate->middleware_ctx;
      free(candidate->segs);
      free(candidate->pattern_buf);
      free(candidate);
      return true;
    }
  }
  return false;
}

static int dyn_array_push(dynamic_entry_t ***arr, size_t *count, size_t *cap, dynamic_entry_t *entry) {
  if (*count >= *cap) {
    size_t new_cap = *cap == 0 ? INITIAL_DYNAMIC_CAP : *cap * 2;
    dynamic_entry_t **new_arr = realloc(*arr, sizeof(dynamic_entry_t *) * new_cap);
    if (!new_arr)
      return -1;
    *arr = new_arr;
    *cap = new_cap;
  }
  (*arr)[(*count)++] = entry;
  return 0;
}

// Insert entry into a sorted array (ascending by seg_count).
// Uses insertion-sort, O(N) per call, acceptable for startup-time registration.
static int dyn_array_insert_sorted(dynamic_entry_t ***arr, size_t *count, size_t *cap, dynamic_entry_t *entry) {
  if (*count >= *cap) {
    size_t new_cap = *cap == 0 ? INITIAL_DYNAMIC_CAP : *cap * 2;
    dynamic_entry_t **new_arr = realloc(*arr, sizeof(dynamic_entry_t *) * new_cap);
    if (!new_arr)
      return -1;
    *arr = new_arr;
    *cap = new_cap;
  }

  // Shift entries with larger seg_count one position to the right
  size_t pos = *count;
  while (pos > 0 && (*arr)[pos - 1]->seg_count > entry->seg_count) {
    (*arr)[pos] = (*arr)[pos - 1];
    pos--;
  }
  (*arr)[pos] = entry;
  (*count)++;
  return 0;
}

// -------------------------------------------------------------------------
// Param capture (shared by match_dynamic_entry)
// -------------------------------------------------------------------------

static int add_param_to_match(route_match_t *match,
                              Arena *arena,
                              const char *key_data,
                              size_t key_len,
                              const char *value_data,
                              size_t value_len) {
  if (!match)
    return -1;

  // Inline storage
  if (match->param_count < MAX_INLINE_PARAMS && !match->params) {
    param_match_t *param = &match->inline_params[match->param_count];
    param->key.data = key_data;
    param->key.len = key_len;
    param->value.data = value_data;
    param->value.len = value_len;
    match->param_count++;
    return 0;
  }

  // Switch to arena-allocated storage
  if (match->param_count == MAX_INLINE_PARAMS && !match->params) {
    uint8_t new_cap = MAX_INLINE_PARAMS * 2;
    param_match_t *new_params = arena_alloc(arena, sizeof(param_match_t) * new_cap);
    if (!new_params) {
      LOG_ERROR("Failed to allocate dynamic param storage");
      return -1;
    }
    arena_memcpy(new_params, match->inline_params, sizeof(param_match_t) * MAX_INLINE_PARAMS);
    match->params = new_params;
    match->param_capacity = new_cap;
    LOG_DEBUG("Route params overflow: switched to dynamic allocation (%d params)", new_cap);
  }

  // Grow arena storage if needed
  if (match->params && match->param_count >= match->param_capacity) {
    uint8_t new_cap = match->param_capacity * 2;
    if (new_cap > 64) {
      LOG_ERROR("Route parameter limit exceeded: %d", new_cap);
      return -1;
    }
    param_match_t *new_params = arena_realloc(arena,
                                              match->params,
                                              sizeof(param_match_t) * match->param_capacity,
                                              sizeof(param_match_t) * new_cap);
    if (!new_params) {
      LOG_ERROR("Failed to reallocate param storage");
      return -1;
    }
    match->params = new_params;
    match->param_capacity = new_cap;
  }

  if (!match->params) {
    LOG_ERROR("Unexpected NULL params pointer with param_count=%d", match->param_count);
    return -1;
  }

  param_match_t *target = &match->params[match->param_count];
  target->key.data = key_data;
  target->key.len = key_len;
  target->value.data = value_data;
  target->value.len = value_len;
  match->param_count++;
  return 0;
}

// -------------------------------------------------------------------------
// Dynamic route matching
// -------------------------------------------------------------------------

// Edge-case behaviours — verified by tests/test-router-edge.c:
//
//  1. Bare ":" (empty param name)
//     Route "/:": the single segment has is_param=true and len=1, so the
//     captured param name is pat.start+1 with length 0 — an empty string.
//     get_param(req, "") retrieves the value.  The route is legal; callers
//     should avoid it in practice because the empty key is easy to confuse.
//
//  2. Wildcard not at the end of a pattern ("/prefix/*/suffix")
//     When match_dynamic_entry reaches a wildcard segment it returns true
//     immediately, consuming the rest of the request path.  Any pattern
//     segments AFTER the "*" are never evaluated.  Effectively "/a/*/b"
//     behaves identically to "/a/*".  Register "/a/:param/b" instead if
//     you need to match the trailing literal.
//
//  3. Percent-encoded characters ("%2F", "%20", ...)
//     The routing layer operates on the raw URL bytes as delivered by
//     llhttp; it performs NO percent-decoding. "%2F" is three literal
//     bytes, not a path separator. A route registered as "/a%2Fb" only
//     matches requests with that exact byte sequence, never "/a/b".
//     Param values are decoded after extraction (like decodeURIComponent).
static bool match_dynamic_entry(const dynamic_entry_t *entry,
                                const tokenized_path_t *req_path,
                                route_match_t *match,
                                Arena *arena) {
  const path_segment_t *pat = entry->segs;
  uint8_t pat_count = entry->seg_count;
  const path_segment_t *req = req_path->segments;
  uint8_t req_count = req_path->count;

  uint8_t saved_param_count = match->param_count;
  uint8_t ri = 0;

  for (uint8_t pi = 0; pi < pat_count; pi++) {
    if (pat[pi].is_wildcard) {
      // '*' matches zero or more remaining segments
      return true;
    }

    if (ri >= req_count) {
      match->param_count = saved_param_count;
      return false;
    }

    if (pat[pi].is_param) {
      // Named param: key is segment text after ':', value is request segment
      if (add_param_to_match(match, arena,
                             pat[pi].start + 1, pat[pi].len - 1,
                             req[ri].start, req[ri].len)
          != 0) {
        match->param_count = saved_param_count;
        return false;
      }
      ri++;
    } else {
      // Exact segment match
      if (pat[pi].len != req[ri].len || memcmp(pat[pi].start, req[ri].start, pat[pi].len) != 0) {
        match->param_count = saved_param_count;
        return false;
      }
      ri++;
    }
  }

  if (ri != req_count) {
    // Pattern ended but request still has remaining segments
    match->param_count = saved_param_count;
    return false;
  }

  return true;
}

// TODO: Use radix-tree also for dynamic paths
// The current implementation uses it only for static paths
bool route_table_match(route_table_t *table,
                       llhttp_t *parser,
                       const tokenized_path_t *tokenized_path,
                       route_match_t *match,
                       Arena *arena) {
  if (!table || !parser || !tokenized_path || !match)
    return false;

  llhttp_method_t method = llhttp_get_method(parser);
  int method_idx = method_to_index(method);

  if (method_idx < 0)
    return false;

  match->handler = NULL;
  match->middleware_ctx = NULL;
  match->param_count = 0;
  match->params = NULL;
  match->param_capacity = MAX_INLINE_PARAMS;

  // Build rax key: canonical_path + NULL + method_idx_byte
  char key_buf[MAX_PATH_BUF];
  size_t key_len = build_static_key(tokenized_path, method_idx,
                                    key_buf, sizeof(key_buf));
  // Path too long to match any registered route
  if (key_len == 0)
    return false;

  // 1. Exact (static) lookup: O(key_len)
  if (table->routes) {
    void *data = raxFind(table->routes, (unsigned char *)key_buf, key_len);
    if (data != raxNotFound) {
      static_val_t *val = (static_val_t *)data;
      match->handler = val->handler;
      match->middleware_ctx = val->middleware_ctx;
      return true;
    }
  }

  uint8_t req_count = tokenized_path->count;

  // 2. Fixed dynamic routes (no wildcards), sorted by seg_count ascending.
  //    Break as soon as seg_count exceeds the request depth; all subsequent
  //    entries are also deeper, so they cannot match.
  for (size_t i = 0; i < table->dyn_fixed_count[method_idx]; i++) {
    dynamic_entry_t *e = table->dyn_fixed[method_idx][i];
    if (e->seg_count > req_count)
      break;
    if (e->seg_count < req_count)
      continue;
    if (match_dynamic_entry(e, tokenized_path, match, arena)) {
      match->handler = e->handler;
      match->middleware_ctx = e->middleware_ctx;
      return true;
    }
  }

  // 3. Wildcard dynamic routes (may match any request depth)
  for (size_t i = 0; i < table->dyn_wildcard_count[method_idx]; i++) {
    dynamic_entry_t *e = table->dyn_wildcard[method_idx][i];
    if (match_dynamic_entry(e, tokenized_path, match, arena)) {
      match->handler = e->handler;
      match->middleware_ctx = e->middleware_ctx;
      return true;
    }
  }

  return false;
}

int route_table_add(route_table_t *table,
                    llhttp_method_t method,
                    const char *path,
                    RequestHandler handler,
                    void *middleware_ctx) {
  if (!table || !path || !handler)
    return -1;

  int method_idx = method_to_index(method);
  if (method_idx < 0) {
    LOG_DEBUG("Unsupported HTTP method: %d", method);
    return -1;
  }

  // Tokenize the path for both the canonical key and segment analysis
  path_segment_t *segs;
  uint8_t seg_count;
  char *pattern_buf;

  if (tokenize_pattern(path, &segs, &seg_count, &pattern_buf) != 0)
    return -1;

  bool is_dynamic = false;
  for (uint8_t i = 0; i < seg_count; i++) {
    if (segs[i].is_param || segs[i].is_wildcard) {
      is_dynamic = true;
      break;
    }
  }

  if (!is_dynamic) {
    // Static route: insert into rax
    if (!table->routes) {
      table->routes = raxNew();
      if (!table->routes) {
        free(segs);
        free(pattern_buf);
        return -1;
      }
    }

    static_val_t *val = malloc(sizeof(static_val_t));
    if (!val) {
      free(segs);
      free(pattern_buf);
      return -1;
    }
    val->handler = handler;
    val->middleware_ctx = middleware_ctx;

    // Build the canonical key: normalized_path + NULL + method_idx_byte
    char key_buf[MAX_PATH_BUF];
    size_t path_len = segs_to_path(segs, seg_count, key_buf, sizeof(key_buf) - 2);
    key_buf[path_len] = '\0';
    key_buf[path_len + 1] = (char)(unsigned char)method_idx;
    size_t key_len = path_len + 2;

    free(segs);
    free(pattern_buf);

    void *old_val = NULL;
    int ret = raxInsert(table->routes, (unsigned char *)key_buf, key_len, val, &old_val);

    if (ret == 0 && old_val == NULL) {
      free(val);
      return -1;
    }

    if (old_val) {
      // A previous registration existed for this path+method; free the old one
      static_val_t *old = (static_val_t *)old_val;
      if (old->middleware_ctx)
        free_middleware_info((MiddlewareInfo *)old->middleware_ctx);
      free(old);
    }

  } else {
    // Dynamic route: store with segments for pattern matching
    dynamic_entry_t *entry = malloc(sizeof(dynamic_entry_t));
    if (!entry) {
      free(segs);
      free(pattern_buf);
      return -1;
    }
    entry->handler = handler;
    entry->middleware_ctx = middleware_ctx;
    entry->segs = segs;
    entry->seg_count = seg_count;
    entry->pattern_buf = pattern_buf;

    int ret;
    if (pattern_has_wildcard(segs, seg_count)) {
      // Wildcard routes: append, checked against any request depth
      if (dyn_array_try_update(table->dyn_wildcard[method_idx],
                               table->dyn_wildcard_count[method_idx], entry))
        return 0;
      ret = dyn_array_push(&table->dyn_wildcard[method_idx],
                           &table->dyn_wildcard_count[method_idx],
                           &table->dyn_wildcard_cap[method_idx],
                           entry);
    } else {
      // Fixed-depth parametric routes: insert sorted by seg_count
      if (dyn_array_try_update(table->dyn_fixed[method_idx],
                               table->dyn_fixed_count[method_idx], entry))
        return 0;
      ret = dyn_array_insert_sorted(&table->dyn_fixed[method_idx],
                                    &table->dyn_fixed_count[method_idx],
                                    &table->dyn_fixed_cap[method_idx],
                                    entry);
    }

    if (ret != 0) {
      free(entry->segs);
      free(entry->pattern_buf);
      free(entry);
      return -1;
    }
  }

  table->route_count++;
  return 0;
}

route_table_t *route_table_create(void) {
  // rax and dynamic arrays are allocated lazily on first route_table_add
  return calloc(1, sizeof(route_table_t));
}

static void free_static_val_cb(void *data) {
  static_val_t *val = (static_val_t *)data;
  if (val->middleware_ctx)
    free_middleware_info((MiddlewareInfo *)val->middleware_ctx);
  free(val);
}

static void free_dynamic_entry(dynamic_entry_t *entry) {
  if (entry->middleware_ctx)
    free_middleware_info((MiddlewareInfo *)entry->middleware_ctx);
  free(entry->segs);
  free(entry->pattern_buf);
  free(entry);
}

void route_table_free(route_table_t *table) {
  if (!table)
    return;

  if (table->routes)
    raxFreeWithCallback(table->routes, free_static_val_cb);

  for (int i = 0; i < METHOD_COUNT; i++) {
    for (size_t j = 0; j < table->dyn_fixed_count[i]; j++)
      free_dynamic_entry(table->dyn_fixed[i][j]);
    free(table->dyn_fixed[i]);

    for (size_t j = 0; j < table->dyn_wildcard_count[i]; j++)
      free_dynamic_entry(table->dyn_wildcard[i][j]);
    free(table->dyn_wildcard[i]);
  }

  free(table);
}
