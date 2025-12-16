#include "../src/espresso.h"
#include <check.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#define sleep(x) Sleep((x) * 1000)
#define usleep(x) Sleep((x) / 1000)
typedef HANDLE thread_t;
typedef DWORD thread_return_t;
#define THREAD_CALL WINAPI
#define close_socket closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
typedef pthread_t thread_t;
typedef void *thread_return_t;
#define THREAD_CALL
#define close_socket close
#endif

#define BASE_TEST_PORT 9876
#define BUFFER_SIZE 8192

static int current_test_port = BASE_TEST_PORT;

typedef struct {
  char *data;
  size_t size;
  size_t capacity;
} ResponseBuffer;

void init_response_buffer(ResponseBuffer *buf) {
  buf->capacity = BUFFER_SIZE;
  buf->size = 0;
  buf->data = malloc(buf->capacity);
  buf->data[0] = '\0';
}

void free_response_buffer(ResponseBuffer *buf) {
  if (buf->data) {
    free(buf->data);
    buf->data = NULL;
  }
}

#ifdef _WIN32
void init_winsock(void) {
  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);
}

void cleanup_winsock(void) { WSACleanup(); }
#else
void init_winsock(void) {}
void cleanup_winsock(void) {}
#endif

int send_http_request(int port, const char *method, const char *path,
                      const char *body, const char *content_type,
                      ResponseBuffer *response) {
#ifdef _WIN32
  SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock == INVALID_SOCKET) {
    return -1;
  }
#else
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    return -1;
  }
#endif

  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

  int connected = 0;
  for (int i = 0; i < 10; i++) {
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) ==
        0) {
      connected = 1;
      break;
    }
    usleep(50000);
  }

  if (!connected) {
    close_socket(sock);
    return -1;
  }

  char request[BUFFER_SIZE];
  int len;

  if (body && content_type) {
    len = snprintf(request, sizeof(request),
                   "%s %s HTTP/1.1\r\n"
                   "Host: localhost:%d\r\n"
                   "Content-Type: %s\r\n"
                   "Content-Length: %zu\r\n"
                   "Connection: close\r\n"
                   "\r\n"
                   "%s",
                   method, path, port, content_type, strlen(body), body);
  } else {
    len = snprintf(request, sizeof(request),
                   "%s %s HTTP/1.1\r\n"
                   "Host: localhost:%d\r\n"
                   "Connection: close\r\n"
                   "\r\n",
                   method, path, port);
  }

  if (send(sock, request, len, 0) < 0) {
    close_socket(sock);
    return -1;
  }

  int bytes_received;
  char buffer[BUFFER_SIZE];

  while ((bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
    if (response->size + bytes_received >= response->capacity) {
      response->capacity *= 2;
      response->data = realloc(response->data, response->capacity);
    }
    memcpy(response->data + response->size, buffer, bytes_received);
    response->size += bytes_received;
    response->data[response->size] = '\0';
  }

  close_socket(sock);
  return 0;
}

char *get_response_body(const char *response) {
  char *body = strstr(response, "\r\n\r\n");
  return body ? body + 4 : NULL;
}

int get_status_code(const char *response) {
  int status = 0;
  sscanf(response, "HTTP/1.1 %d", &status);
  return status;
}

int response_contains_header(const char *response, const char *header,
                             const char *value) {
  char search[256];
  snprintf(search, sizeof(search), "%s: %s", header, value);
  return strstr(response, search) != NULL;
}

/* ==================== Test Handlers ==================== */

void hello_handler(ResponseContext *res) {
  send_text_response(res, "Hello, World!");
}

void json_handler(ResponseContext *res) {
  cJSON *json = cJSON_CreateObject();
  cJSON_AddStringToObject(json, "message", "success");
  cJSON_AddNumberToObject(json, "code", 200);
  send_json_response(res, json);
}

