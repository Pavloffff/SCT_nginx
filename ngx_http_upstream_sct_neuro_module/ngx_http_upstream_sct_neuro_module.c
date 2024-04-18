/*
 * Copyright (C) Ivan Pavlov
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
//#include "ngx_http_upstream_sct_neuro_peer.h" // Объявление своих типов (названия те же но вместо ..._http_upstream_... надо писать ..._http_custom_upstream_...)

typedef struct ngx_http_upstream_sct_neuro_peer_s  ngx_http_upstream_sct_neuro_peer_t;

struct ngx_http_upstream_sct_neuro_peer_s {
    struct sockaddr                *sockaddr;
    socklen_t                       socklen;
    ngx_str_t                       name;
    ngx_str_t                       server;

    ngx_int_t                       current_weight;
    ngx_int_t                       effective_weight;
    ngx_int_t                       weight;

    ngx_uint_t                      conns;
    ngx_uint_t                      max_conns;

    ngx_uint_t                      fails;
    time_t                          accessed;
    time_t                          checked;

    ngx_uint_t                      max_fails;
    time_t                          fail_timeout;
    ngx_msec_t                      slow_start;
    ngx_msec_t                      start_time;

    ngx_uint_t                      down;

    ngx_uint_t                      cnt_requests; // кол-во запросов на этот адрес
    ngx_uint_t                      cnt_responses; // кол-во ответов с этого адреса
    double                          neuro_weight; // текущие веса от нейронки

#if (NGX_HTTP_SSL || NGX_COMPAT)
    void                           *ssl_session;
    int                             ssl_session_len;
#endif

#if (NGX_HTTP_UPSTREAM_ZONE)
    ngx_atomic_t                    lock;
#endif

    ngx_http_upstream_sct_neuro_peer_t    *next;

    NGX_COMPAT_BEGIN(32)
    NGX_COMPAT_END
};

typedef struct ngx_http_upstream_sct_neuro_peers_s  ngx_http_upstream_sct_neuro_peers_t;

struct ngx_http_upstream_sct_neuro_peers_s {
    ngx_uint_t                      number;

#if (NGX_HTTP_UPSTREAM_ZONE)
    ngx_slab_pool_t                *shpool;
    ngx_atomic_t                    rwlock;
    ngx_http_upstream_sct_neuro_peers_t   *zone_next;
#endif

    ngx_uint_t                      total_weight;
    ngx_uint_t                      tries;

    unsigned                        single:1;
    unsigned                        weighted:1;

    ngx_str_t                      *name;

    ngx_http_upstream_sct_neuro_peers_t   *next;

    ngx_http_upstream_sct_neuro_peers_t    *peer;
};

typedef struct {
    ngx_uint_t                      config;
    ngx_http_upstream_sct_neuro_peers_t    *peers;
    ngx_http_upstream_sct_neuro_peer_t     *current;
    uintptr_t                      *tried;
    uintptr_t                       data;

    //
    time_t                          start_iteration_time; //время последнего пересчета весов
    ngx_uint_t                      gap_in_requests; //промежуток (в запросах) через который пересчитываются веса.

} ngx_http_upstream_sct_neuro_peer_data_t;

//

static ngx_int_t ngx_http_upstream_init_sct_neuro_peer(ngx_http_request_t *r,
    ngx_http_upstream_srv_conf_t *us);
static ngx_int_t ngx_http_upstream_get_sct_neuro_peer(
    ngx_peer_connection_t *pc, void *data);
static char *ngx_http_upstream_sct_neuro(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);


static ngx_command_t  ngx_http_upstream_sct_neuro_commands[] = {
    { ngx_string("sct_neuro"),    /* directive for using this module */
      NGX_HTTP_UPS_CONF|NGX_CONF_NOARGS,
      ngx_http_upstream_sct_neuro,
      0,
      0,
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_upstream_sct_neuro_module_ctx = {
    NULL,                                  /* preconfiguration */
    NULL,                                  /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    NULL,                                  /* create location configuration */
    NULL                                   /* merge location configuration */
};


