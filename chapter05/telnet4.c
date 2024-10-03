#include <sys/param.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>			/* 追加 */

#include <arpa/inet.h>
#include <arpa/telnet.h>                /* 追加 */
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
/* ソケット */
int g_soc = -1;
/* 終了フラグ */
volatile sig_atomic_t g_end = 0;
/* 子プロセス:1 */
int g_is_child = 0;
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
/* 送受信処理 */
int
send_recv_loop(void) 
{
    pid_t pid;
    char c;
    /* エコーなし、RAWモード */
    (void) system("stty -echo raw");
    /* バッファリングOFF */
    (void) setbuf(stdin, NULL);
    (void) setbuf(stdout, NULL);
    if ((pid = fork()) == 0) {
        /* 子プロセス */
        g_is_child = 1;
        while (g_end == 0) {
            /* 標準入力から読み込み */
            c = getchar();
            /* サーバに送信 */
            if (send(g_soc, &c, 1, 0) == -1) {
                perror("send");
                break;
            }
        }
        /* 親プロセスにシグナル送信 */
        (void) kill(getppid(), SIGTERM);
        /* 子プロセス終了 */
        _exit(0);
    } else if (pid > 0) {
        /* 親プロセス */
        while (g_end == 0) {
            /* ソケット受信 */
            if (recv_data() == -1) {
                break;
            }
        }
        /* 子プロセスにシグナル送信 */
        (void) kill(pid, SIGTERM);
        /* 子プロセスの終了をwait */
        (void) wait(NULL);
    } else {
        /* fork():失敗 */
        perror("fork");
        return (-1);
    }
    return (0);
}
/* データ受信 */
int
recv_data(void)
{
    char buf[8];
    char c;
    if (recv(g_soc, &c, 1, 0) <= 0) {
        return (-1);
    }
    if ((int) (c & 0xFF) == IAC) {
        /* コマンド */
        if (recv(g_soc, &c, 1, 0) == -1) {
            perror("recv");
            return (-1);
        }
        if (recv(g_soc, &c, 1, 0) == -1) {
            perror("recv");
            return (-1);
        }
        /* 否定で応答 */
        (void) snprintf(buf, sizeof(buf), "%c%c%c", IAC, WONT, c);
        if (send(g_soc, buf, 3, 0) == -1) {
            perror("send");
            return (-1);
        }
    } else {
        /* 画面へ */
        (void) fputc(c & 0xFF, stdout);
    }
    return (0);
}
/* シグナルハンドラ */
void
sig_term_handler(int sig)
{
    g_end = sig;
}
/* シグナルの設定 */
void
init_signal(void)
{
    /* 終了関連 */ 
    struct sigaction sa;
    (void) sigaction(SIGINT, (struct sigaction *) NULL, &sa);
    sa.sa_handler = sig_term_handler;
    sa.sa_flags = SA_NODEFER;
    (void) sigaction(SIGINT, &sa, (struct sigaction *) NULL);
    (void) sigaction(SIGTERM, (struct sigaction *) NULL, &sa);
    sa.sa_handler = sig_term_handler;
    sa.sa_flags = SA_NODEFER;
    (void) sigaction(SIGTERM, &sa, (struct sigaction *) NULL);
    (void) sigaction(SIGQUIT, (struct sigaction *) NULL, &sa);
    sa.sa_handler = sig_term_handler;
    sa.sa_flags = SA_NODEFER;
    (void) sigaction(SIGQUIT, &sa, (struct sigaction *) NULL);
    (void) sigaction(SIGHUP, (struct sigaction *) NULL, &sa);
    sa.sa_handler = sig_term_handler;
    sa.sa_flags = SA_NODEFER;
    (void) sigaction(SIGHUP, &sa, (struct sigaction *) NULL);
}
int
main(int argc,char *argv[])
{
    char *port;
    if (argc <= 1) {
        (void) fprintf(stderr, "telnet4 hostname [port]\n");
        return (EX_USAGE);
    } else if (argc <= 2) {
        port = "telnet";
    } else {
        port = argv[2];
    }
    /* ソケット接続 */
    if ((g_soc = client_socket(argv[1], port)) == -1) {
        return (EX_IOERR);
    }
    /* シグナル設定 */
    init_signal();
    /* メインループ */
    send_recv_loop();
    /* プログラム終了 */
    /* ソケットクローズ */
    if (g_soc != -1) {
        (void) close(g_soc);
    }
    if (!g_is_child) {
        /* エコーあり、cookedモード 8ビット */
        (void) system("stty echo cooked -istrip");
        (void) fprintf(stderr, "Connection Closed.\n");
    }
    return (EX_OK);
}

