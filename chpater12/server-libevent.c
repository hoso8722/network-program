#include <sys/param.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <event.h>			/* 追加 */
/* サーバソケットの準備 */
int
server_socket(const char *portnm)
{
    char nbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
    struct addrinfo hints, *res0;
    int soc, opt, errcode;
    socklen_t opt_len;

    /* アドレス情報のヒントをゼロクリア */
    (void) memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    /* アドレス情報の決定 */
    if ((errcode = getaddrinfo(NULL, portnm, &hints, &res0)) != 0) {
        (void) fprintf(stderr, "getaddrinfo():%s\n", gai_strerror(errcode));
        return (-1);
    }
    if ((errcode = getnameinfo(res0->ai_addr, res0->ai_addrlen,
                               nbuf, sizeof(nbuf),
                               sbuf, sizeof(sbuf),
                               NI_NUMERICHOST | NI_NUMERICSERV)) != 0) {
        (void) fprintf(stderr, "getnameinfo():%s\n", gai_strerror(errcode));
        freeaddrinfo(res0);
        return (-1);
    }
    (void) fprintf(stderr, "port=%s\n", sbuf);
    /* ソケットの生成 */
    if ((soc = socket(res0->ai_family, res0->ai_socktype, res0->ai_protocol)) == -1) {
        perror("socket");
        freeaddrinfo(res0);
        return (-1);
    }
    /* ソケットオプション（再利用フラグ）設定 */
    opt = 1;
    opt_len = sizeof(opt);
    if (setsockopt(soc, SOL_SOCKET, SO_REUSEADDR, &opt, opt_len) == -1) {
        perror("setsockopt");
        (void) close(soc);
        freeaddrinfo(res0);
        return (-1);
    }
    /* ソケットにアドレスを指定 */
    if (bind(soc, res0->ai_addr, res0->ai_addrlen) == -1) {
        perror("bind");
        (void) close(soc);
        freeaddrinfo(res0);
        return (-1);
    }
    /* アクセスバックログの指定 */
    if (listen(soc, SOMAXCONN) == -1) {
        perror("listen");
        (void) close(soc);
        freeaddrinfo(res0);
        return (-1);
    }
    freeaddrinfo(res0);
    return (soc);
}
/* accept_handlerで使う関数のプロトタイプ宣言 */
void read_handler(int, short, void *);

/* アクセプトハンドラ */
void
accept_handler(int soc, short event, void *arg)
{

    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
    struct sockaddr_storage from;
    int acc;
    socklen_t len;
    struct event *ev;

    /* イベントのタイプが読み込み可能だった */
    if (event & EV_READ) {
        len = (socklen_t) sizeof(from);
        /* 接続受付 */
        if ((acc = accept(soc, (struct sockaddr *) &from, &len)) == -1) {
            if (errno != EINTR) {
                perror("accept");
		return;
            }
	} else {
            (void) getnameinfo((struct sockaddr *) &from, len,
                               hbuf, sizeof(hbuf),
                               sbuf, sizeof(sbuf),
                               NI_NUMERICHOST | NI_NUMERICSERV);
            (void) fprintf(stderr, "accept:%s:%s\n", hbuf, sbuf);
	}
	/*
	 * 接続済みソケット:acc が読み込み可能になった際に、
	 * read_handler を呼ぶように登録イベントを登録
	 */
        if ((ev = malloc(sizeof(struct event))) == NULL) {
	    perror("malloc");
	}
        event_set(ev, acc, EV_READ | EV_PERSIST, read_handler, ev);
        if (event_add(ev, NULL) != 0) {
	    perror("event_add");
	    free(ev);
	    return;
	}
    }
}
/* サイズ指定文字列連結 */
size_t
mystrlcat(char *dst, const char *src, size_t size)
{
    const char *ps;
    char *pd, *pde;
    size_t dlen, lest;
    
    for (pd = dst, lest = size; *pd != '\0' && lest !=0; pd++, lest--);
    dlen = pd - dst;
    if (size - dlen == 0) {
        return (dlen + strlen(src));
    }
    pde = dst + size - 1;
    for (ps = src; *ps != '\0' && pd < pde; pd++, ps++) {
        *pd = *ps;
    }
    for (; pd <= pde; pd++) {
        *pd = '\0';
    }
    while (*ps++);
    return (dlen + (ps - src - 1));
}
/* 送受信ハンドラ */
void
read_handler(int acc, short event, void *arg)
{
    char buf[512], *ptr;
    ssize_t len;
    /* event_set()の第５引数のデータをargで受け取れる */
    struct event *ev = (struct event*) arg;

    /* イベントのタイプが読み込み可能だった */
    if (event & EV_READ) {
        /* 受信 */
        if ((len = recv(acc, buf, sizeof(buf), 0)) == -1) {
            /* エラー */
            perror("recv");
	    (void) event_del(ev);
	    free(ev);
            (void) close(acc);
            return;
        }
        if (len == 0) {
            /* エンド・オブ・ファイル */
            (void) fprintf(stderr, "recv:EOF\n");
	    (void) event_del(ev);
	    free(ev);
            (void) close(acc);
            return;
        }
        /* 文字列化・表示 */
        buf[len]='\0';
        if ((ptr = strpbrk(buf, "\r\n")) != NULL) {
            *ptr = '\0';
        }
        (void) fprintf(stderr, "[client]%s\n", buf);
        /* 応答文字列作成 */
        (void) mystrlcat(buf, ":OK\r\n", sizeof(buf));
        len = (ssize_t) strlen(buf);
        /* 応答 */
        if ((len = send(acc, buf, (size_t) len, 0)) == -1) {
            /* エラー */
            perror("send");
	    (void) event_del(ev);
	    free(ev);
            (void) close(acc);
        }
    }
}
/* 送受信ハンドラ */
void
sig_int_handler(int fd, short event, void *arg)
{
    event_loopexit(NULL);
}

int
main(int argc, char *argv[])
{
    struct event ev, sig;
    int soc;
    /* 引数にポート番号が指定されているか？ */
    if (argc <= 1) {
        (void) fprintf(stderr,"server-libevent port\n");
        return (EX_USAGE);
    }
    /* サーバソケットの準備 */
    if ((soc = server_socket(argv[1])) == -1) {
        (void) fprintf(stderr,"server_socket(%s):error\n", argv[1]);
        return (EX_UNAVAILABLE);
    }
    (void) fprintf(stderr, "ready for accept\n");
    /* libeventの初期化 */
    event_init();
    signal_set(&sig, SIGINT, sig_int_handler, NULL);
    signal_add(&sig, NULL);
    /* サーバソケットのlibeventへの登録 */
    event_set(&ev, soc, EV_READ | EV_PERSIST, accept_handler, &ev);
    if (event_add(&ev,NULL) != 0) {
        perror("event_add");
	(void) close(soc);
        return (EX_UNAVAILABLE);
    }
    /*
     * libeventのイベント駆動のためのループ
     */
    event_dispatch();

    (void) close(soc);
    (void) fprintf(stderr, "\nEND\n");
    return (EX_OK);
}

