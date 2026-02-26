#include "router.h"
#include "route-trie.h"
#include "middleware.h"
#include "server.h"
#include "arena.h"
#include "utils.h"
#include "request.h"
#include "body.h"
#include "logger.h"
#include <stdlib.h> // for strtol

extern void send_error(Arena *request_arena, uv_tcp_t *client_socket, int error_code);

// Extracts URL parameters from a previously matched route
// Example: From route /users/:id matched with /users/123, extracts parameter id=123
static int extract_url_params(Arena *arena, const route_match_t *match, request_t *url_params) {
  if (!arena || !match || !url_params)
    return -1;

  if (match->param_count == 0)
    return 0;

  url_params->capacity = match->param_count;
  url_params->count = match->param_count;
  url_params->items = arena_alloc(arena, sizeof(request_item_t) * url_params->capacity);
  if (!url_params->items) {
    url_params->capacity = 0;
    url_params->count = 0;
    return -1;
  }

  const param_match_t *source = match->params ? match->params : match->inline_params;

  for (uint8_t i = 0; i < match->param_count; i++) {
    const string_view_t *key_sv = &source[i].key;
    const string_view_t *value_sv = &source[i].value;

    char *key = arena_alloc(arena, key_sv->len + 1);
    if (!key)
      return -1;
    arena_memcpy(key, key_sv->data, key_sv->len);
    key[key_sv->len] = '\0';

    char *value = arena_alloc(arena, value_sv->len + 1);
    if (!value)
      return -1;
    arena_memcpy(value, value_sv->data, value_sv->len);
    value[value_sv->len] = '\0';

    url_params->items[i].key = key;
    url_params->items[i].value = value;
  }

  return 0;
}

static Req *create_req(Arena *request_arena, uv_tcp_t *client_socket) {
  if (!request_arena)
    return NULL;

  Req *req = arena_alloc(request_arena, sizeof(Req));
  if (!req)
    return NULL;

  memset(req, 0, sizeof(Req));
  req->arena = request_arena;
  req->client_socket = client_socket;
  req->is_head_request = false;
  req->ctx = NULL;

  // TODO: Something like body_ctx_init_fn might be useful
  // for implementing hooks like fastify has
  // but I'm not sure about that
  // so I'll just leave a comment line
  // body_ctx_init(req);

  return req;
}

static Res *create_res(Arena *request_arena, uv_tcp_t *client_socket) {
  if (!request_arena)
    return NULL;

  Res *res = arena_alloc(request_arena, sizeof(Res));
  if (!res)
    return NULL;

  memset(res, 0, sizeof(Res));
  res->arena = request_arena;
  res->client_socket = client_socket;
  res->status = 200;
  res->content_type = arena_strdup(request_arena, "text/plain");
  res->keep_alive = 1;
  res->is_head_request = false;

  return res;
}

static int populate_req_from_context(Req *req, http_context_t *ctx, const char *path, size_t path_len) {
  if (!req || !ctx)
    return -1;

  Arena *arena = req->arena;

  if (ctx->method && ctx->method_length > 0) {
    req->method = arena_alloc(arena, ctx->method_length + 1);
    if (!req->method)
      return -1;
    arena_memcpy(req->method, ctx->method, ctx->method_length);
    req->method[ctx->method_length] = '\0';

    req->is_head_request = (ctx->method_length == 4 && memcmp(ctx->method, "HEAD", 4) == 0);
  }

  if (path && path_len > 0) {
    req->path = arena_alloc(arena, path_len + 1);
    if (!req->path)
      return -1;
    arena_memcpy(req->path, path, path_len);
    req->path[path_len] = '\0';
  }

  // Body only available in buffered mode at this point
  // In streaming mode it arrives via on_body_chunk callbacks after resume
  if (ctx->body && ctx->body_length > 0) {
    req->body = arena_alloc(arena, ctx->body_length + 1);
    if (!req->body)
      return -1;
    arena_memcpy(req->body, ctx->body, ctx->body_length);
    req->body[ctx->body_length] = '\0';
    req->body_len = ctx->body_length;
  }

  req->http_major = ctx->http_major;
  req->http_minor = ctx->http_minor;

  req->headers = ctx->headers;
  req->query = ctx->query_params;

  return 0;
}

// Empty handler for running global middleware only
// it is needed for OPTIONS preflight CORS
static void noop_route_handler(Req *req, Res *res) {
  (void)req;
  (void)res;
}

