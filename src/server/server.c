#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "router.h"
#include "uv.h"

#define READ_BUF_SIZE 4096 // 4 KB read buffer

typedef struct
{
    uv_tcp_t handle;
    uv_buf_t read_buf;
    char buffer[READ_BUF_SIZE];
} client_t;

// Global variables for graceful shutdown
static uv_tcp_t *global_server = NULL;
static uv_signal_t sigint_handle;
#ifndef _WIN32
static uv_signal_t sigterm_handle;
#endif
#ifdef _WIN32
static uv_signal_t sigbreak_handle;
static uv_signal_t sighup_handle;
#endif
static volatile int shutdown_requested = 0;

// Allocation callback: returns the preallocated buffer for each connection.
void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
    (void)suggested_size;
    client_t *client = (client_t *)handle->data;
    *buf = client->read_buf; // Previously set inside on_new_connection
}

// Called after writing is complete; only frees the buffer.
void on_write_end(uv_write_t *req, int status)
{
    if (status)
    {
        fprintf(stderr, "Write error: %s\n", uv_strerror(status));
    }
    write_req_t *wr = (write_req_t *)req;
    free(wr->data);
    free(wr);
}

// Called when the connection is closed; frees the client struct.
void on_client_closed(uv_handle_t *handle)
{
    client_t *client = (client_t *)handle;
    free(client);
}

// Called when data is read
void on_read(uv_stream_t *client_stream, ssize_t nread, const uv_buf_t *buf)
{
    client_t *client = (client_t *)client_stream->data;

    if (nread < 0)
    {
        if (nread != UV_EOF && nread != UV_ECONNRESET)
            fprintf(stderr, "Read error: %s\n", uv_strerror((int)nread));

        uv_close((uv_handle_t *)&client->handle, on_client_closed);
        return;
    }

    if (nread == 0)
        return;

    if (shutdown_requested)
    {
        printf("Rejecting connection due to shutdown\n");
        uv_close((uv_handle_t *)&client->handle, on_client_closed);
        return;
    }

    int should_close = router(&client->handle, buf->base, (size_t)nread);
    if (should_close)
        uv_close((uv_handle_t *)&client->handle, on_client_closed);
}

// Called when a new connection is accepted.
void on_new_connection(uv_stream_t *server_stream, int status)
{
    if (status < 0)
    {
        fprintf(stderr, "New connection error: %s\n", uv_strerror(status));
        return;
    }

    // If shutdown has been requested, reject new connections
    if (shutdown_requested)
    {
        return;
    }

    client_t *client = calloc(1, sizeof(client_t));
    if (!client)
    {
        fprintf(stderr, "Failed to allocate client\n");
        return;
    }

    memset(&client->handle, 0, sizeof(client->handle));
    uv_tcp_init(uv_default_loop(), &client->handle);
    client->handle.data = client; // For use in alloc_buffer

    // Pre-allocate the read buffer
    client->read_buf = uv_buf_init(client->buffer, READ_BUF_SIZE);

    if (uv_accept(server_stream, (uv_stream_t *)&client->handle) == 0)
    {
        int enable = 1;
        uv_tcp_nodelay(&client->handle, enable);
        uv_read_start((uv_stream_t *)&client->handle, alloc_buffer, on_read);
    }
    else
    {
        uv_close((uv_handle_t *)&client->handle, on_client_closed);
    }
}

// Called when the server handle is closed
void on_server_closed(uv_handle_t *handle)
{
    printf("Server closed successfully\n");
    // free(global_server);
    free(handle);
    global_server = NULL;
}

// Called when a signal handler is closed
void on_signal_closed(uv_handle_t *handle)
{
    if (handle == (uv_handle_t *)&sigint_handle)
        printf("SIGINT handler closed\n");
#ifndef _WIN32
    else if (handle == (uv_handle_t *)&sigterm_handle)
        printf("SIGTERM handler closed\n");
#endif
#ifdef _WIN32
    else if (handle == (uv_handle_t *)&sigbreak_handle)
        printf("SIGBREAK handler closed\n");
    else if (handle == (uv_handle_t *)&sighup_handle)
        printf("SIGHUP handler closed\n");
#endif
    else
        printf("Unknown signal handler closed\n");
}

// Used to close all client connections
void walk_callback(uv_handle_t *handle, void *arg)
{
    (void)arg; // Unused parameter
    if (handle->type == UV_TCP && handle != (uv_handle_t *)global_server)
    {
        if (!uv_is_closing(handle))
            uv_close(handle, on_client_closed);
    }
}

