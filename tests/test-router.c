#include "ecewo.h"
#include "ecewo-mock.h"
#include "tester.h"

void handler(Req *req, Res *res) {
  send_text(res, OK, req->path);
}

static int root_test(void) {
  MockParams params = {
    .method = MOCK_GET,
    .path = "/",
  };

  MockResponse res = request(&params);
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("/", res.body);
  free_request(&res);

  RETURN_OK();
}

static int double_slashes_test(void) {
  // Request path "//" is normalized to root "/"
  // So it should match the "/" route
  MockParams params = {
    .method = MOCK_GET,
    .path = "//",
  };

  MockResponse res = request(&params);
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("//", res.body);
  free_request(&res);

  RETURN_OK();
}

static int param_test(void) {
  MockParams params = {
    .method = MOCK_GET,
    .path = "/users/123",
  };

  MockResponse res = request(&params);
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("/users/123", res.body);
  free_request(&res);

  RETURN_OK();
}

static int double_slash_param_test(void) {
  // Request "//users//123" tokenizes to ["users", "123"]
  // So it matches "/users/:id" route
  MockParams params = {
    .method = MOCK_GET,
    .path = "//users//123",
  };

  MockResponse res = request(&params);
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("//users//123", res.body);
  free_request(&res);

  RETURN_OK();
}

static int last_segment_test(void) {
  MockParams params = {
    .method = MOCK_GET,
    .path = "/users/123/",
  };

  MockResponse res = request(&params);
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("/users/123/", res.body);
  free_request(&res);

  RETURN_OK();
}

static int wildcard_test(void) {
  MockParams params = {
    .method = MOCK_GET,
    .path = "/files/anything/here",
  };

  MockResponse res = request(&params);
  ASSERT_EQ(200, res.status_code);
  ASSERT_EQ_STR("/files/anything/here", res.body);
  free_request(&res);

  RETURN_OK();
}

static void setup_routes(void) {
  get("/", handler);
  get("/users/:id", handler);
  get("/files/*", handler);
}

int main(void) {
  mock_init(setup_routes);

  RUN_TEST(root_test);
  RUN_TEST(double_slashes_test);
  RUN_TEST(param_test);
  RUN_TEST(double_slash_param_test);
  RUN_TEST(last_segment_test);
  RUN_TEST(wildcard_test);

  mock_cleanup();
  return 0;
}