void echo_handler(ResponseContext *res) {
  char *msg = get_query_string(res, "msg");
  if (!msg) {
    send_error(res, 400, "Missing 'msg' parameter");
    return;
  }
  send_text_response(res, msg);
}

void user_by_id_handler(ResponseContext *res) {
  char *id = get_param(res, "id");
  if (!id) {
    send_error(res, 400, "Missing user ID");
    return;
  }

  cJSON *json = cJSON_CreateObject();
  cJSON_AddStringToObject(json, "id", id);
  cJSON_AddStringToObject(json, "name", "John Doe");
  send_json_response(res, json);
}

void create_user_handler(ResponseContext *res) {
  if (!res->req->body || res->req->body->type != BODY_JSON) {
    send_error(res, 400, "Expected JSON body");
    return;
  }

  cJSON *json = res->req->body->data.json;
  cJSON *name = cJSON_GetObjectItem(json, "name");

  if (!name || !cJSON_IsString(name)) {
    send_error(res, 400, "Missing 'name' field");
    return;
  }

  cJSON *response = cJSON_CreateObject();
  cJSON_AddStringToObject(response, "id", "123");
  cJSON_AddStringToObject(response, "name", name->valuestring);
  cJSON_AddStringToObject(response, "status", "created");

  res->status = 201;
  send_json_response(res, response);
}

void update_user_handler(ResponseContext *res) {
  char *id = get_param(res, "id");

  if (!res->req->body || res->req->body->type != BODY_JSON) {
    send_error(res, 400, "Expected JSON body");
    return;
  }

  cJSON *json = res->req->body->data.json;
  cJSON *name = cJSON_GetObjectItem(json, "name");

  cJSON *response = cJSON_CreateObject();
  cJSON_AddStringToObject(response, "id", id);
  if (name && cJSON_IsString(name)) {
    cJSON_AddStringToObject(response, "name", name->valuestring);
  }
  cJSON_AddStringToObject(response, "status", "updated");

  send_json_response(res, response);
}

void delete_user_handler(ResponseContext *res) {
  char *id = get_param(res, "id");

  cJSON *response = cJSON_CreateObject();
  cJSON_AddStringToObject(response, "id", id);
  cJSON_AddStringToObject(response, "status", "deleted");

  send_json_response(res, response);
}

void header_test_handler(ResponseContext *res) {
  set_header(res, "X-Custom-Header", "test-value");
  set_header(res, "X-Another-Header", "another-value");
  send_text_response(res, "Headers set");
}

MiddlewareResult auth_middleware(ResponseContext *res) {
  char *auth = get_header(res, "Authorization");

  if (!auth || strncmp(auth, "Bearer ", 7) != 0) {
    send_error(res, 401, "Missing or invalid authorization");
    return MIDDLEWARE_STOP;
  }

  set_data_string(res, "user", "authenticated_user");
  return MIDDLEWARE_CONTINUE;
}

void protected_handler(ResponseContext *res) {
  char *user = get_data_string(res, "user");

  cJSON *json = cJSON_CreateObject();
  cJSON_AddStringToObject(json, "message", "Protected resource");
  cJSON_AddStringToObject(json, "user", user ? user : "unknown");
  send_json_response(res, json);
}

/* ==================== Server Thread Management ==================== */

typedef struct {
  App *app;
  int port;
  thread_t thread;
  volatile int running;
} ServerContext;

thread_return_t THREAD_CALL run_server_thread(void *arg) {
  ServerContext *ctx = (ServerContext *)arg;
  app_listen(ctx->app);
#ifdef _WIN32
  return 0;
#else
  return NULL;
#endif
}

ServerContext *start_server(int port) {
  ServerContext *ctx = malloc(sizeof(ServerContext));
  ctx->app = create_app(port);
  ctx->port = port;
  ctx->running = 1;

  return ctx;
}

void configure_and_start(ServerContext *ctx) {
#ifdef _WIN32
  ctx->thread = CreateThread(NULL, 0, run_server_thread, ctx, 0, NULL);
#else
  pthread_create(&ctx->thread, NULL, run_server_thread, ctx);
#endif
  usleep(200000);
}

