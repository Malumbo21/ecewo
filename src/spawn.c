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

typedef struct {
  uv_work_t work;
  uv_async_t async_send;
  void *context;
  ecewo_spawn_handler_t work_fn;
  ecewo_spawn_done_t done_fn;
  ecewo_response_t *res;
  ecewo_client_t *client;
} spawn_t;

static void spawn_cleanup_cb(uv_handle_t *handle) {
  spawn_t *task = (spawn_t *)handle->data;
  if (!task)
    return;

  if (task->client)
    ecewo_client_unref(task->client);

  free(task);
}

static void spawn_async_cb(uv_async_t *handle) {
  spawn_t *task = (spawn_t *)handle->data;
  if (!task)
    return;

  int client_ok = !task->client || (task->client->valid && !task->client->closing && !uv_is_closing((uv_handle_t *)&task->client->handle));

  if (client_ok && task->done_fn)
    task->done_fn(task->res, task->context);

  uv_close((uv_handle_t *)handle, spawn_cleanup_cb);
}

static void spawn_work_cb(uv_work_t *req) {
  spawn_t *task = (spawn_t *)req->data;
  if (task && task->work_fn)
    task->work_fn(task->context);
}

static void spawn_after_work_cb(uv_work_t *req, int status) {
  spawn_t *task = (spawn_t *)req->data;
  if (!task)
    return;

  if (status < 0)
    LOG_ERROR("Worker spawn execution failed");

  uv_async_send(&task->async_send);
}

static int spawn_internal(
    uv_loop_t *loop,
    void *context,
    ecewo_spawn_handler_t work_fn,
    ecewo_spawn_done_t done_fn,
    ecewo_response_t *res,
    ecewo_client_t *client) {

  if (!loop || !work_fn)
    return -1;

  spawn_t *task = calloc(1, sizeof(spawn_t));
  if (!task)
    return -1;

  if (uv_async_init(loop, &task->async_send, spawn_async_cb) != 0) {
    free(task);
    return -1;
  }

  task->work.data = task;
  task->async_send.data = task;
  task->context = context;
  task->work_fn = work_fn;
  task->done_fn = done_fn;
  task->res = res;
  task->client = client;

  if (client)
    ecewo_client_ref(client);

  int result = uv_queue_work(
      loop,
      &task->work,
      spawn_work_cb,
      spawn_after_work_cb);

  if (result != 0) {
    uv_close((uv_handle_t *)&task->async_send, NULL);

    if (client)
      ecewo_client_unref(client);

    free(task);

    return result;
  }

  return 0;
}

int ecewo_spawn(ecewo_response_t *res, void *context, ecewo_spawn_handler_t work_fn, ecewo_spawn_done_t done_fn) {
  if (!res) {
    uv_loop_t *loop = ecewo_get_loop();
    return spawn_internal(loop, context, work_fn, done_fn, NULL, NULL);
  }

  if (!res->ecewo__client_socket)
    return -1;

  uv_tcp_t *socket = (uv_tcp_t *)res->ecewo__client_socket;

  if (!socket->data)
    return -1;

  ecewo_client_t *client = socket->data;

  if (!client->srv || !client->srv->runtime)
    return -1;

  return spawn_internal(client->srv->runtime->loop, context, work_fn, done_fn, res, client);
}
