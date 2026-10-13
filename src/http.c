#include "http.h"

#include "bx_utils.h"

#include <libwebsockets.h>

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  PH_QUEUED = 0,
  PH_RUNNING = 1,
  PH_PUBLISHED = 2,
  PH_WRITING = 3
};

typedef struct BxJob {
  struct lws *wsi;
  char *path;
  char *query;
  int is_get;
  char *body;
  size_t body_len;
  size_t sent;
  int status;
  char proxy[16];
  int headers_done;
  int dead;
  int phase;
  struct BxJob *next;
} BxJob;

typedef struct BxSess {
  BxJob *job;
} BxSess;

struct BxHttp {
  struct lws_context *ctx;
  BxServe *serve;
  pthread_t worker;
  int worker_started;
  pthread_mutex_t mu;
  pthread_cond_t cv;
  int stop;
  BxJob *qhead;
  BxJob *qtail;
  BxJob *rhead;
  BxJob *rtail;
};

static void free_job(BxJob *job) {
  if (job == NULL) {
    return;
  }
  free(job->path);
  free(job->query);
  free(job->body);
  free(job);
}

static void enqueue(BxJob **head, BxJob **tail, BxJob *job) {
  job->next = NULL;
  if (*tail != NULL) {
    (*tail)->next = job;
  } else {
    *head = job;
  }
  *tail = job;
}

static BxJob *dequeue(BxJob **head, BxJob **tail) {
  BxJob *job = *head;
  if (job == NULL) {
    return NULL;
  }
  *head = job->next;
  if (*head == NULL) {
    *tail = NULL;
  }
  job->next = NULL;
  return job;
}

static int unlink_job(BxJob **head, BxJob **tail, BxJob *job) {
  BxJob **pp = head;
  BxJob *prev = NULL;
  while (*pp != NULL) {
    if (*pp == job) {
      *pp = job->next;
      if (*tail == job) {
        *tail = prev;
      }
      job->next = NULL;
      return 1;
    }
    prev = *pp;
    pp = &(*pp)->next;
  }
  return 0;
}

static void *worker_main(void *arg) {
  BxHttp *http = arg;
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGINT);
  sigaddset(&set, SIGTERM);
  sigaddset(&set, SIGHUP);
  pthread_sigmask(SIG_BLOCK, &set, NULL);

  if (bx_serve_connect(http->serve) != 0) {
    bx_log_error("MySQL is not connected; requests will return 503");
  }

  for (;;) {
    BxJob *job = NULL;
    int status = 500;
    char *body = NULL;
    char proxy[16];
    size_t body_len = 0;

    pthread_mutex_lock(&http->mu);
    while (http->qhead == NULL && !http->stop) {
      pthread_cond_wait(&http->cv, &http->mu);
    }
    if (http->qhead == NULL && http->stop) {
      pthread_mutex_unlock(&http->mu);
      break;
    }
    job = dequeue(&http->qhead, &http->qtail);
    job->phase = PH_RUNNING;
    pthread_mutex_unlock(&http->mu);

    proxy[0] = '\0';
    if (bx_serve_request(http->serve, job->path, job->query, job->is_get,
                         &status, &body, proxy, sizeof(proxy)) != 0 ||
        body == NULL) {
      free(body);
      body = strdup("{\"error\":\"encode failed\"}");
      status = 500;
      proxy[0] = '\0';
    }
    if (body != NULL) {
      body_len = strlen(body);
    }
    bx_serve_recover(http->serve);

    pthread_mutex_lock(&http->mu);
    if (job->dead) {
      pthread_mutex_unlock(&http->mu);
      free(body);
      free_job(job);
      continue;
    }
    job->body = body;
    job->body_len = body_len;
    job->status = status;
    snprintf(job->proxy, sizeof(job->proxy), "%s", proxy);
    job->phase = PH_PUBLISHED;
    enqueue(&http->rhead, &http->rtail, job);
    pthread_mutex_unlock(&http->mu);
    lws_cancel_service(http->ctx);
  }

  bx_serve_close(http->serve);
  return NULL;
}

