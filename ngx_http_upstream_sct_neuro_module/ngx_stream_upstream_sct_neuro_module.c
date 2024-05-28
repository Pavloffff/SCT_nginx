/*
 * Copyright (C) Ivan Pavlov
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_stream.h>

typedef struct {
    ngx_str_t                                addr;
    uintptr_t                                peers;
    ngx_atomic_t                             lock;                                  // unsigned long
    ngx_uint_t                               nreq;
    ngx_uint_t                               nres;
    ngx_uint_t                               fails;
} ngx_stream_upstream_sct_neuro_shm_block_t;

static ngx_int_t ngx_stream_upstream_init_sct_neuro_peer(
    ngx_stream_session_t *s, ngx_stream_upstream_srv_conf_t *us);
static ngx_int_t ngx_stream_upstream_get_sct_neuro_peer(
    ngx_peer_connection_t *pc, void *data);
static char *ngx_stream_upstream_sct_neuro(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);

static char *ngx_stream_upstream_sct_neuro_set_shm_size(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);   
static char *ngx_stream_upstream_sct_neuro_set_gap_in_requests(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);   

// ngx_uint_t                              nreq_since_last_weight_update = 0;

static ngx_command_t  ngx_stream_upstream_sct_neuro_commands[] = {

    { ngx_string("sct_neuro"),
      NGX_STREAM_UPS_CONF|NGX_CONF_NOARGS,
      ngx_stream_upstream_sct_neuro,
      0,
      0,
      NULL },
    
    { ngx_string("upstream_sct_neuro_shm_size"),
      NGX_STREAM_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_stream_upstream_sct_neuro_set_shm_size,
      0,
      0,
      NULL },

    { ngx_string("upstream_sct_neuro_gap_in_requests"),
      NGX_STREAM_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_stream_upstream_sct_neuro_set_gap_in_requests,
      0,
      0,
      NULL },

      ngx_null_command
};

static ngx_uint_t       ngx_stream_upstream_sct_neuro_gap_in_requests; 

static ngx_uint_t       ngx_stream_upstream_sct_neuro_shm_size;
static ngx_shm_zone_t  *ngx_stream_upstream_sct_neuro_shm_zone;

static ngx_stream_module_t  ngx_stream_upstream_sct_neuro_module_ctx = {
    NULL,                                    /* preconfiguration */
    NULL,                                    /* postconfiguration */

    NULL,                                    /* create main configuration */
    NULL,                                    /* init main configuration */

    NULL,                                    /* create server configuration */
    NULL                                     /* merge server configuration */
};

static ngx_int_t
ngx_stream_upstream_sct_neuro_init_shm_zone(ngx_shm_zone_t *shm_zone, void *data)
{
    ngx_slab_pool_t *shpool;
    ngx_stream_upstream_sct_neuro_shm_block_t *blocks;
    ngx_uint_t i, j, num_blocks;
    ngx_stream_upstream_srv_conf_t *uscf = shm_zone->data;
    
    if (data) {
        shm_zone->data = data;
        return NGX_OK;
    }

    shpool = (ngx_slab_pool_t *) shm_zone->shm.addr;

    num_blocks = 0;
    ngx_stream_upstream_server_t *server = uscf->servers->elts;
    for (i = 0; i < uscf->servers->nelts; i++) {
        num_blocks += server[i].naddrs;
    }

    blocks = ngx_slab_alloc(shpool, sizeof(ngx_stream_upstream_sct_neuro_shm_block_t) * num_blocks);
    if (blocks == NULL) {
        return NGX_ERROR;
    }

    ngx_uint_t block_index = 0;
    for (i = 0; i < uscf->servers->nelts; i++) {
        for (j = 0; j < server[i].naddrs; j++) {
            ngx_memzero(&blocks[block_index], sizeof(ngx_stream_upstream_sct_neuro_shm_block_t));
            blocks[block_index].addr = server[i].addrs[j].name;
            block_index++;
        }
    }

    shm_zone->data = blocks;

    return NGX_OK;
}

static char *
ngx_stream_upstream_sct_neuro_set_shm_size(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
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

    if (ngx_stream_upstream_sct_neuro_shm_size &&
        ngx_stream_upstream_sct_neuro_shm_size != (ngx_uint_t) new_shm_size) {
        ngx_conf_log_error(NGX_LOG_WARN, cf, 0, "Cannot change memory area size without restart, ignoring change");
    } else {
        ngx_stream_upstream_sct_neuro_shm_size = new_shm_size;                                 
    }

    ngx_conf_log_error(NGX_LOG_DEBUG, cf, 0, "Using %udKiB of shared memory for upstream_sct_neuro", new_shm_size >> 10);

    return NGX_CONF_OK;
}

