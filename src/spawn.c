#include "uv.h"
#include "ecewo.h"
#include "logger.h"
#include "server.h"
#include <stdlib.h>

// ============= SPAWN (Background work) =============

typedef struct {
  uv_work_t work;
  uv_async_t async_send;
  void *context;
  spawn_handler_t work_fn;
  spawn_handler_t result_fn;
} spawn_t;

static void spawn_cleanup_cb(uv_handle_t *handle) {
  spawn_t *t = (spawn_t *)handle->data;
  if (t)
    free(t);

  decrement_async_work();
}

static void spawn_async_cb(uv_async_t *handle) {
  spawn_t *t = (spawn_t *)handle->data;
  if (!t)
    return;

  if (t->result_fn)
    t->result_fn(t->context);

  uv_close((uv_handle_t *)handle, spawn_cleanup_cb);
}

static void spawn_work_cb(uv_work_t *req) {
  spawn_t *t = (spawn_t *)req->data;
  if (t && t->work_fn)
    t->work_fn(t->context);
}

static void spawn_after_work_cb(uv_work_t *req, int status) {
  spawn_t *t = (spawn_t *)req->data;
  if (!t)
    return;

  if (status < 0)
    LOG_ERROR("Spawn execution failed");

  uv_async_send(&t->async_send);
}

int spawn(void *context, spawn_handler_t work_fn, spawn_handler_t done_fn) {
  if (!work_fn)
    return -1;

  spawn_t *task = calloc(1, sizeof(spawn_t));
  if (!task)
    return -1;

  if (uv_async_init(get_loop(), &task->async_send, spawn_async_cb) != 0) {
    free(task);
    return -1;
  }

  task->work.data = task;
  task->async_send.data = task;
  task->context = context;
  task->work_fn = work_fn;
  task->result_fn = done_fn;

  increment_async_work();

  int result = uv_queue_work(
      get_loop(),
      &task->work,
      spawn_work_cb,
      spawn_after_work_cb);

  if (result != 0) {
    decrement_async_work();
    uv_close((uv_handle_t *)&task->async_send, NULL);
    free(task);
    return result;
  }

  return 0;
}

// ============= SPAWN_HTTP (With response) =============

typedef struct {
  uv_work_t work;
  uv_async_t async_send;
  void *context;
  spawn_handler_t work_fn;
  spawn_done_t done_fn;
  Res *res;
  client_t *client;
} spawn_http_t;

static void spawn_http_cleanup_cb(uv_handle_t *handle) {
  spawn_http_t *t = (spawn_http_t *)handle->data;

  if (t) {
    if (t->client)
      client_unref(t->client);
    free(t);
  }

  decrement_async_work();
}

static void spawn_http_async_cb(uv_async_t *handle) {
  spawn_http_t *t = (spawn_http_t *)handle->data;

  if (!t)
    return;

  Res *res = t->res;
  client_t *client = t->client;

  if (!client) {
    uv_close((uv_handle_t *)handle, spawn_http_cleanup_cb);
    return;
  }

  if (!client->valid || client->closing) {
    uv_close((uv_handle_t *)handle, spawn_http_cleanup_cb);
    return;
  }

  if (uv_is_closing((uv_handle_t *)&client->handle)) {
    uv_close((uv_handle_t *)handle, spawn_http_cleanup_cb);
    return;
  }

  if (t->done_fn)
    t->done_fn(res, t->context);

  uv_close((uv_handle_t *)handle, spawn_http_cleanup_cb);
}

static void spawn_http_work_cb(uv_work_t *req) {
  spawn_http_t *t = (spawn_http_t *)req->data;
  if (t && t->work_fn)
    t->work_fn(t->context);
}

static void spawn_http_after_work_cb(uv_work_t *req, int status) {
  spawn_http_t *t = (spawn_http_t *)req->data;
  if (!t)
    return;

  if (status < 0)
    LOG_ERROR("Async spawn execution failed");

  uv_async_send(&t->async_send);
}

int spawn_http(Res *res, void *context, spawn_handler_t work_fn, spawn_done_t done_fn) {

  if (!res || !work_fn)
    return -1;

  if (!res->client_socket || !res->client_socket->data)
    return -1;

  spawn_http_t *task = calloc(1, sizeof(spawn_http_t));
  if (!task)
    return -1;

  if (uv_async_init(get_loop(), &task->async_send, spawn_http_async_cb) != 0) {
    free(task);
    return -1;
  }

  task->work.data = task;
  task->async_send.data = task;
  task->context = context;
  task->work_fn = work_fn;
  task->done_fn = done_fn;

  task->res = res;
  task->client = NULL;
  if (res && res->client_socket && res->client_socket->data) {
    task->client = (client_t *)res->client_socket->data;
    client_ref(task->client);
  }

  increment_async_work();

  int result = uv_queue_work(
      get_loop(),
      &task->work,
      spawn_http_work_cb,
      spawn_http_after_work_cb);

  if (result != 0) {
    decrement_async_work();
    uv_close((uv_handle_t *)&task->async_send, NULL);
    if (task->client)
      client_unref(task->client);
    free(task);
    return result;
  }

  return 0;
}
