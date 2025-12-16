#include "../src/espresso.h"
#include <check.h>
#include <stdlib.h>
#include <string.h>

static App *app;
static Request *req;
static ResponseContext *res;
static ClientContext *ctx;

void setup(void) {
  app = create_app(8080);
  req = malloc(sizeof(Request));
  memset(req, 0, sizeof(Request));
  req->body = calloc(1, sizeof(Body));
  req->params = NULL;
  req->params_count = 0;

  ctx = malloc(sizeof(ClientContext));
  memset(ctx, 0, sizeof(ClientContext));
  ctx->app = app;

  res = malloc(sizeof(ResponseContext));
  memset(res, 0, sizeof(ResponseContext));
  res->ctx = ctx;
  res->req = req;
  res->status = 200;
  res->data.capacity = 10;
  res->data.count = 0;
  res->data.entries = malloc(sizeof(ResponseDataEntry) * res->data.capacity);
  res->query.capacity = 10;
  res->query.count = 0;
  res->query.entries = malloc(sizeof(DynamicKeyValue) * res->query.capacity);
}

void teardown(void) {
  if (app) {
    app_close(app);
    app = NULL;
  }

  if (req) {
    if (req->params) {
      free(req->params);
      req->params = NULL;
    }
    if (req->body) {
      free(req->body);
      req->body = NULL;
    }
    free(req);
    req = NULL;
  }

  if (res) {
    if (res->data.entries) {
      for (int i = 0; i < res->data.count; i++) {
        if (res->data.entries[i].key)
          free(res->data.entries[i].key);
        if (res->data.entries[i].value)
          free(res->data.entries[i].value);
      }
      free(res->data.entries);
      res->data.entries = NULL;
    }

    if (res->query.entries) {
      for (int i = 0; i < res->query.count; i++) {
        if (res->query.entries[i].key)
          free(res->query.entries[i].key);
        if (res->query.entries[i].value)
          free(res->query.entries[i].value);
      }
      free(res->query.entries);
      res->query.entries = NULL;
    }

    free(res);
    res = NULL;
  }

  if (ctx) {
    free(ctx);
    ctx = NULL;
  }
}

void app_setup(void) { app = create_app(8080); }

void app_teardown(void) {
  if (app) {
    app_close(app);
    app = NULL;
  }
}

START_TEST(test_compare_paths_exact_match) {
  strcpy(req->path, "/users");
  int result = compare_paths("/users", "/users", req);
  ck_assert_int_eq(result, 1);
}
END_TEST

START_TEST(test_compare_paths_no_match) {
  strcpy(req->path, "/posts");
  int result = compare_paths("/users", "/posts", req);
  ck_assert_int_eq(result, 0);
}
END_TEST

START_TEST(test_compare_paths_with_params) {
  strcpy(req->path, "/users/42");
  int result = compare_paths("/users/:id", "/users/42", req);
  ck_assert_int_eq(result, 1);
  ck_assert_int_eq(req->params_count, 1);
  ck_assert_str_eq(req->params[0].key, "id");
  ck_assert_str_eq(req->params[0].value, "42");
}
END_TEST

START_TEST(test_compare_paths_multiple_params) {
  strcpy(req->path, "/users/42/posts/123");
  int result =
      compare_paths("/users/:userId/posts/:postId", "/users/42/posts/123", req);
  ck_assert_int_eq(result, 1);
  ck_assert_int_eq(req->params_count, 2);
  ck_assert_str_eq(req->params[0].key, "userId");
  ck_assert_str_eq(req->params[0].value, "42");
  ck_assert_str_eq(req->params[1].key, "postId");
  ck_assert_str_eq(req->params[1].value, "123");
}
END_TEST

START_TEST(test_compare_paths_trailing_slash) {
  strcpy(req->path, "/users/");
  int result = compare_paths("/users", "/users/", req);
  ck_assert_int_eq(result, 1);
}
END_TEST

START_TEST(test_compare_paths_with_query_string) {
  strcpy(req->path, "/users?name=john");
  int result = compare_paths("/users", "/users?name=john", req);
  ck_assert_int_eq(result, 1);
}
END_TEST