static char *
ngx_stream_upstream_sct_neuro_set_gap_in_requests(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ssize_t new_gap_in_requests;
    ngx_str_t *value;

    value = cf->args->elts;
    
    new_gap_in_requests = ngx_atoi(value[1].data, value[1].len);
    if (new_gap_in_requests == NGX_ERROR || new_gap_in_requests < 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "Invalid gap in requests `%V'", &value[1]);
        return NGX_CONF_ERROR;
    }

    ngx_stream_upstream_sct_neuro_gap_in_requests = new_gap_in_requests;

    return NGX_CONF_OK;
}

ngx_module_t  ngx_stream_upstream_sct_neuro_module = {
    NGX_MODULE_V1,
    &ngx_stream_upstream_sct_neuro_module_ctx,  /* module context */
    ngx_stream_upstream_sct_neuro_commands,     /* module directives */
    NGX_STREAM_MODULE,                          /* module type */
    NULL,                                       /* init master */
    NULL,                                       /* init module */
    NULL,                                       /* init process */
    NULL,                                       /* init thread */
    NULL,                                       /* exit thread */
    NULL,                                       /* exit process */
    NULL,                                       /* exit master */
    NGX_MODULE_V1_PADDING
};

static ngx_int_t
ngx_stream_upstream_init_sct_neuro(ngx_conf_t *cf,
    ngx_stream_upstream_srv_conf_t *us)
{
    ngx_str_t shm_name = ngx_string("sct_neuro_stream");
    ngx_log_debug0(NGX_LOG_DEBUG_STREAM, cf->log, 0,
                   "init sct neuro");

    if (ngx_stream_upstream_init_round_robin(cf, us) != NGX_OK) {
        return NGX_ERROR;
    }

    us->peer.init = ngx_stream_upstream_init_sct_neuro_peer;

    ngx_shm_zone_t *shm_zone = ngx_shared_memory_add(cf, &shm_name, 
        ngx_stream_upstream_sct_neuro_shm_size, &ngx_stream_upstream_sct_neuro_module);
    if (shm_zone == NULL) {
        return NGX_ERROR;
    }

    shm_zone->data = us;
    shm_zone->init = ngx_stream_upstream_sct_neuro_init_shm_zone;
    ngx_stream_upstream_sct_neuro_shm_zone = shm_zone;

    return NGX_OK;
}

static ngx_int_t
ngx_stream_upstream_init_sct_neuro_peer(ngx_stream_session_t *s,
    ngx_stream_upstream_srv_conf_t *us)
{
    ngx_log_debug0(NGX_LOG_DEBUG_STREAM, s->connection->log, 0,
                   "init sct neuro peer");

    if (ngx_stream_upstream_init_round_robin_peer(s, us) != NGX_OK) {
        return NGX_ERROR;
    }

    s->upstream->peer.get = ngx_stream_upstream_get_sct_neuro_peer;

    return NGX_OK;
}

static ngx_int_t
ngx_stream_upstream_get_sct_neuro_peer(ngx_peer_connection_t *pc, void *data)
{
    ngx_stream_upstream_rr_peer_data_t *rrp = data;

    return ngx_stream_upstream_get_round_robin_peer(pc, rrp);
}

static char *
ngx_stream_upstream_sct_neuro(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_stream_upstream_srv_conf_t  *uscf;

    uscf = ngx_stream_conf_get_module_srv_conf(cf, ngx_stream_upstream_module);

    if (uscf->peer.init_upstream) {
        ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                           "load balancing method redefined");
    }

    uscf->peer.init_upstream = ngx_stream_upstream_init_sct_neuro;

    uscf->flags = NGX_STREAM_UPSTREAM_CREATE
                  |NGX_STREAM_UPSTREAM_MAX_CONNS
                  |NGX_STREAM_UPSTREAM_MAX_FAILS
                  |NGX_STREAM_UPSTREAM_FAIL_TIMEOUT
                  |NGX_STREAM_UPSTREAM_DOWN;

    return NGX_CONF_OK;
}


typedef struct {
    ngx_chain_t  *from_upstream;
    ngx_chain_t  *from_downstream;
} ngx_stream_sct_neuro_filter_ctx_t;

static ngx_int_t ngx_stream_sct_neuro_filter(ngx_stream_session_t *s,
    ngx_chain_t *in, ngx_uint_t from_upstream);
static ngx_int_t ngx_stream_sct_neuro_filter_init(ngx_conf_t *cf);

