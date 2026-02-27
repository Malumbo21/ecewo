#include "ecewo.h"
#include "uv.h"
#include "http.h"
#include "arena.h"
#include "server.h"
#include "logger.h"
#include "body.h"
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

#ifndef BODY_MAX_SIZE
#define BODY_MAX_SIZE (10UL * 1024UL * 1024UL) /* 10MB */
#endif

typedef struct {
  Req *req;
  Res *res;
  client_t *client;
  BodyDataCb on_data;
  BodyEndCb on_end;
  size_t max_size;
  size_t bytes_received;
  bool streaming_enabled;
  bool completed;
  bool errored;
} StreamCtx;

static StreamCtx *get_ctx(Req *req) {
  return (StreamCtx *)get_context(req, "_body_stream");
}

static StreamCtx *get_or_create_ctx(Req *req) {
  if (!req || !req->arena)
    return NULL;

  StreamCtx *ctx = get_ctx(req);
  if (ctx)
    return ctx;

  ctx = arena_alloc(req->arena, sizeof(StreamCtx));
  if (!ctx)
    return NULL;

  memset(ctx, 0, sizeof(StreamCtx));
  ctx->req = req;
  ctx->max_size = BODY_MAX_SIZE;

  if (req->client_socket)
    ctx->client = (client_t *)req->client_socket->data;

  set_context(req, "_body_stream", ctx);
  return ctx;
}

// body_chunk_cb_t implementation (called from on_body_cb in http.c)
// This is the function pointer stored in http_context_t->on_body_chunk
// It receives raw chunks as they arrive from the parser.
static int stream_on_chunk(void *udata, const char *data, size_t len) {
  StreamCtx *ctx = (StreamCtx *)udata;
  if (!ctx || !data || len == 0)
    return BODY_CHUNK_CONTINUE;

  if (ctx->max_size > 0 && ctx->bytes_received + len > ctx->max_size)
    return BODY_CHUNK_ERROR;

  ctx->bytes_received += len;

  if (ctx->on_data)
    ctx->on_data(ctx->req, data, len);

  return BODY_CHUNK_CONTINUE;
}

void body_stream(Req *req, Res *res, Next next) {
  if (!req || !res || !next)
    return;

  StreamCtx *ctx = get_or_create_ctx(req);
  if (!ctx) {
    send_text(res, INTERNAL_SERVER_ERROR, "Internal server error");
    return;
  }

  ctx->streaming_enabled = true;

  // Wire the chunk callback into the http context so on_body_cb
  // forwards chunks here instead of buffering them.
  if (ctx->client && ctx->client->parser_initialized) {
    http_context_t *hctx = &ctx->client->persistent_context;
    hctx->on_body_chunk = stream_on_chunk;
    hctx->stream_udata = ctx;
  }

  next(req, res);
}

void body_on_data(Req *req, BodyDataCb callback) {
  if (!req || !callback)
    return;

  StreamCtx *ctx = get_or_create_ctx(req);
  if (!ctx)
    return;

  if (!ctx->streaming_enabled) {
    LOG_ERROR("body_on_data requires body_stream middleware");
    return;
  }

  ctx->on_data = callback;
}

void body_on_end(Req *req, Res *res, BodyEndCb callback) {
  if (!req || !res || !callback)
    return;

  StreamCtx *ctx = get_or_create_ctx(req);
  if (!ctx)
    return;

  ctx->res = res;
  ctx->on_end = callback;

  // In buffered mode body_on_data already marked completed
  if (ctx->completed)
    callback(req, res);
}

size_t body_limit(Req *req, size_t max_size) {
  if (!req)
    return 0;

  StreamCtx *ctx = get_or_create_ctx(req);
  if (!ctx)
    return 0;

  size_t prev = ctx->max_size;
  ctx->max_size = max_size == 0 ? BODY_MAX_SIZE : max_size;
  return prev;
}

const char *body_bytes(const Req *req) {
  if (!req)
    return NULL;
  StreamCtx *ctx = get_ctx((Req *)req);
  if (ctx && ctx->streaming_enabled)
    return NULL;
  return req->body;
}

size_t body_len(const Req *req) {
  if (!req)
    return 0;
  StreamCtx *ctx = get_ctx((Req *)req);
  if (ctx && ctx->streaming_enabled)
    return 0;
  return req->body_len;
}

// Called by router.c after full message received in streaming mode
void body_stream_complete(Req *req) {
  if (!req)
    return;

  StreamCtx *ctx = get_ctx(req);
  if (!ctx || ctx->completed)
    return;

  ctx->completed = true;

  if (ctx->on_end)
    ctx->on_end(req, ctx->res);
}