// Graceful shutdown procedure
void graceful_shutdown()
{
    if (shutdown_requested)
        return;

    shutdown_requested = 1;
    printf("\nShutdown signal received. Shutting down gracefully...\n");

    // Close the server first (stop accepting new connections)
    if (global_server && !uv_is_closing((uv_handle_t *)global_server))
    {
        uv_close((uv_handle_t *)global_server, on_server_closed);
    }

    // Close existing client connections using uv_walk
    uv_walk(uv_default_loop(), walk_callback, NULL);

    // Close signal handlers last
    if (!uv_is_closing((uv_handle_t *)&sigint_handle))
        uv_close((uv_handle_t *)&sigint_handle, on_signal_closed);

#ifndef _WIN32
    if (!uv_is_closing((uv_handle_t *)&sigterm_handle))
        uv_close((uv_handle_t *)&sigterm_handle, on_signal_closed);
#endif

#ifdef _WIN32
    if (!uv_is_closing((uv_handle_t *)&sigbreak_handle))
        uv_close((uv_handle_t *)&sigbreak_handle, on_signal_closed);

    if (!uv_is_closing((uv_handle_t *)&sighup_handle))
        uv_close((uv_handle_t *)&sighup_handle, on_signal_closed);
#endif
}

// Signal callback: triggered when signals are received
void signal_handler(uv_signal_t *handle, int signum)
{
    if (signum == SIGINT)
    {
        printf("Received SIGINT (Ctrl+C), shutting down...\n");
    }
#ifndef _WIN32
    else if (signum == SIGTERM)
    {
        printf("Received SIGTERM, shutting down...\n");
    }
#endif
#ifdef _WIN32
    else if (signum == SIGBREAK)
    {
        printf("Received SIGBREAK (Ctrl+Break), shutting down...\n");
    }
    else if (signum == SIGHUP)
    {
        printf("Received SIGHUP (console close), shutting down...\n");
    }
#endif
    else
    {
        printf("Received unknown signal %d, shutting down...\n", signum);
    }

    graceful_shutdown();
    // uv_stop(uv_default_loop());
}

// DEBUG
static void count_handles(uv_handle_t *h, void *arg)
{
    int *cnt = arg;
    if (!uv_is_closing(h))
        ++*cnt;
}

// DEBUG
void report_open_handles(uv_loop_t *loop)
{
    int outstanding = 0;
    uv_walk(loop, count_handles, &outstanding);
    fprintf(stderr, ">>> Hala %d açık handle var!\n", outstanding);
}

// Server startup function
void ecewo(unsigned short PORT)
{
    uv_loop_t *loop = uv_default_loop();
    uv_tcp_t *server = malloc(sizeof(*server));
    if (!server)
    {
        fprintf(stderr, "Failed to allocate server\n");
        return;
    }

    uv_tcp_init(loop, server);

    // Bind the server socket to the specified port
    struct sockaddr_in addr;
    uv_ip4_addr("0.0.0.0", PORT, &addr);
    int r = uv_tcp_bind(server, (const struct sockaddr *)&addr, 0);
    if (r)
    {
        fprintf(stderr, "Bind error: %s\n", uv_strerror(r));
        uv_close((uv_handle_t *)server, (uv_close_cb)free);
        uv_run(loop, UV_RUN_NOWAIT); // Trigger free callback
        return;
    }

    // Start listening for incoming connections
    uv_tcp_simultaneous_accepts(server, 1);
    r = uv_listen((uv_stream_t *)server, 128, on_new_connection);
    if (r)
    {
        fprintf(stderr, "Listen error: %s\n", uv_strerror(r));
        uv_close((uv_handle_t *)server, (uv_close_cb)free);
        uv_run(loop, UV_RUN_NOWAIT); // Trigger free callback
        return;
    }

    // A valid server handle is now available
    global_server = server;

    // Initialize and start signal handlers
    uv_signal_init(loop, &sigint_handle);
#ifndef _WIN32
    uv_signal_init(loop, &sigterm_handle);
#endif
#ifdef _WIN32
    uv_signal_init(loop, &sigbreak_handle);
    uv_signal_init(loop, &sighup_handle);
#endif

    uv_signal_start(&sigint_handle, signal_handler, SIGINT);
#ifndef _WIN32
    uv_signal_start(&sigterm_handle, signal_handler, SIGTERM);
#endif
#ifdef _WIN32
    uv_signal_start(&sigbreak_handle, signal_handler, SIGBREAK);
    uv_signal_start(&sighup_handle, signal_handler, SIGHUP);
#endif

    printf("Server is running at: http://localhost:%d\n", PORT);

    // Main event loop: runs until a signal stops it
    uv_run(loop, UV_RUN_DEFAULT);

    // Check if loop still has active handles
    // and process remaining close operations
    if (uv_loop_alive(loop))
    {
        uv_run(loop, UV_RUN_DEFAULT);
    }

    report_open_handles(loop);

    // Close the loop
    int close_result = uv_loop_close(loop);
    if (close_result != 0)
    {
        fprintf(stderr, "Failed to close loop: %s\n", uv_strerror(close_result));
    }
}
