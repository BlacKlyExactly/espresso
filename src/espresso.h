#ifndef ESPRESSO_H
#define ESPRESSO_H

/**
 * @file espresso.h
 * @brief Espresso - lightweight Express-like HTTP framework for C
 *
 * Supports HTTP routes, middleware, query parameters, JSON payloads (cJSON),
 * and CORS handling. Works on Unix/Linux/macOS and Windows.
 */

#include "../vendor/cJSON/cJSON.h"
#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <uv.h>

#ifdef DELETE
#undef DELETE
#endif

#define MAX_HEADERS 32
#define KEY_SIZE 64
#define VALUE_SIZE 256
#define MAX_HEADER_SIZE (8 * 1024 * 1024)
#define MAX_BODY_SIZE (10 * 1024 * 1024)
#define MAX_REQUEST_SIZE MAX_HEADER_SIZE + MAX_BODY_SIZE
#define REQUEST_TIMEOUT_TIME 5000
#define MAX_KEEP_ALIVE_REQUESTS 500

/**
 * @enum HttpStatus
 * @brief Standard HTTP status codes
 */
typedef enum {
  HTTP_OK = 200,
  HTTP_CREATED = 201,
  HTTP_BAD_REQUEST = 400,
  HTTP_UNAUTHORIZED = 401,
  HTTP_FORBIDDEN = 403,
  HTTP_NOT_FOUND = 404,
  HTTP_INTERNAL_ERROR = 500
} HttpStatus;

/**
 * @enum BodyType
 * @brief Type of request/response body
 */
typedef enum { BODY_NONE, BODY_TEXT, BODY_JSON } BodyType;

/**
 * @struct Body
 * @brief HTTP request or response body
 */
typedef struct {
  BodyType type;
  union {
    char *text;
    cJSON *json;
  } data;
} Body;

/**
 * @struct KeyValue
 * @brief Fixed-size key/value pair
 */
typedef struct {
  char key[KEY_SIZE];
  char value[VALUE_SIZE];
} KeyValue;

/**
 * @struct DynamicKeyValue
 * @brief Dynamic key/value pair
 */
typedef struct {
  char *key;
  char *value;
} DynamicKeyValue;

/**
 * @struct Request
 * @brief HTTP request structure
 */
typedef struct {
  char method[8];
  char path[256];
  char version[16];
  KeyValue headers[MAX_HEADERS];
  int header_count;
  Body *body;
  KeyValue *params;
  int params_count;
} Request;

/**
 * @enum Method
 * @brief Supported HTTP methods
 */
typedef enum { GET, POST, PATCH, DELETE } Method;

extern const char *MethodNames[];

typedef struct App App;
typedef struct ResponseContext ResponseContext;
typedef struct ClientContext ClientContext;

typedef enum { MIDDLEWARE_CONTINUE = 0, MIDDLEWARE_STOP = 1 } MiddlewareResult;

typedef void (*EndpointHandler)(ResponseContext *);
typedef MiddlewareResult (*Middleware)(ResponseContext *res);

/**
 * @struct Endpoint
 * @brief Represents an HTTP route
 */
typedef struct {
  char *path;
  Method method;
  EndpointHandler handler;
  Middleware *middlewares;
  int middleware_count;
  char *group_path;
} Endpoint;

/**
 * @struct AppGroup
 * @brief Group of endpoints with shared root path and middleware
 */
typedef struct {
  char *root_path;
  Endpoint *endpoints;
  int endpoint_count;
  Middleware *middlewares;
  int middleware_count;
} AppGroup;

/**
 * @struct App
 * @brief Main application object
 */
struct App {
  uv_tcp_t server;
  int port;
  Endpoint *endpoints;
  int endpoint_count;
  int endpoint_capacity;
  Middleware *middlewares;
  int middleware_count;
  int middleware_capacity;
  AppGroup *groups;
  int groups_count;
  int groups_capacity;
  uv_tcp_t **clients;
  int client_count;
  int client_capacity;
};

/**
 * @struct ClientContext
 * @brief Context for a connected client
 */
struct ClientContext {
  App *app;
  uv_tcp_t *client;
  char *buffer;
  size_t buffer_len;
  size_t buffer_capacity;
  int headers_parsed;
  size_t content_length;
  Request *req;
  ResponseContext *res;
  uv_timer_t *req_timer;
  int keep_alive;
  int request_count;
};

