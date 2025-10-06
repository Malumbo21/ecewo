# Using JSON

We'll use [cJSON](https://github.com/DaveGamble/cJSON) to work with JSON objects in this example, but you can also use [jansson](https://github.com/akheron/jansson) if you prefer. For more information, refer to its official documentation.

## Installation

Copy [cJSON.c](https://github.com/DaveGamble/cJSON/blob/master/cJSON.c) and [cJSON.h](https://github.com/DaveGamble/cJSON/blob/master/cJSON.h) files from the repository, and paste them into `vendors/` folder. Make sure `cJSON.c` is part of the CMake build configuration.

## Usage

### Creating JSON

Let's write our `hello world` example again, but this time it will send a JSON object instead of a plain text.

```c
// main.c

#include "ecewo.h"
#include "cJSON.h"
#include <stdio.h>

void hello_world(Req *req, Res *res)
{
    // Create a JSON object
    cJSON *json = cJSON_CreateObject();

    // Add string to the JSON object we just created
    cJSON_AddStringToObject(json, "hello", "world");

    // Convert the JSON object to a string
    char *json_string = cJSON_PrintUnformatted(json);

    // Send the json response with 200 status code
    send_json(res, 200, json_string);

    // Free the memory that allocated by jansson
    cJSON_Delete(json);
    free(json_string);
}

int main(void)
{
    if (server_init() != SERVER_OK)
    {
        fprintf(stderr, "Failed to initialize server\n");
        return 1;
    }

    get("/", hello_world);

    if (server_listen(3000) != SERVER_OK)
    {
        fprintf(stderr, "Failed to start server\n");
        return 1;
    }

    server_run();
    return 0;
}
```

Now we can recompile and send a request to `http://localhost:3000/` again. We'll receive a JSON:

```
{"hello":"world"}
```

### Parsing JSON

This time, let's take a JSON and print it to console.

```c
// main.c

#include "ecewo.h"
#include "cJSON.h"
#include <stdio.h>

void handle_user(Req *req, Res *res)
{
    const char *body = req->body;

    if (body == NULL)
    {
        send_text(res, 400, "Missing request body");
        return;
    }

    cJSON *json = cJSON_Parse(body);

    if (!json)
    {
        send_text(res, 400, "Invalid JSON");
        return;
    }

    const char *name = cJSON_GetObjectItem(json, "name")->valuestring;
    const char *surname = cJSON_GetObjectItem(json, "surname")->valuestring;
    const char *username = cJSON_GetObjectItem(json, "username")->valuestring;

    if (!name || !surname || !username)
    {
        cJSON_Delete(json);
        send_text(res, 400, "Missing fields");
        return;
    }

    printf("Name: %s\n", name);
    printf("Surname: %s\n", surname);
    printf("Username: %s\n", username);

    cJSON_Delete(json);
    send_text(res, 200, "Success!");
}

int main(void)
{
    if (server_init() != SERVER_OK)
    {
        fprintf(stderr, "Failed to initialize server\n");
        return 1;
    }

    post("/user", handle_user);

    if (server_listen(3000) != SERVER_OK)
    {
        fprintf(stderr, "Failed to start server\n");
        return 1;
    }

    server_run();
    return 0;
}
```

Let's recompile the program and send a `POST` request to `http://localhost:3000/user` with this body:

```
{
    "name": "John",
    "surname": "Doe",
    "username": "johndoe"
}
```

We'll see in the console:

```
Name: John
Surname: Doe
Username: johndoe
```
