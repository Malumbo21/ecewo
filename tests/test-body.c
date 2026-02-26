#include "ecewo.h"
#include "ecewo-mock.h"
#include "tester.h"

void handler_body(Req *req, Res *res) {
  char *response = arena_sprintf(req->arena, "received=%zu", req->body_len);
  send_text(res, 200, response);
}

int test_large_body(void) {
  // 1MB body
  size_t size = 1024 * 1024;
  char *large_body = malloc(size + 1);
  memset(large_body, 'A', size);
  large_body[size] = '\0';

  MockParams params = {
    .method = MOCK_POST,
    .path = "/large-body",
    .body = large_body
  };

  MockResponse res = request(&params);
  ASSERT_EQ(413, res.status_code);
  ASSERT_EQ_STR("Payload Too Large", res.body);

  free(large_body);
  free_request(&res);
  RETURN_OK();
}

int test_normal_body(void) {
  size_t size = 1024 * 1024 - 1;
  char *normal_body = malloc(size + 1);
  memset(normal_body, 'A', size);
  normal_body[size] = '\0';

  MockParams params = {
    .method = MOCK_POST,
    .path = "/normal-body",
    .body = normal_body
  };

  MockResponse res = request(&params);
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("received=1048575", res.body);

  free(normal_body);
  free_request(&res);
  RETURN_OK();
}

// STREAMING
static int chunks_received = 0;
static size_t total_bytes = 0;

bool on_chunk(Req *req, const char *data, size_t len) {
  (void)req;
  
  chunks_received++;
  total_bytes += len;
  
  char *log = arena_sprintf(req->arena, "Chunk %d: %zu bytes", chunks_received, len);
  set_context(req, "last_chunk", log);
  
  return true; // Continue receiving
}

void on_complete(Req *req, Res *res) {
  char *response = arena_sprintf(req->arena, 
    "Received %d chunks, total %zu bytes",
    chunks_received, total_bytes);

  printf("%s\n", response);
  
  send_text(res, OK, response);
}

void handler_streaming(Req *req, Res *res) {
  chunks_received = 0;
  total_bytes = 0;
  
  body_on_data(req, on_chunk);
  body_on_end(req, res, on_complete);
}

void handler_buffered(Req *req, Res *res) {
  // Body already available
  const char *body = body_bytes(req);
  size_t len = body_len(req);
  
  if (!body) {
    send_text(res, INTERNAL_SERVER_ERROR, "Body is NULL");
    return;
  }
  
  char *response = arena_sprintf(req->arena, 
    "Buffered: %zu bytes, body='%s'", len, body);
  
  send_text(res, OK, response);
}

void handler_no_middleware(Req *req, Res *res) {
  bool result = body_on_data(req, on_chunk);
  if (!result)
    send_text(res, BAD_REQUEST, "streaming_disabled");
  else
    send_text(res, OK, "streaming_enabled");
}

int test_no_middleware(void) {  
  MockParams params = { .method = MOCK_POST, .path = "/no-middleware", .body = "test" };
  MockResponse res = request(&params);
  
  ASSERT_EQ(400, res.status_code);
  ASSERT_EQ_STR("streaming_disabled", res.body);
  
  free_request(&res);
  RETURN_OK();
}

int test_streaming_mode(void) {
  MockParams params = {
    .method = MOCK_POST,
    .path = "/streaming",
    .body = "Hello from streaming test!"
  };
  
  MockResponse res = request(&params);
  
  ASSERT_EQ(200, res.status_code);
  // Should have received 1 chunk (mock sends all at once)
  ASSERT_EQ_STR("Received 1 chunks, total 26 bytes", res.body);
  
  free_request(&res);
  RETURN_OK();
}

int test_buffered_mode(void) {
  MockParams params = {
    .method = MOCK_POST,
    .path = "/buffered",
    .body = "Hello from buffered test!"
  };
  
  MockResponse res = request(&params);
  
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("Buffered: 25 bytes, body='Hello from buffered test!'", res.body);
  
  free_request(&res);
  RETURN_OK();
}

int test_streaming_vs_buffered_isolation(void) {
  // Streaming mode should not affect buffered mode
  test_streaming_mode();
  test_buffered_mode();
  
  RETURN_OK();
}

static void setup_routes(void) {
  post("/large-body", handler_body);
  post("/normal-body", handler_body);
  post("/streaming", body_stream, handler_streaming);
  post("/buffered", handler_buffered);
  post("/no-middleware", handler_no_middleware);
}

int main(void) {
  mock_init(setup_routes);

  RUN_TEST(test_large_body);
  RUN_TEST(test_normal_body);
  RUN_TEST(test_no_middleware);
  RUN_TEST(test_streaming_mode);
  RUN_TEST(test_buffered_mode);
  RUN_TEST(test_streaming_vs_buffered_isolation);

  mock_cleanup();
  return 0;
}
