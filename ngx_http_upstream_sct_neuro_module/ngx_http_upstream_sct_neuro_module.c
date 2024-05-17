/*
 * Copyright (C) Ivan Pavlov
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


static ngx_int_t ngx_http_sct_neuro_header_filter(ngx_http_request_t *r);
static ngx_int_t ngx_http_sct_neuro_filter_init(ngx_conf_t *cf);


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

static ngx_int_t
ngx_http_upstream_init_sct_neuro(ngx_conf_t *cf,
    ngx_http_upstream_srv_conf_t *us)
{
    // ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0,
    //                "init sct neuro");

    // ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0,
    //                "info: %s", cf->args->elts);

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


static ngx_int_t
ngx_http_sct_neuro_header_filter(ngx_http_request_t *r)
{
    ngx_table_elt_t  *h;
    struct sockaddr_in  *sin;

    if (r->headers_out.status != NGX_HTTP_OK) {
        return ngx_http_next_header_filter(r);
    }

    // Upstream server address
    if (r->upstream && r->upstream->peer.name) {
        h = ngx_list_push(&r->headers_out.headers);
        if (h == NULL) {
            return NGX_ERROR;
        }
        h->hash = 1;
        ngx_str_set(&h->key, "X-Upstream-Addr");
        h->value.data = ngx_pnalloc(r->pool, r->upstream->peer.name->len + 1);
        if (h->value.data == NULL) {
            return NGX_ERROR;
        }
        ngx_memcpy(h->value.data, r->upstream->peer.name->data, r->upstream->peer.name->len);
        h->value.data[r->upstream->peer.name->len] = '\0';
        h->value.len = r->upstream->peer.name->len;

        // Upstream server port
        h = ngx_list_push(&r->headers_out.headers);
        if (h == NULL) {
            return NGX_ERROR;
        }
        h->hash = 1;
        ngx_str_set(&h->key, "X-Upstream-Port");
        h->value.data = ngx_pnalloc(r->pool, NGX_INT_T_LEN);
        if (h->value.data == NULL) {
            return NGX_ERROR;
        }
        sin = (struct sockaddr_in *) r->upstream->peer.sockaddr;
        h->value.len = ngx_sprintf(h->value.data, "%d", ntohs(sin->sin_port)) - h->value.data;
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
