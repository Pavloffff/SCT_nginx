/*
 * Copyright (C) Ivan Pavlov
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include "ngx_http_custom_upstream_rr_peer.h" // Объявление своих типов (названия те же но вместо ..._http_upstream_... надо писать ..._http_custom_upstream_...)

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


ngx_int_t
ngx_http_upstream_init_sct_neuro(ngx_conf_t *cf,                      // тут парсится upstream походу, cf это низкоуровневая конфига модуля
    ngx_http_upstream_srv_conf_t *us)                                   // us это настройки блока upstream
{
    ngx_url_t                      u;
    ngx_uint_t                     i, j, n, w, t;
    ngx_http_upstream_server_t    *server;
    ngx_http_custom_upstream_rr_peer_t   *peer, **peerp;
    ngx_http_custom_upstream_rr_peers_t  *peers, *backup;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0,
                   "init sct neuro");

    us->peer.init = ngx_http_upstream_init_sct_neuro_peer;            // запоминаем в upstream метод инициализации запроса

    if (us->servers) {                                                  // сервера из upstream (если есть)
        server = us->servers->elts;                                     // первый сервер

        n = 0;                                                          // кол-во адресов
        w = 0;                                                          // кол-во адресов с учетом веса
        t = 0;                                                          // кол-во адресов с учетом работоспособности сервера

        for (i = 0; i < us->servers->nelts; i++) {                      // идем по списку серверов
            if (server[i].backup) {                                     // если является бэкапом, не учитываем
                continue;
            }

            n += server[i].naddrs;
            w += server[i].naddrs * server[i].weight;

            if (!server[i].down) {
                t += server[i].naddrs;
            }
        }

        if (n == 0) {
            ngx_log_error(NGX_LOG_EMERG, cf->log, 0,                    // пример лога! логаем на файлик определенный в ngx_conf_t (тип ngx_log_t)
                          "no servers in upstream \"%V\" in %s:%ui",    // типа ngx_log_error(NGX_LOG_ALERT, log, err, "kill(%P, %d) failed", pid, signo);
                          &us->host, us->file_name, us->line);
            return NGX_ERROR;
        }

        peers = ngx_pcalloc(cf->pool, sizeof(ngx_http_custom_upstream_rr_peers_t));    // выделяем память для структуры, заполняем нулями
        if (peers == NULL) {
            return NGX_ERROR;
        }

        peer = ngx_pcalloc(cf->pool, sizeof(ngx_http_custom_upstream_rr_peer_t) * n);  // peer — объект, содержащий общие методы для инициализации конфигурации upstream
        if (peer == NULL) {
            return NGX_ERROR;
        }
                                                                        // заполняем конфигу кластера адресов
        peers->single = (n == 1);                                       // если кол-во адресов 1
        peers->number = n;                                              // всего адресов
        peers->weighted = (w != n);                                     // если у всех есть вес
        peers->total_weight = w;                                        // всего адресов с учетом весов (на 1 адрес несколько запросов мб)
        peers->tries = t;                                               // всего живых адресов
        peers->name = &us->host;                                        // имя кластера (название upstream из конфиги)

        n = 0;
        peerp = &peers->peer;                                           // начинаем с первого адреса

        for (i = 0; i < us->servers->nelts; i++) {                      // идем по адресам серверам внутри upstream
            if (server[i].backup) {                                     // запасные сервера скипаем
                continue;
            }

            for (j = 0; j < server[i].naddrs; j++) {                    // идем по адресам сервера
                peer[n].sockaddr = server[i].addrs[j].sockaddr;         // и всю дату копируем в кластер
                peer[n].socklen = server[i].addrs[j].socklen;
                peer[n].name = server[i].addrs[j].name;
                peer[n].weight = server[i].weight;                      // сейчас effective_weight == weight
                peer[n].effective_weight = server[i].weight;
                peer[n].current_weight = 0;                             // а current_weight = 0
                peer[n].max_conns = server[i].max_conns;
                peer[n].max_fails = server[i].max_fails;
                peer[n].fail_timeout = server[i].fail_timeout;
                peer[n].down = server[i].down;
                peer[n].server = server[i].name;

                // Инициализация новых переменных
                peer[n].cnt_requests = 0;
                peer[n].cnt_responses = 0;
                peer[n].neuro_weight = 1.0; // начальное значение, например, равное 1.0

                *peerp = &peer[n];                                      // переходим к следующему блоку в кластере
                peerp = &peer[n].next;
                n++;                                                    // n в конце станет кол-во скопированных адресов
            }
        }

        us->peer.data = peers;                                          // присваиваем в upstream итоговый список адресов

        // backup servers

        n = 0;                                                          // еще раз то же самое, для адресов backup серверов
        w = 0;
        t = 0;

        for (i = 0; i < us->servers->nelts; i++) {
            if (!server[i].backup) {
                continue;
            }

            n += server[i].naddrs;
            w += server[i].naddrs * server[i].weight;

            if (!server[i].down) {
                t += server[i].naddrs;
            }
        }

        if (n == 0) {
            return NGX_OK;
        }

        backup = ngx_pcalloc(cf->pool, sizeof(ngx_http_custom_upstream_rr_peers_t));   // не пон, как идет присвоение backup->peer = peer (мб оно внутри аллокатора)
        if (backup == NULL) {
            return NGX_ERROR;
        }

        peer = ngx_pcalloc(cf->pool, sizeof(ngx_http_custom_upstream_rr_peer_t) * n);
        if (peer == NULL) {
            return NGX_ERROR;
        }

        peers->single = 0;
        backup->single = 0;
        backup->number = n;
        backup->weighted = (w != n);
        backup->total_weight = w;
        backup->tries = t;
        backup->name = &us->host;

        n = 0;
        peerp = &backup->peer;

        for (i = 0; i < us->servers->nelts; i++) {
            if (!server[i].backup) {
                continue;
            }

            for (j = 0; j < server[i].naddrs; j++) {
                peer[n].sockaddr = server[i].addrs[j].sockaddr;
                peer[n].socklen = server[i].addrs[j].socklen;
                peer[n].name = server[i].addrs[j].name;
                peer[n].weight = server[i].weight;
                peer[n].effective_weight = server[i].weight;
                peer[n].current_weight = 0;
                peer[n].max_conns = server[i].max_conns;
                peer[n].max_fails = server[i].max_fails;
                peer[n].fail_timeout = server[i].fail_timeout;
                peer[n].down = server[i].down;
                peer[n].server = server[i].name;

                // Инициализация новых переменных
                peer[n].cnt_requests = 0;
                peer[n].cnt_responses = 0;
                peer[n].neuro_weight = 1.0; // начальное значение, например, равное 1.0

                *peerp = &peer[n];
                peerp = &peer[n].next;
                n++;
            }
        }

        peers->next = backup;

        return NGX_OK;
    }

    // иначе у нас upstream - и есть проксируемый сервер
    // an upstream implicitly defined by proxy_pass, etc. //

    if (us->port == 0) {
        ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                      "no port in upstream \"%V\" in %s:%ui",
                      &us->host, us->file_name, us->line);
        return NGX_ERROR;
    }

    ngx_memzero(&u, sizeof(ngx_url_t));                                         // храним url из upstream

    u.host = us->host;                                                          
    u.port = us->port;

    if (ngx_inet_resolve_host(cf->pool, &u) != NGX_OK) {                        // преобразование имени хоста в IP-адрес
        if (u.err) {
            ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                          "%s in upstream \"%V\" in %s:%ui",
                          u.err, &us->host, us->file_name, us->line);
        }

        return NGX_ERROR;
    }

    n = u.naddrs;                                                               // кол-во адресов для данного хоста

    peers = ngx_pcalloc(cf->pool, sizeof(ngx_http_custom_upstream_rr_peers_t));        // далее то же самое
    if (peers == NULL) {
        return NGX_ERROR;
    }

    peer = ngx_pcalloc(cf->pool, sizeof(ngx_http_custom_upstream_rr_peer_t) * n);
    if (peer == NULL) {
        return NGX_ERROR;
    }

    peers->single = (n == 1);
    peers->number = n;
    peers->weighted = 0;
    peers->total_weight = n;
    peers->tries = n;
    peers->name = &us->host;

    peerp = &peers->peer;

    for (i = 0; i < u.naddrs; i++) {
        peer[i].sockaddr = u.addrs[i].sockaddr;
        peer[i].socklen = u.addrs[i].socklen;
        peer[i].name = u.addrs[i].name;
        peer[i].weight = 1;
        peer[i].effective_weight = 1;
        peer[i].current_weight = 0;
        peer[i].max_conns = 0;
        peer[i].max_fails = 1;
        peer[i].fail_timeout = 10;
        *peerp = &peer[i];
        peerp = &peer[i].next;
    }

    us->peer.data = peers;

    // implicitly defined upstream has no backup servers

    return NGX_OK;
}


//Если закомментить ngx_http_upstream_init_sct_neuro выше и раскомментить его ниже то обращения будут нормально обрабатываться

/*static ngx_int_t
ngx_http_upstream_init_sct_neuro(ngx_conf_t *cf,
    ngx_http_upstream_srv_conf_t *us)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0,
                   "init sct neuro");

    if (ngx_http_upstream_init_round_robin(cf, us) != NGX_OK) {
        return NGX_ERROR;
    }

    us->peer.init = ngx_http_upstream_init_sct_neuro_peer;

    return NGX_OK;
}*/

