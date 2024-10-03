#include <sys/param.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>                      /* 追加 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
/* ノンブロッキング:'n' */
char    g_mode='b';
/* 受信バッファ */
char    g_buf[1000 * 1000];
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
    int acc;
    socklen_t len;
    for (;;) {
        len = (socklen_t) sizeof(from);
        /* 接続受付 */
        if ((acc = accept(soc, (struct sockaddr *) &from, &len)) == -1) {
            if (errno != EINTR) {
                perror("accept");
            }
        } else {
            (void) getnameinfo((struct sockaddr *) &from, len,
                               hbuf, sizeof(hbuf),
                               sbuf, sizeof(sbuf),
                               NI_NUMERICHOST | NI_NUMERICSERV);
            (void) fprintf(stderr, "accept:%s:%s\n", hbuf, sbuf);
            /* 受信ループ */
            recv_loop(acc);
            /* アクセプトソケットクローズ */
            (void) close(acc);
            acc = 0;
        }
    }
}
/* ブロッキングモードのセット */
int
set_block(int fd, int flag)
{
    int flags;

    if ((flags = fcntl(fd, F_GETFL, 0)) == -1) {
        perror("fcntl");
        return (-1);
    }
    if (flag == 0) {
        /* ノンブロッキング */
        (void) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    } else if (flag == 1) {
        /* ブロッキング */
        (void) fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    }
    return (0);
}
/* 受信ループ */
void
recv_loop(int acc)
{
    ssize_t total, len;
    if (g_mode == 'n') {
        /* ノンブロッキングモード */
        (void) set_block(acc, 0);
    }
    for (total = 0; ;) {
        /* 受信 */
        if ((len = recv(acc, g_buf, sizeof(g_buf), 0)) == -1) {
            /* エラー */
            if (errno == EAGAIN) {
                (void) fprintf(stderr, ".");
                continue;
            } else {
                perror("recv");
                break;
            }
        }
        if (len == 0) {
            /* エンド・オブ・ファイル */
            (void) fprintf(stderr, "recv:EOF\n");
            break;
        }
        (void) fprintf(stderr, "recv:%d\n", (int) len);
        total += len;
    }
    (void) fprintf(stderr, "total:%d\n", (int) total);
}
int
main(int argc, char *argv[])
{
    int soc;
    /* 引数にポート番号が指定されているか？ */
    if (argc <= 1) {
        (void) fprintf(stderr,"bigserver port\n");
        return (EX_USAGE);
    }
    /* ブロッキングモードオプションの判定 */
    if (argc >= 3 && argv[2][0] == 'n') {
        (void) fprintf(stderr, "Nonblocking mode\n");
        g_mode = 'n';
    } else {
        g_mode = 'b';
    }
    /* サーバソケットの準備 */
    if ((soc = server_socket(argv[1])) == -1) {
        (void) fprintf(stderr,"server_socket(%s):error\n", argv[1]);
        return (EX_UNAVAILABLE);
    }
    (void) fprintf(stderr, "ready for accept\n");
    /* アクセプトループ */
    accept_loop(soc);
    /* ソケットクローズ */
    (void) close(soc);
    return (EX_OK);
}