// Matches a route and invokes the handler/middleware chain.
// Returns 0 on success, -1 if a fatal error occurred (500 already sent).
// req_out / res_out are set on success so the caller can inspect res->replied.
static int dispatch(Arena *arena, uv_tcp_t *handle, http_context_t *ctx, client_t *client, const char *path, size_t path_len, Req **req_out, Res **res_out) {
  Req *req = create_req(arena, handle);
  Res *res = create_res(arena, handle);
  if (!req || !res) {
    send_error(arena, handle, 500);
    return -1;
  }

  res->keep_alive = ctx->keep_alive;
  res->is_head_request = (ctx->method_length == 4 && memcmp(ctx->method, "HEAD", 4) == 0);

  if (populate_req_from_context(req, ctx, path, path_len) != 0) {
    send_error(arena, handle, 500);
    return -1;
  }
  req->is_head_request = res->is_head_request;

  if (req_out)
    *req_out = req;
  if (res_out)
    *res_out = res;

  if (!global_route_trie || !ctx->method) {
    set_header(res, "Content-Type", "text/plain");
    reply(res, 404, "404 Not Found", 13);
    return 0;
  }

  tokenized_path_t tok = { 0 };
  if (tokenize_path(arena, path, path_len, &tok) != 0) {
    send_error(arena, handle, 500);
    return -1;
  }

  route_match_t match;
  if (!route_trie_match(global_route_trie, ctx->parser, &tok, &match, arena)) {
    // OPTIONS preflight: give global middleware a chance (e.g. CORS)
    if (ctx->method_length == 7 && memcmp(ctx->method, "OPTIONS", 7) == 0) {
      MiddlewareInfo dummy = { NULL, 0, noop_route_handler };
      chain_start(req, res, &dummy);
      if (res->replied)
        return 0;
    }
    set_header(res, "Content-Type", "text/plain");
    reply(res, 404, "404 Not Found", 13);
    return 0;
  }

  if (extract_url_params(arena, &match, &req->params) != 0) {
    send_error(arena, handle, 500);
    return -1;
  }

  if (!match.handler) {
    send_error(arena, handle, 500);
    return -1;
  }

  MiddlewareInfo *mw = (MiddlewareInfo *)match.middleware_ctx;

  bool has_stream_middleware = false;
  if (mw) {
    for (uint16_t i = 0; i < mw->middleware_count; i++) {
      if ((void *)mw->middleware[i] == (void *)body_stream) {
        has_stream_middleware = true;
        break;
      }
    }
  }
  if (!has_stream_middleware) {
    for (uint16_t i = 0; i < global_middleware_count; i++) {
      if ((void *)global_middleware[i] == (void *)body_stream) {
        has_stream_middleware = true;
        break;
      }
    }
  }

  bool is_chunked = false;
  bool has_body = false;
  long content_length = 0;

  for (uint16_t i = 0; i < ctx->headers.count; i++) {
    const char *k = ctx->headers.items[i].key;
    const char *v = ctx->headers.items[i].value;

    if (strcasecmp(k, "Content-Length") == 0 && strcmp(v, "0") != 0) {
      has_body = true;
      char *endptr;
      content_length = strtol(v, &endptr, 10);
      if (endptr == v || *endptr != '\0')
        content_length = 0;
    }

    if (strcasecmp(k, "Transfer-Encoding") == 0) {
      has_body = true;
      is_chunked = true;
    }
  }

  // Deny large body if no streaming middleware
  if (!ctx->on_body_chunk && has_body && (content_length >= (long)BUFFERED_BODY_MAX_SIZE || is_chunked)) {
    set_header(res, "Content-Type", "text/plain");
    res->keep_alive = false;
    reply(res, 413, "Payload Too Large", 17);
    return 0;
  }

  if (!has_stream_middleware && has_body && !ctx->message_complete) {
    if (client) {
      client->pending_handler = match.handler;
      client->pending_mw = (void *)mw;
      client->pending_req = req;
      client->pending_res = res;
      client->handler_pending = true;
    }
    return 0;
  }

  if (!has_stream_middleware && ctx->body_length > 0) {
    req->body = ctx->body;
    req->body_len = ctx->body_length;
  }

  if (mw)
    chain_start(req, res, mw);
  else
    match.handler(req, res);

  return 0;
}