typedef struct {
  char *key;
  void *value;
} ResponseDataEntry;

typedef struct {
  int capacity;
  int count;
  ResponseDataEntry *entries;
} ResponseData;

typedef struct {
  int count;
  int capacity;
  DynamicKeyValue *entries;
} ResponseContextQuery;

struct ResponseContext {
  ClientContext *ctx;
  Request *req;
  int status;
  KeyValue headers[MAX_HEADERS];
  int header_count;
  ResponseData data;
  ResponseContextQuery query;
};

typedef struct {
  ClientContext *ctx;
  EndpointHandler handler;
} EndpointWork;

/* ---------------- Function Declarations ---------------- */

/**
 * @brief Creates a route group with optional middleware
 */
AppGroup *_app_create_group(App *app, const char *root_path,
                            Middleware *middlewares);

/**
 * @brief Appends a route to a group
 */
void _app_append_endpoint_to_group(App *app, AppGroup *group, Method method,
                                   const char *path, EndpointHandler handler,
                                   Middleware *middlewares);

/**
 * @brief Appends a route to the app
 */
Endpoint *_app_append_endpoint(App *app, Method method, const char *path,
                               EndpointHandler handler,
                               Middleware *middlewares);

/**
 * @brief Creates a new Espresso app on the specified port
 */
App *create_app(int port);

/**
 * @brief Starts the HTTP server loop
 */
void app_listen(App *app);

/**
 * @brief Adds a global middleware
 */
void app_use(App *app, Middleware mw);

/**
 * @brief Closes the app and frees resources
 */
void app_close(App *app);

/**
 * @brief Sends JSON response
 */
void send_json_response(ResponseContext *res, cJSON *json);

/**
 * @brief Sends text response
 */
void send_text_response(ResponseContext *res, const char *text);

/**
 * @brief Sends error response with status code and message
 */
void send_error(ResponseContext *res, int status, const char *message);

/**
 * @brief Retrieves client ip and passes it to buffer, "unknown" if not found
 */
void get_client_ip(ResponseContext *res, char *buffer, size_t size);

/* ---------------- Header & Data Access ---------------- */

/**
 * @brief Retrieve a path parameter from the request
 *
 * Example: If route is "/users/:id" and request is "/users/42",
 * get_param(res, "id") returns "42".
 *
 * @param res Pointer to ResponseContext
 * @param key Parameter name
 * @return Value as a string, or NULL if not found
 */
char *get_param(ResponseContext *res, const char *key);

/**
 * @brief Retrieve a header from the request
 *
 * @param res Pointer to ResponseContext
 * @param key Header name
 * @return Value as a string, or NULL if not found
 */
char *get_header(ResponseContext *res, const char *key);

/**
 * @brief Retrieve a header int from the request
 *
 * @param res Pointer to ResponseContext
 * @param key Header name
 * @return Value as a int, or 0 if not found
 */
int get_header_int(ResponseContext *res, const char *key);

/**
 * @brief Set a header in the response
 *
 * @param res Pointer to ResponseContext
 * @param key Header name
 * @param value Header value
 */
void set_header(ResponseContext *res, const char *key, const char *value);

/**
 * @brief Set arbitrary data in the response context
 *
 * This can be used to store custom data that can be retrieved
 * later during request processing.
 *
 * @param res Pointer to ResponseContext
 * @param key Data key
 * @param value Pointer to data
 */
void set_data(ResponseContext *res, const char *key, void *value);

/**
 * @brief Retrieve arbitrary data stored in the response context
 *
 * @param res Pointer to ResponseContext
 * @param key Data key
 * @return Pointer to the stored data, or NULL if not found
 */
void *get_data(ResponseContext *res, const char *key);

/**
 * @brief Convenience function to store an integer in the response context
 */
void set_data_int(ResponseContext *res, const char *key, int value);

/**
 * @brief Convenience function to store a string in the response context
 */
void set_data_string(ResponseContext *res, const char *key, const char *value);

/**
 * @brief Convenience function to store a double in the response context
 */
void set_data_double(ResponseContext *res, const char *key, double value);

/**
 * @brief Retrieve an integer stored in the response context
 */
int get_data_int(ResponseContext *res, const char *key);

/**
 * @brief Retrieve a string stored in the response context
 */
char *get_data_string(ResponseContext *res, const char *key);

/**
 * @brief Retrieve a double stored in the response context
 */
