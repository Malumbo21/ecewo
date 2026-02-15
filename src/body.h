#ifndef ECEWO_BODY_H
#define ECEWO_BODY_H

#include "ecewo.h"

typedef struct BodyStreamCtx {
  // Request reference
  Req *req;
  client_t *client;
  
  // Configuration
  bool streaming_enabled;
  size_t max_size;
  
  // Metrics
  size_t bytes_received;
  bool first_chunk;
  bool completed;
  bool errored;
  
  // Streaming callbacks
  BodyDataCb on_data;
  BodyEndCb on_end;
  BodyErrorCb on_error;
} BodyStreamCtx;

// Called from http.c on_body_cb when chunk arrives
// Returns: 0 = continue, 1 = pause (backpressure), -1 = error
int body_stream_on_chunk(void *stream_ctx, const char *data, size_t len);

// Called from router.c after request fully parsed
void body_on_complete(Req *req);

// Called from router.c if parse error occurs
void body_on_error_internal(Req *req, const char *error);

#endif