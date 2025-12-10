#ifdef _WIN32
#include <process.h>
#include <signal.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif
#include "espresso.h"
#include "vendor/cJSON/cJSON.h"
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#define READ(s, buf, len) recv(s, buf, len, 0)
#define WRITE(s, buf, len) send(s, buf, len, 0)
#define CLOSE_SOCKET(s) closesocket(s)
#else
#define READ(s, buf, len) read(s, buf, len)
#define WRITE(s, buf, len) write(s, buf, len)
#define CLOSE_SOCKET(s) close(s)
#endif

static App *lib_global_app = NULL;

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
    "HTTP Version Not Supported\n";

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
      perror("Failed to realloc query entries");
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
    perror("Faiied to malloc target copy");
    return;
  }

  char *query = strchr(target_copy, '?');

  if (!query) {
    free(target_copy);
    return;
  }

  query++;

  if (!res->query.entries) {
    perror("Failed to malloc query entries");
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

int parse_http_request(char *buffer, Request *req, int client_fd) {
  req->header_count = 0;
  req->params_count = 0;
  req->params = NULL;

  if (sscanf(buffer, "%7s %255s %15s", req->method, req->path, req->version) !=
      3) {
    perror("Scan path and method");
    WRITE(client_fd, bad_request_response, strlen(bad_request_response));
    return 1;
  }

  char *header_end = strstr(buffer, "\r\n\r\n");

  if (header_end != NULL) {
    *header_end = '\0';
    char *body = header_end + 4;
    char *content_type = NULL;
    int content_length = -1;

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

        if (strcasecmp(key, "Content-Type") == 0) {
          content_type = value;
        }

        if (strcasecmp(key, "Content-Length") == 0) {
          int cl = 0;
          if (sscanf(value, "%d", &cl) != 1 || cl < 0) {
            WRITE(client_fd, bad_request_response,
                  strlen(bad_request_response));

            return 1;
          }
          if (content_length != -1 && content_length != cl) {
            WRITE(client_fd, bad_request_response,
                  strlen(bad_request_response));

            return 1;
          }
          content_length = cl;
        }
      }

      line = strtok(NULL, "\r\n");
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
#ifdef _WIN32
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    fprintf(stderr, "WSAStartup failed\n");
    return;
  }
#endif

  App *app = malloc(sizeof(App));
  lib_global_app = app;

  signal(SIGINT, lib_handle_sigint);

  if (app == NULL) {
    printf("Failed to malloc an app");
    return NULL;
  }

  app->endpoint_count = 0;
  app->endpoint_capacity = 10;
  app->endpoints = malloc(app->endpoint_capacity * sizeof(Endpoint));

  if (app->endpoints == NULL) {
    printf("Failed to malloc an endpoints");
    return NULL;
  }

  app->middleware_count = 0;
  app->middleware_capacity = 10;
  app->middlewares = malloc(app->middleware_capacity * sizeof(Middleware));

  if (app->middlewares == NULL) {
    printf("Failed to malloc middlewares");
    free(app->endpoints);
    free(app);
    return NULL;
  }

  app->groups_count = 0;
  app->groups_capacity = 10;
  app->groups = malloc(app->groups_capacity * sizeof(AppGroup));

  if (app->groups == NULL) {
    printf("Failed to malloc groups");
    free(app->endpoints);
    free(app->middlewares);
    free(app);
    return NULL;
  }

  app->port = port;
  app->server_fd = socket(AF_INET, SOCK_STREAM, 0);

  if (app->server_fd < 0) {
    perror("socket error");
    free(app->endpoints);
    free(app->middlewares);
    free(app->groups);
    free(app);
    return NULL;
  }

  app->server_addr.sin_family = AF_INET;
  app->server_addr.sin_addr.s_addr = INADDR_ANY;
  app->server_addr.sin_port = htons(port);

#ifdef _WIN32
  int opt = 1;
  if (setsockopt(app->server_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt,
                 sizeof(opt)) == SOCKET_ERROR) {
    fprintf(stderr, "setsockopt failed: %d\n", WSAGetLastError());
    CLOSE_SOCKET(app->server_fd);
    WSACleanup();
    free(app->endpoints);
    free(app->middlewares);
    free(app->groups);
    free(app);
    return NULL;
  }
#else
  int opt = 1;
  if (setsockopt(app->server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) <
      0) {
    perror("setsockopt failed");
    CLOSE_SOCKET(app->server_fd);
    free(app->endpoints);
    free(app->middlewares);
    free(app->groups);
    free(app);
    return NULL;
  }
#endif

  if (bind(app->server_fd, (struct sockaddr *)&app->server_addr,
           sizeof(app->server_addr)) < 0) {
    perror("bind error");
    CLOSE_SOCKET(app->server_fd);
    free(app->endpoints);
    free(app->middlewares);
    free(app->groups);
    free(app);
    return NULL;
  }

  return app;
}