ngx_module_t  ngx_http_upstream_sct_neuro_module = {
    NGX_MODULE_V1,
    &ngx_http_upstream_sct_neuro_module_ctx,  /* module context */
    ngx_http_upstream_sct_neuro_commands,     /* module directives */
    NGX_HTTP_MODULE,                                    /* module type */
    NULL,                                               /* init master */
    NULL,                                               /* init module */
    NULL,                                               /* init process */
    NULL,                                               /* init thread */
    NULL,                                               /* exit thread */
    NULL,                                               /* exit process */
    NULL,                                               /* exit master */
    NGX_MODULE_V1_PADDING
};

static ngx_int_t
ngx_http_upstream_init_sct_neuro(ngx_conf_t *cf,
    ngx_http_upstream_srv_conf_t *us)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0,
                   "init sct neuro");

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0,
                   "info: %s", cf->args->elts);

    if (ngx_http_upstream_init_round_robin(cf, us) != NGX_OK) {
        return NGX_ERROR;
    }

    us->peer.init = ngx_http_upstream_init_sct_neuro_peer;

    return NGX_OK;
}

static ngx_int_t
ngx_http_upstream_init_sct_neuro_peer(ngx_http_request_t *r,
    ngx_http_upstream_srv_conf_t *us)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "init sct neuro peer");

    if (ngx_http_upstream_init_round_robin_peer(r, us) != NGX_OK) {
        return NGX_ERROR;
    }

    r->upstream->peer.get = ngx_http_upstream_get_sct_neuro_peer;

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_get_sct_neuro_peer(ngx_peer_connection_t *pc, void *data)
{
    ngx_uint_t                    i;
    ngx_http_upstream_rr_peer_t  *peer;

    ngx_http_upstream_rr_peer_data_t  *rrp = data;
    for (peer = rrp->peers->peer, i = 0;
        peer;
        peer = peer->next, i++)
    {
        ngx_log_debug8(NGX_LOG_DEBUG_HTTP, pc->log, 0,
                   "peer no: %ui, socklen: %ui, name: %s, server: %s, "
                   "current_weight: %d, effective_weight: %d, weight: %d, "
                   "conns: %ui", i, peer->socklen, peer->name.data,
                   peer->server.data, peer->current_weight, 
                   peer->effective_weight, peer->weight, peer->conns);
        ngx_log_debug8(NGX_LOG_DEBUG_HTTP, pc->log, 0,
                   "max_conns: %ui, fails: %ui, accessed: %ld, checked: %ld"
                   ", max_fails: %ui, fail_timeout: %ld, slow_start: %d, "
                   "start_time: %d", peer->max_conns, peer->fails,
                   peer->accessed, peer->checked, peer->max_fails, 
                   peer->fail_timeout, peer->slow_start, peer->start_time);
    }
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, pc->log, 0,
                   "get sct neuro peer, try: %ui", pc->tries);
    
    return ngx_http_upstream_get_round_robin_peer(pc, rrp);
}


static char *
ngx_http_upstream_sct_neuro(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_upstream_srv_conf_t  *uscf;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0,
                   "info: %s", cf->args->elts);

    uscf = ngx_http_conf_get_module_srv_conf(cf, ngx_http_upstream_module);

    if (uscf->peer.init_upstream) {
        ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                           "load balancing method redefined");
    }

    uscf->peer.init_upstream = ngx_http_upstream_init_sct_neuro;

    uscf->flags = NGX_HTTP_UPSTREAM_CREATE
                  |NGX_HTTP_UPSTREAM_WEIGHT
                  |NGX_HTTP_UPSTREAM_MAX_CONNS
                  |NGX_HTTP_UPSTREAM_MAX_FAILS
                  |NGX_HTTP_UPSTREAM_FAIL_TIMEOUT
                  |NGX_HTTP_UPSTREAM_DOWN
                  |NGX_HTTP_UPSTREAM_BACKUP;

    return NGX_CONF_OK;
}