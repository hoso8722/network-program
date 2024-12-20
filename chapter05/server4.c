#include <sys/epoll.h>                  /* 追加 */
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

int send_recv(int, int);
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
    if ((soc = socket(res0->ai_family, res0->ai_socktype, res0->ai_protocol))
        == -1) {
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
/* 最大同時処理数 */
#define    MAX_CHILD    (20)
/* アクセプトループ */
void
accept_loop(int soc)
{
    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
    struct sockaddr_storage from;
    int acc, count, i, epollfd, nfds, ret;
    socklen_t len;
    struct epoll_event ev, events[MAX_CHILD];

    if ((epollfd = epoll_create(MAX_CHILD + 1)) == -1) {
        perror("epoll_create");
        return;
    }
    /* EPOLL用データの作成 */
    ev.data.fd = soc;
    ev.events = EPOLLIN;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, soc, &ev) == -1) {
        perror("epoll_ctl");
        (void) close(epollfd);
        return;
    }
    count = 0;
    for (;;) {
        (void) fprintf(stderr,"<<child count:%d>>\n", count);
        switch ((nfds = epoll_wait(epollfd, events, MAX_CHILD+1, 10 * 1000))) {
        case -1:
            /* エラー */
            perror("epoll_wait");
            break;
        case 0:
            /* タイムアウト */
            break;
        default:
            /* ソケットがレディ */
            for (i = 0; i < nfds; i++) {
                if (events[i].data.fd == soc) {
                    /* サーバソケットレディ */
                    len = (socklen_t) sizeof(from);
                    /* 接続受付 */
                    if ((acc = accept(soc, (struct sockaddr *)&from, &len))==-1) {
                        if (errno != EINTR){
                            perror("accept");
                        }
                    } else {
                        (void) getnameinfo((struct sockaddr *) &from, len,
                                           hbuf, sizeof(hbuf),
                                           sbuf, sizeof(sbuf),
                                           NI_NUMERICHOST | NI_NUMERICSERV);
                        (void) fprintf(stderr, "accept:%s:%s\n", hbuf, sbuf);
                        /* 空きが無い */
                        if (count + 1 >= MAX_CHILD) {
                            /* これ以上接続できない */
                            (void) fprintf(stderr,
					  "connection is full : cannot accept\n");
                            /* クローズしてしまう */
                            (void) close(acc);
                        } else {
                            ev.data.fd = acc;
                            ev.events = EPOLLIN;
                            if (epoll_ctl(epollfd, EPOLL_CTL_ADD, acc, &ev) == -1) {
                                perror("epoll_ctl");
                                (void) close(acc);
                                (void) close(epollfd);
                                return;
                            }
                            count++;
                        }
                    }
                } else {
                    /* 送受信 */
                    if ((ret = send_recv(events[i].data.fd, events[i].data.fd))
		        == -1) {
                        /* エラーまたは切断 */
                        if (epoll_ctl(epollfd,
				      EPOLL_CTL_DEL,
				      events[i].data.fd,
				      &ev) == -1) {
                            perror("epoll_ctl");
                            (void) close(acc);
                            (void) close(epollfd);
                            return;
                        }
                        /* クローズ */
                        (void) close(events[i].data.fd);
                        count--;
                    }
                }
            }
	    break;
        }
    }
    (void) close(epollfd);
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

/* 送受信 */
int
send_recv(int acc, int child_no)
{
    char buf[512],*ptr;
    ssize_t len;

    /* 受信 */
    if ((len = recv(acc, buf, sizeof(buf), 0)) == -1) {
        /* エラー */
        perror("recv");
        return (-1);
    }
    if (len == 0) {
        /* エンド・オブ・ファイル */
        (void) fprintf(stderr, "[child%d]recv:EOF\n", child_no);
        return (-1);
    }
    /* 文字列化・表示 */
    buf[len] = '\0';
    if ((ptr = strpbrk(buf, "\r\n")) != NULL) {
        *ptr='\0';
    }
    (void) fprintf(stderr, "[child%d]%s\n", child_no, buf);
    /* 応答文字列作成 */
    (void) mystrlcat(buf, ":OK\r\n", sizeof(buf));
    len = strlen(buf);
    /* 応答 */
    if ((len = send(acc, buf, len, 0)) == -1) {
        /* エラー */
        perror("send");
        return (-1);
    }
    return (0);
}
int
main(int argc, char *argv[])
{
    int soc;
    /* 引数にポート番号が指定されているか？ */
    if (argc <= 1) {
        (void) fprintf(stderr, "server4 port\n");
        return (EX_USAGE);
    }
    /* サーバソケットの準備 */
    if ((soc = server_socket(argv[1])) == -1) {
        (void) fprintf(stderr, "server_socket(%s):error\n", argv[1]);
        return (EX_UNAVAILABLE);
    }
    (void) fprintf(stderr, "ready for accept\n");
    /* アクセプトループ */
    accept_loop(soc);
    /* ソケットクローズ */
    (void) close(soc);
    return (EX_OK);
}
