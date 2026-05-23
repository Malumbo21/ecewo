// MIT License

// Copyright (c) 2026 Savas Sahin <savashn@proton.me>

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

// ===============================================================================

// Streaming bodies that exceed the buffered cap (BUFFERED_BODY_MAX_SIZE, 1MB)
// or use chunked transfer-encoding must NOT be rejected by the buffered-path
// 413 guard. Regression for the dispatch guard keying off on_body_chunk (which
// is only set after the stream middleware runs) instead of has_stream_middleware.

#include "ecewo.h"
#include "ecewo-mock.h"
#include "tester.h"
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define SOCK_INVALID INVALID_SOCKET
#define sock_close(s) closesocket(s)
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <unistd.h>
typedef int sock_t;
#define SOCK_INVALID (-1)
#define sock_close(s) close(s)
#endif

typedef struct {
  int chunks;
  size_t bytes;
} Ctx;

static void on_chunk(ecewo_request_t *req, const uint8_t *data, size_t len) {
  Ctx *c = ecewo_context_get(req, "c");
  c->chunks++;
  c->bytes += len;
  (void)data;
}

static void on_end(ecewo_request_t *req, ecewo_response_t *res) {
  Ctx *c = ecewo_context_get(req, "c");
  char *b = ecewo_sprintf(ecewo_req_arena(req), "bytes=%zu", c->bytes);
  ecewo_send_text(res, ECEWO_OK, b);
}

static void handler(ecewo_request_t *req, ecewo_response_t *res) {
  Ctx *c = ecewo_alloc(ecewo_req_arena(req), sizeof(Ctx));
  memset(c, 0, sizeof(Ctx));
  ecewo_context_set(req, "c", c);
  ecewo_body_limit(req, 50UL * 1024UL * 1024UL); // 50MB, above the body sizes here
  ecewo_body_on_data(req, on_chunk);
  ecewo_body_on_end(req, res, on_end);
}

static void setup_routes(ecewo_app_t *app) {
  ECEWO_POST(app, "/stream", ecewo_body_stream, handler);
}

static int send_all(sock_t s, const char *buf, size_t len) {
  size_t off = 0;
  while (off < len) {
    ssize_t n = send(s, buf + off, (int)(len - off), 0);
    if (n <= 0)
      return -1;
    off += (size_t)n;
  }
  return 0;
}

static int read_status(sock_t s) {
  char resp[4096];
  memset(resp, 0, sizeof(resp));
  ssize_t total = 0;
  while (1) {
    ssize_t n = recv(s, resp + total, (int)(sizeof(resp) - 1 - (size_t)total), 0);
    if (n <= 0)
      break;
    total += n;
    if ((size_t)total >= sizeof(resp) - 1)
      break;
  }
  if (total <= 0)
    return -1;
  // Status code is the second space-delimited token of the status line.
  const char *sp = memchr(resp, ' ', (size_t)total);
  if (!sp)
    return -1;
  return (int)strtol(sp + 1, NULL, 10);
}

static sock_t connect_sock(void) {
  sock_t sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock == SOCK_INVALID)
    return SOCK_INVALID;
  int one = 1;
  setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof(one));
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(TEST_PORT);
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
    sock_close(sock);
    return SOCK_INVALID;
  }
  return sock;
}

// Chunked transfer-encoding to a streaming route must stream, not 413.
static int test_streaming_chunked(void) {
  sock_t s = connect_sock();
  ASSERT_TRUE(s != SOCK_INVALID);

  const char *req =
      "POST /stream HTTP/1.1\r\n"
      "Host: x\r\n"
      "Connection: close\r\n"
      "Transfer-Encoding: chunked\r\n"
      "\r\n"
      "5\r\nhello\r\n"
      "6\r\n world\r\n"
      "0\r\n\r\n";
  ASSERT_TRUE(send_all(s, req, strlen(req)) == 0);

  int code = read_status(s);
  sock_close(s);
  ASSERT_EQ(200, code);
  RETURN_OK();
}

// A body larger than BUFFERED_BODY_MAX_SIZE (1MB) on a streaming route must
// stream, not get rejected by the buffered-path 413 guard.
static int test_streaming_over_buffered_cap(void) {
  sock_t s = connect_sock();
  ASSERT_TRUE(s != SOCK_INVALID);

  size_t body_len = 2UL * 1024UL * 1024UL; // 2MB > 1MB buffered cap
  char hdr[256];
  int hl = snprintf(hdr, sizeof(hdr),
                    "POST /stream HTTP/1.1\r\n"
                    "Host: x\r\n"
                    "Connection: close\r\n"
                    "Content-Length: %zu\r\n"
                    "\r\n",
                    body_len);
  ASSERT_TRUE(send_all(s, hdr, (size_t)hl) == 0);

  char *body = malloc(body_len);
  ASSERT_TRUE(body != NULL);
  memset(body, 'a', body_len);
  int sent = send_all(s, body, body_len);
  free(body);
  ASSERT_TRUE(sent == 0);

  int code = read_status(s);
  sock_close(s);
  ASSERT_EQ(200, code);
  RETURN_OK();
}

int main(void) {
  mock_init(setup_routes);

  RUN_TEST(test_streaming_chunked);
  RUN_TEST(test_streaming_over_buffered_cap);

  mock_cleanup();
  return 0;
}
