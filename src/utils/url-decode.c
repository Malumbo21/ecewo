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

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

static int hex_digit(unsigned char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  return -1;
}

// Decodes percent-encoded characters in-place.
// plus_as_space=true: '+' -> ' '  (used for query strings, form encoding)
// plus_as_space=false: '+' is left as-is (used for path params, like decodeURIComponent)
void url_decode(char *str, bool plus_as_space) {
  if (!str)
    return;

  unsigned char *src = (unsigned char *)str;
  unsigned char *dst = (unsigned char *)str;

  while (*src) {
    if (*src == '%') {
      int hi = (src[1] != '\0') ? hex_digit(src[1]) : -1;
      int lo = (hi >= 0 && src[2] != '\0') ? hex_digit(src[2]) : -1;
      if (hi >= 0 && lo >= 0) {
        *dst++ = (unsigned char)((hi << 4) | lo);
        src += 3;
      } else {
        *dst++ = *src++;
      }
    } else if (plus_as_space && *src == '+') {
      *dst++ = ' ';
      src++;
    } else {
      *dst++ = *src++;
    }
  }

  *dst = '\0';
}
