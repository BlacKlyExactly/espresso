#ifdef _WIN32
#include <signal.h>
#include <windows.h>
#else
#include <strings.h>
#include <unistd.h>
#endif
#include "../vendor/cJSON/cJSON.h"
#include "espresso.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <uv.h>

static App *lib_global_app = NULL;
uv_loop_t *loop = NULL;

#ifdef _WIN32
#include <ctype.h>
#include <string.h>

char *strcasestr(const char *haystack, const char *needle) {
  if (!*needle)
    return (char *)haystack;

  for (; *haystack; haystack++) {
    const char *h = haystack;
    const char *n = needle;

    while (*h && *n &&
           tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
      h++;
      n++;
    }

    if (!*n)
      return (char *)haystack;
  }

  return NULL;
}

char *strsep(char **stringp, const char *delim) {
  char *start = *stringp;
  char *p;

  if (!start)
    return NULL;

  p = strpbrk(start, delim);
  if (p) {
    *p = '\0';
    *stringp = p + 1;
  } else {
    *stringp = NULL;
  }
  return start;
}
#endif

#ifdef _WIN32
BOOL WINAPI console_handler(DWORD signal) {
  if (signal == CTRL_C_EVENT) {
    if (lib_global_app)
      app_close(lib_global_app);
    printf("\nServer stopped by Ctrl+C\n");
    exit(0);
  }
  return TRUE;
}
#endif

void handle_client_read(uv_stream_t *stream, ssize_t nread,
                        const uv_buf_t *buf);

void log_error(const char *fmt, ...) {
  time_t t = time(NULL);
  struct tm *tm_info = localtime(&t);
  char time_str[26];
  strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

  fprintf(stderr, "[%s] ERROR: ", time_str);

  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);

  fprintf(stderr, "\n");
}

static void lib_handle_sigint(int sig) {
  if (lib_global_app) {
    app_close(lib_global_app);
    lib_global_app = NULL;
  }
  printf("\nServer stopped by Ctrl+C\n");
  fflush(stdout);
  exit(0);
}

const char *MethodNames[] = {"GET",  "POST",    "PATCH", "DELETE",
                             "HEAD", "OPTIONS", "PUT"};

const char *bad_request_response = "HTTP/1.1 400 Bad Request\r\n"
                                   "Content-Type: text/plain\r\n"
                                   "Content-Length: 11\r\n"
                                   "Connection: close\r\n"
                                   "\r\n"
                                   "Bad Request";

const char *payload_too_large_response = "HTTP/1.1 413 Payload Too Large\r\n"
                                         "Content-Type: text/plain\r\n"
                                         "Content-Length: 17\r\n"
                                         "Connection: close\r\n"
                                         "\r\n"
                                         "Payload Too Large";

const char *not_found_response = "HTTP/1.1 404 Not Found\r\n"
                                 "Content-Type: text/plain\r\n"
                                 "Content-Length: 13\r\n"
                                 "Connection: close\r\n"
                                 "\r\n"
                                 "404 Not Found";

const char *http_version_not_supported_response =
    "HTTP/1.1 505 HTTP Version Not Supported\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 25\r\n"
    "\r\n"
    "HTTP Version Not Supported";

const char *headers_too_large_response =
    "HTTP/1.1 431 Request Header Fields Too Large\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 31\r\n"
    "\r\n"
    "Request Header Fields Too Large";

const char *request_timeput_response = "HTTP/1.1 408 Request Timeout\r\n"
                                       "Content-Type: text/plain\r\n"
                                       "Content-Length: 15\r\n"
                                       "\r\n"
                                       "Request Timeout";

void remove_client(App *app, uv_tcp_t *client) {
  for (int i = 0; i < app->client_count; i++) {
    if (app->clients[i] == client) {
      app->clients[i] = app->clients[app->client_count - 1];
      app->client_count--;
      break;
    }
  }
}

void add_client(App *app, uv_tcp_t *client) {
  if (app->client_count >= app->client_capacity) {
    int new_capacity = app->client_capacity * 2;
    app->clients = realloc(app->clients, sizeof(uv_tcp_t *) * new_capacity);
    app->client_capacity = new_capacity;
  }
  app->clients[app->client_count] = client;
  app->client_count++;
}

void handle_timer_close(uv_handle_t *handle) { free(handle); }

void handle_write_end(uv_write_t *uv_req, int status) {
  if (status < 0) {
    fprintf(stderr, "Write error: %s\n", uv_strerror(status));
  }
  free(uv_req);
}

void send_message(uv_tcp_t *client, const char *message) {
  uv_write_t *req = malloc(sizeof(uv_write_t));
  uv_buf_t buf = uv_buf_init((char *)message, strlen(message));

  uv_write(req, (uv_stream_t *)client, &buf, 1, handle_write_end);
}

