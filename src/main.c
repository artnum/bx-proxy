#include "http.h"

#include "bx_conf.h"
#include "bx_mutex.h"
#include "bx_net.h"
#include "bx_utils.h"
#include "bxobjects/country_code.h"

#include <mysql/mysql.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern BXMutex MTX_COUNTRY_LIST;

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig) {
  (void)sig;
  g_stop = 1;
}

static void usage(void) {
  fprintf(stderr, "Usage: bx-proxy <config>\n");
}

int main(int argc, char **argv) {
  BXConf *conf = NULL;
  bXill app;
  const char *config_path = NULL;
  const char *host = NULL;
  int port = 0;
  int stale = 0;
  BXNet *net = NULL;
  BXNetRequestList *queue = NULL;
  pthread_t request_thread;
  pthread_t log_thread;
  BxServe *serve = NULL;
  BxHttp *http = NULL;
  struct sigaction sa;

  if (argc < 2 || argv[1][0] == '\0') {
    usage();
    return EXIT_FAILURE;
  }
  config_path = argv[1];
  memset(&app, 0, sizeof(app));

  conf = bx_conf_init();
  if (conf == NULL || !bx_conf_loadfile(conf, config_path)) {
    fprintf(stderr, "Could not load config\n");
    bx_conf_destroy(&conf);
    return EXIT_FAILURE;
  }
  mysql_library_init(argc, argv, NULL);
  if (!bx_conf_load_database(conf)) {
    fprintf(stderr, "Database configuration was not loaded; keeping the file "
                    "values\n");
  }
  if (!bx_log_init(&app, bx_conf_get_string(conf, "log-file"),
                   bx_conf_get_int(conf, "log-level"))) {
    fprintf(stderr, "Could not open the log file\n");
    mysql_library_end();
    bx_conf_destroy(&conf);
    return EXIT_FAILURE;
  }

  host = bx_conf_get_string(conf, "listen-host");
  if (host == NULL || host[0] == '\0') {
    host = "127.0.0.1";
  }
  port = bx_conf_get_int(conf, "listen-port");
  if (port <= 0) {
    port = 8080;
  }
  if (port > 65535) {
    fprintf(stderr, "listen-port is out of range\n");
    bx_log_end();
    mysql_library_end();
    bx_conf_destroy(&conf);
    return EXIT_FAILURE;
  }
  stale = bx_conf_get_int(conf, "stale-seconds");

  if (!bx_utils_init()) {
    fprintf(stderr, "bx_utils_init failed\n");
    mysql_library_end();
    bx_log_end();
    bx_conf_destroy(&conf);
    return EXIT_FAILURE;
  }
  app.conf = conf;
  atomic_store(&app.logthread, true);
  if (pthread_create(&log_thread, NULL, bx_log_out_thread, &app) != 0) {
    fprintf(stderr, "Could not start the log thread\n");
    bx_utils_close();
    mysql_library_end();
    bx_log_end();
    bx_conf_destroy(&conf);
    return EXIT_FAILURE;
  }

  bx_mutex_init(&MTX_COUNTRY_LIST);
  net = bx_net_init(conf);
  if (net == NULL) {
    bx_log_error("Net configuration failed");
    atomic_store(&app.logthread, false);
    pthread_join(log_thread, NULL);
    bx_utils_close();
    mysql_library_end();
    bx_log_end();
    bx_conf_destroy(&conf);
    return EXIT_FAILURE;
  }
  app.net = net;
  queue = bx_net_request_list_init(net);
  app.queue = queue;
  request_thread = bx_net_loop(queue);

  if (!bx_country_code_load(&app)) {
    bx_log_error("Country list load failed; continuing");
  }

  serve = bx_serve_new(&app, stale);
  http = bx_http_new(serve, host, port);
  if (serve == NULL || http == NULL) {
    fprintf(stderr, "Could not listen on %s:%d\n", host, port);
    bx_http_free(http);
    bx_serve_free(serve);
    atomic_store(&queue->run, false);
    pthread_cond_signal(&queue->in_cond);
    pthread_join(request_thread, NULL);
    bx_country_list_free();
    bx_net_request_list_destroy(queue);
    bx_net_destroy(&net);
    atomic_store(&app.logthread, false);
    pthread_join(log_thread, NULL);
    bx_utils_close();
    mysql_library_end();
    bx_log_end();
    bx_conf_destroy(&conf);
    return EXIT_FAILURE;
  }

  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = on_signal;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);

  fprintf(stderr, "bx-proxy listening on %s:%d\n", host, port);
  bx_log_info("bx-proxy listening on %s:%d", host, port);
  bx_http_run(http, &g_stop);

  bx_http_free(http);
  bx_serve_free(serve);
  atomic_store(&queue->run, false);
  pthread_cond_signal(&queue->in_cond);
  pthread_join(request_thread, NULL);
  bx_country_list_free();
  bx_net_request_list_destroy(queue);
  bx_net_destroy(&net);
  atomic_store(&app.logthread, false);
  pthread_join(log_thread, NULL);
  bx_utils_close();
  mysql_library_end();
  bx_log_end();
  bx_conf_destroy(&conf);
  return EXIT_SUCCESS;
}
