/*
 * Copyright (C) Ivan Pavlov
 * Copyright (C) Fedor Merkulov
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

//     ngx_uint_t                      tries;

//     unsigned                        single:1;

//     ngx_str_t                      *name;

//     ngx_http_upstream_sct_neuro_peer_t    *peer;
// };

// typedef struct {
//     ngx_uint_t                              config;
//     ngx_http_upstream_sct_neuro_peers_t    *peers;
//     ngx_http_upstream_sct_neuro_peer_t     *current;
//     uintptr_t                              *tried;
//     uintptr_t                               data;

//     ngx_uint_t                              nreq_since_last_weight_update;          // количество запросов с последнего пересчёта весов
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
#define ngx_http_upstream_tries(p) ((p)->tries)

static ngx_int_t ngx_http_sct_neuro_header_filter(ngx_http_request_t *r);
static ngx_int_t ngx_http_sct_neuro_filter_init(ngx_conf_t *cf);


static ngx_int_t ngx_http_upstream_init_sct_neuro_peer(ngx_http_request_t *r,
    ngx_http_upstream_srv_conf_t *us);
// static ngx_http_upstream_sct_neuro_peer_t *
// ngx_http_upstream_get_peer_from_neuro(ngx_http_upstream_sct_neuro_peer_data_t *scp);
static ngx_int_t ngx_http_upstream_get_sct_neuro_peer(
    ngx_peer_connection_t *pc, void *data);
// static void ngx_http_upstream_free_sct_neuro_peer(ngx_peer_connection_t *pc, void *data,
//     ngx_uint_t state);
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
    // ngx_uint_t num_blocks = 0;
    // ngx_http_upstream_server_t *server;

    if (ngx_http_upstream_init_round_robin(cf, us) != NGX_OK) {
        return NGX_ERROR;
    }
    // ngx_url_t                      u;
    // ngx_uint_t                     i, j, n, w, t;
    // ngx_http_upstream_sct_neuro_peer_t   *peer, **peerp;
    // ngx_http_upstream_sct_neuro_peers_t  *peers;

    // us->peer.init = ngx_http_upstream_init_sct_neuro_peer;            // запоминаем в upstream метод инициализации запроса

    // if (us->servers) {                                                  // сервера из upstream (если есть)
    //     server = us->servers->elts;                                     // первый сервер

    //     n = 0;                                                          // кол-во адресов
    //     w = 0;                                                          // кол-во адресов с учетом веса
    //     t = 0;                                                          // кол-во адресов с учетом работоспособности сервера

    //     for (i = 0; i < us->servers->nelts; i++) {                      // идем по списку серверов

    //         n += server[i].naddrs;
    //         w += server[i].naddrs * server[i].weight;

    //         if (!server[i].down) {
    //             t += server[i].naddrs;
    //         }
    //     }

    //     if (n == 0) {
    //         ngx_log_error(NGX_LOG_EMERG, cf->log, 0,                    // пример лога! логаем на файлик определенный в ngx_conf_t (тип ngx_log_t)
    //                       "no servers in upstream \"%V\" in %s:%ui",    // типа ngx_log_error(NGX_LOG_ALERT, log, err, "kill(%P, %d) failed", pid, signo);
    //                       &us->host, us->file_name, us->line);
    //         return NGX_ERROR;
    //     }

    //     peers = ngx_pcalloc(cf->pool, sizeof(ngx_http_upstream_sct_neuro_peers_t));    // выделяем память для структуры, заполняем нулями
    //     if (peers == NULL) {
    //         return NGX_ERROR;
    //     }

    //     peer = ngx_pcalloc(cf->pool, sizeof(ngx_http_upstream_sct_neuro_peer_t) * n);  // peer — объект, содержащий общие методы для инициализации конфигурации upstream
    //     if (peer == NULL) {
    //         return NGX_ERROR;
    //     }
    //                                                                     // заполняем конфигу кластера адресов
    //     peers->single = (n == 1);                                       // если кол-во адресов 1
    //     peers->number = n;                                              // всего адресов
    //     peers->tries = t;                                               // всего живых адресов
    //     peers->name = &us->host;                                        // имя кластера (название upstream из конфиги)

    //     n = 0;
    //     peerp = &peers->peer;

    //     for (i = 0; i < us->servers->nelts; i++) {                      // идем по адресам серверам внутри upstream
    //         if (server[i].backup) {                                     // запасные сервера скипаем
    //             continue;
    //         }

    //         for (j = 0; j < server[i].naddrs; j++) {                    // идем по адресам сервера
    //             peer[n].sockaddr = server[i].addrs[j].sockaddr;         // и всю дату копируем в кластер
    //             peer[n].socklen = server[i].addrs[j].socklen;
    //             peer[n].name = server[i].addrs[j].name;
    //             peer[n].max_conns = server[i].max_conns;
    //             peer[n].max_fails = server[i].max_fails;
    //             peer[n].fail_timeout = server[i].fail_timeout;
    //             peer[n].down = server[i].down;
    //             peer[n].server = server[i].name;

    //             peer[n].cnt_requests = 0;
    //             peer[n].cnt_responses = 0;
    //             peer[n].neuro_weight = 0.0;

    //             *peerp = &peer[n];                                      // переходим к следующему блоку в кластере
    //             peerp = &peer[n].next;
    //             n++;                                                    // n в конце станет кол-во скопированных адресов
    //         }
    //     }

    //     us->peer.data = peers;                                          // присваиваем в upstream итоговый список адресов

    // } else {

    //     // иначе у нас upstream - и есть проксируемый сервер
    //     /* an upstream implicitly defined by proxy_pass, etc. */

    //     if (us->port == 0) {
    //         ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
    //                     "no port in upstream \"%V\" in %s:%ui",
    //                     &us->host, us->file_name, us->line);
    //         return NGX_ERROR;
    //     }

    //     ngx_memzero(&u, sizeof(ngx_url_t));                                         // храним url из upstream

    //     u.host = us->host;                                                          
    //     u.port = us->port;

    //     if (ngx_inet_resolve_host(cf->pool, &u) != NGX_OK) {                        // преобразование имени хоста в IP-адрес
    //         if (u.err) {
    //             ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
    //                         "%s in upstream \"%V\" in %s:%ui",
    //                         u.err, &us->host, us->file_name, us->line);
    //         }

    //         return NGX_ERROR;
    //     }

    //     n = u.naddrs;                                                               // кол-во адресов для данного хоста

    //     peers = ngx_pcalloc(cf->pool, sizeof(ngx_http_upstream_sct_neuro_peers_t));        // далее то же самое
    //     if (peers == NULL) {
    //         return NGX_ERROR;
    //     }

    //     peer = ngx_pcalloc(cf->pool, sizeof(ngx_http_upstream_sct_neuro_peer_t) * n);
    //     if (peer == NULL) {
    //         return NGX_ERROR;
    //     }

    //     peers->single = (n == 1);
    //     peers->number = n;
    //     peers->tries = n;
    //     peers->name = &us->host;

    //     for (i = 0; i < u.naddrs; i++) {
    //         peer[i].sockaddr = u.addrs[i].sockaddr;
    //         peer[i].socklen = u.addrs[i].socklen;
    //         peer[i].name = u.addrs[i].name;
    //         peer[i].max_conns = 0;
    //         peer[i].max_fails = 1;
    //         peer[i].fail_timeout = 10;

    //         peer[i].cnt_requests = 0;
    //         peer[i].cnt_responses = 0;
    //         peer[i].neuro_weight = 0.0; 
    //     }

    //     us->peer.data = peers;
        
    //     /* implicitly defined upstream has no backup servers */
    // }

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
//     ngx_uint_t                         n;
//     ngx_http_upstream_sct_neuro_peer_data_t  *scp;                                     