void request_cleanup(ClientContext *ctx) {
  if (!ctx)
    return;

  if (ctx->req) {
    if (ctx->req->body) {
      if (ctx->req->body->type == BODY_JSON && ctx->req->body->data.json)
        cJSON_Delete(ctx->req->body->data.json);
      free(ctx->req->body);
    }
    if (ctx->req->params)
      free(ctx->req->params);
    free(ctx->req);
    ctx->req = NULL;
  }

  if (ctx->res) {
    if (ctx->res->data.entries) {
      for (int i = 0; i < ctx->res->data.count; i++)
        free(ctx->res->data.entries[i].key);
      free(ctx->res->data.entries);
    }
    if (ctx->res->query.entries) {
      for (int i = 0; i < ctx->res->query.count; i++) {
        free(ctx->res->query.entries[i].key);
        free(ctx->res->query.entries[i].value);
      }
      free(ctx->res->query.entries);
    }
    free(ctx->res);
    ctx->res = NULL;
  }

  ctx->buffer_len = 0;
  ctx->headers_parsed = 0;
  ctx->content_length = 0;

  ctx->request_count++;
  if (ctx->request_count >= MAX_KEEP_ALIVE_REQUESTS)
    ctx->keep_alive = 0;
}

void endpoint_cleanup(ClientContext *ctx) {
  if (!ctx)
    return;

  request_cleanup(ctx);

  if (ctx->buffer)
    free(ctx->buffer);

  if (ctx->req_timer) {
    uv_timer_stop(ctx->req_timer);
    uv_close((uv_handle_t *)ctx->req_timer, handle_timer_close);
    ctx->req_timer = NULL;
  }

  free(ctx);
}

void handle_connection_close(uv_handle_t *client) {
  ClientContext *ctx = client->data;
  remove_client(ctx->app, (uv_tcp_t *)client);
  endpoint_cleanup(ctx);
  free(client);
}

void close_connection(uv_tcp_t *client) {
  uv_close((uv_handle_t *)client, handle_connection_close);
}