#ifdef _WIN32
unsigned __stdcall client_thread(void *arg)
#else
void *client_thread(void *arg)
#endif
{
  ClientContext *ctx = (ClientContext *)arg;
  App *app = ctx->app;

#ifdef _WIN32
  SOCKET client_fd = ctx->client_fd;
#else
  int client_fd = ctx->client_fd;
#endif

#ifdef _WIN32
  DWORD tv_read = 5000;
  setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv_read,
             sizeof(tv_read));
  DWORD tv_write = 10000;
  setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv_write,
             sizeof(tv_write));
#else
  struct timeval tv_read = {5, 0};
  setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv_read, sizeof(tv_read));
  struct timeval tv_write = {10, 0};
  setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv_write, sizeof(tv_write));
#endif

  char *buffer = malloc(2048);

  if (!buffer) {
    perror("Failed to malloc a client_thread buffer");
    CLOSE_SOCKET(client_fd);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
  }

  size_t buffer_size = 2048;
  size_t total_read = 0;

  while (1) {
    if (total_read >= MAX_HEADER_SIZE) {
      WRITE(client_fd, payload_too_large_response,
            strlen(payload_too_large_response));
      CLOSE_SOCKET(client_fd);
      free(buffer);
#ifdef _WIN32
      return 0;
#else
      return NULL;
#endif
    }

    if (total_read == buffer_size - 1) {
      int new_size = buffer_size * 2;

      if (new_size > MAX_HEADER_SIZE)
        new_size = MAX_HEADER_SIZE;

      buffer_size = new_size;
      char *new_buffer = realloc(buffer, buffer_size);

      if (!new_buffer) {
        free(buffer);
        CLOSE_SOCKET(client_fd);
#ifdef _WIN32
        return 0;
#else
        return NULL;
#endif
      }

      buffer = new_buffer;
    }

    int bytes;
#ifdef _WIN32
    bytes = recv(client_fd, buffer + total_read,
                 (int)(buffer_size - 1 - total_read), 0);
#else
    bytes = read(client_fd, buffer + total_read, buffer_size - 1 - total_read);
#endif

    if (bytes <= 0) {
#ifdef _WIN32
      if (WSAGetLastError() == WSAEWOULDBLOCK)
        break;
      closesocket(client_fd);
#else
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        break;
      close(client_fd);
#endif
      free(buffer);
      free(ctx);
#ifdef _WIN32
      return 0;
#else
      return NULL;
#endif
    }

    total_read += bytes;

    char *header_end = strstr(buffer, "\r\n\r\n");

    if (header_end) {
      int headers_len = header_end - buffer + 4;
      int content_length = 0;

      char *content_length_str = strcasestr(buffer, "Content-Length:");

      if (content_length_str) {
        if (sscanf(content_length_str, "Content-Length: %d", &content_length) !=
            1) {
          WRITE(client_fd, bad_request_response, strlen(bad_request_response));
          CLOSE_SOCKET(client_fd);
          free(buffer);
          free(ctx);
#ifdef _WIN32
          return 0;
#else
          return NULL;
#endif
        }
      } else {
        content_length = 0;
      }

      if (content_length < 0) {
        WRITE(client_fd, bad_request_response, strlen(bad_request_response));
        CLOSE_SOCKET(client_fd);
        free(buffer);
        free(ctx);
#ifdef _WIN32
        return 0;
#else
        return NULL;
#endif
      }

      if (total_read >= content_length + headers_len)
        break;
    }

    buffer[total_read] = '\0';
  }

  Endpoint curr;
  Request req;
  parse_http_request(buffer, &req, client_fd);

  if (strcmp(req.version, "HTTP/1.0") != 0 &&
      strcmp(req.version, "HTTP/1.1") != 0) {
    WRITE(client_fd, http_version_not_supported_response,
          strlen(http_version_not_supported_response));

    CLOSE_SOCKET(client_fd);
    free(req.body);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
  }

  int is_valid = 0;
  for (int i = 0; i < sizeof(MethodNames) / sizeof(MethodNames[0]); i++) {
    if (strcmp(req.method, MethodNames[i]) == 0) {
      is_valid = 1;
      break;
    }
  }

  if (!is_valid) {
    char allow_header[256] = {0};
    generate_allow_header(app, req.path, allow_header, sizeof(allow_header));

    char response[512];
    int len = snprintf(response, sizeof(response),
                       "HTTP/1.1 405 Method Not Allowed\r\n"
                       "Allow: %s\r\n"
                       "Content-Type: text/plain\r\n"
                       "Content-Length: 19\r\n"
                       "Connection: close\r\n"
                       "\r\n"
                       "Method Not Allowed\n",
                       allow_header);
    WRITE(client_fd, response, len);
    CLOSE_SOCKET(client_fd);
    free(req.body);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
  }

  ResponseContext res;
  res.ctx = ctx;
  res.req = &req;
  res.header_count = 0;

  res.data.capacity = 10;
  res.data.count = 0;
  res.data.entries = malloc(sizeof(ResponseDataEntry) * res.data.capacity);

  if (!res.data.entries) {
    perror("Failed to malloc res->data.entries");
    CLOSE_SOCKET(client_fd);
    free(req.body);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
  }

  res.query.capacity = 10;
  res.query.count = 0;
  res.query.entries = malloc(sizeof(DynamicKeyValue) * res.query.capacity);

  if (!res.query.entries) {
    perror("Failed to malloc res->data.entries");
    CLOSE_SOCKET(client_fd);
    free(req.body);
    free(res.data.entries);

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
  }

  int exists = 0;
  for (int i = 0; i < app->endpoint_count; i++) {
    curr = app->endpoints[i];
    int do_paths_match = compare_paths(curr.path, req.path, &req);

    if (do_paths_match) {
      exists = 1;
      if (strcmp(MethodNames[curr.method], req.method) == 0) {
        parse_query_params(&res);

        int should_continue = 1;
        for (int i = 0; i < app->middleware_count && should_continue; i++) {
          should_continue = !app->middlewares[i](&res);
        }

        if (should_continue) {
          for (int i = 0; i < curr.middleware_count && should_continue; i++) {
            should_continue = !curr.middlewares[i](&res);
          }
        }

        if (should_continue) {
          curr.handler(&res);
        }

        break;
      }
      if (curr.method == GET && strcmp(req.method, "HEAD") == 0) {
        (&req)->body = NULL;

        int should_continue = 1;
        for (int i = 0; i < app->middleware_count && should_continue; i++) {
          should_continue = !app->middlewares[i](&res);
        }

        if (should_continue) {
          for (int i = 0; i < curr.middleware_count && should_continue; i++) {
            should_continue = !curr.middlewares[i](&res);
          }
        }

        if (should_continue) {
          curr.handler(&res);
        }
      } else if (strcmp(req.method, "OPTIONS") == 0) {
        char allow_header[256] = {0};
        generate_allow_header(app, req.path, allow_header,
                              sizeof(allow_header));

        char response[512];
        int len = snprintf(response, sizeof(response),
                           "HTTP/1.1 200 OK\r\n"
                           "Allow: %s\r\n"
                           "Content-Length: 0\r\n"
                           "Connection: close\r\n\r\n",
                           allow_header);
        WRITE(client_fd, response, len);
        break;
      }
    }
  }

  if (!exists)
    WRITE(client_fd, not_found_response, strlen(not_found_response));

  CLOSE_SOCKET(client_fd);
  free(ctx);
  free(buffer);

  if (req.body->type == BODY_JSON && req.body->data.json != NULL) {
    cJSON_Delete(req.body->data.json);
    req.body->data.json = NULL;
  }

  if (req.params != NULL) {
    free(req.params);
    req.params = NULL;
  }

  if (res.data.entries) {
    for (int i = 0; i < res.data.count; i++) {
      if (res.data.entries[i].key)
        free(res.data.entries[i].key);

      if (res.data.entries[i].value)
        free(res.data.entries[i].value);
    }

    free(res.data.entries);
  }

  if (res.query.entries) {
    for (int i = 0; i < res.query.count; i++) {
      if (res.query.entries[i].key)
        free(res.query.entries[i].key);

      if (res.query.entries[i].value)
        free(res.query.entries[i].value);
    }

    free(res.query.entries);
  }

  free(req.body);

