<div align="center">
  <img src="https://raw.githubusercontent.com/ecewo/ecewo/main/img/ecewo.svg" />
  <h1>Express-C Effect for Web Operations</h1>
  Asynchronous, single-threaded and FFI-friendly C web framework with a minimal embedded runtime. Inspired by the simplicity of <a href="https://expressjs.com">express.js</a>.
</div>

## Table of Contents

- [Requirements](#requirements)
- [Quick Start](#quick-start)
- [Running Tests](#running-tests)
- [Documentation](#documentation)
- [Example App](#example-app)
- [Plugins](#plugins)
- [Features](#features)
- [What's Missing?](#whats-missing)
- [Contributing](#contributing)
- [License](#license)

---

## Requirements

- A C compiler (GCC or Clang)
- CMake version 3.14 or higher

---

## Quick Start

**main.c:**
```c
#include "ecewo.h"
#include <stdio.h>

void hello_world(ecewo_request_t *req, ecewo_response_t *res) {
  ecewo_send_text(res, ECEWO_OK, "Hello, World!");
}

int main(void) {
  ecewo_app_t *app = ecewo_create();
  if (!app) {
    fprintf(stderr, "Failed to initialize server\n");
    return -1;
  }

  ECEWO_GET(app, "/", hello_world);

  if (ecewo_listen(app, 3000) != 0) {
    fprintf(stderr, "Failed to start server\n");
    return -1;
  }

  return 0;
}
```

**CMakeLists.txt:**
```sh
cmake_minimum_required(VERSION 3.14)
project(app VERSION 1.0.0 LANGUAGES C)

include(FetchContent)

FetchContent_Declare(
  ecewo
  GIT_REPOSITORY https://github.com/ecewo/ecewo.git
  GIT_TAG v4.1.0
)

FetchContent_MakeAvailable(ecewo)

add_executable(${PROJECT_NAME}
  main.c
)

target_link_libraries(${PROJECT_NAME} PRIVATE ecewo)
```

**Build and Run:**

```shell
mkdir build
cd build
cmake ..
cmake --build .
./app
```

---

## Running Tests

```shell
mkdir build
cd build
cmake -DECEWO_BUILD_TESTS=ON ..
cmake --build .
ctest
```

---

## Documentation

Refer to the [docs](/docs/) for usage.

---

## Example App

[Here](https://github.com/ecewo/ecewo-example) is an example blog app built with ecewo and PostgreSQL.

---

## Plugins

- [`ecewo-cluster`](https://github.com/ecewo/ecewo-cluster) for multithreading.
- [`ecewo-cookie`](https://github.com/ecewo/ecewo-cookie) for cookie management.
- [`ecewo-cors`](https://github.com/ecewo/ecewo-cors) for CORS impelentation.
- [`ecewo-fs`](https://github.com/ecewo/ecewo-fs) for file operations.
- [`ecewo-helmet`](https://github.com/ecewo/ecewo-helmet) for automatically setting safety headers.
- [`ecewo-https`](https://github.com/ecewo/ecewo-https) for encrypted connections via SSL/TLS.
- [`ecewo-mock`](https://github.com/ecewo/ecewo-mock) for mocking requests.
- [`ecewo-multipart`](https://github.com/ecewo/ecewo-multipart) for parsing `multipart/form-data` payloads.
- [`ecewo-postgres`](https://github.com/ecewo/ecewo-postgres) for async PostgreSQL integration.
- [`ecewo-session`](https://github.com/ecewo/ecewo-session) for session management.
- [`ecewo-static`](https://github.com/ecewo/ecewo-static) for static file serving.
- [`ecewo-ws`](https://github.com/ecewo/ecewo-ws) for WebSocket.

---

## Features

### Core

- Single-threaded asynchronous event loop built on [libuv](https://github.com/libuv/libuv).
- HTTP/1.1 parsing based on [llhttp](https://github.com/nodejs/llhttp) for HTTP parsing.
- Radix-tree router built on [rax](https://github.com/antirez/rax).
- Arena memory management with a customized allocator based on [tsoding/arena](https://github.com/tsoding/arena).
- Express.js-style API with routes, middleware, and handlers.
- Single public header (`ecewo.h`); every public symbol is `ecewo_*`-prefixed.
- Builds as a static or shared library (`-DECEWO_BUILD_SHARED=ON`) with hidden visibility.
- Multiple app instances can share one event loop, each on its own port.
- Configurable listen address with IPv4 and IPv6 support (`"0.0.0.0"`, `"::"`, specific NIC, etc.).
- Opaque public types; safe ABI for shared-library distribution and FFI bindings.

### Routing

- Methods: `GET`, `POST`, `PUT`, `PATCH`, `DELETE`, `HEAD`, `OPTIONS`.
- Dynamic path parameters (`/users/:id`) and wildcard routes (`*`).
- O(1) average-case route lookup via a radix tree.
- Two registration styles:
  - Convenience macros (`ECEWO_GET(app, path, ...)`).
  - Builder API (`ecewo_route_new` / `ecewo_route_middleware` / `ecewo_route_handler`) for runtime registration, plugins, and FFI bindings.
- Automatic `HEAD` body suppression and `405 Method Not Allowed` handling.

### Middleware

- Global middleware (runs on every request).
- Path-prefix middleware (runs only when the path starts with a given prefix).
- Route-specific middleware chains attached at registration time.
- Per-request context (`ecewo_context_set` / `ecewo_context_get`) for passing values from middleware to handler.

### Request handling

- Accessors for path params (`ecewo_param`), query string (`ecewo_query`), and headers (`ecewo_header_get`).
- Case-insensitive header lookup with O(1) cost.
- Body as raw bytes (`uint8_t*` + length). No UTF-8 assumption.
- Buffered body by default; opt-in streaming mode via `ecewo_body_stream` middleware with `on_data` / `on_end` callbacks.
- Configurable body size limit per request, with `413 Payload Too Large` rejection.

### Response handling

- Typed helpers: `ecewo_send_text`, `ecewo_send_html`, `ecewo_send_json`.
- Raw `ecewo_send(res, status, body, body_len)` for arbitrary payloads.
- `ecewo_redirect(res, status, url)` for 3xx redirects with `Location` set automatically.
- Full `ecewo_status_t` enum covering 1xx–5xx status codes.
- Response header API (`ecewo_header_set`) that rejects reserved framing headers (`Content-Length`, `Transfer-Encoding`, `Connection`, `Host`, `Date`) to prevent request-smuggling foot-guns.
- RFC 9110-compliant: bodies and `Content-Length` are stripped on `1xx` and `204` responses.

### Memory model

- Per-request arena: allocations live until the response is sent, then released as a block.
- App-lifetime arena for plugin and long-lived state (`ecewo_app_arena`).
- Pooled scratch arenas (`ecewo_arena_borrow` / `ecewo_arena_return`) with heap-allocated LIFO and cache cap decoupled from live count.
- Allocator helpers: `ecewo_alloc`, `ecewo_realloc`, `ecewo_strdup`, `ecewo_memdup`, `ecewo_sprintf`.
- No manual frees in user code for anything ecewo returns.

### Async and concurrency

- `ecewo_spawn` offloads blocking work to the libuv thread pool, then resumes on the loop thread.
- Per-request deadline (`ecewo_timeout_request`).
- One-shot timeouts (`ecewo_timeout`) and recurring intervals (`ecewo_interval`).
- Process-wide async work counter (`ecewo_increment_async_work` / `ecewo_decrement_async_work`) so background tasks keep the loop alive.

### Lifecycle

- Graceful shutdown with configurable drain timeout.
- `ecewo_atexit` callback for releasing per-app resources during shutdown.
- Per-app shutdown in multi-app processes; one app can stop while others stay running.
- `ecewo_bind` + `ecewo_run` split for the case where timers or libuv handles need to be set up before the loop runs.

### Plugin and FFI surface

- `ecewo_add` plugin loader.
- Per-app key/value store (`ecewo_set_app_data` / `ecewo_get_app_data`).
- Client reference counting (`ecewo_client_ref` / `ecewo_client_unref`) for holding a client across async boundaries.
- Direct libuv loop access (`ecewo_get_loop`) for advanced integrations.
- Connection takeover API (`ecewo_connection_takeover`); for WebSocket and other protocols that need raw TCP after the HTTP upgrade.
- C++ safe header (wrapped in `extern "C"`); written with FFI bindings in mind. See [docs/17.ffi-bindings.md](docs/17.ffi-bindings.md).

### Configuration

Tune per-app: max connections, listen backlog, idle timeout, request timeout, cleanup interval, shutdown drain timeout, listen address. All defaults are sensible; setters apply before `ecewo_bind` / `ecewo_listen`. See [docs/10.configurations.md](docs/10.configurations.md).

---

## What's Missing?

- HTTP/2/3
- SSE
- Rate limiter
- Redis plugin

---

## Contributing

Contributions are welcome. Please feel free to submit pull requests or open issues. See the [CONTRIBUTING.md](/CONTRIBUTING.md).

---

## License

Licensed under [MIT](./LICENSE).
