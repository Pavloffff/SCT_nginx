/*
 * Copyright (C) Ivan Pavlov
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


// typedef struct ngx_http_upstream_sct_neuro_peer_s  ngx_http_upstream_sct_neuro_peer_t;

// struct ngx_http_upstream_sct_neuro_peer_s {
//     struct sockaddr                *sockaddr;
//     socklen_t                       socklen;
//     ngx_str_t                       name;
//     ngx_str_t                       server;

//     ngx_int_t                       current_weight;
//     ngx_int_t                       effective_weight;
//     ngx_int_t                       weight;

//     ngx_uint_t                      conns;
//     ngx_uint_t                      max_conns;

//     ngx_uint_t                      fails;
//     time_t                          accessed;
//     time_t                          checked;

//     ngx_uint_t                      max_fails;
//     time_t                          fail_timeout;
//     ngx_msec_t                      slow_start;
//     ngx_msec_t                      start_time;

//     ngx_uint_t                      down;

//     ngx_uint_t                      cnt_requests;   // кол-во запросов на этот адрес
//     ngx_uint_t                      cnt_responses;  // кол-во ответов с этого адреса
//     double                          neuro_weight;   // текущие веса от нейронки

// #if (NGX_HTTP_SSL || NGX_COMPAT)
//     void                           *ssl_session;
//     int                             ssl_session_len;
// #endif

// #if (NGX_HTTP_UPSTREAM_ZONE)
//     ngx_atomic_t                    lock;
// #endif

//     ngx_http_upstream_sct_neuro_peer_t    *next;

//     NGX_COMPAT_BEGIN(32)
//     NGX_COMPAT_END
// };

// typedef struct ngx_http_upstream_sct_neuro_peers_s  ngx_http_upstream_sct_neuro_peers_t;

// struct ngx_http_upstream_sct_neuro_peers_s {
//     ngx_uint_t                      number;

// #if (NGX_HTTP_UPSTREAM_ZONE)
//     ngx_slab_pool_t                *shpool;
//     ngx_atomic_t                    rwlock;
//     ngx_http_upstream_sct_neuro_peers_t   *zone_next;
// #endif

//     ngx_uint_t                      total_weight;
//     ngx_uint_t                      tries;

//     unsigned                        single:1;
//     unsigned                        weighted:1;

//     ngx_str_t                      *name;

//     ngx_http_upstream_sct_neuro_peers_t   *next;

//     ngx_http_upstream_sct_neuro_peers_t    *peer;
// };

// typedef struct {
//     ngx_uint_t                              config;
//     ngx_http_upstream_sct_neuro_peers_t    *peers;
//     ngx_http_upstream_sct_neuro_peer_t     *current;
//     uintptr_t                              *tried;
//     uintptr_t                               data;

//     time_t                                  start_iteration_time;   // время последнего пересчета весов
//     ngx_uint_t                              gap_in_requests;        // промежуток (в запросах) через который пересчитываются веса.

// } ngx_http_upstream_sct_neuro_peer_data_t;


typedef struct {
    ngx_str_t                                addr;
    uintptr_t                                peers;
    ngx_atomic_t                             lock;       // unsigned long
    ngx_uint_t                               nreq;
    ngx_uint_t                               nres;
    ngx_uint_t                               fails;
} ngx_http_upstream_sct_neuro_shm_block_t;

#define ngx_spinlock_unlock(lock)       (void) ngx_atomic_cmp_set(lock, ngx_pid, 0)

static ngx_int_t ngx_http_sct_neuro_header_filter(ngx_http_request_t *r);
static ngx_int_t ngx_http_sct_neuro_filter_init(ngx_conf_t *cf);


static ngx_int_t ngx_http_upstream_init_sct_neuro_peer(ngx_http_request_t *r,
    ngx_http_upstream_srv_conf_t *us);
static ngx_int_t ngx_http_upstream_get_sct_neuro_peer(
    ngx_peer_connection_t *pc, void *data);
static char *ngx_http_upstream_sct_neuro(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_upstream_sct_neuro_set_shm_size(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);   


static ngx_command_t  ngx_http_upstream_sct_neuro_commands[] = {
    { ngx_string("sct_neuro"),    /* directive for using this module */
      NGX_HTTP_UPS_CONF|NGX_CONF_NOARGS,
      ngx_http_upstream_sct_neuro,
      0,
      0,
      NULL },

      { ngx_string("upstream_sct_neuro_shm_size"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_http_upstream_sct_neuro_set_shm_size,
      0,
      0,
      NULL },

      ngx_null_command
};

