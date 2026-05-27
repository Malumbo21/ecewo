#include "hello.h"

static void log_mw(ecewo_request_t *req, ecewo_response_t *res, ecewo_next_t next) {
  (void)res;
  /* Pass a per-request value from middleware to the handler. */
  ecewo_context_set(req, "greeting", "Hello from the ecewo plugin template!");
  next(req, res);
}

static void hello_handler(ecewo_request_t *req, ecewo_response_t *res) {
  const char *msg = ecewo_context_get(req, "greeting");
  ecewo_send_text(res, ECEWO_OK, msg ? msg : "Hello!");
}

void hello_register(ecewo_app_t *app) {
  ecewo_route_t *r = ecewo_route_new(app, ECEWO_METHOD_GET, "/hello");
  ecewo_route_middleware(r, log_mw);
  ecewo_route_handler(r, hello_handler);
}
