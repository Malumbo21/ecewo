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

#include <stdarg.h>
#include <stdlib.h>
#include "route-table.h"
#include "middleware.h"
#include "logger.h"

#define MAX_STACK_MW 8

#define ROUTE_REGISTER(func_name, method_enum)                                   \
  void func_name(const char *path, int mw_count, ...) {                          \
    if (!path) {                                                                 \
      LOG_ERROR("NULL path in route registration");                              \
      return;                                                                    \
    }                                                                            \
                                                                                 \
    va_list args;                                                                \
    va_start(args, mw_count);                                                    \
                                                                                 \
    MiddlewareHandler stack_mw[MAX_STACK_MW];                                    \
    MiddlewareHandler *mw = NULL;                                                \
    if (mw_count > 0) {                                                          \
      if (mw_count <= MAX_STACK_MW) {                                            \
        mw = stack_mw;                                                           \
      } else {                                                                   \
        mw = malloc(sizeof(MiddlewareHandler) * mw_count);                       \
        if (!mw) {                                                               \
          LOG_ERROR("Middleware allocation failed");                             \
          va_end(args);                                                          \
          return;                                                                \
        }                                                                        \
      }                                                                          \
                                                                                 \
      for (int i = 0; i < mw_count; i++) {                                       \
        mw[i] = va_arg(args, MiddlewareHandler);                                 \
        if (!mw[i]) {                                                            \
          LOG_ERROR("NULL middleware handler at index %d", i);                   \
          if (mw_count > MAX_STACK_MW)                                           \
            free(mw);                                                            \
          va_end(args);                                                          \
          return;                                                                \
        }                                                                        \
      }                                                                          \
    }                                                                            \
                                                                                 \
    RequestHandler handler = va_arg(args, RequestHandler);                       \
    va_end(args);                                                                \
                                                                                 \
    if (!handler) {                                                              \
      LOG_ERROR("NULL handler in route registration");                           \
      if (mw_count > MAX_STACK_MW)                                               \
        free(mw);                                                                \
      return;                                                                    \
    }                                                                            \
                                                                                 \
    MiddlewareInfo *info = calloc(1, sizeof(MiddlewareInfo));                    \
    if (!info) {                                                                 \
      if (mw_count > MAX_STACK_MW)                                               \
        free(mw);                                                                \
      return;                                                                    \
    }                                                                            \
                                                                                 \
    info->handler = handler;                                                     \
    info->middleware_count = mw_count;                                           \
                                                                                 \
    if (mw_count > 0 && mw_count <= MAX_STACK_MW) {                              \
      info->middleware = malloc(sizeof(MiddlewareHandler) * mw_count);           \
      if (!info->middleware) {                                                   \
        free(info);                                                              \
        return;                                                                  \
      }                                                                          \
      memcpy(info->middleware, mw, sizeof(MiddlewareHandler) * mw_count);        \
    } else {                                                                     \
      info->middleware = mw;                                                     \
    }                                                                            \
                                                                                 \
    int result = route_table_add(route_table, method_enum, path, handler, info); \
    if (result != 0) {                                                           \
      LOG_ERROR("Failed to add route: %s", path);                                \
      free_middleware_info(info);                                                \
    }                                                                            \
  }

ROUTE_REGISTER(ecewo_register_get, HTTP_GET) // NOLINT(clang-analyzer-valist.Uninitialized)
ROUTE_REGISTER(ecewo_register_post, HTTP_POST) // NOLINT(clang-analyzer-valist.Uninitialized)
ROUTE_REGISTER(ecewo_register_put, HTTP_PUT) // NOLINT(clang-analyzer-valist.Uninitialized)
ROUTE_REGISTER(ecewo_register_patch, HTTP_PATCH) // NOLINT(clang-analyzer-valist.Uninitialized)
ROUTE_REGISTER(ecewo_register_del, HTTP_DELETE) // NOLINT(clang-analyzer-valist.Uninitialized)
ROUTE_REGISTER(ecewo_register_headl, HTTP_HEAD) // NOLINT(clang-analyzer-valist.Uninitialized)
ROUTE_REGISTER(ecewo_register_options, HTTP_OPTIONS) // NOLINT(clang-analyzer-valist.Uninitialized)