int router(client_t *client, const char *request_data, size_t request_len) {
  if (!client || !request_data || request_len == 0) {
    if (client)
      send_error(NULL, (uv_tcp_t *)&client->handle, 400);
    return REQUEST_CLOSE;
  }

  uv_tcp_t *handle = (uv_tcp_t *)&client->handle;
  http_context_t *ctx = &client->persistent_context;
  Arena *arena = client->connection_arena;

  if (uv_is_closing((uv_handle_t *)handle))
    return REQUEST_CLOSE;

  // Feed data
  parse_result_t result = http_parse_request(ctx, request_data, request_len);

  // Headers complete, invoke handler
  if (result == PARSE_PAUSED) {
    if (!arena) {
      send_error(NULL, handle, 500);
      return REQUEST_CLOSE;
    }

    const char *path = ctx->url;
    size_t path_len = ctx->path_length;
    if (!path || path_len == 0) {
      path = "/";
      path_len = 1;
    }

    Req *req = NULL;
    Res *res = NULL;

    if (dispatch(arena, handle, ctx, client, path, path_len, &req, &res) != 0)
      return REQUEST_CLOSE;

    // Calculate remaining bytes before touching the parser
    // llhttp_get_error_pos() points to where the pause happened
    // Everything after that has not been parsed yet
    const char *pause_pos = llhttp_get_error_pos(ctx->parser);
    size_t consumed = pause_pos ? (size_t)(pause_pos - request_data) : request_len;
    size_t left = request_len > consumed ? request_len - consumed : 0;

    llhttp_resume(ctx->parser);

    if (res && res->replied)
      return res->keep_alive ? REQUEST_KEEP_ALIVE : REQUEST_CLOSE;

    // Handler wants the body, feed remaining bytes now
    if (left > 0)
      result = http_parse_request(ctx, pause_pos, left);
    else
      result = ctx->message_complete ? PARSE_SUCCESS : PARSE_INCOMPLETE;

    // Result after resume
    switch (result) {
    case PARSE_SUCCESS:
      if (ctx->on_body_chunk && req) {
        body_stream_complete(req);
      } else if (client->handler_pending) {
        client->handler_pending = false;
        Req *preq = client->pending_req;
        Res *pres = client->pending_res;
        if (preq && pres) {
          preq->body = ctx->body_length > 0 ? ctx->body : NULL;
          preq->body_len = ctx->body_length;
          MiddlewareInfo *pmw = (MiddlewareInfo *)client->pending_mw;
          if (pmw)
            chain_start(preq, pres, pmw);
          else if (client->pending_handler)
            client->pending_handler(preq, pres);
        }
      }
      {
        Res *final_res = (client->pending_res && client->pending_res->replied)
            ? client->pending_res
            : res;
        if (final_res && !final_res->replied)
          return REQUEST_PENDING;
        return final_res && final_res->keep_alive ? REQUEST_KEEP_ALIVE : REQUEST_CLOSE;
      }

    case PARSE_INCOMPLETE:
      // Body still arriving over the network, wait for more data
      return REQUEST_PENDING;

    case PARSE_OVERFLOW:
      LOG_ERROR("Body too large: %s", ctx->error_reason ? ctx->error_reason : "");
      send_error(arena, handle, 413);
      return REQUEST_CLOSE;

    case PARSE_PAUSED:
    case PARSE_ERROR:
    default:
      LOG_ERROR("Parse error after resume: %s",
                ctx->error_reason ? ctx->error_reason : "unknown");
      send_error(arena, handle, 400);
      return REQUEST_CLOSE;
    }
  }

  // Fallthrough: result before headers-complete
  switch (result) {
  case PARSE_INCOMPLETE:
    return REQUEST_PENDING;

  case PARSE_OVERFLOW:
    LOG_ERROR("Request too large: %s", ctx->error_reason ? ctx->error_reason : "");
    send_error(NULL, handle, 413);
    return REQUEST_CLOSE;

  case PARSE_ERROR:
    LOG_ERROR("Parse error: %s", ctx->error_reason ? ctx->error_reason : "unknown");
    send_error(NULL, handle, 400);
    return REQUEST_CLOSE;

  case PARSE_SUCCESS:
    break; // EOF-terminated request completed without pausing

  default:
    send_error(NULL, handle, 400);
    return REQUEST_CLOSE;
  }

  // PARSE_SUCCESS (EOF-terminated, no pause)
  if (!arena) {
    send_error(NULL, handle, 500);
    return REQUEST_CLOSE;
  }

  if (http_message_needs_eof(ctx)) {
    if (http_finish_parsing(ctx) != PARSE_SUCCESS) {
      LOG_ERROR("Finish parse failed: %s", ctx->error_reason ? ctx->error_reason : "");
      send_error(arena, handle, 400);
      return REQUEST_CLOSE;
    }
  }

  const char *path = ctx->url;
  size_t path_len = ctx->path_length;
  if (!path || path_len == 0) {
    path = "/";
    path_len = 1;
  }

  Req *req = NULL;
  Res *res = NULL;

  if (dispatch(arena, handle, ctx, client, path, path_len, &req, &res) != 0)
    return REQUEST_CLOSE;

  if (res && !res->replied)
    return REQUEST_PENDING;

  return res && res->keep_alive ? REQUEST_KEEP_ALIVE : REQUEST_CLOSE;
}