//     scp = r->upstream->peer.data;                                               // данные из запроса

//     if (scp == NULL) {
//         scp = ngx_palloc(r->pool, sizeof(ngx_http_upstream_sct_neuro_peer_data_t));
//         if (scp == NULL) {
//             return NGX_ERROR;
//         }

//         r->upstream->peer.data = scp;                                           // если данных нет, создаем пустую дату из пула
//     }

//     scp->peers = us->peer.data;                                                 // те самые данные, что получили в прошлом методе
//     scp->current = NULL;
//     scp->config = 0;

//     scp->nreq_since_last_weight_update = 0;
//     scp->gap_in_requests = 5; //TODO: переменную сделать

//     n = scp->peers->number;                                                     // кол-во подключений

//     /*
//         tried отслеживает, какие серверы были уже испробованы. 
//         Если количество серверов меньше или равно количеству битов в uintptr_t, 
//         достаточно одного такого значения для отслеживания. 
//         Если серверов больше, требуется массив для их отслеживания.                                                                            
//     */

//     if (n <= 8 * sizeof(uintptr_t)) {
//         scp->tried = &scp->data;
//         scp->data = 0;
//     } else {
//         n = (n + (8 * sizeof(uintptr_t) - 1)) / (8 * sizeof(uintptr_t));

