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
    if ((soc = socket(res0->ai_family, res0->ai_socktype, res0->ai_protocol)) == -1) {
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

/* 送受信処理 */
void
send_recv_loop(int soc)
{
    char buf[512];
    struct timeval timeout;
    int end, width;
    ssize_t len;
    fd_set  mask, ready;
    /* select()用マスク */
    FD_ZERO(&mask);
    /* ソケットディスクリプタをセット */
    FD_SET(soc, &mask);
    /* 標準入力をセット */
    FD_SET(0, &mask);
    width = soc + 1;
    /* 送受信 */
    for (end = 0;; ) {
        /* マスクの代入 */
        ready = mask;
        /* タイムアウト値のセット */
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        switch (select(width, (fd_set *) &ready, NULL, NULL, &timeout)) {
        case -1:
            /* エラー */
            perror("select");
            break;
        case 0:
            /* タイムアウト */
            break;
        default:
            /* レディ有り */
            /* ソケットレディ */
            if (FD_ISSET(soc, &ready)) {
                /* 受信 */
                if ((len = recv(soc, buf, sizeof(buf), 0)) == -1) {
                    /* エラー */
                    perror("recv");
                    end = 1;
                    break;
                }
                if (len == 0) {
                    /* エンド・オブ・ファイル */
                    (void) fprintf(stderr, "recv:EOF\n");
                    end = 1;
                    break;
                }
                /* 文字列化・表示 */
                buf[len]='\0';
                (void) printf("> %s", buf);
            }
            /* 標準入力レディ */
            if (FD_ISSET(0, &ready)) {
                /* 標準入力から１行読み込み */
                (void) fgets(buf,sizeof(buf),stdin);
                if (feof(stdin)) {
                    end = 1;
                    break;
                }
                /* 送信 */
                if ((len = send(soc, buf, strlen(buf), 0)) == -1) {
                    /* エラー */
                    perror("send");
                    end = 1;
                    break;
                }
            }
        }
        if (end) {
            break;
        }
    }
}
/* パーサのプロトタイプ宣言 */
extern int yyparse(void);
/* パーサが読み込むファイルを指定するための外部変数 */
extern FILE *yyin;
/* 設定ファイルに書かれたプログラム名を格納する変数 */
char g_program_name[80];
/* 設定ファイルに書かれたホスト名を格納する変数 */
char g_hostnm[80];
/* 設定ファイルに書かれたポート番号を格納する変数 */
char g_portnm[80];
int
main(int argc, char *argv[])
{
    int soc;
    char fname[80], *ptr;
    /* 実行ファイル名をptrに保持 */
    if ((ptr = strrchr(argv[0], '/')) == NULL) {
        ptr = argv[0];
    } else {
        ptr++;
    }
    /* 実行ファイル名.confを設定ファイルとして開く */
    (void) snprintf(fname, sizeof(fname), "%s.conf", ptr);
    if ((yyin = fopen(fname, "r")) == NULL) {
        (void) fprintf(stderr, "Cannot read %s\n", fname);
        return (EX_DATAERR);
    }
    /* 設定ファイルを読み込む */
    if (yyparse() != 0) {
        (void) fprintf(stderr, "Cannot read %s\n", fname);
        (void) fclose(yyin);
        return (EX_DATAERR);
    }
    (void) fclose(yyin);
    /* 設定ファイルに書かれたプログラム名と実行ファイル名が異なればエラーとする */
    if (strcmp(ptr, g_program_name) != 0) {
        (void) fprintf(stderr, "Configuration not found for %s\n", ptr);
        return (EX_DATAERR);
    }
    /* サーバにソケット接続 */
    if ((soc = client_socket(g_hostnm, g_portnm)) == -1) {
        (void) fprintf(stderr,"client_socket():error\n");
        return (EX_UNAVAILABLE);
    }
    /* 送受信処理 */
    send_recv_loop(soc);
    /* ソケットクローズ */
    (void) close(soc);
    return (EX_OK);
}