double get_data_double(ResponseContext *res, const char *key);

/**
 * @brief Retrieve a query parameter as string from the URL
 *
 * Example: For "/users?id=42", get_query_string(res, "id") returns "42"
 *
 * @param res Pointer to ResponseContext
 * @param key Query parameter name
 * @return Value as a string, or NULL if not present
 */
char *get_query_string(ResponseContext *res, const char *key);

/**
 * @brief Retrieve a query parameter as a void pointer
 *
 * Can be used for complex query values stored as pointers
 */
void *get_query(ResponseContext *res, const char *key);

/**
 * @brief Retrieve a query parameter as an integer
 */
int get_query_int(ResponseContext *res, const char *key);

/**
 * @brief Retrieve a query parameter as a double
 */
double get_query_double(ResponseContext *res, const char *key);

/* ---------------- Route Macros ---------------- */

/**
 * @brief Append an endpoint (route) to the app with optional middleware
 *
 * This macro calls the internal function `_app_append_endpoint`.
 * You can specify zero or more middleware functions that will run
 * before the handler. Middleware functions must match the `Middleware`
 * type signature: `int func(ResponseContext *res)`.
 *
 * @param app Pointer to the App instance
 * @param method HTTP method (GET, POST, PATCH, DELETE)
 * @param path Route path (e.g., "/users/:id")
 * @param handler Endpoint handler function
 * @param ... Optional middleware functions
 */
#define app_append_endpoint(app, method, path, handler, ...)                   \
  _app_append_endpoint(app, method, path, handler,                             \
                       (Middleware[]){__VA_ARGS__ __VA_OPT__(, ) NULL})

/**
 * @brief Shortcut macro to append a GET endpoint
 *
 * Same as `app_append_endpoint` but sets method to GET.
 * Example:
 *   app_get(app, "/users/:id", handler_func, mw1, mw2);
 */