#ifdef _WIN32
  return 0;
#else
  return NULL;
#endif
}

void app_listen(App *app) {
#ifdef _WIN32
  app->server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
  app->server_fd = socket(AF_INET, SOCK_STREAM, 0);
#endif
  if (app->server_fd < 0) {
    perror("socket error");
    return;
  }

  int opt = 1;
#ifdef _WIN32
  setsockopt(app->server_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt,
             sizeof(opt));
#else
  setsockopt(app->server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

  if (bind(app->server_fd, (struct sockaddr *)&app->server_addr,
           sizeof(app->server_addr)) < 0) {
    perror("bind error");
#ifdef _WIN32
    closesocket(app->server_fd);
    WSACleanup();
#else
    close(app->server_fd);
#endif
    return;
  }

  if (listen(app->server_fd, 10) < 0) {
    perror("listen error");
#ifdef _WIN32
    closesocket(app->server_fd);
    WSACleanup();
#else
    close(app->server_fd);
#endif
    return;
  }

  printf("Server listening on port %d\n", app->port);

  socklen_t client_len = sizeof(app->client_addr);

  while (1) {
#ifdef _WIN32
    SOCKET client_fd = accept(
        app->server_fd, (struct sockaddr *)&app->client_addr, &client_len);
#else
    int client_fd = accept(app->server_fd, (struct sockaddr *)&app->client_addr,
                           &client_len);
#endif
    if (client_fd < 0) {
      perror("accept error");
      continue;
    }

    ClientContext *ctx = malloc(sizeof(ClientContext));
    if (!ctx) {
      perror("Failed to malloc client context");
#ifdef _WIN32
      closesocket(client_fd);
#else
      close(client_fd);
#endif
      continue;
    }

    ctx->app = app;
    ctx->client_fd = client_fd;
    ctx->client_addr = app->client_addr;

#ifdef _WIN32
    HANDLE hThread =
        (HANDLE)_beginthreadex(NULL, 0, client_thread, ctx, 0, NULL);
    if (hThread)
      CloseHandle(hThread);
#else
    pthread_t tid;
    pthread_create(&tid, NULL, client_thread, ctx);
    pthread_detach(tid);
#endif
  }

#ifdef _WIN32
  WSACleanup();
#endif
}

AppGroup *_app_create_group(App *app, const char *root_path,
                            Middleware *middlewares) {
  app->groups_capacity = 10;
  if (app->groups_count >= app->groups_capacity) {
    int new_capacity = app->groups_capacity * 2;

    AppGroup *new_groups =
        realloc(app->groups, new_capacity * sizeof(AppGroup));

    if (!new_groups) {
      perror("Failed to realloc groups");
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
      perror("Failed to malloc middlewares");
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
    perror("Failed to mallon an full path");
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
      perror("Failed to malloc combined middlewares");
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
      perror("Failed to realloc endpoints");
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
      perror("Failed to malloc middlewares");
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
      perror("Failed to realloc middlewares");
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

  CLOSE_SOCKET(app->server_fd);
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

  snprintf(dest + len, size - len, "Connection: close\r\n\r\n");
}

void send_json_response(ResponseContext *res, cJSON *json) {
  if (!res->ctx || !json)
    return;

  char *json_str = cJSON_PrintUnformatted(json);
  if (!json_str) {
    const char *err = "{\"error\":\"Failed to serialize JSON\"}";
    WRITE(res->ctx->client_fd, err, strlen(err));
  } else {
    char header[2048];
    build_response_headers(res, json_str, header, sizeof(header),
                           "application/json");

    WRITE(res->ctx->client_fd, header, strlen(header));
    WRITE(res->ctx->client_fd, json_str, strlen(json_str));
    free(json_str);
  }

  cJSON_Delete(json);
}

void send_text_response(ResponseContext *res, const char *message) {
  char header[2048];
  build_response_headers(res, message, header, sizeof(header),
                         "application/text");

  WRITE(res->ctx->client_fd, header, strlen(header));
  WRITE(res->ctx->client_fd, message, strlen(message));
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
    perror("Failed to malloc data string");
    return;
  }

  set_data(res, key, ptr);
}
void set_data_int(ResponseContext *res, const char *key, int value) {
  int *ptr = malloc(sizeof(int));

  if (!ptr) {
    perror("Failed to malloc data int");
    return;
  }

  *ptr = value;
  set_data(res, key, ptr);
}

void set_data_double(ResponseContext *res, const char *key, double value) {
  double *ptr = malloc(sizeof(double));
  *ptr = value;

  if (!ptr) {
    perror("Failed to malloc data double");
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
