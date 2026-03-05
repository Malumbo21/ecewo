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

#include "uv.h"
#include <stdbool.h>
#include <time.h>

typedef struct
{
  time_t timestamp;
  char date_str[64];
  uv_mutex_t mutex;
} date_cache_t;

static date_cache_t g_date_cache = { 0 };
static bool g_date_cache_initialized = false;

void init_date_cache(void) {
  if (g_date_cache_initialized)
    return;

  uv_mutex_init(&g_date_cache.mutex);
  g_date_cache.timestamp = 0;
  g_date_cache_initialized = true;
}

void destroy_date_cache(void) {
  if (!g_date_cache_initialized)
    return;

  uv_mutex_destroy(&g_date_cache.mutex);
  g_date_cache_initialized = false;
}

const char *get_cached_date(void) {
  time_t now = time(NULL);

  if (g_date_cache.timestamp == now)
    return g_date_cache.date_str;

  uv_mutex_lock(&g_date_cache.mutex);

  if (g_date_cache.timestamp != now) {
    struct tm *gmt = gmtime(&now);
    strftime(g_date_cache.date_str, sizeof(g_date_cache.date_str),
             "%a, %d %b %Y %H:%M:%S GMT", gmt);
    g_date_cache.timestamp = now;
  }

  uv_mutex_unlock(&g_date_cache.mutex);

  return g_date_cache.date_str;
}