static ngx_http_module_t  ngx_http_upstream_sct_neuro_module_ctx = {
    NULL,        /* preconfiguration */
    NULL,          /* postconfiguration */

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

static ngx_http_module_t  ngx_http_sct_neuro_filter_module_ctx = {
    NULL,        /* preconfiguration */
    ngx_http_sct_neuro_filter_init,          /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    NULL,                                  /* create location configuration */
    NULL                                   /* merge location configuration */
};

ngx_module_t  ngx_http_sct_neuro_filter_module = {
    NGX_MODULE_V1,
    &ngx_http_sct_neuro_filter_module_ctx,   /* module context */
    NULL,                                  /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_uint_t ngx_http_upstream_sct_neuro_shm_size;             
static ngx_shm_zone_t *ngx_http_upstream_sct_neuro_shm_zone;

static ngx_int_t
ngx_http_upstream_sct_neuro_init_shm_zone(ngx_shm_zone_t *shm_zone, void *data)
{
    ngx_slab_pool_t *shpool;
    ngx_http_upstream_sct_neuro_shm_block_t *blocks;
    ngx_uint_t i, j, num_blocks;
    ngx_http_upstream_srv_conf_t *uscf = shm_zone->data;
    
    if (data) {
        shm_zone->data = data;
        return NGX_OK;
    }

    shpool = (ngx_slab_pool_t *) shm_zone->shm.addr;

    num_blocks = 0;
    ngx_http_upstream_server_t *server = uscf->servers->elts;
    for (i = 0; i < uscf->servers->nelts; i++) {
        num_blocks += server[i].naddrs;
    }

    blocks = ngx_slab_alloc(shpool, sizeof(ngx_http_upstream_sct_neuro_shm_block_t) * num_blocks);
    if (blocks == NULL) {
        return NGX_ERROR;
    }

    ngx_uint_t block_index = 0;
    for (i = 0; i < uscf->servers->nelts; i++) {
        for (j = 0; j < server[i].naddrs; j++) {
            ngx_memzero(&blocks[block_index], sizeof(ngx_http_upstream_sct_neuro_shm_block_t));
            blocks[block_index].addr = server[i].addrs[j].name;
            block_index++;
        }
    }

    shm_zone->data = blocks;

    return NGX_OK;
}

static char *
ngx_http_upstream_sct_neuro_set_shm_size(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ssize_t new_shm_size;
    ngx_str_t *value;

    value = cf->args->elts;

    new_shm_size = ngx_parse_size(&value[1]);                                           
    if (new_shm_size == NGX_ERROR) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "Invalid memory area size `%V'", &value[1]);
        return NGX_CONF_ERROR;
    }

    new_shm_size = ngx_align(new_shm_size, ngx_pagesize);                               

    if (new_shm_size < 8 * (ssize_t) ngx_pagesize) {
        ngx_conf_log_error(NGX_LOG_WARN, cf, 0, "The upstream_sct_neuro_shm_size value must be at least %udKiB", (8 * ngx_pagesize) >> 10);
        new_shm_size = 8 * ngx_pagesize;
    }

    if (ngx_http_upstream_sct_neuro_shm_size &&
        ngx_http_upstream_sct_neuro_shm_size != (ngx_uint_t) new_shm_size) {
        ngx_conf_log_error(NGX_LOG_WARN, cf, 0, "Cannot change memory area size without restart, ignoring change");
    } else {
        ngx_http_upstream_sct_neuro_shm_size = new_shm_size;                                 
    }

    ngx_conf_log_error(NGX_LOG_DEBUG, cf, 0, "Using %udKiB of shared memory for upstream_sct_neuro", new_shm_size >> 10);

    return NGX_CONF_OK;
}

static ngx_int_t
ngx_http_upstream_init_sct_neuro(ngx_conf_t *cf,
    ngx_http_upstream_srv_conf_t *us)
{
    // ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0,
    //                "init sct neuro");

    // ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0,
    //                "info: %s", cf->args->elts);

    ngx_str_t shm_name = ngx_string("sct_neuro");
    ngx_uint_t num_blocks = 0;
    ngx_http_upstream_server_t *server;

    if (ngx_http_upstream_init_round_robin(cf, us) != NGX_OK) {
        return NGX_ERROR;
    }

    if (us->servers) {
        server = us->servers->elts;
        for (ngx_uint_t i = 0; i < us->servers->nelts; i++) {
            num_blocks += server[i].naddrs;
        }
    }

    ngx_shm_zone_t *shm_zone = ngx_shared_memory_add(cf, &shm_name, ngx_http_upstream_sct_neuro_shm_size, &ngx_http_upstream_sct_neuro_module);
    if (shm_zone == NULL) {
        return NGX_ERROR;
    }

    shm_zone->data = us;
    shm_zone->init = ngx_http_upstream_sct_neuro_init_shm_zone;
    ngx_http_upstream_sct_neuro_shm_zone = shm_zone;

    us->peer.init = ngx_http_upstream_init_sct_neuro_peer;

    return NGX_OK;
}