START_TEST(test_parse_query_params_single) {
  strcpy(req->path, "/users?name=john");
  parse_query_params(res);

  char *value = get_query_string(res, "name");
  ck_assert_ptr_nonnull(value);
  ck_assert_str_eq(value, "john");
}
END_TEST

START_TEST(test_parse_query_params_multiple) {
  strcpy(req->path, "/users?name=john&age=30&city=NYC");
  parse_query_params(res);

  ck_assert_str_eq(get_query_string(res, "name"), "john");
  ck_assert_str_eq(get_query_string(res, "age"), "30");
  ck_assert_str_eq(get_query_string(res, "city"), "NYC");
}
END_TEST

START_TEST(test_parse_query_params_url_encoded) {
  strcpy(req->path, "/search?q=hello+world&msg=foo%20bar");
  parse_query_params(res);

  ck_assert_str_eq(get_query_string(res, "q"), "hello world");
  ck_assert_str_eq(get_query_string(res, "msg"), "foo bar");
}
END_TEST

START_TEST(test_parse_query_params_special_chars) {
  strcpy(req->path, "/search?email=test%40example.com");
  parse_query_params(res);

  ck_assert_str_eq(get_query_string(res, "email"), "test@example.com");
}
END_TEST

START_TEST(test_parse_query_params_empty_value) {
  strcpy(req->path, "/users?filter=");
  parse_query_params(res);

  char *value = get_query_string(res, "filter");
  ck_assert_ptr_nonnull(value);
  ck_assert_str_eq(value, "");
}
END_TEST

START_TEST(test_parse_query_params_no_value) {
  strcpy(req->path, "/users?active");
  parse_query_params(res);

  char *value = get_query_string(res, "active");
  ck_assert_ptr_nonnull(value);
  ck_assert_str_eq(value, "");
}
END_TEST

START_TEST(test_get_query_int) {
  strcpy(req->path, "/users?id=42&count=100");
  parse_query_params(res);

  ck_assert_int_eq(get_query_int(res, "id"), 42);
  ck_assert_int_eq(get_query_int(res, "count"), 100);
  ck_assert_int_eq(get_query_int(res, "missing"), 0);
}
END_TEST

START_TEST(test_get_query_double) {
  strcpy(req->path, "/products?price=19.99&discount=0.15");
  parse_query_params(res);

  ck_assert_double_eq_tol(get_query_double(res, "price"), 19.99, 0.01);
  ck_assert_double_eq_tol(get_query_double(res, "discount"), 0.15, 0.01);
  ck_assert_double_eq(get_query_double(res, "missing"), 0.0);
}
END_TEST

START_TEST(test_set_get_data_string) {
  set_data_string(res, "username", "john_doe");
  char *value = get_data_string(res, "username");
  ck_assert_ptr_nonnull(value);
  ck_assert_str_eq(value, "john_doe");
}
END_TEST

START_TEST(test_set_get_data_int) {
  set_data_int(res, "user_id", 42);
  int value = get_data_int(res, "user_id");
  ck_assert_int_eq(value, 42);
}
END_TEST

START_TEST(test_set_get_data_double) {
  set_data_double(res, "price", 19.99);
  double value = get_data_double(res, "price");
  ck_assert_double_eq_tol(value, 19.99, 0.01);
}
END_TEST

START_TEST(test_set_data_overwrite) {
  set_data_int(res, "counter", 1);
  set_data_int(res, "counter", 2);
  int value = get_data_int(res, "counter");
  ck_assert_int_eq(value, 2);
  ck_assert_int_eq(res->data.count, 1);
}
END_TEST

START_TEST(test_get_data_missing) {
  void *value = get_data(res, "nonexistent");
  ck_assert_ptr_null(value);
  ck_assert_int_eq(get_data_int(res, "nonexistent"), 0);
  ck_assert_ptr_null(get_data_string(res, "nonexistent"));
}
END_TEST

START_TEST(test_set_get_header) {
  set_header(res, "Content-Type", "application/json");
  set_header(res, "X-Custom-Header", "custom-value");

  ck_assert_int_eq(res->header_count, 2);
  ck_assert_str_eq(res->headers[0].key, "Content-Type");
  ck_assert_str_eq(res->headers[0].value, "application/json");
  ck_assert_str_eq(res->headers[1].key, "X-Custom-Header");
  ck_assert_str_eq(res->headers[1].value, "custom-value");
}
END_TEST