static ngx_stream_module_t  ngx_stream_sct_neuro_filter_module_ctx = {
    NULL,                                     /* preconfiguration */
    ngx_stream_sct_neuro_filter_init,         /* postconfiguration */

    NULL,                                     /* create main configuration */
    NULL,                                     /* init main configuration */

    NULL,                                     /* create server configuration */
    NULL                                      /* merge server configuration */
};

ngx_module_t  ngx_stream_sct_neuro_filter_module = {
    NGX_MODULE_V1,
    &ngx_stream_sct_neuro_filter_module_ctx,  /* module context */
    NULL,                                     /* module directives */
    NGX_STREAM_MODULE,                        /* module type */
    NULL,                                     /* init master */
    NULL,                                     /* init module */
    NULL,                                     /* init process */
    NULL,                                     /* init thread */
    NULL,                                     /* exit thread */
    NULL,                                     /* exit process */
    NULL,                                     /* exit master */
    NGX_MODULE_V1_PADDING
};

static ngx_int_t
ngx_stream_sct_neuro_filter(ngx_stream_session_t *s, ngx_chain_t *in,
    ngx_uint_t from_upstream)
{
    off_t                           size;
    ngx_uint_t                      last, flush, sync;
    ngx_chain_t                    *cl, *ln, **ll, **out, *chain;
    ngx_connection_t               *c;
    ngx_stream_sct_neuro_filter_ctx_t  *ctx;
    // ngx_int_t                      rc;
    // ngx_chain_t                   *cl;
    // ngx_connection_t              *c;
    // ngx_stream_sct_neuro_filter_ctx_t *ctx;


    ctx = ngx_stream_get_module_ctx(s, ngx_stream_sct_neuro_filter_module);

    if (ctx == NULL) {
        ctx = ngx_pcalloc(s->connection->pool,
                          sizeof(ngx_stream_sct_neuro_filter_ctx_t));
        if (ctx == NULL) {
            return NGX_ERROR;
        }

        ngx_stream_set_ctx(s, ctx, ngx_stream_sct_neuro_filter_module);
    }

    if (from_upstream) {
        c = s->connection;
        out = &ctx->from_upstream;

    } else {
        c = s->upstream->peer.connection;
        out = &ctx->from_downstream;

        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, c->log, 0,
                        "upstream addr: %s",
                        s->upstream->peer.name->data);
    }

    if (c->error) {
        return NGX_ERROR;
    }

    // ngx_log_debug1(NGX_LOG_DEBUG_EVENT, c->log, 0,
    //                    "upstream addr: %s",
    //                    s->upstream->upstream->host.data);

    size = 0;
    flush = 0;
    sync = 0;
    last = 0;
    ll = out;

    /* find the size, the flush point and the last link of the saved chain */

    for (cl = *out; cl; cl = cl->next) {
        ll = &cl->next;

        ngx_log_debug7(NGX_LOG_DEBUG_EVENT, c->log, 0,
                       "sct_neuro old buf t:%d f:%d %p, pos %p, size: %z "
                       "file: %O, size: %O",
                       cl->buf->temporary, cl->buf->in_file,
                       cl->buf->start, cl->buf->pos,
                       cl->buf->last - cl->buf->pos,
                       cl->buf->file_pos,
                       cl->buf->file_last - cl->buf->file_pos);

        if (ngx_buf_size(cl->buf) == 0 && !ngx_buf_special(cl->buf)) {
            ngx_log_error(NGX_LOG_ALERT, c->log, 0,
                          "zero size buf in writer "
                          "t:%d r:%d f:%d %p %p-%p %p %O-%O",
                          cl->buf->temporary,
                          cl->buf->recycled,
                          cl->buf->in_file,
                          cl->buf->start,
                          cl->buf->pos,
                          cl->buf->last,
                          cl->buf->file,
                          cl->buf->file_pos,
                          cl->buf->file_last);

            ngx_debug_point();
            return NGX_ERROR;
        }

        if (ngx_buf_size(cl->buf) < 0) {
            ngx_log_error(NGX_LOG_ALERT, c->log, 0,
                          "negative size buf in writer "
                          "t:%d r:%d f:%d %p %p-%p %p %O-%O",
                          cl->buf->temporary,
                          cl->buf->recycled,
                          cl->buf->in_file,
                          cl->buf->start,
                          cl->buf->pos,
                          cl->buf->last,
                          cl->buf->file,
                          cl->buf->file_pos,
                          cl->buf->file_last);

            ngx_debug_point();
            return NGX_ERROR;
        }

        size += ngx_buf_size(cl->buf);

        if (cl->buf->flush || cl->buf->recycled) {
            flush = 1;
        }

        if (cl->buf->sync) {
            sync = 1;
        }

        if (cl->buf->last_buf) {
            last = 1;
        }
    }

    /* add the new chain to the existent one */

    for (ln = in; ln; ln = ln->next) {
        cl = ngx_alloc_chain_link(c->pool);
        if (cl == NULL) {
            return NGX_ERROR;
        }

        cl->buf = ln->buf;
        *ll = cl;
        ll = &cl->next;

        ngx_log_debug7(NGX_LOG_DEBUG_EVENT, c->log, 0,
                       "write new buf t:%d f:%d %p, pos %p, size: %z "
                       "file: %O, size: %O",
                       cl->buf->temporary, cl->buf->in_file,
                       cl->buf->start, cl->buf->pos,
                       cl->buf->last - cl->buf->pos,
                       cl->buf->file_pos,
                       cl->buf->file_last - cl->buf->file_pos);

        if (ngx_buf_size(cl->buf) == 0 && !ngx_buf_special(cl->buf)) {
            ngx_log_error(NGX_LOG_ALERT, c->log, 0,
                          "zero size buf in writer "
                          "t:%d r:%d f:%d %p %p-%p %p %O-%O",
                          cl->buf->temporary,
                          cl->buf->recycled,
                          cl->buf->in_file,
                          cl->buf->start,
                          cl->buf->pos,
                          cl->buf->last,
                          cl->buf->file,
                          cl->buf->file_pos,
                          cl->buf->file_last);

            ngx_debug_point();
            return NGX_ERROR;
        }

        if (ngx_buf_size(cl->buf) < 0) {
            ngx_log_error(NGX_LOG_ALERT, c->log, 0,
                          "negative size buf in writer "
                          "t:%d r:%d f:%d %p %p-%p %p %O-%O",
                          cl->buf->temporary,
                          cl->buf->recycled,
                          cl->buf->in_file,
                          cl->buf->start,
                          cl->buf->pos,
                          cl->buf->last,
                          cl->buf->file,
                          cl->buf->file_pos,
                          cl->buf->file_last);

            ngx_debug_point();
            return NGX_ERROR;
        }

        size += ngx_buf_size(cl->buf);

        if (cl->buf->flush || cl->buf->recycled) {
            flush = 1;
        }

        if (cl->buf->sync) {
            sync = 1;
        }

        if (cl->buf->last_buf) {
            last = 1;
        }
    }

    *ll = NULL;

    ngx_log_debug3(NGX_LOG_DEBUG_STREAM, c->log, 0,
                   "stream sct_neuro filter: l:%ui f:%ui s:%O", last, flush, size);

    if (size == 0
        && !(c->buffered & NGX_LOWLEVEL_BUFFERED)
        && !(last && c->need_last_buf)
        && !(flush && c->need_flush_buf))
    {
        if (last || flush || sync) {
            for (cl = *out; cl; /* void */) {
                ln = cl;
                cl = cl->next;
                ngx_free_chain(c->pool, ln);
            }

            *out = NULL;
            c->buffered &= ~NGX_STREAM_WRITE_BUFFERED;

            return NGX_OK;
        }

        ngx_log_error(NGX_LOG_ALERT, c->log, 0,
                      "the stream output chain is empty");

        ngx_debug_point();

        return NGX_ERROR;
    }

    chain = c->send_chain(c, *out, 0);

    ngx_log_debug1(NGX_LOG_DEBUG_STREAM, c->log, 0,
                   "stream sct_neuro filter %p", chain);

    if (chain == NGX_CHAIN_ERROR) {
        c->error = 1;
        return NGX_ERROR;
    }

    for (cl = *out; cl && cl != chain; /* void */) {
        ln = cl;
        cl = cl->next;
        ngx_free_chain(c->pool, ln);
    }

    *out = chain;

    if (chain) {
        if (c->shared) {
            ngx_log_error(NGX_LOG_ALERT, c->log, 0,
                          "shared connection is busy");
            return NGX_ERROR;
        }

        c->buffered |= NGX_STREAM_WRITE_BUFFERED;
        return NGX_AGAIN;
    }

    c->buffered &= ~NGX_STREAM_WRITE_BUFFERED;

    if (c->buffered & NGX_LOWLEVEL_BUFFERED) {
        return NGX_AGAIN;
    }

    return NGX_OK;
}

static ngx_int_t
ngx_stream_sct_neuro_filter_init(ngx_conf_t *cf)
{
    ngx_stream_top_filter = ngx_stream_sct_neuro_filter;
    return NGX_OK;
}