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

static void setup_routes(void) {
  post("/large-body", handler_body);
  post("/normal-body", handler_body);
}

int main(void) {
  mock_init(setup_routes);

  RUN_TEST(test_large_body);
  RUN_TEST(test_normal_body);

  mock_cleanup();
  return 0;
}
