#ifndef ECEWO_H
#define ECEWO_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include "arena.h"

// ============================================================================
// PUBLIC TYPES AND FORWARD DECLARATIONS
// ============================================================================

typedef struct uv_loop_s uv_loop_t;
typedef struct uv_timer_s uv_timer_t;
typedef struct uv_tcp_s uv_tcp_t;

typedef struct Req Req;
typedef struct Res Res;
typedef struct Chain Chain;

// Context entry
typedef struct
{
    char *key;
    void *data;
    size_t size;
} context_entry_t;

// Context
typedef struct
{
    context_entry_t *entries;
    uint32_t count;
    uint32_t capacity;
    Arena *arena;
} context_t;

// Request item
typedef struct
{
    char *key;
    char *value;
} request_item_t;

// Request structure
typedef struct
{
    request_item_t *items;
    uint16_t count;
    uint16_t capacity;
} request_t;

// Request structure
struct Req
{
    Arena *arena;
    uv_tcp_t *client_socket;
    char *method;
    char *path;
    char *body;
    size_t body_len;
    request_t headers;
    request_t query;
    request_t params;
    context_t ctx; // Middleware context
};

// HTTP Header structure
typedef struct
{
    char *name;
    char *value;
} http_header_t;

// Response structure
struct Res
{
    Arena *arena;
    uv_tcp_t *client_socket;
    uint16_t status;
    char *content_type;
    void *body;
    size_t body_len;
    bool keep_alive;
    http_header_t *headers;
    uint16_t header_count;
    uint16_t header_capacity;
};

// HTTP Status Codes
typedef enum
{
    // 1xx Informational
    CONTINUE = 100,
    SWITCHING_PROTOCOLS = 101,
    PROCESSING = 102,
    EARLY_HINTS = 103,

    // 2xx Success
    OK = 200,
    CREATED = 201,
    ACCEPTED = 202,
    NON_AUTHORITATIVE_INFORMATION = 203,
    NO_CONTENT = 204,
    RESET_CONTENT = 205,
    PARTIAL_CONTENT = 206,
    MULTI_STATUS = 207,
    ALREADY_REPORTED = 208,
    IM_USED = 226,

    // 3xx Redirection
    MULTIPLE_CHOICES = 300,
    MOVED_PERMANENTLY = 301,
    FOUND = 302,
    SEE_OTHER = 303,
    NOT_MODIFIED = 304,
    USE_PROXY = 305,
    TEMPORARY_REDIRECT = 307,
    PERMANENT_REDIRECT = 308,

    // 4xx Client Error
    BAD_REQUEST = 400,
    UNAUTHORIZED = 401,
    PAYMENT_REQUIRED = 402,
    FORBIDDEN = 403,
    NOT_FOUND = 404,
    METHOD_NOT_ALLOWED = 405,
    NOT_ACCEPTABLE = 406,
    PROXY_AUTHENTICATION_REQUIRED = 407,
    REQUEST_TIMEOUT = 408,
    CONFLICT = 409,
    GONE = 410,
    LENGTH_REQUIRED = 411,
    PRECONDITION_FAILED = 412,
    PAYLOAD_TOO_LARGE = 413,
    URI_TOO_LONG = 414,
    UNSUPPORTED_MEDIA_TYPE = 415,
    RANGE_NOT_SATISFIABLE = 416,
    EXPECTATION_FAILED = 417,
    IM_A_TEAPOT = 418,
    MISDIRECTED_REQUEST = 421,
    UNPROCESSABLE_ENTITY = 422,
    LOCKED = 423,
    FAILED_DEPENDENCY = 424,
    TOO_EARLY = 425,
    UPGRADE_REQUIRED = 426,
    PRECONDITION_REQUIRED = 428,
    TOO_MANY_REQUESTS = 429,
    REQUEST_HEADER_FIELDS_TOO_LARGE = 431,
    UNAVAILABLE_FOR_LEGAL_REASONS = 451,

    // 5xx Server Error
    INTERNAL_SERVER_ERROR = 500,
    NOT_IMPLEMENTED = 501,
    BAD_GATEWAY = 502,
    SERVICE_UNAVAILABLE = 503,
    GATEWAY_TIMEOUT = 504,
    HTTP_VERSION_NOT_SUPPORTED = 505,
    VARIANT_ALSO_NEGOTIATES = 506,
    INSUFFICIENT_STORAGE = 507,
    LOOP_DETECTED = 508,
    NOT_EXTENDED = 510,
    NETWORK_AUTHENTICATION_REQUIRED = 511
} http_status_t;

