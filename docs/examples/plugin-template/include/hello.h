#ifndef ECEWO_HELLO_H
#define ECEWO_HELLO_H

#include "ecewo.h"

#if defined(_WIN32)
#define HELLO_EXPORT __declspec(dllexport)
#else
#define HELLO_EXPORT __attribute__((visibility("default")))
#endif

/* Register the plugin's routes/middleware on an app. Call before listening. */
HELLO_EXPORT void hello_register(ecewo_app_t *app);

#endif /* ECEWO_HELLO_H */
