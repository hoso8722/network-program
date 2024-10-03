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
/* 固定バッファ:1 , 動的バッファ:2 */
int g_mode;
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
            /* 送受信ループ */
            if (g_mode == 1) {
                /* 固定バッファ */
                send_recv_loop_1(acc);
            } else {
                /* 動的バッファ */
                send_recv_loop_2(acc);
            }
            /* アクセプトソケットクローズ */
            (void) close(acc);
            acc = 0;
        }
    }
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
recv_one_line_1(int soc, char *buf, size_t bufsize, int flag)
{
    int end;
    ssize_t len, pos, rv;
    char c;
    /* 初期化 */
    buf[0] = '\0';
    pos = 0;
    do {
        end = 0;
        /* 1バイト受信 */
        c='\0';
        if ((len = recv(soc, &c, 1, flag)) == -1) {
            /* エラー：エラー終了 */
            perror("recv");
            rv = -1;
            end = 1;
        } else if (len == 0) {
            /* 切断 */
            (void) fprintf(stderr, "recv:EOF\n");
            if (pos > 0) {
                /* すでに受信データ有り：正常終了 */
                rv = pos;
                end = 1;
            } else {
                /* 受信データ無し：切断終了 */
                rv = 0;
                end = 1;
            }
        } else {
            /* 正常受信 */
            buf[pos++] = c;
            if (c == '\n') {
                /* 改行：終了 */
                rv = pos;
                end = 1;
            }
            if (pos == bufsize - 1) {
                /* 指定サイズ：終了 */
                rv = pos;
                end = 1;
            }
        }
    } while (end == 0);
    buf[pos] = '\0';
    return(rv);
}
/* ソケットから1行受信:動的バッファ */
ssize_t
recv_one_line_2(int soc, char **ret_buf, int flag)
{
#define RECV_ONE_LINE_2_ALLOC_SIZE      (1024)
#define RECV_ONE_LINE_2_ALLOC_LIMIT     (1024 * 1024)
    char buf[RECV_ONE_LINE_2_ALLOC_SIZE], *data = NULL;
    int end;
    ssize_t size = 0, now_len = 0, len, rv;
    *ret_buf = NULL;
    do {
        end = 0;
        /* 1行受信 */
        if ((len = recv_one_line_1(soc, buf, sizeof(buf), flag)) == -1) {
            /* エラー */
            free(data);
            data = NULL;
            rv = -1;
            end = 1;
        } else if (len == 0) {
            /* 切断 */
            if (now_len > 0) {
                /* 受信データ有り */
                rv = now_len;
                end = 1;
            } else {
                /* 受信データ無し */
                rv = 0;
                end = 1;
            }
        } else {
            /* 正常受信 */
            if (now_len + len >= size) {
                /* 領域不足 */
                size += RECV_ONE_LINE_2_ALLOC_SIZE;
                if (size > RECV_ONE_LINE_2_ALLOC_LIMIT) {
                    free(data);
                    data = NULL;
                } else if (data == NULL) {
                    data = malloc(size);
                } else {
                    data=realloc(data,size);
                }
            }
            if (data == NULL) {
                /* メモリー確保エラー */
                perror("malloc or realloc or limit-over");
                rv = -1;
                end = 1;
            } else {
                /* データ格納 */
                (void) memcpy(&data[now_len], buf, len);
                now_len += len;
                data[now_len] = '\0';
                if (data[now_len-1] == '\n') {
                    /* 末尾が改行 */
                    rv = now_len;
                    end = 1;
                }
            }
        }
    } while (end == 0);
    *ret_buf = data;
    return(rv);
}
/* 送受信ループ:固定バッファ */
void
send_recv_loop_1(int acc)
{
#define FIXED_BUFFER_SIZE (20)
    char buf[FIXED_BUFFER_SIZE], buf2[512], *ptr;
    ssize_t len;
    (void) fprintf(stderr, "Fixed buffer : sizeof(buf)=%d\n", (int) sizeof(buf));
    for (;;) {
        /* 受信 */
        if ((len = recv_one_line_1(acc, buf, sizeof(buf), 0)) == -1) {
            /* エラー */
            break;
        }
        if (len == 0) {
            /* エンド・オブ・ファイル */
            break;
        }
        /* デバッグ表示 */
        (void) fprintf(stderr, "[client(%d)]:", (int) len);
        debug_print(buf);
        /* 末尾の改行文字をカット */
        if ((ptr = strpbrk(buf, "\r\n")) != NULL) {
            *ptr='\0';
        }
        /* 応答文字列作成 */
        len = snprintf(buf2, sizeof(buf2), "%s:OK\r\n", buf);
        /* 応答 */
        if ((len = send(acc, buf2, len, 0)) == -1) {
            /* エラー */
            perror("send");
            break;
        }
    }
}
/* 送受信ループ:動的バッファ */
void
send_recv_loop_2(int acc)
{
    char *buf, *buf2, *ptr;
    ssize_t len;
    size_t alloc_len;
    for (;;) {
        /* 受信 */
        if ((len = recv_one_line_2(acc, &buf, 0)) == -1) {
            /* エラー */
            break;
        }
        if (len == 0) {
            /* エンド・オブ・ファイル */
            break;
        }
        /* デバッグ表示 */
        (void) fprintf(stderr, "[client(%d)]:", (int) len);
        debug_print(buf);
        /* 末尾の改行文字をカット */
        if ((ptr = strpbrk(buf, "\r\n")) != NULL) {
            *ptr='\0';
        }
        /* 応答文字列作成 */
        alloc_len = strlen(buf) + strlen(":OK\r\n") + 1;
        if ((buf2 = malloc(alloc_len)) == NULL) {
            perror("malloc");
            free(buf);
            break;
        }
        len = snprintf(buf2, alloc_len, "%s:OK\r\n", buf);
        /* 応答 */
        if ((len = send(acc, buf2, len, 0)) == -1) {
            /* エラー */
            perror("send");
            free(buf2);
            free(buf);
            break;
        }
        free(buf2);
        free(buf);
    }
}
int
main(int argc, char *argv[])
{
    int soc;
    /* 引数にポート番号とバッファモードが指定されているか？ */
    if (argc <= 2) {
        (void) fprintf(stderr,"oneline port\n");
        return (EX_USAGE);
    }
    if (argv[2][0] == '1') {
        (void) fprintf(stderr, "fixed buffer mode\n");
        g_mode = 1;
    } else {
        (void) fprintf(stderr, "variable buffer mode\n");
        g_mode = 2;
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