void stop_server(ServerContext *ctx) {
  if (ctx) {
    ctx->running = 0;

#ifdef _WIN32
    TerminateThread(ctx->thread, 0);
    WaitForSingleObject(ctx->thread, INFINITE);
    CloseHandle(ctx->thread);
#else
    pthread_cancel(ctx->thread);
    pthread_join(ctx->thread, NULL);
#endif

    App *app = ctx->app;
    if (app) {
      if (app->endpoints) {
        for (int i = 0; i < app->endpoint_count; i++) {
          if (app->endpoints[i].middlewares)
            free(app->endpoints[i].middlewares);
          if (app->endpoints[i].path)
            free(app->endpoints[i].path);
          if (app->endpoints[i].group_path)
            free(app->endpoints[i].group_path);
        }
        free(app->endpoints);
      }

      if (app->middlewares)
        free(app->middlewares);

      if (app->groups) {
        for (int i = 0; i < app->groups_count; i++) {
          if (app->groups[i].middlewares)
            free(app->groups[i].middlewares);
          if (app->groups[i].root_path)
            free(app->groups[i].root_path);
        }
        free(app->groups);
      }

      if (app->clients)
        free(app->clients);

      free(app);
    }

    free(ctx);
    usleep(200000);
  }
}

START_TEST(test_simple_get_request) {
  int port = current_test_port++;
  ServerContext *ctx = start_server(port);
  _app_append_endpoint(ctx->app, GET, "/hello", hello_handler, NULL);
  configure_and_start(ctx);

  ResponseBuffer response;
  init_response_buffer(&response);

  int result = send_http_request(port, "GET", "/hello", NULL, NULL, &response);

  ck_assert_int_eq(result, 0);
  ck_assert_int_eq(get_status_code(response.data), 200);

  char *body = get_response_body(response.data);
  ck_assert_ptr_nonnull(body);
  ck_assert_str_eq(body, "Hello, World!");

  free_response_buffer(&response);
  stop_server(ctx);
}
END_TEST

START_TEST(test_json_response) {
  int port = current_test_port++;
  ServerContext *ctx = start_server(port);
  _app_append_endpoint(ctx->app, GET, "/json", json_handler, NULL);
  configure_and_start(ctx);

  ResponseBuffer response;
  init_response_buffer(&response);

  send_http_request(port, "GET", "/json", NULL, NULL, &response);

  ck_assert_int_eq(get_status_code(response.data), 200);
  ck_assert(response_contains_header(response.data, "Content-Type",
                                     "application/json"));

  char *body = get_response_body(response.data);
  cJSON *json = cJSON_Parse(body);
  ck_assert_ptr_nonnull(json);

  cJSON *message = cJSON_GetObjectItem(json, "message");
  ck_assert_str_eq(message->valuestring, "success");

  cJSON *code = cJSON_GetObjectItem(json, "code");
  ck_assert_int_eq(code->valueint, 200);

  cJSON_Delete(json);
  free_response_buffer(&response);
  stop_server(ctx);
}
END_TEST

START_TEST(test_query_parameters) {
  int port = current_test_port++;
  ServerContext *ctx = start_server(port);
  _app_append_endpoint(ctx->app, GET, "/echo", echo_handler, NULL);
  configure_and_start(ctx);

  ResponseBuffer response;
  init_response_buffer(&response);

  send_http_request(port, "GET", "/echo?msg=hello+world", NULL, NULL,
                    &response);

  ck_assert_int_eq(get_status_code(response.data), 200);

  char *body = get_response_body(response.data);
  ck_assert_str_eq(body, "hello world");

  free_response_buffer(&response);
  stop_server(ctx);
}
END_TEST