static int write_response(struct lws *wsi, BxJob *job) {
  unsigned char buf[LWS_PRE + 4096];
  unsigned char *start = buf + LWS_PRE;
  unsigned char *p = start;
  unsigned char *end = buf + sizeof(buf);
  size_t chunk;
  int n;

  if (job->dead) {
    return -1;
  }
  if (!job->headers_done) {
    if (lws_add_http_header_status(wsi, (unsigned int)job->status, &p, end)) {
      return -1;
    }
    if (lws_add_http_header_by_token(
            wsi, WSI_TOKEN_HTTP_CONTENT_TYPE,
            (unsigned char *)"application/json", 16, &p, end)) {
      return -1;
    }
    if (job->proxy[0] != '\0' &&
        lws_add_http_header_by_name(wsi, (unsigned char *)"X-Bx-Proxy:",
                                    (unsigned char *)job->proxy,
                                    (int)strlen(job->proxy), &p, end)) {
      return -1;
    }
    if (lws_add_http_header_content_length(wsi, (lws_filepos_t)job->body_len, &p,
                                           end)) {
      return -1;
    }
    if (lws_finalize_write_http_header(wsi, start, &p, end)) {
      return -1;
    }
    job->headers_done = 1;
    if (job->body_len == 0) {
      return lws_http_transaction_completed(wsi) ? -1 : 1;
    }
    lws_callback_on_writable(wsi);
    return 0;
  }

  chunk = job->body_len - job->sent;
  if (chunk > 4096) {
    chunk = 4096;
  }
  memcpy(start, job->body + job->sent, chunk);
  n = lws_write(wsi, start, chunk, LWS_WRITE_HTTP);
  if (n <= 0) {
    return -1;
  }
  job->sent += (size_t)n;
  if (job->sent < job->body_len) {
    lws_callback_on_writable(wsi);
    return 0;
  }
  return lws_http_transaction_completed(wsi) ? -1 : 1;
}

static void schedule_ready(BxHttp *http) {
  BxJob *job = NULL;
  BxJob *list = NULL;
  pthread_mutex_lock(&http->mu);
  list = http->rhead;
  http->rhead = NULL;
  http->rtail = NULL;
  for (job = list; job != NULL; job = job->next) {
    job->phase = PH_WRITING;
  }
  pthread_mutex_unlock(&http->mu);
  while (list != NULL) {
    job = list;
    list = list->next;
    job->next = NULL;
    if (job->wsi != NULL && !job->dead) {
      lws_callback_on_writable(job->wsi);
    }
  }
}