START_TEST(test_set_header_overwrite) {
  set_header(res, "Content-Type", "text/plain");
  set_header(res, "Content-Type", "application/json");

  ck_assert_int_eq(res->header_count, 1);
  ck_assert_str_eq(res->headers[0].value, "application/json");
}
END_TEST

START_TEST(test_set_header_case_insensitive) {
  set_header(res, "content-type", "text/plain");
  set_header(res, "Content-Type", "application/json");

  ck_assert_int_eq(res->header_count, 1);
  ck_assert_str_eq(res->headers[0].value, "application/json");
}
END_TEST

START_TEST(test_get_header_from_request) {
  strcpy(req->headers[0].key, "Authorization");
  strcpy(req->headers[0].value, "Bearer token123");
  strcpy(req->headers[1].key, "User-Agent");
  strcpy(req->headers[1].value, "TestClient/1.0");
  req->header_count = 2;

  char *auth = get_header(res, "Authorization");
  ck_assert_ptr_nonnull(auth);
  ck_assert_str_eq(auth, "Bearer token123");

  char *ua = get_header(res, "user-agent");
  ck_assert_ptr_nonnull(ua);
  ck_assert_str_eq(ua, "TestClient/1.0");
}
END_TEST

START_TEST(test_get_param) {
  req->params = malloc(sizeof(KeyValue) * 2);
  strcpy(req->params[0].key, "id");
  strcpy(req->params[0].value, "42");
  strcpy(req->params[1].key, "name");
  strcpy(req->params[1].value, "john");
  req->params_count = 2;

  char *id = get_param(res, "id");
  ck_assert_ptr_nonnull(id);
  ck_assert_str_eq(id, "42");

  char *name = get_param(res, "name");
  ck_assert_ptr_nonnull(name);
  ck_assert_str_eq(name, "john");

  ck_assert_ptr_null(get_param(res, "missing"));
}
END_TEST

START_TEST(test_parse_http_request_simple_get) {
  char buffer[] = "GET /users HTTP/1.1\r\n"
                  "Host: localhost:8080\r\n"
                  "User-Agent: TestClient\r\n"
                  "\r\n";

  uv_tcp_t client;

  ClientContext ctx;
  memset(&ctx, 0, sizeof(ClientContext));

  ctx.client = &client;

  Request test_req;
  memset(&test_req, 0, sizeof(Request));
  test_req.body = calloc(1, sizeof(Body));

  int result = parse_http_request(buffer, &test_req, &client, &ctx);

  ck_assert_int_eq(result, 0);
  ck_assert_str_eq(test_req.method, "GET");
  ck_assert_str_eq(test_req.path, "/users");
  ck_assert_str_eq(test_req.version, "HTTP/1.1");
  ck_assert_int_eq(test_req.header_count, 2);

  free(test_req.body);
}
END_TEST

START_TEST(test_parse_http_request_with_body) {
  char buffer[] = "POST /users HTTP/1.1\r\n"
                  "Host: localhost:8080\r\n"
                  "Content-Type: application/json\r\n"
                  "Content-Length: 27\r\n"
                  "\r\n"
                  "{\"name\":\"john\",\"age\":30}";

  uv_tcp_t client;

  ClientContext ctx;
  memset(&ctx, 0, sizeof(ClientContext));

  ctx.client = &client;

  Request test_req;
  memset(&test_req, 0, sizeof(Request));
  test_req.body = calloc(1, sizeof(Body));

  int result = parse_http_request(buffer, &test_req, &client, &ctx);

  ck_assert_int_eq(result, 0);
  ck_assert_str_eq(test_req.method, "POST");
  ck_assert_int_eq(test_req.body->type, BODY_JSON);
  ck_assert_ptr_nonnull(test_req.body->data.json);

  cJSON *json = test_req.body->data.json;
  cJSON *name = cJSON_GetObjectItem(json, "name");
  ck_assert_ptr_nonnull(name);
  ck_assert_str_eq(name->valuestring, "john");

  cJSON_Delete(test_req.body->data.json);
  free(test_req.body);
}
END_TEST