START_TEST(test_path_parameters) {
  int port = current_test_port++;
  ServerContext *ctx = start_server(port);
  _app_append_endpoint(ctx->app, GET, "/users/:id", user_by_id_handler, NULL);
  configure_and_start(ctx);

  ResponseBuffer response;
  init_response_buffer(&response);

  send_http_request(port, "GET", "/users/42", NULL, NULL, &response);

  ck_assert_int_eq(get_status_code(response.data), 200);

  char *body = get_response_body(response.data);
  cJSON *json = cJSON_Parse(body);

  cJSON *id = cJSON_GetObjectItem(json, "id");
  ck_assert_str_eq(id->valuestring, "42");

  cJSON_Delete(json);
  free_response_buffer(&response);
  stop_server(ctx);
}
END_TEST

START_TEST(test_post_json) {
  int port = current_test_port++;
  ServerContext *ctx = start_server(port);
  _app_append_endpoint(ctx->app, POST, "/users", create_user_handler, NULL);
  configure_and_start(ctx);

  ResponseBuffer response;
  init_response_buffer(&response);

  const char *json_body =
      "{\"name\":\"Alice\",\"email\":\"alice@example.com\"}";
  send_http_request(port, "POST", "/users", json_body, "application/json",
                    &response);

  ck_assert_int_eq(get_status_code(response.data), 201);

  char *body = get_response_body(response.data);
  cJSON *json = cJSON_Parse(body);

  cJSON *name = cJSON_GetObjectItem(json, "name");
  ck_assert_str_eq(name->valuestring, "Alice");

  cJSON *status = cJSON_GetObjectItem(json, "status");
  ck_assert_str_eq(status->valuestring, "created");

  cJSON_Delete(json);
  free_response_buffer(&response);
  stop_server(ctx);
}
END_TEST

START_TEST(test_patch_request) {
  int port = current_test_port++;
  ServerContext *ctx = start_server(port);
  _app_append_endpoint(ctx->app, PATCH, "/users/:id", update_user_handler,
                       NULL);
  configure_and_start(ctx);

  ResponseBuffer response;
  init_response_buffer(&response);

  const char *json_body = "{\"name\":\"Bob Updated\"}";
  send_http_request(port, "PATCH", "/users/99", json_body, "application/json",
                    &response);

  ck_assert_int_eq(get_status_code(response.data), 200);

  char *body = get_response_body(response.data);
  cJSON *json = cJSON_Parse(body);

  cJSON *id = cJSON_GetObjectItem(json, "id");
  ck_assert_str_eq(id->valuestring, "99");

  cJSON *name = cJSON_GetObjectItem(json, "name");
  ck_assert_str_eq(name->valuestring, "Bob Updated");

  cJSON_Delete(json);
  free_response_buffer(&response);
  stop_server(ctx);
}
END_TEST

START_TEST(test_delete_request) {
  int port = current_test_port++;
  ServerContext *ctx = start_server(port);
  _app_append_endpoint(ctx->app, DELETE, "/users/:id", delete_user_handler,
                       NULL);
  configure_and_start(ctx);

  ResponseBuffer response;
  init_response_buffer(&response);

  send_http_request(port, "DELETE", "/users/42", NULL, NULL, &response);

  ck_assert_int_eq(get_status_code(response.data), 200);

  char *body = get_response_body(response.data);
  cJSON *json = cJSON_Parse(body);

  cJSON *status = cJSON_GetObjectItem(json, "status");
  ck_assert_str_eq(status->valuestring, "deleted");

  cJSON_Delete(json);
  free_response_buffer(&response);
  stop_server(ctx);
}
END_TEST

START_TEST(test_404_not_found) {
  int port = current_test_port++;
  ServerContext *ctx = start_server(port);
  _app_append_endpoint(ctx->app, GET, "/exists", hello_handler, NULL);
  configure_and_start(ctx);

  ResponseBuffer response;
  init_response_buffer(&response);

  send_http_request(port, "GET", "/does-not-exist", NULL, NULL, &response);

  ck_assert_int_eq(get_status_code(response.data), 404);

  free_response_buffer(&response);
  stop_server(ctx);
}
END_TEST

