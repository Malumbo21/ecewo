// MIT License

// Copyright (c) 2025-2026 Savas Sahin <savashn@proton.me>

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

// Wire-level regressions that MockParams/request() cannot express: they need
// control over how a request is split across TCP segments.

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

static void handler_admin(ecewo_request_t *req, ecewo_response_t *res) {
  (void)req;
  ecewo_send_text(res, ECEWO_OK, "SMUGGLED");
}

static void handler_echo_token(ecewo_request_t *req, ecewo_response_t *res) {
  const char *token = ecewo_header_get(req, "X-Token");
  char *body = ecewo_sprintf(ecewo_req_arena(req), "token=[%s]", token ? token : "null");
  ecewo_send_text(res, ECEWO_OK, body);
}

static sock_t connect_to_server(void) {
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

// Reads until the peer closes or the socket goes quiet.
static size_t drain(sock_t sock, char *buf, size_t cap) {
  struct timeval tv = { 0, 400000 };
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));

  size_t n = 0;
  while (n + 1 < cap) {
    ssize_t r = recv(sock, buf + n, (int)(cap - 1 - n), 0);
    if (r <= 0)
      break;
    n += (size_t)r;
  }
  buf[n] = '\0';
  return n;
}

static int count_responses(const char *buf) {
  int n = 0;
  for (const char *p = buf; (p = strstr(p, "HTTP/1.1 ")) != NULL; p++)
    n++;
  return n;
}

// A response produced before the request body was read (here: 404 for an
// unknown route) must close the connection. Otherwise the unread body is
// parsed as the next request on the same connection - request smuggling.
static int test_unconsumed_body_is_not_a_request(void) {
  sock_t sock = connect_to_server();
  ASSERT_TRUE(sock != SOCK_INVALID);

  // Body is exactly 40 bytes and is itself a valid HTTP request.
  const char *smuggled = "GET /smuggle-target HTTP/1.1\r\n\r\nAAAAAAAA";
  char headers[256];
  int headers_len = snprintf(headers, sizeof(headers),
                             "POST /no-such-route HTTP/1.1\r\n"
                             "Host: localhost:%d\r\n"
                             "Content-Length: %zu\r\n"
                             "\r\n",
                             TEST_PORT, strlen(smuggled));

  ASSERT_TRUE(send(sock, headers, headers_len, 0) == headers_len);
  usleep(80000); // let the 404 go out before the body arrives
  send(sock, smuggled, (int)strlen(smuggled), 0);

  char buf[8192];
  drain(sock, buf, sizeof(buf));
  sock_close(sock);

  ASSERT_EQ(1, count_responses(buf));
  ASSERT_NULL(strstr(buf, "SMUGGLED"));
  ASSERT_NOT_NULL(strstr(buf, "Connection: close"));
  RETURN_OK();
}

// llhttp hands a header name or value over in as many pieces as the TCP reads
// split it into. The pieces must be reassembled into one header, not truncated
// to the first fragment and not stored as several entries.
static int test_split_header_value(void) {
  sock_t sock = connect_to_server();
  ASSERT_TRUE(sock != SOCK_INVALID);

  const char *part1 = "GET /echo-token HTTP/1.1\r\nHost: x\r\nX-Token: AAAA";
  const char *part2 = "BBBB\r\nConnection: close\r\n\r\n";

  send(sock, part1, (int)strlen(part1), 0);
  usleep(80000);
  send(sock, part2, (int)strlen(part2), 0);

  char buf[8192];
  drain(sock, buf, sizeof(buf));
  sock_close(sock);

  ASSERT_NOT_NULL(strstr(buf, "token=[AAAABBBB]"));
  RETURN_OK();
}

static int test_split_header_name(void) {
  sock_t sock = connect_to_server();
  ASSERT_TRUE(sock != SOCK_INVALID);

  const char *part1 = "GET /echo-token HTTP/1.1\r\nHost: x\r\nX-To";
  const char *part2 = "ken: reassembled\r\nConnection: close\r\n\r\n";

  send(sock, part1, (int)strlen(part1), 0);
  usleep(80000);
  send(sock, part2, (int)strlen(part2), 0);

  char buf[8192];
  drain(sock, buf, sizeof(buf));
  sock_close(sock);

  ASSERT_NOT_NULL(strstr(buf, "token=[reassembled]"));
  RETURN_OK();
}

// A keep-alive request that is fully read and answered normally must still
// reuse the connection: the smuggling guard above must not close everything.
static int test_keep_alive_still_works(void) {
  sock_t sock = connect_to_server();
  ASSERT_TRUE(sock != SOCK_INVALID);

  char req[256];
  int len = snprintf(req, sizeof(req),
                     "POST /echo-token HTTP/1.1\r\n"
                     "Host: localhost:%d\r\n"
                     "X-Token: kept\r\n"
                     "Content-Length: 5\r\n"
                     "\r\n"
                     "hello",
                     TEST_PORT);

  for (int i = 0; i < 2; i++) {
    ASSERT_TRUE(send(sock, req, len, 0) == len);

    char buf[4096];
    size_t n = 0;
    struct timeval tv = { 0, 400000 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
    while (n + 1 < sizeof(buf)) {
      ssize_t r = recv(sock, buf + n, (int)(sizeof(buf) - 1 - n), 0);
      if (r <= 0)
        break;
      n += (size_t)r;
      buf[n] = '\0';
      if (strstr(buf, "token=[kept]"))
        break;
    }
    buf[n] = '\0';
    ASSERT_NOT_NULL(strstr(buf, "token=[kept]"));
    ASSERT_NOT_NULL(strstr(buf, "Connection: keep-alive"));
  }

  sock_close(sock);
  RETURN_OK();
}

static void setup_routes(ecewo_app_t *app) {
  ECEWO_GET(app, "/smuggle-target", handler_admin);
  ECEWO_GET(app, "/echo-token", handler_echo_token);
  ECEWO_POST(app, "/echo-token", handler_echo_token);
}

int main(void) {
  mock_init(setup_routes);
  RUN_TEST(test_unconsumed_body_is_not_a_request);
  RUN_TEST(test_split_header_value);
  RUN_TEST(test_split_header_name);
  RUN_TEST(test_keep_alive_still_works);
  mock_cleanup();
  return 0;
}
