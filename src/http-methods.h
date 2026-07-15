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

#include "ecewo.h" // ecewo_method_t (ECEWO_METHOD_*)
#include "llhttp.h" // llhttp_method_t (HTTP_*)

// Source of truth for the HTTP methods ecewo can route.
//
// Generated from this list:
//   - ECEWO__METHOD_COUNT and the order asserts below
//   - method_to_index() and the per-method route arrays (route-table.c)
//   - the 405 Allow-header names and buffer size        (router.c)
//   - the fuzzers' method arrays                        (fuzz/)
//
// Still updated by hand when a method is added:
//   - the public ecewo_method_t enum and its ECEWO_<METHOD> macro (include/ecewo.h)
//   - the ecewo-mock plugin's MOCK_* enum
//   - docs/02.defining-routes.md, docs/16.api-reference.md, README.md
//
// X is invoked as X(suffix, http_method):
//   suffix      -> token appended to ECEWO_METHOD_; #suffix is also the
//                  canonical method string for the Allow header
//   http_method -> the matching llhttp HTTP_* enumerator
//
// The order MUST match ecewo_method_t in include/ecewo.h; the asserts below
// fail to compile if the two ever drift.

#define ECEWO_METHOD_TABLE(X) \
  X(DELETE, HTTP_DELETE)      \
  X(GET, HTTP_GET)            \
  X(HEAD, HTTP_HEAD)          \
  X(POST, HTTP_POST)          \
  X(PUT, HTTP_PUT)            \
  X(OPTIONS, HTTP_OPTIONS)    \
  X(PATCH, HTTP_PATCH)        \
  X(QUERY, HTTP_QUERY)

// Table position of each entry; the trailing enumerator is the method count,
// so there is no hand-maintained number to forget.
enum {
#define X(suffix, http_method) ECEWO__METHOD_INDEX_##suffix,
  ECEWO_METHOD_TABLE(X)
#undef X
      ECEWO__METHOD_COUNT
};

// ecewo_method_t values double as indices into the per-method route arrays and
// as bit positions in the Allow-header bitmask, so every entry must sit at the
// same position as its public enum value.
#define X(suffix, http_method)                                                    \
  _Static_assert((int)ECEWO__METHOD_INDEX_##suffix == (int)ECEWO_METHOD_##suffix, \
                 #suffix " out of sync with ecewo_method_t (include/ecewo.h)");
ECEWO_METHOD_TABLE(X)
#undef X

#endif
