#ifndef ECEWO_BODY_H
#define ECEWO_BODY_H

#include "ecewo.h"

void body_stream_complete(Req *req);
void body_stream_error(Req *req, const char *reason);

#endif