typedef void (*RequestHandler)(Req *req, Res *res);

typedef int (*MiddlewareHandler)(Req *req, Res *res, Chain *chain);

typedef struct
{
    MiddlewareHandler *handlers;
    size_t count;
} MiddlewareArray;

#define use(...)                                        \
    ((MiddlewareArray){                                 \
        .handlers = (MiddlewareHandler[]){__VA_ARGS__}, \
        .count = sizeof((MiddlewareHandler[]){__VA_ARGS__}) / sizeof(MiddlewareHandler)})

#define NO_MW ((MiddlewareArray){.handlers = NULL, .count = 0})

typedef enum
{
    SERVER_OK = 0,
    SERVER_ALREADY_INITIALIZED = -1,
    SERVER_NOT_INITIALIZED = -2,
    SERVER_ALREADY_RUNNING = -3,
    SERVER_INIT_FAILED = -4,
    SERVER_OUT_OF_MEMORY = -5,
    SERVER_BIND_FAILED = -6,
    SERVER_LISTEN_FAILED = -7,
    SERVER_INVALID_PORT = -8,
} server_error_t;

typedef void (*shutdown_callback_t)(void);
typedef void (*timer_callback_t)(void *user_data);

typedef struct task_s Task;
typedef void (*result_handler_t)(void *context, char *error);
typedef void (*work_handler_t)(Task *task, void *context);

// ============================================================================
// Server Functions
// ============================================================================

int server_init(void);
int server_listen(uint16_t port);
void server_run(void);
void shutdown_hook(shutdown_callback_t callback);
bool server_is_running(void);
int get_active_connections(void);
uv_loop_t *get_loop(void);

// ============================================================================
// Timer Functions
// ============================================================================

uv_timer_t *set_timeout(timer_callback_t callback, uint64_t delay_ms, void *user_data);
uv_timer_t *set_interval(timer_callback_t callback, uint64_t interval_ms, void *user_data);
void clear_timer(uv_timer_t *timer);

// ============================================================================
// Request Functions
// ============================================================================

const char *get_param(const Req *req, const char *key);
const char *get_query(const Req *req, const char *key);
const char *get_header(const Req *req, const char *key);
void set_context(Req *req, const char *key, void *data, size_t size);
void *get_context(Req *req, const char *key);

// ============================================================================
// Response Functions
// ============================================================================

void set_header(Res *res, const char *name, const char *value);
void reply(Res *res, int status, const char *content_type, const void *body, size_t body_len);
void redirect(Res *res, int status, const char *url);

static inline void send_text(Res *res, int status, const char *body)
{
    reply(res, status, "text/plain", body, strlen(body));
}

static inline void send_html(Res *res, int status, const char *body)
{
    reply(res, status, "text/html", body, strlen(body));
}

static inline void send_json(Res *res, int status, const char *body)
{
    reply(res, status, "application/json", body, strlen(body));
}

static inline void send_cbor(Res *res, int status, const char *body, size_t body_len)
{
    reply(res, status, "application/cbor", body, body_len);
}

// ============================================================================
// Memory Functions (Arena)
// ============================================================================

void *arena_alloc(Arena *a, size_t size_bytes);
void *arena_realloc(Arena *a, void *oldptr, size_t oldsz, size_t newsz);
char *arena_strdup(Arena *a, const char *cstr);
void *arena_memdup(Arena *a, void *data, size_t size);
char *arena_sprintf(Arena *a, const char *format, ...);
void *arena_memcpy(void *dest, const void *src, size_t n);

#define ecewo_alloc(x, size_bytes) \
    arena_alloc((x)->arena, size_bytes)

