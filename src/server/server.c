#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "router.h"
#include "uv.h"

#define READ_BUF_SIZE 4096 // 4 KB read buffer

typedef struct
{
    uv_tcp_t handle;
    uv_buf_t read_buf;
    char buffer[READ_BUF_SIZE];
} client_t;

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

// Called when data is read.
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

// Server startup function
void ecewo(unsigned short PORT)
{
    uv_loop_t *loop = uv_default_loop();

    uv_tcp_t server;
    memset(&server, 0, sizeof(server));
    uv_tcp_init(loop, &server);

    uv_tcp_simultaneous_accepts(&server, 1);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    uv_ip4_addr("0.0.0.0", PORT, &addr);

    int result = uv_tcp_bind(&server, (const struct sockaddr *)&addr, 0);
    if (result)
    {
        fprintf(stderr, "Bind error: %s\n", uv_strerror(result));
        return;
    }

    result = uv_listen((uv_stream_t *)&server, 128, on_new_connection);
    if (result)
    {
        fprintf(stderr, "Listen error: %s\n", uv_strerror(result));
        return;
    }

    printf("Server is running at: http://localhost:%d\n", PORT);
    uv_run(loop, UV_RUN_DEFAULT);

    uv_loop_close(loop);
}
