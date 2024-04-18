#ifndef NGX_HTTP_UPSTREAM_SCT_NEURO_PEER_H
#define NGX_HTTP_UPSTREAM_SCT_NEURO_PEER_H

#include <ngx_config.h>
#include <ngx_core.h>

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

//Пытаюсь изменить уже существующие типы данных под нас

#endif /* NGX_HTTP_CUSTOM_UPSTREAM_RR_PEER_H */