START_TEST(test_create_app) {
  App *test_app = create_app(3000);
  ck_assert_ptr_nonnull(test_app);
  ck_assert_int_eq(test_app->port, 3000);
  ck_assert_int_eq(test_app->endpoint_count, 0);
  ck_assert_int_eq(test_app->middleware_count, 0);
  app_close(test_app);
}
END_TEST

void dummy_handler(ResponseContext *res) { send_text_response(res, "OK"); }

START_TEST(test_app_append_endpoint) {
  Endpoint *ep = _app_append_endpoint(app, GET, "/test", dummy_handler, NULL);

  ck_assert_ptr_nonnull(ep);
  ck_assert_int_eq(app->endpoint_count, 1);
  ck_assert_str_eq(app->endpoints[0].path, "/test");
  ck_assert_int_eq(app->endpoints[0].method, GET);
  ck_assert_ptr_eq(app->endpoints[0].handler, dummy_handler);
}
END_TEST

START_TEST(test_app_multiple_endpoints) {
  _app_append_endpoint(app, GET, "/users", dummy_handler, NULL);
  _app_append_endpoint(app, POST, "/users", dummy_handler, NULL);
  _app_append_endpoint(app, PATCH, "/users/:id", dummy_handler, NULL);
  _app_append_endpoint(app, DELETE, "/users/:id", dummy_handler, NULL);

  ck_assert_int_eq(app->endpoint_count, 4);
  ck_assert_int_eq(app->endpoints[0].method, GET);
  ck_assert_int_eq(app->endpoints[1].method, POST);
  ck_assert_int_eq(app->endpoints[2].method, PATCH);
  ck_assert_int_eq(app->endpoints[3].method, DELETE);
}
END_TEST

MiddlewareResult test_middleware(ResponseContext *res) {
  set_data_int(res, "middleware_called", 1);
  return MIDDLEWARE_CONTINUE;
}

int blocking_middleware(ResponseContext *res) {
  set_data_int(res, "blocked", 1);
  return 1; // Stop
}

START_TEST(test_app_use_middleware) {
  app_use(app, test_middleware);
  ck_assert_int_eq(app->middleware_count, 1);
}
END_TEST

START_TEST(test_endpoint_with_middleware) {
  Middleware mws[] = {test_middleware, NULL};
  _app_append_endpoint(app, GET, "/protected", dummy_handler, mws);

  ck_assert_int_eq(app->endpoints[0].middleware_count, 1);
  ck_assert_ptr_eq(app->endpoints[0].middlewares[0], test_middleware);
}
END_TEST

START_TEST(test_create_group) {
  AppGroup *group = _app_create_group(app, "/api", NULL);

  ck_assert_ptr_nonnull(group);
  ck_assert_str_eq(group->root_path, "/api");
  ck_assert_int_eq(app->groups_count, 1);
}
END_TEST

START_TEST(test_group_with_endpoints) {
  AppGroup *group = _app_create_group(app, "/api", NULL);
  _app_append_endpoint_to_group(app, group, GET, "/users", dummy_handler, NULL);
  _app_append_endpoint_to_group(app, group, POST, "/users", dummy_handler,
                                NULL);

  ck_assert_int_eq(app->endpoint_count, 2);
  ck_assert_str_eq(app->endpoints[0].path, "/api/users");
  ck_assert_str_eq(app->endpoints[1].path, "/api/users");
}
END_TEST

START_TEST(test_group_with_middleware) {
  Middleware mws[] = {test_middleware, NULL};
  AppGroup *group = _app_create_group(app, "/api", mws);
  _app_append_endpoint_to_group(app, group, GET, "/users", dummy_handler, NULL);

  ck_assert_int_eq(app->endpoints[0].middleware_count, 1);
}
END_TEST

START_TEST(test_group_path_normalization) {
  AppGroup *group = _app_create_group(app, "/api/", NULL);
  _app_append_endpoint_to_group(app, group, GET, "/users", dummy_handler, NULL);

  ck_assert_str_eq(app->endpoints[0].path, "/api/users");
}
END_TEST