#define app_get(app, path, handler, ...)                                       \
  app_append_endpoint(app, GET, path, handler, ##__VA_ARGS__)

/**
 * @brief Shortcut macro to append a POST endpoint
 *
 * Same as `app_append_endpoint` but sets method to POST.
 * Example:
 *   app_post(app, "/users", create_user_handler, auth_middleware);
 */
#define app_post(app, path, handler, ...)                                      \
  app_append_endpoint(app, POST, path, handler, ##__VA_ARGS__)

/**
 * @brief Shortcut macro to append a PATCH endpoint
 *
 * Same as `app_append_endpoint` but sets method to PATCH.
 * Example:
 *   app_patch(app, "/users/:id", update_user_handler);
 */
#define app_patch(app, path, handler, ...)                                     \
  app_append_endpoint(app, PATCH, path, handler, ##__VA_ARGS__)

/**
 * @brief Shortcut macro to append a DELETE endpoint
 *
 * Same as `app_append_endpoint` but sets method to DELETE.
 * Example:
 *   app_delete(app, "/users/:id", delete_user_handler);
 */
#define app_delete(app, path, handler, ...)                                    \
  app_append_endpoint(app, DELETE, path, handler, ##__VA_ARGS__)

/* ---------------- Group Macros ---------------- */

/**
 * @brief Create a route group with optional middleware
 *
 * Usage:
 * AppGroup *group = app_group(app, "/api", mw1, mw2);
 */
#define app_group(app, root_path, ...)                                         \
  _app_create_group(app, root_path,                                            \
                    (Middleware[]){__VA_ARGS__ __VA_OPT__(, ) NULL})

/**
 * @brief Add GET route to a group
 */
#define app_group_get(app, group, path, handler, ...)                          \
  _app_append_endpoint_to_group(                                               \
      app, group, GET, path, handler,                                          \
      (Middleware[]){__VA_ARGS__ __VA_OPT__(, ) NULL})

/**
 * @brief Add POST route to a group
 */
#define app_group_post(app, group, path, handler, ...)                         \
  _app_append_endpoint_to_group(                                               \
      app, group, POST, path, handler,                                         \
      (Middleware[]){__VA_ARGS__ __VA_OPT__(, ) NULL})

/**
 * @brief Add PATCH route to a group
 */
#define app_group_patch(app, group, path, handler, ...)                        \
  _app_append_endpoint_to_group(                                               \
      app, group, PATCH, path, handler,                                        \
      (Middleware[]){__VA_ARGS__ __VA_OPT__(, ) NULL})

/**
 * @brief Add DELETE route to a group
 */
#define app_group_delete(app, group, path, handler, ...)                       \
  _app_append_endpoint_to_group(                                               \
      app, group, DELETE, path, handler,                                       \
      (Middleware[]){__VA_ARGS__ __VA_OPT__(, ) NULL})

/* ---------------- CORS Helpers ---------------- */

/**
 * @brief Define a CORS middleware for handling preflight requests
 *
 * This macro generates a static function that sets the necessary
 * CORS headers on the response and handles OPTIONS preflight requests.
 *
 * @param name Name of the generated function
 * @param origin_val Value for "Access-Control-Allow-Origin" header
 * @param methods_val Value for "Access-Control-Allow-Methods" header
 * @param headers_val Value for "Access-Control-Allow-Headers" header
 * @param max_age_val Value for "Access-Control-Max-Age" header (in seconds)
 *
 * @return Returns 1 if the request is an OPTIONS preflight request and
 *         the response was sent, otherwise 0.
 *
 * @note The generated function can be used as a middleware in espresso:
 *       ```
 *       DEFINE_CORS(cors_allow_all, "*", "GET, POST, PATCH, DELETE, OPTIONS",
 *                   "Content-Type, Authorization", 86400)
 *       app_use(app, cors_allow_all);
 *       ```
 */
#define DEFINE_CORS(name, origin_val, methods_val, headers_val, max_age_val)   \
  static MiddlewareResult name(ResponseContext *res) {                         \
    set_header(res, "Access-Control-Allow-Origin", origin_val);                \
    set_header(res, "Access-Control-Allow-Methods", methods_val);              \
    set_header(res, "Access-Control-Allow-Headers", headers_val);              \
    char max_age[32];                                                          \
    snprintf(max_age, sizeof(max_age), "%d", max_age_val);                     \
    set_header(res, "Access-Control-Max-Age", max_age);                        \
    if (strcmp(res->req->method, "OPTIONS") == 0) {                            \
      res->status = 204;                                                       \
      send_text_response(res, "");                                             \
      return MIDDLEWARE_STOP;                                                  \
    }                                                                          \
    return MIDDLEWARE_CONTINUE;                                                \
  }

/**
 * @brief CORS middleware that allows requests from any origin.
 *
 * Sets the following headers on the response:
 * - Access-Control-Allow-Origin: *
 * - Access-Control-Allow-Methods: GET, POST, PUT, DELETE, PATCH, OPTIONS
 * - Access-Control-Allow-Headers: Content-Type, Authorization
 * - Access-Control-Max-Age: 86400 (1 day)
 *
 * Also handles OPTIONS preflight requests by returning a 204 No Content.
 *
 * Usage:
 * @code
 * app_use(app, cors_allow_all);
 * @endcode
 */
DEFINE_CORS(cors_allow_all, "*", "GET, POST, PUT, DELETE, PATCH, OPTIONS",
            "Content-Type, Authorization", 86400)

/**
 * @brief CORS middleware that allows requests only from localhost.
 *
 * Sets the following headers on the response:
 * - Access-Control-Allow-Origin: http://localhost:*
 * - Access-Control-Allow-Methods: GET, POST, PUT, DELETE, PATCH, OPTIONS
 * - Access-Control-Allow-Headers: Content-Type, Authorization
 * - Access-Control-Max-Age: 86400 (1 day)
 *
 * Also handles OPTIONS preflight requests by returning a 204 No Content.
 *
 * Usage:
 * @code
 * app_use(app, cors_localhost);
 * @endcode
 */
DEFINE_CORS(cors_localhost, "http://localhost:*",
            "GET, POST, PUT, DELETE, PATCH, OPTIONS",
            "Content-Type, Authorization", 86400)

/**
 * @brief ONLY FOR UNIT TESTS.
 */
void parse_query_params(ResponseContext *res);

/**
 * @brief ONLY FOR UNIT TESTS.
 */
int handle_endpoint(ClientContext *ctx);

/**
 * @brief ONLY FOR UNIT TESTS.
 */
int parse_http_request(char *buffer, Request *req, uv_tcp_t *client,
                       ClientContext *ctx);

/**
 * @brief ONLY FOR UNIT TESTS.
 */
int compare_paths(char *target, char *request_path, Request *req);

#endif // ESPRESSO_H