static ngx_int_t
ngx_http_upstream_init_sct_neuro_peer(ngx_http_request_t *r,
    ngx_http_upstream_srv_conf_t *us)
{
    // ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
    //                "init sct neuro peer");

    if (ngx_http_upstream_init_round_robin_peer(r, us) != NGX_OK) {
        return NGX_ERROR;
    }

    r->upstream->peer.get = ngx_http_upstream_get_sct_neuro_peer;

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_get_sct_neuro_peer(ngx_peer_connection_t *pc, void *data)
{
    // ngx_uint_t                    i;
    // ngx_http_upstream_rr_peer_t  *peer;

    ngx_http_upstream_rr_peer_data_t  *rrp = data;
    // for (peer = rrp->peers->peer, i = 0;
    //     peer;
    //     peer = peer->next, i++)
    // {
    //     ngx_log_debug8(NGX_LOG_DEBUG_HTTP, pc->log, 0,
    //                "peer no: %ui, socklen: %ui, name: %s, server: %s, "
    //                "current_weight: %d, effective_weight: %d, weight: %d, "
    //                "conns: %ui", i, peer->socklen, peer->name.data,
    //                peer->server.data, peer->current_weight, 
    //                peer->effective_weight, peer->weight, peer->conns);
    //     ngx_log_debug8(NGX_LOG_DEBUG_HTTP, pc->log, 0,
    //                "max_conns: %ui, fails: %ui, accessed: %ld, checked: %ld"
    //                ", max_fails: %ui, fail_timeout: %ld, slow_start: %d, "
    //                "start_time: %d", peer->max_conns, peer->fails,
    //                peer->accessed, peer->checked, peer->max_fails, 
    //                peer->fail_timeout, peer->slow_start, peer->start_time);
    // }
    // ngx_log_debug1(NGX_LOG_DEBUG_HTTP, pc->log, 0,
    //                "get sct neuro peer, try: %ui", pc->tries);
    
    return ngx_http_upstream_get_round_robin_peer(pc, rrp);
}


static char *
ngx_http_upstream_sct_neuro(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_upstream_srv_conf_t  *uscf;

    // ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0,
    //                "info: %s", cf->args->elts);

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


static ngx_http_output_header_filter_pt  ngx_http_next_header_filter;

// static ngx_int_t
// ngx_http_sct_neuro_header_filter(ngx_http_request_t *r)
// {
//     ngx_table_elt_t  *h;
//     struct sockaddr_in  *sin;

//     if (r->headers_out.status != NGX_HTTP_OK) {
//         return ngx_http_next_header_filter(r);
//     }

//     // Upstream server address
//     if (r->upstream && r->upstream->peer.name) {
//         h = ngx_list_push(&r->headers_out.headers);
//         if (h == NULL) {
//             return NGX_ERROR;
//         }
//         h->hash = 1;
//         ngx_str_set(&h->key, "X-Upstream-Addr");
//         h->value.data = ngx_pnalloc(r->pool, r->upstream->peer.name->len + 1);
//         if (h->value.data == NULL) {
//             return NGX_ERROR;
//         }
//         ngx_memcpy(h->value.data, r->upstream->peer.name->data, r->upstream->peer.name->len);
//         h->value.data[r->upstream->peer.name->len] = '\0';
//         h->value.len = r->upstream->peer.name->len;

//         // Upstream server port
//         h = ngx_list_push(&r->headers_out.headers);
//         if (h == NULL) {
//             return NGX_ERROR;
//         }
//         h->hash = 1;
//         ngx_str_set(&h->key, "X-Upstream-Port");
//         h->value.data = ngx_pnalloc(r->pool, NGX_INT_T_LEN);
//         if (h->value.data == NULL) {
//             return NGX_ERROR;
//         }
//         sin = (struct sockaddr_in *) r->upstream->peer.sockaddr;
//         h->value.len = ngx_sprintf(h->value.data, "%d", ntohs(sin->sin_port)) - h->value.data;
//     }

//     return ngx_http_next_header_filter(r);
// }

static ngx_int_t ngx_http_sct_neuro_header_filter(ngx_http_request_t *r) {
    ngx_table_elt_t  *h;
    struct sockaddr_in  *sin;
    ngx_http_upstream_sct_neuro_shm_block_t *blocks;
    ngx_http_upstream_sct_neuro_shm_block_t *block = NULL;
    // ngx_slab_pool_t *shpool;
    ngx_uint_t i, num_blocks;
    ngx_atomic_t *lock;

    if (r->headers_out.status != NGX_HTTP_OK) {
        return ngx_http_next_header_filter(r);
    }

    if (r->upstream && r->upstream->peer.name) {
        // Получаем доступ к разделяемой памяти
        // shpool = (ngx_slab_pool_t *) ngx_http_upstream_sct_neuro_shm_zone->shm.addr;
        blocks = (ngx_http_upstream_sct_neuro_shm_block_t *) ngx_http_upstream_sct_neuro_shm_zone->data;

        // Определяем количество блоков
        num_blocks = ngx_http_upstream_sct_neuro_shm_size / sizeof(ngx_http_upstream_sct_neuro_shm_block_t);

        // Ищем блок, соответствующий текущему upstream серверу
        for (i = 0; i < num_blocks; i++) {
            if (ngx_strcmp(blocks[i].addr.data, r->upstream->peer.name->data) == 0) {
                block = &blocks[i];
                break;
            }
        }

        if (block) {
            lock = &block->lock;
            ngx_spinlock(lock, ngx_pid, 1024);
            
            h = ngx_list_push(&r->headers_out.headers);
            if (h == NULL) {
                ngx_spinlock_unlock(lock);
                return NGX_ERROR;
            }
            h->hash = 1;
            ngx_str_set(&h->key, "X-Upstream-Addr");
            h->value.data = ngx_pnalloc(r->pool, block->addr.len + 1);
            if (h->value.data == NULL) {
                ngx_spinlock_unlock(lock);
                return NGX_ERROR;
            }
            ngx_memcpy(h->value.data, block->addr.data, block->addr.len);
            h->value.data[block->addr.len] = '\0';
            h->value.len = block->addr.len;

            // Добавляем заголовок X-Upstream-Port
            h = ngx_list_push(&r->headers_out.headers);
            if (h == NULL) {
                ngx_spinlock_unlock(lock);
                return NGX_ERROR;
            }
            h->hash = 1;
            ngx_str_set(&h->key, "X-Upstream-Port");
            h->value.data = ngx_pnalloc(r->pool, NGX_INT_T_LEN);
            if (h->value.data == NULL) {
                ngx_spinlock_unlock(lock);
                return NGX_ERROR;
            }
            sin = (struct sockaddr_in *) r->upstream->peer.sockaddr;
            h->value.len = ngx_sprintf(h->value.data, "%d", ntohs(sin->sin_port)) - h->value.data;

            // Добавляем заголовок X-Upstream-nreq
            h = ngx_list_push(&r->headers_out.headers);
            if (h == NULL) {
                ngx_spinlock_unlock(lock);
                return NGX_ERROR;
            }
            h->hash = 1;
            ngx_str_set(&h->key, "X-Upstream-nreq");
            h->value.data = ngx_pnalloc(r->pool, NGX_INT_T_LEN);
            if (h->value.data == NULL) {
                ngx_spinlock_unlock(lock);
                return NGX_ERROR;
            }
            h->value.len = ngx_sprintf(h->value.data, "%ui", block->nreq) - h->value.data;

            // Добавляем заголовок X-Upstream-nres
            h = ngx_list_push(&r->headers_out.headers);
            if (h == NULL) {
                ngx_spinlock_unlock(lock);
                return NGX_ERROR;
            }
            h->hash = 1;
            ngx_str_set(&h->key, "X-Upstream-nres");
            h->value.data = ngx_pnalloc(r->pool, NGX_INT_T_LEN);
            if (h->value.data == NULL) {
                ngx_spinlock_unlock(lock);
                return NGX_ERROR;
            }
            h->value.len = ngx_sprintf(h->value.data, "%ui", block->nres) - h->value.data;
            ngx_spinlock_unlock(lock);
        }
    }

    return ngx_http_next_header_filter(r);
}

static ngx_int_t
ngx_http_sct_neuro_filter_init(ngx_conf_t *cf)
{

    ngx_http_next_header_filter = ngx_http_top_header_filter;
    ngx_http_top_header_filter = ngx_http_sct_neuro_header_filter;

    return NGX_OK;
}