//         scp->tried = ngx_pcalloc(r->pool, n * sizeof(uintptr_t));
//         if (scp->tried == NULL) {
//             return NGX_ERROR;
//         }
//     }

//     r->upstream->peer.get = ngx_http_upstream_get_sct_neuro_peer;           // Устанавливаем методы для обработки
//     r->upstream->peer.free = ngx_http_upstream_free_sct_neuro_peer;
//     r->upstream->peer.tries = ngx_http_upstream_tries(scp->peers);
// #if (NGX_HTTP_SSL)
//     r->upstream->peer.set_session =
//                                ngx_http_upstream_set_round_robin_peer_session;
//     r->upstream->peer.save_session =
//                                ngx_http_upstream_save_round_robin_peer_session;
// #endif

    r->upstream->peer.get = ngx_http_upstream_get_sct_neuro_peer;

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_get_sct_neuro_peer(ngx_peer_connection_t *pc, void *data)
{
    // ngx_uint_t                    i;
    // ngx_http_upstream_rr_peer_t  *peer;
    ngx_http_upstream_rr_peer_data_t  *scp = data;
    // ngx_http_upstream_sct_neuro_peer_data_t  *scp = data;
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
    


/*Говно начинается*/
    return ngx_http_upstream_get_round_robin_peer(pc, scp);
//     ngx_http_upstream_sct_neuro_peer_t   *peer;
//     ngx_http_upstream_sct_neuro_peers_t  *peers;

//     ngx_log_debug1(NGX_LOG_DEBUG_HTTP, pc->log, 0,
//                    "get scp peer, try: %ui", pc->tries);

//     pc->cached = 0;
//     pc->connection = NULL;

//     peers = scp->peers;
//     ngx_http_upstream_rr_peers_wlock(peers);

//     if (peers->single) { 
//         peer = peers->peer;
//         scp->current = peer;
//     } else {

//         /* there are several peers */

//         // peer = ngx_http_upstream_get_peer_from_neuro(scp);                                 // иначе выбираем из спика согласно алгоритму round robin 
//         peer = peers->peer;
//     }

//     pc->sockaddr = peer->sockaddr;
//     pc->socklen = peer->socklen;
//     pc->name = &peer->name;

//     peer->conns++;

//     ngx_http_upstream_rr_peers_unlock(peers);

//     return NGX_OK;
// /*Говно заканчивается*/
}

// static ngx_http_upstream_sct_neuro_peer_t *
// ngx_http_upstream_get_peer_from_neuro(ngx_http_upstream_sct_neuro_peer_data_t *scp)               // выбра пира из списка
// {
//     // time_t                        now;
//     // uintptr_t                     m;
//     //ngx_int_t                     total;
//     ngx_uint_t                    i;//, n;
//     ngx_http_upstream_sct_neuro_peer_t  *peer; //*best;
//     // ngx_http_upstream_sct_neuro_peers_t *peers = scp->peers;


//     //if (scp->nreq_since_last_weight_update >= scp->gap_in_requests){
//         for (peer = scp->peers->peer, i = 0; peer; peer = peer->next, i++)
//         {     
//             return peer;                                                                      // смотрим с какимии узлами уже была попытка связи
//             // n = i / (8 * sizeof(uintptr_t));                                       // индекс элемента в rrp->tried
            
//             // m = (uintptr_t) (1 << i % (8 * sizeof(uintptr_t)));                       // битовая маска (определяет была ли попытка)

//             // if (peer->down) {                                                       // если узел мертвый
//             //     continue;
//             // }

//             // if (peer->max_fails                                                     // достиг ли узел максимального количества неудач 
//             //     && peer->fails >= peer->max_fails                                   // (max_fails) в течение определенного времени (fail_timeout)
//             //     && now - peer->checked <= peer->fail_timeout)
//             // {
//             //     continue;
//             // }

//             // if (peer->max_conns && peer->conns >= peer->max_conns) {                // проверка на максимум подключений
//             //     continue;
//             // }
//         }                       
//         scp->nreq_since_last_weight_update = 0;
//     //}
//     scp->nreq_since_last_weight_update++;
//     //return best;
//     return peer;
// }

// static void
// ngx_http_upstream_free_sct_neuro_peer(ngx_peer_connection_t *pc, void *data,  // очистка памяти узла
//     ngx_uint_t state)                                                           // нигде не вызывается
// {
//     ngx_http_upstream_rr_peer_data_t  *scp = data;

//     time_t                       now;
//     ngx_http_upstream_rr_peer_t  *peer;

//     ngx_log_debug2(NGX_LOG_DEBUG_HTTP, pc->log, 0,
//                    "free scp peer %ui %ui", pc->tries, state);