Suite *path_suite(void) {
  Suite *s = suite_create("Path Matching");
  TCase *tc = tcase_create("Core");

  tcase_add_checked_fixture(tc, setup, teardown);
  tcase_add_test(tc, test_compare_paths_exact_match);
  tcase_add_test(tc, test_compare_paths_no_match);
  tcase_add_test(tc, test_compare_paths_with_params);
  tcase_add_test(tc, test_compare_paths_multiple_params);
  tcase_add_test(tc, test_compare_paths_trailing_slash);
  tcase_add_test(tc, test_compare_paths_with_query_string);

  suite_add_tcase(s, tc);
  return s;
}

Suite *query_suite(void) {
  Suite *s = suite_create("Query Parameters");
  TCase *tc = tcase_create("Core");

  tcase_add_checked_fixture(tc, setup, teardown);
  tcase_add_test(tc, test_parse_query_params_single);
  tcase_add_test(tc, test_parse_query_params_multiple);
  tcase_add_test(tc, test_parse_query_params_url_encoded);
  tcase_add_test(tc, test_parse_query_params_special_chars);
  tcase_add_test(tc, test_parse_query_params_empty_value);
  tcase_add_test(tc, test_parse_query_params_no_value);
  tcase_add_test(tc, test_get_query_int);
  tcase_add_test(tc, test_get_query_double);

  suite_add_tcase(s, tc);
  return s;
}

Suite *data_suite(void) {
  Suite *s = suite_create("Request/Response Data");
  TCase *tc = tcase_create("Core");

  tcase_add_checked_fixture(tc, setup, teardown);
  tcase_add_test(tc, test_set_get_data_string);
  tcase_add_test(tc, test_set_get_data_int);
  tcase_add_test(tc, test_set_get_data_double);
  tcase_add_test(tc, test_set_data_overwrite);
  tcase_add_test(tc, test_get_data_missing);

  suite_add_tcase(s, tc);
  return s;
}

Suite *header_suite(void) {
  Suite *s = suite_create("Headers and Params");
  TCase *tc = tcase_create("Core");

  tcase_add_checked_fixture(tc, setup, teardown);
  tcase_add_test(tc, test_set_get_header);
  tcase_add_test(tc, test_set_header_overwrite);
  tcase_add_test(tc, test_set_header_case_insensitive);
  tcase_add_test(tc, test_get_header_from_request);
  tcase_add_test(tc, test_get_param);

  suite_add_tcase(s, tc);
  return s;
}

Suite *parsing_suite(void) {
  Suite *s = suite_create("HTTP Parsing");
  TCase *tc = tcase_create("Core");

  tcase_add_test(tc, test_parse_http_request_simple_get);
  tcase_add_test(tc, test_parse_http_request_with_body);

  suite_add_tcase(s, tc);
  return s;
}

Suite *app_suite(void) {
  Suite *s = suite_create("App and Endpoints");
  TCase *tc = tcase_create("Core");

  tcase_add_checked_fixture(tc, app_setup, app_teardown);
  tcase_add_test(tc, test_create_app);
  tcase_add_test(tc, test_app_append_endpoint);
  tcase_add_test(tc, test_app_multiple_endpoints);
  tcase_add_test(tc, test_app_use_middleware);
  tcase_add_test(tc, test_endpoint_with_middleware);

  suite_add_tcase(s, tc);
  return s;
}

Suite *group_suite(void) {
  Suite *s = suite_create("AppGroup");
  TCase *tc = tcase_create("Core");

  tcase_add_checked_fixture(tc, app_setup, app_teardown);
  tcase_add_test(tc, test_create_group);
  tcase_add_test(tc, test_group_with_endpoints);
  tcase_add_test(tc, test_group_with_middleware);
  tcase_add_test(tc, test_group_path_normalization);

  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  int failed = 0;

  Suite *suites[] = {
      path_suite(),    query_suite(), data_suite(),  header_suite(),
      parsing_suite(), app_suite(),   group_suite(), NULL};

  for (int i = 0; suites[i] != NULL; i++) {
    SRunner *sr = srunner_create(suites[i]);
    srunner_run_all(sr, CK_NORMAL);
    failed += srunner_ntests_failed(sr);
    srunner_free(sr);
  }

  return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
