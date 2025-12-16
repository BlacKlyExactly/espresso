# ☕ Espresso

> Express.js-inspired web framework for C

**Espresso** brings the simplicity and elegance of Express.js to C, making it easy to build fast, lightweight HTTP servers and REST APIs with a familiar, developer-friendly API.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C Standard](https://img.shields.io/badge/C-C99-blue.svg)](https://en.wikipedia.org/wiki/C99)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20Windows-lightgrey.svg)](https://github.com/yourusername/espresso)
[![Build Status](https://github.com/BlacKlyExactly/espresso/workflows/Linux%20Build%20&%20Test/badge.svg](https://github.com/BlacKlyExactly/espresso/actions)
[![Build Status](https://github.com/BlacKlyExactly/espresso/workflows/Windows%20Build%20&%20Test/badge.svg](https://github.com/BlacKlyExactly/espresso/actions)

```c
#include "espresso.h"

void hello(ResponseContext *res) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "message", "Hello, World!");
    send_json_response(res, json);
}

int main() {
    App *app = create_app(3000);
    app_get(app, "/", hello);
    app_listen(app);
    return 0;
}
```

---

## ✨ Features

- **🚀 Express-like API** - Familiar syntax for web developers
- **📦 Route Parameters** - `/users/:id` style URL parameters
- **🔍 Query String Parsing** - Automatic parsing with type-safe getters
- **📝 JSON Support** - Built-in JSON request/response handling via cJSON
- **🔌 Middleware System** - Global, per-route, and group middleware
- **🌐 CORS Support** - Easy-to-use CORS macros
- **🔒 Memory Safe** - No memory leaks, automatic cleanup
- **💻 Cross-Platform** - Works on Linux, macOS, and Windows
- **⏱️ Request Timeouts** - Protection against slowloris attacks
- **⚡ HTTP/1.1 Keep-Alive** - Connection reuse for better performance
- **🛡️ Security Hardened** - Request size limits, header validation
- **⚡ Lightweight** - Minimal dependencies, pure C implementation

---

## 📦 Installation

### Prerequisites

- C compiler (GCC, Clang, or MSVC)
- libuv (will be linked automatically)
- Make (optional, for easier building)

### Quick Start

```bash
# Clone the repository
git clone https://github.com/blacklyexactly/espresso.git
cd espresso

# Build the example
make

# Run the server
./main
```

### Manual Build

**Linux/macOS:**

```bash
gcc -o server main.c espresso.c vendor/cJSON/cJSON.c -lpthread
./server
```

**Windows:**

```cmd
cl /Fe:server.exe main.c espresso.c vendor\cJSON\cJSON.c ws2_32.lib
server.exe
```

---

## 🚀 Quick Examples

### Hello World

```c
#include "espresso.h"

void hello_world(ResponseContext *res) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "message", "Hello, World!");
    send_json_response(res, json);
}

int main() {
    App *app = create_app(3000);
    app_get(app, "/", hello_world);

    printf("Server running on http://localhost:3000\n");
    app_listen(app);
    app_close(app);
    return 0;
}
```

### Route Parameters

```c
void get_user(ResponseContext *res) {
    char *id = get_param(res, "id");

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "user_id", id);
    cJSON_AddStringToObject(response, "name", "John Doe");

    send_json_response(res, response);
}

int main() {
    App *app = create_app(3000);
    app_get(app, "/users/:id", get_user);
    app_listen(app);
    return 0;
}
```

**Test it:**

```bash
curl http://localhost:3000/users/42
# {"user_id":"42","name":"John Doe"}
```

### Query Parameters

```c
void search(ResponseContext *res) {
    char *query = get_query_string(res, "q");
    int page = get_query_int(res, "page");

    if (!query) {
        send_error(res, 400, "Missing query parameter 'q'");
        return;
    }

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "query", query);
    cJSON_AddNumberToObject(response, "page", page ? page : 1);

    send_json_response(res, response);
}

int main() {
    App *app = create_app(3000);
    app_get(app, "/search", search);
    app_listen(app);
    return 0;
}
```

**Test it:**

```bash
curl 'http://localhost:3000/search?q=hello&page=2'
# {"query":"hello","page":2}
```

### JSON Request Body

```c
void create_user(ResponseContext *res) {
    if (res->req->body->type != BODY_JSON) {
        send_error(res, 400, "JSON body required");
        return;
    }

    cJSON *name = cJSON_GetObjectItem(res->req->body->data.json, "name");
    cJSON *email = cJSON_GetObjectItem(res->req->body->data.json, "email");

    if (!name || !email) {
        send_error(res, 400, "Name and email are required");
        return;
    }

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "id", "123");
    cJSON_AddStringToObject(response, "name", name->valuestring);
    cJSON_AddStringToObject(response, "email", email->valuestring);

    res->status = HTTP_CREATED;
    send_json_response(res, response);
}

int main() {
    App *app = create_app(3000);
    app_post(app, "/users", create_user);
    app_listen(app);
    return 0;
}
```

**Test it:**

```bash
curl -X POST http://localhost:3000/users \
  -H "Content-Type: application/json" \
  -d '{"name":"Alice","email":"alice@example.com"}'
```

---

## 🔌 Middleware

### Global Middleware

```c
MiddlewareResult logger(ResponseContext *res) {
    printf("[%s] %s\n", res->req->method, res->req->path);
    return MIDDLEWARE_CONTINUE;
}

int main() {
    App *app = create_app(3000);

    app_use(app, logger);  // Applies to all routes

    app_get(app, "/", hello_handler);
    app_listen(app);
    return 0;
}
```

### Per-Route Middleware

```c
MiddlewareResult auth_middleware(ResponseContext *res) {
    char *token = get_header(res, "Authorization");

    if (!token || strcmp(token, "Bearer secret") != 0) {
        send_error(res, 401, "Unauthorized");
        return MIDDLEWARE_STOP;
    }

    return MIDDLEWARE_CONTINUE;
}

int main() {
    App *app = create_app(3000);

    app_get(app, "/public", public_handler);
    app_get(app, "/private", private_handler, auth_middleware);

    app_listen(app);
    return 0;
}
```

### Multiple Middleware

```c
int main() {
    App *app = create_app(3000);

    app_post(app, "/admin",
             admin_handler,
             auth_middleware,      // Check authentication
             admin_check,          // Check admin role
             rate_limit);          // Rate limiting

    app_listen(app);
    return 0;
}
```

---

## 🌐 CORS Support

Espresso includes built-in CORS middleware:

```c
int main() {
    App *app = create_app(3000);

    // Allow all origins (development)
    app_use(app, cors_allow_all);

    app_get(app, "/api/data", data_handler);
    app_listen(app);
    return 0;
}
```

### Custom CORS

```c
// Define custom CORS middleware
DEFINE_CORS(cors_custom,
    "https://myapp.com",              // Specific origin
    "GET, POST, PUT, DELETE",         // Allowed methods
    "Content-Type, Authorization",    // Allowed headers
    3600)                             // Max age (1 hour)

int main() {
    App *app = create_app(3000);
    app_use(app, cors_custom);
    app_listen(app);
    return 0;
}
```

---

## 📁 Route Groups

Organize related routes with shared middleware:

```c
int main() {
    App *app = create_app(3000);

    // Public routes
    app_get(app, "/health", health_check);

    // API v1 group with authentication
    AppGroup *api = app_group(app, "/api/v1", auth_middleware);
    app_group_get(app, api, "/users", get_users);
    app_group_post(app, api, "/users", create_user);
    app_group_delete(app, api, "/users/:id", delete_user);

    app_listen(app);
    return 0;
}
```

Routes in the group:

- `GET /api/v1/users`
- `POST /api/v1/users`
- `DELETE /api/v1/users/:id`

All routes in the group automatically use `auth_middleware`.

---

## 🔧 Configuration

### Request Limits

Espresso enforces sensible defaults to protect against attacks:

```c
// Default limits (defined in espresso.h)
#define MAX_HEADER_SIZE (8 * 1024)          // 8 KB
#define MAX_BODY_SIZE (10 * 1024 * 1024)    // 10 MB
#define MAX_REQUEST_SIZE (MAX_HEADER_SIZE + MAX_BODY_SIZE)
#define REQUEST_TIMEOUT_TIME 5000           // 5 seconds
#define MAX_KEEP_ALIVE_REQUESTS 500         // Max requests per connection
```

**Why these limits?**

| Limit                   | Value | Reason                                                                                   |
| ----------------------- | ----- | ---------------------------------------------------------------------------------------- |
| **Header Size**         | 8 KB  | Matches nginx/Apache; sufficient for most headers including large cookies and JWT tokens |
| **Body Size**           | 10 MB | Handles JSON payloads and small file uploads; prevents memory exhaustion                 |
| **Request Timeout**     | 5s    | Prevents slowloris attacks while allowing slow clients                                   |
| **Keep-Alive Requests** | 500   | Balances connection reuse with resource management                                       |

**To customize these limits**, modify the values in `espresso.h` before building.

### Security Features

Espresso includes multiple layers of protection:

✅ **Request size validation** - Prevents memory exhaustion attacks  
✅ **Header size limits** - Blocks oversized header attacks  
✅ **Request timeouts** - Mitigates slowloris DoS attacks  
✅ **HTTP/1.1 Keep-Alive** - With connection limits to prevent abuse  
✅ **Proper Content-Length validation** - Detects conflicting headers  
✅ **HTTP version checking** - Only HTTP/1.0 and HTTP/1.1 supported

---

## 📖 Complete Example: TODO API

```c
#include "espresso.h"
#include <stdio.h>

typedef struct {
    int id;
    char title[256];
    int completed;
} Todo;

Todo todos[100];
int todo_count = 0;
int next_id = 1;

// Middleware
MiddlewareResult logger(ResponseContext *res) {
    printf("[%s] %s\n", res->req->method, res->req->path);
    return MIDDLEWARE_CONTINUE;
}

MiddlewareResult auth_middleware(ResponseContext *res) {
    char *token = get_header(res, "Authorization");
    if (!token || strcmp(token, "Bearer secret-token") != 0) {
        send_error(res, 401, "Unauthorized");
        return MIDDLEWARE_STOP;
    }
    return MIDDLEWARE_CONTINUE;
}

// Handlers
void get_todos(ResponseContext *res) {
    cJSON *array = cJSON_CreateArray();

    for (int i = 0; i < todo_count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "id", todos[i].id);
        cJSON_AddStringToObject(item, "title", todos[i].title);
        cJSON_AddBoolToObject(item, "completed", todos[i].completed);
        cJSON_AddItemToArray(array, item);
    }

    send_json_response(res, array);
}

void create_todo(ResponseContext *res) {
    if (res->req->body->type != BODY_JSON) {
        send_error(res, 400, "JSON body required");
        return;
    }

    cJSON *title = cJSON_GetObjectItem(res->req->body->data.json, "title");
    if (!title) {
        send_error(res, 400, "Title is required");
        return;
    }

    todos[todo_count].id = next_id++;
    strncpy(todos[todo_count].title, title->valuestring, 255);
    todos[todo_count].completed = 0;
    todo_count++;

    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "id", todos[todo_count - 1].id);
    res->status = HTTP_CREATED;
    send_json_response(res, response);
}

int main() {
    App *app = create_app(3000);

    // Global middleware
    app_use(app, logger);
    app_use(app, cors_allow_all);

    // Routes
    app_get(app, "/todos", get_todos, auth_middleware);
    app_post(app, "/todos", create_todo, auth_middleware);

    printf("TODO API running on http://localhost:3000\n");
    printf("Try:\n");
    printf("  curl -H 'Authorization: Bearer secret-token' http://localhost:3000/todos\n");

    app_listen(app);
    app_close(app);
    return 0;
}
```

---

## 🔧 API Reference

### Core Functions

| Function                   | Description                        |
| -------------------------- | ---------------------------------- |
| `create_app(port)`         | Create a new app on specified port |
| `app_listen(app)`          | Start the HTTP server              |
| `app_close(app)`           | Close and cleanup the app          |
| `app_use(app, middleware)` | Add global middleware              |

### Route Macros

| Macro                                 | Description           |
| ------------------------------------- | --------------------- |
| `app_get(app, path, handler, ...)`    | Register GET route    |
| `app_post(app, path, handler, ...)`   | Register POST route   |
| `app_patch(app, path, handler, ...)`  | Register PATCH route  |
| `app_delete(app, path, handler, ...)` | Register DELETE route |

### Request Data

| Function                        | Description                 |
| ------------------------------- | --------------------------- |
| `get_param(res, key)`           | Get route parameter (`:id`) |
| `get_query_string(res, key)`    | Get query string value      |
| `get_query_int(res, key)`       | Get query string as int     |
| `get_header(res, key)`          | Get request header          |
| `get_header_int(res, key)`      | Get request header as int   |
| `get_client_ip(res, buf, size)` | Get client IP address       |

### Response

| Function                        | Description         |
| ------------------------------- | ------------------- |
| `send_json_response(res, json)` | Send JSON response  |
| `send_text_response(res, text)` | Send text response  |
| `send_error(res, status, msg)`  | Send error response |
| `set_header(res, key, value)`   | Set response header |

### Data Passing

| Function                         | Description                                                  |
| -------------------------------- | ------------------------------------------------------------ |
| `set_data(res, key, value)`      | Store data in request context (value must be heap allocated) |
| `get_data(res, key)`             | Retrieve stored data                                         |
| `set_data_string(res, key, val)` | Store string (auto-allocated)                                |
| `get_data_string(res, key)`      | Get stored string                                            |

---

## 🎯 Comparison with Other Frameworks

| Feature           | Espresso        | Express.js         | facil.io      | Mongoose      |
| ----------------- | --------------- | ------------------ | ------------- | ------------- |
| Route Parameters  | ✅ `:id`        | ✅ `:id`           | ❌ Manual     | ❌ Manual     |
| Query Parsing     | ✅ Built-in     | ✅ Built-in        | ❌ Manual     | ❌ Manual     |
| Type-Safe Queries | ✅ Yes          | ❌ No              | ❌ No         | ❌ No         |
| JSON Auto-Parse   | ✅ Yes          | ✅ With middleware | ❌ Manual     | ❌ Manual     |
| Middleware        | ✅ Full support | ✅ Full support    | ⚠️ Limited    | ❌ No         |
| CORS              | ✅ Macros       | ✅ Package         | ❌ Manual     | ❌ Manual     |
| HTTP Keep-Alive   | ✅ Yes          | ✅ Yes             | ✅ Yes        | ⚠️ Limited    |
| Request Timeouts  | ✅ Yes          | ⚠️ Manual          | ✅ Yes        | ❌ No         |
| Async I/O         | ✅ libuv        | ✅ libuv           | ✅ Custom     | ❌ Blocking   |
| Learning Curve    | ⭐⭐ Easy       | ⭐ Easiest         | ⭐⭐⭐⭐ Hard | ⭐⭐⭐⭐ Hard |

---

## 🤝 Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/AmazingFeature`)
3. Commit your changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

---

## 📝 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

---

## 🙏 Acknowledgments

- Inspired by [Express.js](https://expressjs.com/)
- Uses [cJSON](https://github.com/DaveGamble/cJSON) for JSON parsing
- Built with ☕ and lots of C

---

## 🐛 Known Limitations

- **SSL/TLS:** Not yet supported (use reverse proxy like nginx)
- **WebSockets:** Not yet supported
- **Single-threaded:** Uses one event loop (but handles many connections efficiently)

---

## 🗺️ Roadmap

- [ ] Static file serving
- [ ] Cookie parsing
- [ ] Session management
- [ ] File upload support (multipart/form-data)
- [ ] Rate limiting middleware
- [ ] WebSocket support
- [ ] SSL/TLS support
- [x] Async I/O with libuv
- [x] HTTP/1.1 Keep-Alive
- [x] Request timeouts

---

## 📫 Contact

- **Issues:** [GitHub Issues](https://github.com/blacklyexactly/espresso/issues)
- **Discussions:** [GitHub Discussions](https://github.com/blacklyexactly/espresso/discussions)

---

<div align="center">

**⭐ If you find Espresso helpful, please give it a star! ⭐**

Made with ☕ by developers who miss Express.js in C

</div>