START_TEST(test_custom_headers) {
  int port = current_test_port++;
  ServerContext *ctx = start_server(port);
  _app_append_endpoint(ctx->app, GET, "/headers", header_test_handler, NULL);
  configure_and_start(ctx);

  ResponseBuffer response;
  init_response_buffer(&response);

  send_http_request(port, "GET", "/headers", NULL, NULL, &response);

  ck_assert(
      response_contains_header(response.data, "X-Custom-Header", "test-value"));
  ck_assert(response_contains_header(response.data, "X-Another-Header",
                                     "another-value"));

  free_response_buffer(&response);
  stop_server(ctx);
}
END_TEST

START_TEST(test_middleware) {
  int port = current_test_port++;
  ServerContext *ctx = start_server(port);
  Middleware mws[] = {auth_middleware, NULL};
  _app_append_endpoint(ctx->app, GET, "/protected", protected_handler, mws);
  configure_and_start(ctx);

  ResponseBuffer response;
  init_response_buffer(&response);

  send_http_request(port, "GET", "/protected", NULL, NULL, &response);
  ck_assert_int_eq(get_status_code(response.data), 401);

  free_response_buffer(&response);
  stop_server(ctx);
}
END_TEST

START_TEST(test_cors_middleware) {
  int port = current_test_port++;
  ServerContext *ctx = start_server(port);
  app_use(ctx->app, cors_allow_all);
  _app_append_endpoint(ctx->app, GET, "/test", hello_handler, NULL);
  configure_and_start(ctx);

  ResponseBuffer response;
  init_response_buffer(&response);

  send_http_request(port, "GET", "/test", NULL, NULL, &response);

  ck_assert(response_contains_header(response.data,
                                     "Access-Control-Allow-Origin", "*"));

  free_response_buffer(&response);
  stop_server(ctx);
}
END_TEST

START_TEST(test_route_group) {
  int port = current_test_port++;
  ServerContext *ctx = start_server(port);

  AppGroup *api = _app_create_group(ctx->app, "/api", NULL);
  _app_append_endpoint_to_group(ctx->app, api, GET, "/users/:id",
                                user_by_id_handler, NULL);
  configure_and_start(ctx);

  ResponseBuffer response;
  init_response_buffer(&response);

  send_http_request(port, "GET", "/api/users/123", NULL, NULL, &response);

  ck_assert_int_eq(get_status_code(response.data), 200);

  char *body = get_response_body(response.data);
  cJSON *json = cJSON_Parse(body);

  cJSON *id = cJSON_GetObjectItem(json, "id");
  ck_assert_str_eq(id->valuestring, "123");

  cJSON_Delete(json);
  free_response_buffer(&response);
  stop_server(ctx);
}
END_TEST

Suite *integration_suite(void) {
  Suite *s = suite_create("Integration Tests");
  TCase *tc = tcase_create("Core");

  tcase_set_timeout(tc, 30);

  tcase_add_test(tc, test_simple_get_request);
  tcase_add_test(tc, test_json_response);
  tcase_add_test(tc, test_query_parameters);
  tcase_add_test(tc, test_path_parameters);
  tcase_add_test(tc, test_post_json);
  tcase_add_test(tc, test_patch_request);
  tcase_add_test(tc, test_delete_request);
  tcase_add_test(tc, test_404_not_found);
  tcase_add_test(tc, test_custom_headers);
  tcase_add_test(tc, test_middleware);
  tcase_add_test(tc, test_cors_middleware);
  tcase_add_test(tc, test_route_group);

  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  int failed = 0;

  init_winsock();

  Suite *s = integration_suite();
  SRunner *sr = srunner_create(s);

  srunner_run_all(sr, CK_VERBOSE);
  failed = srunner_ntests_failed(sr);
  srunner_free(sr);

  cleanup_winsock();

  return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
