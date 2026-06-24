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

#ifndef ECEWO_HTTP_METHODS_H
#define ECEWO_HTTP_METHODS_H

#include "ecewo.h"  // ecewo_method_t (ECEWO_METHOD_*)
#include "llhttp.h" // llhttp_method_t (HTTP_*)

// Single source of truth for the HTTP methods ecewo can route.
//
// Every per-method table in the codebase is generated from this list, so a new
// method is added in exactly one place:
//
//   - the public ecewo_method_t enum                (include/ecewo.h)
//   - to_llhttp_method()                            (route-register.c)
//   - METHOD_INDEX_* / METHOD_COUNT / method_to_index() (route-table.c)
//   - the 405 Allow-header method_names[]           (router.c)
//
// X is invoked as X(suffix, http_method, name):
//   suffix      -> token appended to ECEWO_METHOD_ and METHOD_INDEX_
//   http_method -> the matching llhttp HTTP_* enumerator
//   name        -> the canonical method string (for the Allow header)
//
// The order MUST match ecewo_method_t in include/ecewo.h; route-table.c holds a
// static_assert per entry that fails to compile if the two ever drift.

#define ECEWO_METHOD_TABLE(X)         \
  X(DELETE, HTTP_DELETE, "DELETE")    \
  X(GET, HTTP_GET, "GET")             \
  X(HEAD, HTTP_HEAD, "HEAD")          \
  X(POST, HTTP_POST, "POST")          \
  X(PUT, HTTP_PUT, "PUT")             \
  X(OPTIONS, HTTP_OPTIONS, "OPTIONS") \
  X(PATCH, HTTP_PATCH, "PATCH")       \
  X(QUERY, HTTP_QUERY, "QUERY")

#endif