static int callback_http(struct lws *wsi, enum lws_callback_reasons reason,
                         void *user, void *in, size_t len) {
  BxSess *sess = user;
  BxHttp *http = NULL;
  struct lws_context *ctx = NULL;

  if (wsi != NULL) {
    ctx = lws_get_context(wsi);
    http = lws_context_user(ctx);
  }

  switch (reason) {
  case LWS_CALLBACK_HTTP: {
    BxJob *job = NULL;
    int argc_len = 0;
    if (sess == NULL || http == NULL || in == NULL || len == 0 || len > 512) {
      return -1;
    }
    job = calloc(1, sizeof(*job));
    if (job == NULL) {
      return -1;
    }
    job->wsi = wsi;
    job->path = malloc(len + 1);
    if (job->path == NULL) {
      free_job(job);
      return -1;
    }
    memcpy(job->path, in, len);
    job->path[len] = '\0';
    job->is_get = lws_hdr_total_length(wsi, WSI_TOKEN_GET_URI) > 0;
    argc_len = lws_hdr_total_length(wsi, WSI_TOKEN_HTTP_URI_ARGS);
    if (argc_len > 0 && argc_len < 2048) {
      job->query = malloc((size_t)argc_len + 1);
      if (job->query == NULL ||
          lws_hdr_copy(wsi, job->query, argc_len + 1, WSI_TOKEN_HTTP_URI_ARGS) <
              0) {
        free_job(job);
        return -1;
      }
    }
    job->phase = PH_QUEUED;
    sess->job = job;
    pthread_mutex_lock(&http->mu);
    enqueue(&http->qhead, &http->qtail, job);
    pthread_cond_signal(&http->cv);
    pthread_mutex_unlock(&http->mu);
    return 0;
  }
  case LWS_CALLBACK_HTTP_WRITEABLE: {
    int done;
    if (sess == NULL || sess->job == NULL) {
      return -1;
    }
    done = write_response(wsi, sess->job);
    if (done < 0) {
      free_job(sess->job);
      sess->job = NULL;
      return -1;
    }
    if (done > 0) {
      free_job(sess->job);
      sess->job = NULL;
    }
    return 0;
  }
  case LWS_CALLBACK_CLOSED_HTTP:
    if (sess == NULL || sess->job == NULL || http == NULL) {
      return 0;
    }
    pthread_mutex_lock(&http->mu);
    sess->job->dead = 1;
    sess->job->wsi = NULL;
    if (sess->job->phase == PH_QUEUED) {
      unlink_job(&http->qhead, &http->qtail, sess->job);
    } else if (sess->job->phase == PH_PUBLISHED) {
      unlink_job(&http->rhead, &http->rtail, sess->job);
    }
    if (sess->job->phase == PH_RUNNING) {
      pthread_mutex_unlock(&http->mu);
      return 0;
    }
    pthread_mutex_unlock(&http->mu);
    free_job(sess->job);
    sess->job = NULL;
    return 0;
  case LWS_CALLBACK_EVENT_WAIT_CANCELLED:
    if (http != NULL) {
      schedule_ready(http);
    }
    return 0;
  default:
    return lws_callback_http_dummy(wsi, reason, user, in, len);
  }
}

static const struct lws_protocols protocols[] = {
    {"http", callback_http, sizeof(BxSess), 0, 0, NULL, 0},
    LWS_PROTOCOL_LIST_TERM};

BxHttp *bx_http_new(BxServe *serve, const char *host, int port) {
  BxHttp *http = NULL;
  struct lws_context_creation_info info;

  if (serve == NULL || host == NULL || port < 1 || port > 65535) {
    return NULL;
  }
  http = calloc(1, sizeof(*http));
  if (http == NULL) {
    return NULL;
  }
  http->serve = serve;
  pthread_mutex_init(&http->mu, NULL);
  pthread_cond_init(&http->cv, NULL);
  memset(&info, 0, sizeof(info));
  info.port = port;
  info.iface = host;
  info.protocols = protocols;
  info.user = http;
  info.gid = (gid_t)-1;
  info.uid = (uid_t)-1;
  info.timeout_secs = 120;
  info.options = LWS_SERVER_OPTION_VALIDATE_UTF8;
  http->ctx = lws_create_context(&info);
  if (http->ctx == NULL) {
    pthread_cond_destroy(&http->cv);
    pthread_mutex_destroy(&http->mu);
    free(http);
    return NULL;
  }
  if (pthread_create(&http->worker, NULL, worker_main, http) != 0) {
    lws_context_destroy(http->ctx);
    pthread_cond_destroy(&http->cv);
    pthread_mutex_destroy(&http->mu);
    free(http);
    return NULL;
  }
  http->worker_started = 1;
  return http;
}

int bx_http_run(BxHttp *http, volatile sig_atomic_t *stop) {
  if (http == NULL || http->ctx == NULL) {
    return -1;
  }
  while (stop == NULL || *stop == 0) {
    lws_service(http->ctx, 200);
  }
  return 0;
}

void bx_http_free(BxHttp *http) {
  if (http == NULL) {
    return;
  }
  if (http->worker_started) {
    pthread_mutex_lock(&http->mu);
    http->stop = 1;
    pthread_cond_broadcast(&http->cv);
    pthread_mutex_unlock(&http->mu);
    pthread_join(http->worker, NULL);
    http->worker_started = 0;
  }
  if (http->ctx != NULL) {
    lws_context_destroy(http->ctx);
    http->ctx = NULL;
  }
  pthread_cond_destroy(&http->cv);
  pthread_mutex_destroy(&http->mu);
  free(http);
}
