#include <sys/epoll.h>
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
#define FIXED_BUFFER_SIZE (20)
/* 最大同時処理数 */
#define    MAX_CHILD    (20)
/* 中間バッファ */
char g_buf[MAX_CHILD * 4][FIXED_BUFFER_SIZE];
ssize_t g_len[MAX_CHILD * 4];
ssize_t g_pos[MAX_CHILD * 4];
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
        switch ((nfds = epoll_wait(epollfd, events, MAX_CHILD + 1, 10 * 1000))) {
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
                    if ((acc = accept(soc, (struct sockaddr *) &from, &len))
		        == -1) {
                        if(errno!=EINTR){
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
                            if (epoll_ctl(epollfd, EPOLL_CTL_ADD, acc, &ev)
			        == -1) {
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
                    if ((ret = send_recv_1(events[i].data.fd)) == -1) {
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
/* デバッグ表示用 */
void
debug_print(char *buf)
{
    char *ptr;
    for (ptr = buf; *ptr != '\0'; ptr++) {
        if (isprint(*ptr)) {
            (void) fputc(*ptr, stderr);
        } else {
            (void) fprintf(stderr, "[%02X]", *ptr);
        }
    }
    (void) fputc('\n', stderr);
}
/* ソケットから1行受信:固定バッファ */
ssize_t
recv_one_line_1(int soc, int flag)
{
    ssize_t rv;
    char c;
    /* 初期化 */
    if (g_buf[soc][0] == '\0') {
        g_pos[soc] = 0;
    }
    /* 1バイト受信 */
    c='\0';
    if ((g_len[soc] = recv(soc, &c, 1, flag)) == -1) {
        /* エラー：エラー終了 */
        perror("recv");
        rv = -1;
    } else if (g_len[soc] == 0) {
        /* 切断 */
        (void) fprintf(stderr, "recv:EOF\n");
        if (g_pos[soc] > 0) {
            /* すでに受信データ有り：正常終了 */
            rv = g_pos[soc];
            } else {
            /* 受信データ無し：切断終了 */
            rv = 0;
        }
    } else {
        /* 正常受信 */
        g_buf[soc][g_pos[soc]++] = c;
        if (c == '\n') {
            /* 改行：終了 */
            rv = g_pos[soc];
        } else if (g_pos[soc] == FIXED_BUFFER_SIZE - 1) {
            /* 指定サイズ：終了 */
            rv = g_pos[soc];
        } else {
            /* 次回継続 */
            rv = -2;
        }
    }

    return(rv);
}
/* 送受信 */
int
send_recv_1(int acc)
{
    char buf2[512], *ptr;
    ssize_t len;
    (void) fprintf(stderr,
                   "Fixed buffer : sizeof(g_buf[%d])=%d\n",
		   acc,
		   (int) sizeof(g_buf[acc]));
    /* 受信 */
    if ((len = recv_one_line_1(acc, 0)) == -1) {
        /* エラー */
        /* バッファの初期化 */
        g_buf[acc][0] = '\0';
        return (-1);
    }
    if (len == 0) {
        /* エンド・オブ・ファイル */
        /* バッファの初期化 */
        g_buf[acc][0] = '\0';
        return (-1);
    }
    if (len == -2) {
        /* 継続が必要 */
        return (-2);
    }
    /* デバッグ表示 */
    (void) fprintf(stderr, "[client(%d)]:", (int) len);
    debug_print(g_buf[acc]);
    /* 末尾の改行文字をカット */
    if ((ptr = strpbrk(g_buf[acc], "\r\n")) != NULL) {
        *ptr='\0';
    }
    /* 応答文字列作成 */
    len = snprintf(buf2, sizeof(buf2), "%s:OK\r\n", g_buf[acc]);
    /* 応答 */
    if ((len = send(acc, buf2, len, 0)) == -1) {
        /* エラー */
        perror("send");
        /* バッファの初期化 */
        g_buf[acc][0] = '\0';
        return (-1);
    }
    /* バッファの初期化 */
    g_buf[acc][0] = '\0';
    return (0);
}
int
main(int argc, char *argv[])
{
    int soc;
    /* 引数にポート番号が指定されているか？ */
    if (argc <= 1) {
        (void) fprintf(stderr,"oneline1 port\n");
        return (EX_USAGE);
    }
    /* サーバソケットの準備 */
    if ((soc = server_socket(argv[1])) == -1) {
        (void) fprintf(stderr,"server_socket(%s):error\n", argv[1]);
        return (EX_UNAVAILABLE);
    }
    (void) memset(g_buf, '\0', sizeof(g_buf));
    (void) memset(g_len, 0, sizeof(g_len));
    (void) memset(g_pos, 0, sizeof(g_pos));
    (void) fprintf(stderr, "ready for accept\n");
    /* アクセプトループ */
    accept_loop(soc);
    /* ソケットクローズ */
    (void) close(soc);
    return (EX_OK);
}

