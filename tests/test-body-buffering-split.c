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

// Buffered body (no body_stream middleware) that arrives across two separate
// TCP reads: headers on the first read, body on the second. The deferred
// handler must run exactly once with the full buffered body. Regression for the
// deferred-handler path in router() being run via the saved req rather than a
// second route dispatch.

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
#define usleep(us) Sleep((us) / 1000)
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

static int g_handler_calls = 0;

static void handler(ecewo_request_t *req, ecewo_response_t *res) {
  g_handler_calls++;
  char *body = ecewo_sprintf(ecewo_req_arena(req), "calls=%d,len=%zu,body=%s",
                             g_handler_calls,
                             ecewo_req_body_len(req),
                             ecewo_req_body(req) ? (const char *)ecewo_req_body(req) : "NULL");
  ecewo_send_text(res, ECEWO_OK, body);
}

static void setup_routes(ecewo_app_t *app) {
  ECEWO_POST(app, "/buffered-split", handler);
}

static int test_buffered_body_split_across_reads(void) {
  g_handler_calls = 0;

  const char *payload = "Hello, buffered split!"; // 22 bytes
  size_t payload_len = strlen(payload);

  char headers[512];
  int headers_len = snprintf(headers, sizeof(headers),
                             "POST /buffered-split HTTP/1.1\r\n"
                             "Host: localhost:%d\r\n"
                             "Connection: close\r\n"
                             "Content-Length: %zu\r\n"
                             "\r\n",
                             TEST_PORT, payload_len);

  sock_t sock = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_TRUE(sock != SOCK_INVALID);

  int one = 1;
  setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof(one));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(TEST_PORT);
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

  ASSERT_TRUE(connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0);

  // First write: headers only.
  ssize_t sent = send(sock, headers, (int)headers_len, 0);
  ASSERT_TRUE(sent == (ssize_t)headers_len);

  // Force the body to land in a separate on_read callback.
  usleep(50000);

  // Second write: body.
  sent = send(sock, payload, (int)payload_len, 0);
  ASSERT_TRUE(sent == (ssize_t)payload_len);

  char response[4096];
  memset(response, 0, sizeof(response));
  ssize_t total = 0;
  while (1) {
    ssize_t n = recv(sock, response + total,
                     (int)(sizeof(response) - 1 - (size_t)total), 0);
    if (n <= 0)
      break;
    total += n;
  }
  sock_close(sock);

  ASSERT_TRUE(total > 0);
  ASSERT_TRUE(strstr(response, "200") != NULL);
  ASSERT_TRUE(strstr(response, "calls=1") != NULL);
  ASSERT_TRUE(strstr(response, "len=22") != NULL);
  ASSERT_TRUE(strstr(response, "Hello, buffered split!") != NULL);
  ASSERT_EQ(1, g_handler_calls);

  RETURN_OK();
}

int main(void) {
  mock_init(setup_routes);

  RUN_TEST(test_buffered_body_split_across_reads);

  mock_cleanup();
  return 0;
}