#define ecewo_realloc(x, oldptr, oldsz, newsz) \
    arena_realloc((x)->arena, oldptr, oldsz, newsz)

#define ecewo_strdup(x, cstr) \
    arena_strdup((x)->arena, cstr)

#define ecewo_memdup(x, data, size) \
    arena_memdup((x)->arena, data, size)

#define ecewo_sprintf(x, format, ...) \
    arena_sprintf((x)->arena, format, ##__VA_ARGS__)

// ============================================================================
// Middleware Functions
// ============================================================================

void hook(MiddlewareHandler middleware_handler);
int next(Req *req, Res *res, Chain *chain);

// ============================================================================
// Task Functions
// ============================================================================

int task(Arena *arena, void *context, work_handler_t work_fn, result_handler_t result_fn);

#define worker(context, work_fn, result_fn) \
    task((context)->res->arena, (context), (work_fn), (result_fn))

// ============================================================================
// Route Registration
// ============================================================================

// Internal functions (used by macros below)
void register_sync_route(int method, const char *path, MiddlewareArray middleware, RequestHandler handler);
void register_async_route(int method, const char *path, MiddlewareArray middleware, RequestHandler handler);

// ============================================================================
// Synchronous Route Macros
// ============================================================================

// Argument chooser
#define ECEWO_ROUTE_CHOOSER(_1, _2, _3, NAME, ...) NAME

// Reusable inline helpers (shared by all routes)
static inline void route_sync_no_mw(int method, const char *path, RequestHandler handler)
{
    register_sync_route(method, path, NO_MW, handler);
}

static inline void route_sync_with_mw(int method, const char *path, MiddlewareArray mw, RequestHandler handler)
{
    register_sync_route(method, path, mw, handler);
}

static inline void route_async_no_mw(int method, const char *path, RequestHandler handler)
{
    register_async_route(method, path, NO_MW, handler);
}

static inline void route_async_with_mw(int method, const char *path, MiddlewareArray mw, RequestHandler handler)
{
    register_async_route(method, path, mw, handler);
}

// ============================================================================
// Synchronous Routes
// ============================================================================

// HTTP method values from llhttp: DELETE=0, GET=1, POST=3, PUT=4, PATCH=28

#define get(...) \
    ECEWO_ROUTE_CHOOSER(__VA_ARGS__, route_sync_with_mw, route_sync_no_mw)(1, __VA_ARGS__)

#define post(...) \
    ECEWO_ROUTE_CHOOSER(__VA_ARGS__, route_sync_with_mw, route_sync_no_mw)(3, __VA_ARGS__)

#define put(...) \
    ECEWO_ROUTE_CHOOSER(__VA_ARGS__, route_sync_with_mw, route_sync_no_mw)(4, __VA_ARGS__)

#define patch(...) \
    ECEWO_ROUTE_CHOOSER(__VA_ARGS__, route_sync_with_mw, route_sync_no_mw)(28, __VA_ARGS__)

#define del(...) \
    ECEWO_ROUTE_CHOOSER(__VA_ARGS__, route_sync_with_mw, route_sync_no_mw)(0, __VA_ARGS__)

// ============================================================================
// Asynchronous Routes
// ============================================================================

#define get_worker(...) \
    ECEWO_ROUTE_CHOOSER(__VA_ARGS__, route_async_with_mw, route_async_no_mw)(1, __VA_ARGS__)

#define post_worker(...) \
    ECEWO_ROUTE_CHOOSER(__VA_ARGS__, route_async_with_mw, route_async_no_mw)(3, __VA_ARGS__)

#define put_worker(...) \
    ECEWO_ROUTE_CHOOSER(__VA_ARGS__, route_async_with_mw, route_async_no_mw)(4, __VA_ARGS__)

#define patch_worker(...) \
    ECEWO_ROUTE_CHOOSER(__VA_ARGS__, route_async_with_mw, route_async_no_mw)(28, __VA_ARGS__)

#define del_worker(...) \
    ECEWO_ROUTE_CHOOSER(__VA_ARGS__, route_async_with_mw, route_async_no_mw)(0, __VA_ARGS__)

#endif
