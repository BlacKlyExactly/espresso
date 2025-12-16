#include "espresso.h"
#include <stdio.h>

MiddlewareResult logger(ResponseContext *res) {
  printf("[%s] %s\n", res->req->method, res->req->path);
  return MIDDLEWARE_CONTINUE;
}

void index_handler(ResponseContext *res) {
  send_text_response(res, "Hello, Espresso!");
}

void echo_handler(ResponseContext *res) {
  char *msg = get_query_string(res, "msg");
  if (!msg)
    msg = "No message provided";
  send_text_response(res, msg);
}

void hello_handler(ResponseContext *res) {
  cJSON *json = cJSON_CreateObject();
  cJSON_AddStringToObject(json, "message", "Hello, World!");
  send_json_response(res, json);
}

int main() {
  App *app = create_app(8080);

  app_use(app, cors_allow_all);

  AppGroup *group = app_group(app, "/api");
  app_group_get(app, group, "/", index_handler);
  app_group_get(app, group, "/echo", echo_handler);

  app_get(app, "/", index_handler);
  app_get(app, "/hello", hello_handler);

  printf("Starting Espresso server...\n");
  app_listen(app);

  app_close(app);
  return 0;
}