static int hex_to_int(char c) {
  if ('0' <= c && c <= '9')
    return c - '0';
  if ('a' <= c && c <= 'f')
    return c - 'a' + 10;
  if ('A' <= c && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

void decode_query(char *str) {
  char *src = str;
  char *dst = str;

  while (*src) {
    if (*src == '%' && isxdigit(*(src + 1)) && isxdigit(*(src + 2))) {
      int hi = hex_to_int(*(src + 1));
      int lo = hex_to_int(*(src + 2));
      if (hi >= 0 && lo >= 0) {
        *dst = (char)((hi << 4) | lo);
        dst++;
        src += 3;
        continue;
      }
    } else if (*src == '+') {
      *dst = ' ';
      dst++;
      src++;
      continue;
    }

    *dst = *src;
    dst++;
    src++;
  }

  *dst = '\0';
}

void append_query(ResponseContext *res, const char *key, const char *value) {
  char *key_dup = strdup(key);
  char *value_dup = strdup(value ? value : "");

  decode_query(key_dup);
  decode_query(value_dup);

  if (res->query.count >= res->query.capacity) {
    res->query.capacity *= 2;

    DynamicKeyValue *new_entries = realloc(
        res->query.entries, sizeof(DynamicKeyValue) * res->query.capacity);

    if (!new_entries) {
      free(res->query.entries);
      res->query.entries = NULL;
      res->query.capacity = 10;
      res->query.count = 0;
      log_error("Failed to realloc query entries");
      return;
    }
  }

  res->query.entries[res->query.count].key = key_dup;
  res->query.entries[res->query.count].value = value_dup;
  res->query.count++;
}

void parse_query_params(ResponseContext *res) {
  char *target_copy = strdup(res->req->path);

  if (!target_copy) {
    log_error("Faiied to malloc target copy");
    return;
  }

  char *query = strchr(target_copy, '?');

  if (!query) {
    free(target_copy);
    return;
  }

  query++;

  if (!res->query.entries) {
    log_error("Failed to malloc query entries");
    free(target_copy);
    return;
  }

  char *start = query;

  while (start && *start) {
    char *amp = strchr(start, '&');
    char *param = start;

    if (amp) {
      *amp = '\0';
    }

    char *eq = strchr(param, '=');
    char *key = param;

    if (eq) {
      *eq = '\0';
      char *value = eq + 1;
      append_query(res, key, value);
    } else {
      append_query(res, key, NULL);
    }

    if (!amp)
      break;

    start = amp + 1;
  }

  free(target_copy);
}

int compare_paths(char *target, char *request_path, Request *req) {
  if (strcmp(target, request_path) == 0)
    return 1;

  char *target_copy = strdup(target);

  if (!target_copy)
    return 0;

  char *request_copy = strdup(request_path);

  if (!request_copy) {
    free(target_copy);
    return 0;
  }

  char *query = strchr(request_copy, '?');
  if (query)
    *query = '\0';

  size_t target_len = strlen(target_copy);
  size_t request_len = strlen(request_copy);

  if (target_len > 0 && target_copy[target_len - 1] == '/')
    target_copy[target_len - 1] = '\0';

  if (request_len > 0 && request_copy[request_len - 1] == '/')
    request_copy[request_len - 1] = '\0';

  if (strcmp(request_copy, target_copy) == 0) {
    free(target_copy);
    free(request_copy);
    return 1;
  }

  int capacity = 10;
  int count = 0;
  KeyValue *params = malloc(sizeof(KeyValue) * capacity);

  if (!params) {
    perror("Failed to malloc params");
    free(target_copy);
    free(request_copy);
    return 0;
  }

  char *target_segment, *request_segment;
  char *target_ptr = target_copy;
  char *request_ptr = request_copy;

  while ((target_segment = strsep(&target_ptr, "/")) &&
         (request_segment = strsep(&request_ptr, "/"))) {
    if (*target_segment != ':' &&
        strcmp(target_segment, request_segment) != 0) {
      free(params);
      free(target_copy);
      free(request_copy);
      return 0;
    } else if (*target_segment == ':') {
      if (count >= capacity) {
        capacity *= 2;
        KeyValue *new_params = realloc(params, sizeof(KeyValue) * capacity);

        if (!new_params) {
          free(params);
          free(target_copy);
          free(request_copy);
          return 0;
        }

        params = new_params;
      }

      char *param_key = target_segment;
      param_key++;
      strncpy(params[count].key, param_key, KEY_SIZE - 1);
      strncpy(params[count].value, request_segment, VALUE_SIZE - 1);

      params[count].key[KEY_SIZE - 1] = '\0';
      params[count].value[VALUE_SIZE - 1] = '\0';

      count++;
    }
  }

  int match = (target_ptr == NULL && request_ptr == NULL);

  if (match) {
    if (req->params)
      free(req->params);

    req->params = params;
    req->params_count = count;
  } else {
    free(params);
  }

  free(target_copy);
  free(request_copy);

  return match;
}
void generate_allow_header(App *app, const char *path, char *allow_header,
                           size_t size) {
  int first = 1;
  int has_get = 0;

  for (int i = 0; i < app->endpoint_count; i++) {
    if (strcmp(app->endpoints[i].path, path) == 0) {
      if (!first)
        strncat(allow_header, ", ", size - strlen(allow_header) - 1);
      strncat(allow_header, MethodNames[app->endpoints[i].method],
              size - strlen(allow_header) - 1);
      first = 0;

      if (app->endpoints[i].method == GET)
        has_get = 1;
    }
  }

  if (has_get) {
    if (!first)
      strncat(allow_header, ", ", size - strlen(allow_header) - 1);
    strncat(allow_header, "HEAD", size - strlen(allow_header) - 1);
    first = 0;
  }

  if (!first)
    strncat(allow_header, ", ", size - strlen(allow_header) - 1);
  strncat(allow_header, "OPTIONS", size - strlen(allow_header) - 1);
}

int parse_http_request(char *buffer, Request *req, uv_tcp_t *client,
                       ClientContext *ctx) {
  req->header_count = 0;
  req->params_count = 0;
  req->params = NULL;

  if (sscanf(buffer, "%7s %255s %15s", req->method, req->path, req->version) !=
      3) {
    log_error("Scan path and method");
    send_message(client, bad_request_response);
    return 1;
  }

  int keep_alive = 0;

  if (strcmp(req->version, "HTTP/1.1") == 0) {
    keep_alive = 1;
  }

  int content_length_seen = 0;
  size_t first_content_length = 0;

  char *header_end = strstr(buffer, "\r\n\r\n");

  if (header_end != NULL) {
    *header_end = '\0';
    char *body = header_end + 4;
    char *content_type = NULL;
    size_t content_length = 0;

    char *line = strtok(buffer, "\r\n");

    while (line != NULL && line[0] != '\0') {
      char *colon = strchr(line, ':');

      if (colon) {
        *colon = '\0';
        char *key = line;
        char *value = colon + 1;

        while (*value == ' ')
          value++;

        strncpy(req->headers[req->header_count].key, key, KEY_SIZE - 1);
        strncpy(req->headers[req->header_count].value, value, VALUE_SIZE - 1);
        req->header_count++;

        if (strcasecmp(key, "Connection") == 0) {
          if (strcasecmp(value, "close") == 0) {
            keep_alive = 0;
          } else if (strcasecmp(value, "keep-alive") == 0) {
            keep_alive = 1;
          }
        }

        if (strcasecmp(key, "Content-Type") == 0) {
          content_type = value;
        }

        if (strcasecmp(key, "Content-Length") == 0) {
          size_t cl = 0;
          if (sscanf(value, "%zu", &cl) != 1) {
            send_message(client, bad_request_response);
            log_error("Failed to scan Content-Length");
            return 1;
          }

          if (content_length_seen && first_content_length != cl) {
            send_message(client, bad_request_response);
            log_error("Multiple conflicting Content-Length headers");
            return 1;
          }

          if (cl > MAX_BODY_SIZE) {
            send_message(client, payload_too_large_response);
            log_error("content-length exceeds max body size");
            return 1;
          }

          content_length_seen = 1;
          first_content_length = cl;
          content_length = cl;
        }
      }

      line = strtok(NULL, "\r\n");
    }

    ctx->keep_alive = keep_alive;

    if (content_length > MAX_BODY_SIZE) {
      send_message(client, payload_too_large_response);
      return 1;
    }

    Body *req_body = calloc(1, sizeof(Body));

    if (content_type && strcmp(content_type, "application/json") == 0) {
      req_body->type = BODY_JSON;
      req_body->data.json = cJSON_Parse(body);

      req->body = req_body;

      if (!req->body->data.json) {
        req_body->type = BODY_TEXT;
        req_body->data.text = body;

        req->body = req_body;
      }
    } else {
      req_body->type = BODY_TEXT;
      req_body->data.text = body;

      req->body = req_body;
    }
  }

  return 0;
}

App *create_app(int port) {
  App *app = malloc(sizeof(App));
  lib_global_app = app;

#ifdef _WIN32
  SetConsoleCtrlHandler(console_handler, TRUE);
#else
  signal(SIGINT, lib_handle_sigint);
#endif

  if (app == NULL) {
    printf("Failed to malloc an app");
    return NULL;
  }

  app->client_count = 0;
  app->client_capacity = 10;
  app->clients = calloc(app->client_capacity, sizeof(uv_tcp_t *));

  if (!app->clients) {
    log_error("Failed to malloc clients");
    return NULL;
  }

  app->endpoint_count = 0;
  app->endpoint_capacity = 10;
  app->endpoints = calloc(app->endpoint_capacity, sizeof(Endpoint));

  if (app->endpoints == NULL) {
    printf("Failed to malloc an endpoints");
    free(app->clients);
    return NULL;
  }

  app->middleware_count = 0;
  app->middleware_capacity = 10;
  app->middlewares = calloc(app->middleware_capacity, sizeof(Middleware));

  if (app->middlewares == NULL) {
    printf("Failed to malloc middlewares");
    free(app->endpoints);
    free(app->clients);
    free(app);
    return NULL;
  }

  app->groups_count = 0;
  app->groups_capacity = 10;
  app->groups = calloc(app->groups_capacity, sizeof(AppGroup));

  if (app->groups == NULL) {
    printf("Failed to malloc groups");
    free(app->endpoints);
    free(app->middlewares);
    free(app->clients);
    free(app);
    return NULL;
  }

  app->port = port;
  loop = uv_default_loop();

  uv_tcp_init(loop, &app->server);
  app->server.data = app;

  struct sockaddr_in address;

  uv_ip4_addr("0.0.0.0", port, &address);
  uv_tcp_bind(&app->server, (const struct sockaddr *)&address, 0);

  return app;
}

int handle_endpoint(ClientContext *ctx) {
  Request *req = ctx->req;
  ResponseContext *res = ctx->res;
  App *app = ctx->app;

  Endpoint curr;
  int exists = 0;

  for (int i = 0; i < app->endpoint_count; i++) {
    curr = app->endpoints[i];

    // TODO: This sucks, but its working so im gonna keep this for now
    int do_paths_match = compare_paths(curr.path, req->path, req);

    if (do_paths_match) {
      exists = 1;
      if (strcmp(MethodNames[curr.method], req->method) == 0) {
        parse_query_params(res);

        int should_continue = 1;
        for (int i = 0; i < app->middleware_count && should_continue; i++) {
          should_continue = !app->middlewares[i](res);
        }

        if (should_continue) {
          for (int i = 0; i < curr.middleware_count && should_continue; i++) {
            should_continue = !curr.middlewares[i](res);
          }
        }

        if (should_continue) {
          curr.handler(res);
        }

        break;
      }
      if (curr.method == GET && strcmp(req->method, "HEAD") == 0) {
        req->body = NULL;

        int should_continue = 1;
        for (int i = 0; i < app->middleware_count && should_continue; i++) {
          should_continue = !app->middlewares[i](res);
        }

        if (should_continue) {
          for (int i = 0; i < curr.middleware_count && should_continue; i++) {
            should_continue = !curr.middlewares[i](res);
          }
        }

        if (should_continue) {
          curr.handler(res);
        }
      } else if (strcmp(req->method, "OPTIONS") == 0) {
        char allow_header[256] = {0};
        generate_allow_header(app, req->path, allow_header,
                              sizeof(allow_header));

        char response[512];
        snprintf(response, sizeof(response),
                 "HTTP/1.1 200 OK\r\n"
                 "Allow: %s\r\n"
                 "Content-Length: 0\r\n"
                 "Connection: close\r\n\r\n",
                 allow_header);
        send_message(res->ctx->client, response);
        break;
      } else {
        char allow_header[256] = {0};
        generate_allow_header(app, req->path, allow_header,
                              sizeof(allow_header));

        char response[512];
        snprintf(response, sizeof(response),
                 "HTTP/1.1 405 Method Not Allowed\r\n"
                 "Allow: %s\r\n"
                 "Content-Type: text/plain\r\n"
                 "Content-Length: 19\r\n"
                 "Connection: close\r\n\r\n"
                 "Method Not Allowed\n",
                 allow_header);
        send_message(res->ctx->client, response);
        return 1;
      }
    }
  }

  return exists;
}

void allocate_buffer(uv_handle_t *handle, size_t size, uv_buf_t *buffer) {
  buffer->base = malloc(size);
  buffer->len = size;
}

void handle_buffer(ClientContext *ctx) {
  ctx->buffer[ctx->buffer_len] = '\0';
  if (!ctx->headers_parsed) {
    char *headers_end = strstr(ctx->buffer, "\r\n\r\n");

    if (headers_end) {
      size_t headers_size = headers_end - ctx->buffer + 4;

      if (headers_size > MAX_HEADER_SIZE) {
        send_message(ctx->client, headers_too_large_response);
        close_connection(ctx->client);
        return;
      }

      ctx->headers_parsed = 1;
      char *content_length_string = strcasestr(ctx->buffer, "Content-Length: ");

      if (content_length_string) {
        if (!sscanf(content_length_string, "Content-Length: %zu",
                    &ctx->content_length)) {
          ctx->content_length = 0;
        }
      } else {
        ctx->content_length = 0;
      }
    } else {
      if (ctx->buffer_len > MAX_HEADER_SIZE) {
        send_message(ctx->client, headers_too_large_response);
        close_connection(ctx->client);
        return;
      }
    }
  }

  size_t total_needed =
      (strstr(ctx->buffer, "\r\n\r\n") - ctx->buffer) + 4 + ctx->content_length;

  if (ctx->buffer_len >= total_needed) {

    Request *req = malloc(sizeof(Request));

    if (!req) {
      log_error("Failed to malloc Request");
      close_connection(ctx->client);
      return;
    }
    ctx->buffer[ctx->buffer_len] = '\0';
    parse_http_request(ctx->buffer, req, ctx->client, ctx);

    if (strcmp(req->version, "HTTP/1.0") != 0 &&
        strcmp(req->version, "HTTP/1.1") != 0) {
      send_message(ctx->client, http_version_not_supported_response);

      close_connection(ctx->client);
      return;
    }

    int is_valid = 0;
    for (size_t i = 0; i < sizeof(MethodNames) / sizeof(MethodNames[0]); i++) {
      if (strcmp(req->method, MethodNames[i]) == 0) {
        is_valid = 1;
        break;
      }
    }

    if (!is_valid) {
      char allow_header[256] = {0};
      generate_allow_header(ctx->app, req->path, allow_header,
                            sizeof(allow_header));

      char response[512];
      snprintf(response, sizeof(response),
               "HTTP/1.1 405 Method Not Allowed\r\n"
               "Allow: %s\r\n"
               "Content-Type: text/plain\r\n"
               "Content-Length: 19\r\n"
               "Connection: close\r\n"
               "\r\n"
               "Method Not Allowed\n",
               allow_header);
      send_message(ctx->client, response);
      close_connection(ctx->client);
      return;
    }

    ResponseContext *res = malloc(sizeof(ResponseContext));

    if (!res) {
      log_error("Failed to malloc response context");
      close_connection(ctx->client);
      return;
    }

    res->ctx = ctx;
    res->req = req;
    res->header_count = 0;
    res->status = 200;

    res->data.capacity = 10;
    res->data.count = 0;
    res->data.entries = malloc(sizeof(ResponseDataEntry) * res->data.capacity);

    if (!res->data.entries) {
      log_error("Failed to malloc res->data.entries");
      close_connection(ctx->client);
      return;
    }

    res->query.capacity = 10;
    res->query.count = 0;
    res->query.entries = malloc(sizeof(DynamicKeyValue) * res->query.capacity);

    if (!res->query.entries) {
      log_error("Failed to malloc res->data.entries");
      close_connection(ctx->client);
      return;
    }

    ctx->res = res;
    ctx->req = req;

    int exists = handle_endpoint(ctx);

    if (!exists)
      send_message(ctx->client, not_found_response);

    if (ctx->keep_alive) {
      uv_read_stop((uv_stream_t *)ctx->client);
      request_cleanup(ctx);
      uv_timer_again(ctx->req_timer);
      uv_read_start((uv_stream_t *)ctx->client, allocate_buffer,
                    handle_client_read);
    } else {
      close_connection(ctx->client);
    }
  }
}

void handle_client_read(uv_stream_t *stream, ssize_t nread,
                        const uv_buf_t *buf) {

  ClientContext *ctx = stream->data;

  if (!ctx) {
    free(buf->base);
    return;
  }

  if (ctx->buffer_len + nread > MAX_REQUEST_SIZE) {
    send_message(ctx->client, payload_too_large_response);
    close_connection(ctx->client);
    free(buf->base);
    return;
  }

  if (nread < 0) {
    uv_close((uv_handle_t *)stream, NULL);
    free(ctx);
    free(buf->base);
    return;
  }

  if (nread > 0) {
    if (ctx->buffer_len + nread > ctx->buffer_capacity) {
      ctx->buffer_capacity *= 2;
      ctx->buffer = realloc(ctx->buffer, ctx->buffer_capacity);
    }
    memcpy(ctx->buffer + ctx->buffer_len, buf->base, nread);
    ctx->buffer_len += nread;
  }

  free(buf->base);
  handle_buffer(ctx);
}

void handle_client_timeout(uv_timer_t *timer) {
  ClientContext *ctx = timer->data;

  send_message(ctx->client, request_timeput_response);
  close_connection(ctx->client);
}

void handle_connection(uv_stream_t *server, int status) {
  if (status < 0) {
    log_error("Connection error");
    return;
  }

  App *app = (App *)server->data;

  uv_tcp_t *client = malloc(sizeof(uv_tcp_t));
  uv_tcp_init(server->loop, client);

  add_client(app, client);

  if (uv_accept(server, (uv_stream_t *)client) == 0) {
    ClientContext *ctx = malloc(sizeof(ClientContext));
    ctx->client = client;
    ctx->buffer_capacity = 2048;
    ctx->buffer = malloc(ctx->buffer_capacity);
    ctx->buffer_len = 0;
    ctx->headers_parsed = 0;
    ctx->content_length = 0;
    ctx->keep_alive = 0;
    ctx->request_count = 0;
    ctx->app = app;

    client->data = ctx;

    ctx->req_timer = malloc(sizeof(uv_timer_t));
    uv_timer_init(server->loop, ctx->req_timer);
    ctx->req_timer->data = ctx;
    uv_timer_start(ctx->req_timer, handle_client_timeout, REQUEST_TIMEOUT_TIME,
                   0);

    uv_read_start((uv_stream_t *)client, allocate_buffer, handle_client_read);
  } else {
    close_connection(client);
  }
}

void app_listen(App *app) {
  uv_listen((uv_stream_t *)&app->server, 1024, handle_connection);

  if (loop != NULL) {
    printf("Server listening on port %d\n", app->port);
    uv_run(loop, UV_RUN_DEFAULT);
  }
}

AppGroup *_app_create_group(App *app, const char *root_path,
                            Middleware *middlewares) {
  app->groups_capacity = 10;
  if (app->groups_count >= app->groups_capacity) {
    int new_capacity = app->groups_capacity * 2;

    AppGroup *new_groups =
        realloc(app->groups, new_capacity * sizeof(AppGroup));

    if (!new_groups) {
      log_error("Failed to realloc groups");
      return NULL;
    }

    app->groups = new_groups;
    app->groups_capacity = new_capacity;
  }

  AppGroup *group = &app->groups[app->groups_count];
  group->root_path = strdup(root_path);

  int middleware_count = 0;
  if (middlewares) {
    while (middlewares[middleware_count] != NULL) {
      middleware_count++;
    }
  }

  group->middleware_count = middleware_count;

  if (middleware_count > 0) {
    group->middlewares = malloc(sizeof(Middleware) * middleware_count);

    if (!group->middlewares) {
      log_error("Failed to malloc middlewares");
      return NULL;
    }

    for (int i = 0; i < middleware_count; i++) {
      group->middlewares[i] = middlewares[i];
    }
  } else {
    group->middlewares = NULL;
  }

  app->groups_count++;
  return group;
}

void _app_append_endpoint_to_group(App *app, AppGroup *group, Method method,
                                   const char *path, EndpointHandler handler,
                                   Middleware *middlewares) {
  if (!group)
    return;

  char *root_path = group->root_path;
  size_t root_path_len = strlen(root_path);
  size_t path_len = strlen(path);
  size_t buffer_size = root_path_len + path_len + 2;

  char *full_path = malloc(buffer_size);
  if (!full_path) {
    log_error("Failed to mallon an full path");
    return;
  }

  if (root_path[root_path_len - 1] == '/') {
    snprintf(full_path, buffer_size, "%s%s", root_path,
             path[0] == '/' ? path + 1 : path);
  } else {
    snprintf(full_path, buffer_size, "%s/%s", root_path,
             path[0] == '/' ? path + 1 : path);
  }

  Middleware *group_middlewares = group->middlewares;
  int group_count = group->middleware_count;

  Middleware *endpoint_middlewares = middlewares;
  int endpoint_count = 0;
  if (middlewares) {
    while (middlewares[endpoint_count] != NULL)
      endpoint_count++;
  }

  int total = group_count + endpoint_count;
  Middleware *combined = NULL;

  if (total > 0) {
    combined = malloc(total * sizeof(Middleware));

    if (!combined) {
      free(full_path);
      log_error("Failed to malloc combined middlewares");
      return;
    }

    for (int i = 0; i < group_count; i++) {
      combined[i] = group_middlewares[i];
    }

    for (int i = 0; i < endpoint_count; i++) {
      combined[i + group_count] = endpoint_middlewares[i];
    }
  }

  Endpoint *ep =
      _app_append_endpoint(app, method, full_path, handler, combined);

  ep->group_path = full_path;
  ep->middleware_count = total;

  free(combined);
}

Endpoint *_app_append_endpoint(App *app, Method method, const char *path,
                               EndpointHandler handler,
                               Middleware *middlewares) {
  if (app->endpoint_count >= app->endpoint_capacity) {
    int new_capacity = app->endpoint_capacity * 2;

    Endpoint *new_endpoints =
        realloc(app->endpoints, new_capacity * sizeof(Endpoint));

    if (!new_endpoints) {
      log_error("Failed to realloc endpoints");
      return NULL;
    }
    app->endpoints = new_endpoints;
    app->endpoint_capacity = new_capacity;
  }

  Endpoint *ep = &app->endpoints[app->endpoint_count];
  ep->method = method;
  ep->path = strdup(path);
  ep->handler = handler;

  int middleware_count = 0;
  if (middlewares) {
    while (middlewares[middleware_count] != NULL) {
      middleware_count++;
    }
  }

  ep->middleware_count = middleware_count;

  if (middleware_count > 0) {
    ep->middlewares = malloc(sizeof(Middleware) * middleware_count);
    if (!ep->middlewares) {
      log_error("Failed to malloc middlewares");
      return NULL;
    }

    for (int i = 0; i < middleware_count; i++) {
      ep->middlewares[i] = middlewares[i];
    }
  } else {
    ep->middlewares = NULL;
  }

  app->endpoint_count++;

  return ep;
}

void app_use(App *app, Middleware mw) {
  if (app->middleware_count >= app->middleware_capacity) {
    int new_capacity = app->middleware_capacity * 2;

    Middleware *new_middlewares =
        realloc(app->middlewares, new_capacity * sizeof(Middleware));

    if (!new_middlewares) {
      log_error("Failed to realloc middlewares");
      return;
    }
    app->middlewares = new_middlewares;
    app->middleware_capacity = new_capacity;
  }

  app->middlewares[app->middleware_count++] = mw;
}

void app_close(App *app) {
  if (app == NULL)
    return;

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

  for (int i = 0; i < app->client_count; i++)
    close_connection(app->clients[i]);

  uv_close((uv_handle_t *)&app->server, NULL);

  uv_run(loop, UV_RUN_DEFAULT);
  uv_loop_close(loop);

  free(app->clients);
  free(app);
}

void build_response_headers(ResponseContext *res, const char *data, char *dest,
                            size_t size, char *content_type) {
  int len =
      snprintf(dest, size,
               "HTTP/1.1 %d\r\n"
               "Content-Type: %s\r\n"
               "Content-Length: %zu\r\n",
               res->status ? res->status : 200,
               content_type ? content_type : "application/json", strlen(data));

  for (int i = 0; i < res->header_count; i++) {
    int header_len = snprintf(dest + len, size - len, "%s: %s\r\n",
                              res->headers[i].key, res->headers[i].value);

    if (header_len < 0 || header_len >= (int)(size - len))
      break;
    len += header_len;
  }

  snprintf(dest + len, size - len, "Connection: %s\r\n\r\n",
           res->ctx->keep_alive ? "keep-alive" : "close");
}

void send_json_response(ResponseContext *res, cJSON *json) {
  if (!res->ctx || !json)
    return;

  char *json_str = cJSON_PrintUnformatted(json);
  if (!json_str) {
    const char *err = "{\"error\":\"Failed to serialize JSON\"}";
    send_message(res->ctx->client, err);
  } else {
    char header[2048];
    build_response_headers(res, json_str, header, sizeof(header),
                           "application/json");

    send_message(res->ctx->client, header);
    send_message(res->ctx->client, json_str);

    free(json_str);
  }

  cJSON_Delete(json);
}

void send_text_response(ResponseContext *res, const char *message) {
  char header[2048];
  build_response_headers(res, message, header, sizeof(header),
                         "application/text");

  send_message(res->ctx->client, header);
  send_message(res->ctx->client, message);
}

void send_error(ResponseContext *res, int status, const char *message) {
  cJSON *error = cJSON_CreateObject();
  cJSON_AddStringToObject(error, "error", message);
  res->status = status;
  send_json_response(res, error);
}

char *get_param(ResponseContext *res, const char *key) {
  for (int i = 0; i < res->req->params_count; i++) {
    if (strcmp(res->req->params[i].key, key) == 0)
      return res->req->params[i].value;
  }
  return NULL;
}

char *get_header(ResponseContext *res, const char *key) {
  for (int i = 0; i < res->req->header_count; i++) {
    if (strcasecmp(res->req->headers[i].key, key) == 0)
      return res->req->headers[i].value;
  }
  return NULL;
}

int get_header_int(ResponseContext *res, const char *key) {
  char *header_value = get_header(res, key);

  if (!header_value)
    return 0;

  return atoi(header_value);
}

int header_allows_multiple(const char *key) {
  return strcasecmp(key, "Set-Cookie") == 0;
}

void set_header(ResponseContext *res, const char *key, const char *value) {
  if (!key || key[0] == '\0')
    return;

  if (!header_allows_multiple(key)) {
    for (int i = 0; i < res->header_count; i++) {
      if (strcasecmp(res->headers[i].key, key) == 0) {
        strncpy(res->headers[i].value, value, VALUE_SIZE - 1);
        res->headers[i].value[VALUE_SIZE - 1] = '\0';
        return;
      }
    }
  }

  if (res->header_count >= MAX_HEADERS)
    return;

  strncpy(res->headers[res->header_count].key, key, KEY_SIZE - 1);
  res->headers[res->header_count].key[KEY_SIZE - 1] = '\0';
  strncpy(res->headers[res->header_count].value, value, VALUE_SIZE - 1);
  res->headers[res->header_count].value[VALUE_SIZE - 1] = '\0';

  res->header_count++;
}

void set_data(ResponseContext *res, const char *key, void *value) {
  for (int i = 0; i < res->data.count; i++) {
    if (strcmp(key, res->data.entries[i].key) == 0) {
      res->data.entries[i].value = value;
      return;
    }
  }

  if (res->data.count >= res->data.capacity) {
    res->data.capacity *= 2;
    ResponseDataEntry *new_entries = realloc(
        res->data.entries, sizeof(ResponseDataEntry) * res->data.capacity);

    if (!new_entries)
      return;
    res->data.entries = new_entries;
  }

  char *dup_key = strdup(key);
  if (!dup_key)
    return;

  res->data.entries[res->data.count].key = dup_key;
  res->data.entries[res->data.count].value = value;
  res->data.count++;
}

void *get_data(ResponseContext *res, const char *key) {
  if (res->data.entries == NULL)
    return NULL;

  for (int i = 0; i < res->data.count; i++) {
    if (strcmp(key, res->data.entries[i].key) == 0) {
      return res->data.entries[i].value;
    }
  }

  return NULL;
}

void set_data_string(ResponseContext *res, const char *key, const char *value) {
  char *ptr = strdup(value);

  if (!ptr) {
    log_error("Failed to malloc data string");
    return;
  }

  set_data(res, key, ptr);
}
void set_data_int(ResponseContext *res, const char *key, int value) {
  int *ptr = malloc(sizeof(int));

  if (!ptr) {
    log_error("Failed to malloc data int");
    return;
  }

  *ptr = value;
  set_data(res, key, ptr);
}

void set_data_double(ResponseContext *res, const char *key, double value) {
  double *ptr = malloc(sizeof(double));

  if (!ptr) {
    log_error("Failed to malloc set data double pointer");
    return;
  }

  *ptr = value;

  if (!ptr) {
    log_error("Failed to malloc data double");
    return;
  }

  set_data(res, key, ptr);
}

int get_data_int(ResponseContext *res, const char *key) {
  int *ptr = (int *)get_data(res, key);
  return ptr ? *ptr : 0;
}

char *get_data_string(ResponseContext *res, const char *key) {
  return (char *)get_data(res, key);
}

double get_data_double(ResponseContext *res, const char *key) {
  double *ptr = (double *)get_data(res, key);
  return ptr ? *ptr : 0.0;
}

void *get_query(ResponseContext *res, const char *key) {
  if (res->query.entries == NULL)
    return NULL;

  for (int i = 0; i < res->query.count; i++) {
    if (strcmp(key, res->query.entries[i].key) == 0) {
      return res->query.entries[i].value;
    }
  }

  return NULL;
}

int get_query_int(ResponseContext *res, const char *key) {
  char *val = (char *)get_query(res, key);
  if (!val)
    return 0;
  return atoi(val);
}

double get_query_double(ResponseContext *res, const char *key) {
  char *val = (char *)get_query(res, key);
  if (!val)
    return 0.0;
  return atof(val);
}

char *get_query_string(ResponseContext *res, const char *key) {
  return (char *)get_query(res, key);
}

void get_client_ip(ResponseContext *res, char *buffer, size_t size) {
  struct sockaddr_storage peername;
  int namelen = sizeof(peername);

  if (uv_tcp_getpeername(res->ctx->client, (struct sockaddr *)&peername,
                         &namelen) != 0) {
    strncpy(buffer, "unknown", size - 1);
    buffer[size - 1] = '\0';
    return;
  }

  if (peername.ss_family == AF_INET) {
    struct sockaddr_in *addr = (struct sockaddr_in *)&peername;
    uv_ip4_name(addr, buffer, size);
  } else if (peername.ss_family == AF_INET6) {
    struct sockaddr_in6 *addr = (struct sockaddr_in6 *)&peername;
    uv_ip6_name(addr, buffer, size);
  } else {
    strncpy(buffer, "unknown", size - 1);
    buffer[size - 1] = '\0';
  }
}
