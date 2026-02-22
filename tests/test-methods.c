#include "ecewo.h"
#include "ecewo-mock.h"
#include "tester.h"
#include <string.h>

void handler_body(Req *req, Res *res) {
  const char *body_str = req->body ? req->body : "0";

  char *response = arena_sprintf(req->arena,
                                 "len=%zu, body=%s, method=%s",
                                 req->body_len,
                                 body_str,
                                 req->method);

  send_text(res, 200, response);
}

int test_method_get(void) {
  MockParams params = { .method = MOCK_GET, .path = "/method" };
  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("len=0, body=0, method=GET", res.body);

  free_request(&res);
  RETURN_OK();
}

int test_method_post(void) {
  MockParams params = {
    .method = MOCK_POST,
    .path = "/method",
    .body = "{\"test\":true}"
  };

  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("len=13, body={\"test\":true}, method=POST", res.body);

  free_request(&res);
  RETURN_OK();
}

int test_method_put(void) {
  MockParams params = {
    .method = MOCK_PUT,
    .path = "/method",
    .body = "{\"test\":true}"
  };

  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("len=13, body={\"test\":true}, method=PUT", res.body);

  free_request(&res);
  RETURN_OK();
}

int test_method_delete(void) {
  MockParams params = {
    .method = MOCK_DELETE,
    .path = "/method"
  };

  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("len=0, body=0, method=DELETE", res.body);

  free_request(&res);
  RETURN_OK();
}

int test_method_patch(void) {
  MockParams params = {
    .method = MOCK_PATCH,
    .path = "/method",
    .body = "{\"test\":true}"
  };

  MockResponse res = request(&params);

  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("len=13, body={\"test\":true}, method=PATCH", res.body);

  free_request(&res);
  RETURN_OK();
}

static void setup_routes(void) {
  get("/method", handler_body);
  post("/method", handler_body);
  put("/method", handler_body);
  del("/method", handler_body);
  patch("/method", handler_body);
}

int main(void) {
  mock_init(setup_routes);
  RUN_TEST(test_method_get);
  RUN_TEST(test_method_post);
  RUN_TEST(test_method_put);
  RUN_TEST(test_method_delete);
  RUN_TEST(test_method_patch);
  mock_cleanup();
  return 0;
}
