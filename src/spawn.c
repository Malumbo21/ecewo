// Copyright 2025-2026 Savas Sahin <savashn@proton.me>

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

#include "uv.h"
#include "ecewo.h"
#include "logger.h"
#include "server.h"
#include <stdlib.h>

typedef enum {
  SPAWN_BACKGROUND,
  SPAWN_RESPONSE
} spawn_type_t;

typedef struct {
  uv_work_t work;
  uv_async_t async_send;
  void *context;
  ecewo_spawn_handler_t work_fn;
} spawn_base_t;

typedef struct {
  spawn_base_t base;
  spawn_type_t type;
  union {
    struct {
      ecewo_spawn_handler_t result_fn;
    } background;
    struct {
      ecewo_spawn_done_t done_fn;
      ecewo_response_t *res;
      ecewo_client_t *client;
    } response;
  };
} spawn_t;

static void spawn_cleanup_cb(uv_handle_t *handle) {
  spawn_t *task = (spawn_t *)handle->data;

  if (!task)
    return;

  if (task->type == SPAWN_RESPONSE) {
    if (task->response.client)
      ecewo_client_unref(task->response.client);
  }

  free(task);
}

static void spawn_async_cb(uv_async_t *handle) {
  spawn_t *task = (spawn_t *)handle->data;
  if (!task)
    return;

  switch (task->type) {
  case SPAWN_BACKGROUND: {
    if (task->background.result_fn)
      task->background.result_fn(task->base.context);

    break;
  }

  case SPAWN_RESPONSE: {
    ecewo_response_t *res = task->response.res;
    ecewo_client_t *client = task->response.client;

    if (!client)
      break;

    if (!client->valid || client->closing)
      break;

    if (uv_is_closing((uv_handle_t *)&client->handle))
      break;

    if (task->response.done_fn)
      task->response.done_fn(res, task->base.context);

    break;
  }

  default:
    break;
  }

  uv_close((uv_handle_t *)handle, spawn_cleanup_cb);
}

static void spawn_work_cb(uv_work_t *req) {
  spawn_t *task = (spawn_t *)req->data;
  if (task && task->base.work_fn)
    task->base.work_fn(task->base.context);
}

static void spawn_after_work_cb(uv_work_t *req, int status) {
  spawn_t *task = (spawn_t *)req->data;
  if (!task)
    return;

  if (status < 0)
    LOG_ERROR("Worker spawn execution failed");

  uv_async_send(&task->base.async_send);
}

static int spawn_internal(
    uv_loop_t *loop,
    spawn_type_t type,
    void *context,
    ecewo_spawn_handler_t work_fn,
    ecewo_spawn_handler_t result_fn,
    ecewo_spawn_done_t done_fn,
    ecewo_response_t *res,
    ecewo_client_t *client) {

  if (!loop || !work_fn)
    return -1;

  spawn_t *task = calloc(1, sizeof(spawn_t));
  if (!task)
    return -1;

  if (uv_async_init(loop, &task->base.async_send, spawn_async_cb) != 0) {
    free(task);
    return -1;
  }

  task->type = type;

  task->base.work.data = task;
  task->base.async_send.data = task;

  task->base.context = context;
  task->base.work_fn = work_fn;

  switch (type) {
  case SPAWN_BACKGROUND:
    task->background.result_fn = result_fn;
    break;

  case SPAWN_RESPONSE:
    task->response.done_fn = done_fn;
    task->response.res = res;
    task->response.client = client;

    if (client)
      ecewo_client_ref(client);

    break;
  }

  int result = uv_queue_work(
      loop,
      &task->base.work,
      spawn_work_cb,
      spawn_after_work_cb);

  if (result != 0) {
    uv_close((uv_handle_t *)&task->base.async_send, NULL);

    if (type == SPAWN_RESPONSE && client)
      ecewo_client_unref(client);

    free(task);

    return result;
  }

  return 0;
}

int ecewo_spawn(
    void *context,
    ecewo_spawn_handler_t work_fn,
    ecewo_spawn_handler_t done_fn) {

  uv_loop_t *loop = ecewo_get_loop();

  return spawn_internal(
      loop,
      SPAWN_BACKGROUND,
      context,
      work_fn,
      done_fn,
      NULL,
      NULL,
      NULL);
}

int ecewo_spawn_http(
    ecewo_response_t *res,
    void *context,
    ecewo_spawn_handler_t work_fn,
    ecewo_spawn_done_t done_fn) {

  if (!res)
    return -1;

  if (!res->ecewo__client_socket)
    return -1;

  uv_tcp_t *socket = (uv_tcp_t *)res->ecewo__client_socket;

  if (!socket->data)
    return -1;

  ecewo_client_t *client = socket->data;

  if (!client->srv || !client->srv->runtime)
    return -1;

  return spawn_internal(
      client->srv->runtime->loop,
      SPAWN_RESPONSE,
      context,
      work_fn,
      NULL,
      done_fn,
      res,
      client);
}