/*
ngx_int_t
ngx_http_upstream_init_round_robin_peer(ngx_http_request_t *r,                  // этот метод используется в других модулях (типа нашего)
    ngx_http_upstream_srv_conf_t *us)
{
    ngx_uint_t                         n;
    ngx_http_upstream_rr_peer_data_t  *rrp;                                     

    rrp = r->upstream->peer.data;                                               // данные из запроса

    if (rrp == NULL) {
        rrp = ngx_palloc(r->pool, sizeof(ngx_http_upstream_rr_peer_data_t));
        if (rrp == NULL) {
            return NGX_ERROR;
        }

        r->upstream->peer.data = rrp;                                           // если данных нет, создаем пустую дату из пула
    }

    rrp->peers = us->peer.data;                                                 // те самые данные, что получили в прошлом методе
    rrp->current = NULL;
    rrp->config = 0;

    n = rrp->peers->number;                                                     // кол-во подключений

    if (rrp->peers->next && rrp->peers->next->number > n) {                     // если есть backup
        n = rrp->peers->next->number;
    }

    
        // tried отслеживает, какие серверы были уже испробованы. 
        // Если количество серверов меньше или равно количеству битов в uintptr_t, 
        // достаточно одного такого значения для отслеживания. 
        // Если серверов больше, требуется массив для их отслеживания.                                                                            
    

    if (n <= 8 * sizeof(uintptr_t)) {
        rrp->tried = &rrp->data;
        rrp->data = 0;
    } else {
        n = (n + (8 * sizeof(uintptr_t) - 1)) / (8 * sizeof(uintptr_t));

        rrp->tried = ngx_pcalloc(r->pool, n * sizeof(uintptr_t));
        if (rrp->tried == NULL) {
            return NGX_ERROR;
        }
    }

    r->upstream->peer.get = ngx_http_upstream_get_round_robin_peer;           // Устанавливаем методы для обработки
    r->upstream->peer.free = ngx_http_upstream_free_round_robin_peer;
    r->upstream->peer.tries = ngx_http_upstream_tries(rrp->peers);
#if (NGX_HTTP_SSL)
    r->upstream->peer.set_session =
                               ngx_http_upstream_set_round_robin_peer_session;
    r->upstream->peer.save_session =
                               ngx_http_upstream_save_round_robin_peer_session;
#endif

    return NGX_OK;
}*/

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
    ngx_http_custom_upstream_rr_peer_t  *peer;

    ngx_http_custom_upstream_rr_peer_data_t  *rrp = data;
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