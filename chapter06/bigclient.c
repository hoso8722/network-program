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
/* 送信バッファ */
char g_buf[1000 * 1000];
/* サーバにソケット接続 */
int
client_socket(const char *hostnm, const char *portnm)
{
    char nbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
    struct addrinfo hints, *res0;
    int soc, errcode;
    
    /* アドレス情報のヒントをゼロクリア */
    (void) memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    /* アドレス情報の決定 */
    if ((errcode = getaddrinfo(hostnm, portnm, &hints, &res0)) != 0) {
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
    (void) fprintf(stderr, "addr=%s\n", nbuf);
    (void) fprintf(stderr, "port=%s\n", sbuf);
    /* ソケットの生成 */
    if ((soc = socket(res0->ai_family, res0->ai_socktype, res0->ai_protocol))
        == -1) {
        perror("socket");
        freeaddrinfo(res0);
        return (-1);
    }
    /* コネクト */
    if (connect(soc, res0->ai_addr, res0->ai_addrlen) == -1) {
        perror("connect");
        (void) close(soc);
        freeaddrinfo(res0);
        return (-1);
    }
    freeaddrinfo(res0);
    return (soc);
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
/* 送受信処理 */
void
send_one(int soc)
{
    ssize_t len;
    (void) memset(g_buf, 0, sizeof(g_buf));
    /* 送信 */
    if ((len = send(soc, g_buf, sizeof(g_buf), 0)) == -1) {
        /* エラー:EAGAINでもリトライしない */
        perror("send");
    }
    (void) fprintf(stderr, "send:%d\n", (int) len);
}
int
main(int argc, char *argv[])
{
    int soc;
    /* 引数にホスト名、ポート番号が指定されているか？ */
    if (argc <= 2) {
        (void) fprintf(stderr,"bigclient server-host port\n");
        return (EX_USAGE);
    }
    /* サーバにソケット接続 */
    if ((soc = client_socket(argv[1], argv[2])) == -1) {
        (void) fprintf(stderr,"client_socket():error\n");
        return (EX_UNAVAILABLE);
    }
    /* ブロッキングモードオプションの判定 */
    if (argc >= 4 && argv[3][0] == 'n') {
        (void) fprintf(stderr, "Nonblocking mode\n");
        /* ノンブロッキングモード */
        (void) set_block(soc, 0);
    }
    /* 送信処理 */
    send_one(soc);
    /* ソケットクローズ */
    (void) close(soc);
    return (EX_OK);
}