//     /* TODO: NGX_PEER_KEEPALIVE */

//     peer = scp->current;

//     ngx_http_upstream_rr_peers_rlock(scp->peers);
//     ngx_http_upstream_rr_peer_lock(scp->peers, peer);

//     if (scp->peers->single) {

//         peer->conns--;

//         ngx_http_upstream_rr_peer_unlock(scp->peers, peer);
//         ngx_http_upstream_rr_peers_unlock(scp->peers);

//         pc->tries = 0;
//         return;
//     }

//     if (state & NGX_PEER_FAILED) {
//         now = ngx_time();

//         peer->fails++;
//         peer->accessed = now;
//         peer->checked = now;

//         if (peer->max_fails) {
//             if (peer->fails >= peer->max_fails) {
//                 ngx_log_error(NGX_LOG_WARN, pc->log, 0,
//                               "upstream server temporarily disabled");
//             }
//         }
//     } else {

//         /* mark peer live if check passed */

//         if (peer->accessed < peer->checked) {
//             peer->fails = 0;
//         }
//     }

//     peer->conns--;

//     ngx_http_upstream_rr_peer_unlock(scp->peers, peer);
//     ngx_http_upstream_rr_peers_unlock(scp->peers);

//     if (pc->tries) {
//         pc->tries--;
//     }
// }


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

static ngx_int_t ngx_http_sct_neuro_header_filter(ngx_http_request_t *r) {
    ngx_table_elt_t  *h;
    struct sockaddr_in  *sin;
    ngx_http_upstream_sct_neuro_shm_block_t *blocks;
    ngx_http_upstream_sct_neuro_shm_block_t *block = NULL;
    // ngx_slab_pool_t *shpool;
    ngx_uint_t i, num_blocks;
    // ngx_atomic_t *lock;

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
            // lock = &block->lock;
            // ngx_spinlock(lock, ngx_pid, 1024);
            
            h = ngx_list_push(&r->headers_out.headers);
            if (h == NULL) {
                // ngx_spinlock_unlock(lock);
                return NGX_ERROR;
            }
            h->hash = 1;
            ngx_str_set(&h->key, "X-Upstream-Addr");
            h->value.data = ngx_pnalloc(r->pool, block->addr.len + 1);
            if (h->value.data == NULL) {
                // ngx_spinlock_unlock(lock);
                return NGX_ERROR;
            }
            ngx_memcpy(h->value.data, block->addr.data, block->addr.len);
            h->value.data[block->addr.len] = '\0';
            h->value.len = block->addr.len;

            // Добавляем заголовок X-Upstream-Port
            h = ngx_list_push(&r->headers_out.headers);
            if (h == NULL) {
                // ngx_spinlock_unlock(lock);
                return NGX_ERROR;
            }
            h->hash = 1;
            ngx_str_set(&h->key, "X-Upstream-Port");
            h->value.data = ngx_pnalloc(r->pool, NGX_INT_T_LEN);
            if (h->value.data == NULL) {
                // ngx_spinlock_unlock(lock);
                return NGX_ERROR;
            }
            sin = (struct sockaddr_in *) r->upstream->peer.sockaddr;
            h->value.len = ngx_sprintf(h->value.data, "%d", ntohs(sin->sin_port)) - h->value.data;

            // Добавляем заголовок X-Upstream-nreq
            h = ngx_list_push(&r->headers_out.headers);
            if (h == NULL) {
                // ngx_spinlock_unlock(lock);
                return NGX_ERROR;
            }
            h->hash = 1;
            ngx_str_set(&h->key, "X-Upstream-nreq");
            h->value.data = ngx_pnalloc(r->pool, NGX_INT_T_LEN);
            if (h->value.data == NULL) {
                // ngx_spinlock_unlock(lock);
                return NGX_ERROR;
            }
            h->value.len = ngx_sprintf(h->value.data, "%ui", block->nreq) - h->value.data;

            // Добавляем заголовок X-Upstream-nres
            h = ngx_list_push(&r->headers_out.headers);
            if (h == NULL) {
                // ngx_spinlock_unlock(lock);
                return NGX_ERROR;
            }
            h->hash = 1;
            ngx_str_set(&h->key, "X-Upstream-nres");
            h->value.data = ngx_pnalloc(r->pool, NGX_INT_T_LEN);
            if (h->value.data == NULL) {
                // ngx_spinlock_unlock(lock);
                return NGX_ERROR;
            }
            h->value.len = ngx_sprintf(h->value.data, "%ui", block->nres) - h->value.data;
            // ngx_spinlock_unlock(lock);
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
