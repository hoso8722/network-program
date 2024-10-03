#include <sys/param.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <arpa/inet.h>
#include <net/if.h>                     /* 追加 */
#include <netinet/in.h>
#include <netdb.h>

#include <ctype.h>
#include <errno.h>
#include <ifaddrs.h>                    /* 追加 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
/* インターフェースアドレスからインターフェスインデックスへの変換 */
int
if_addrtoindex(const char *if_address)
{
    struct addrinfo hints, *res0;
    struct ifaddrs *ifaddrs, *ifa;
    int errcode, rv = 0;
    /* アドレス情報のヒントをゼロクリア */
    (void) memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_DGRAM;
    /* アドレス情報の決定 */
    if ((errcode = getaddrinfo(if_address, NULL, &hints, &res0)) != 0) {
        (void) fprintf(stderr, "getaddrinfo():%s\n", gai_strerror(errcode));
        return (-1);
    }
    /* getifaddrs()でインタフェースのアドレス一覧をifaddrsに取得 */
    if (getifaddrs(&ifaddrs) == -1) {
        perror("getifaddrs");
        freeaddrinfo(res0);
        return (-1);
    }
    /* ifaddrsに取得したアドレス一覧のリンクドリストを辿る */
    for (ifa = ifaddrs; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) {
            continue;
        }
        if (ifa->ifa_addr->sa_family == AF_INET6) {
            /* IPv6 の場合 */
            /* ユニキャストアドレスの取得・表示 */
            if (memcmp(&((struct sockaddr_in6 *) res0->ai_addr)->sin6_addr,
	               &((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr,
		       sizeof(struct in6_addr)) == 0) {
                rv = if_nametoindex(ifa->ifa_name);
                break;
            }
        }
    }
    /* getifaddrs()が確保したメモリを解放 */
    freeifaddrs(ifaddrs);
    freeaddrinfo(res0);
    return (rv);
}
/* アドレス情報の取得 */
int
get_sockaddr_info6(const char *hostnm,
                   const char *portnm,
		   struct sockaddr_storage *saddr,
		   socklen_t *saddr_len)
{
    char nbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
    struct addrinfo hints, *res0;
    int errcode;
    
    /* アドレス情報のヒントをゼロクリア */
    (void) memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_DGRAM;
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
    (void) memcpy(saddr, res0->ai_addr, res0->ai_addrlen);
    *saddr_len = res0->ai_addrlen;
    freeaddrinfo(res0);
    return (0);
}
/* 送受信処理 */
void
send_recv_loop(int soc)
{
    char nbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
    char buf[512], buf2[512];
    struct sockaddr_storage from, to;
    struct timeval timeout;
    int end, errcode, width;
    ssize_t len;
    socklen_t fromlen, tolen;
    fd_set    mask, ready;
    char *hostnm, *portnm;
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
                fromlen = sizeof(from);
                if ((len = recvfrom(soc,
		                    buf,
				    sizeof(buf),
				    0,
				    (struct sockaddr *) &from,
				    &fromlen)) == -1) {
                    /* エラー */
                    perror("recvfrom");
                    end = 1;
                    break;
                }
                if ((errcode = getnameinfo((struct sockaddr *) &from, fromlen,
                               nbuf, sizeof(nbuf),
                               sbuf, sizeof(sbuf),
                               NI_NUMERICHOST | NI_NUMERICSERV)) != 0) {
                    (void) fprintf(stderr,
		                   "getnameinfo():%s\n", gai_strerror(errcode));
                }
                (void) printf("recvfrom:%s:%s:len=%d\n", nbuf, sbuf, (int) len);
                /* 文字列化・表示 */
                buf[len] = '\0';
                (void) printf("> %s",buf);
            }
            /* 標準入力レディ */
            if (FD_ISSET(0, &ready)) {
                /* 標準入力から1行読み込み */
                (void) fgets(buf, sizeof(buf), stdin);
                if (feof(stdin)) {
                    end = 1;
                    break;
                }
                (void) memcpy(buf2, buf, sizeof(buf2));
                if ((hostnm = strtok(buf2, " ")) == NULL) {
                    (void) fprintf(stderr, "Input-error\n");
                    (void) fprintf(stderr, "host:port\n");
                    break;
                }
                if ((portnm = strtok(NULL, "\r\n")) == NULL) {
                    (void) fprintf(stderr, "Input-error\n");
                    (void) fprintf(stderr, "host:port\n");
                    break;
                }
                /* サーバアドレス情報の取得 */
                if (get_sockaddr_info6(hostnm, portnm, &to, &tolen) == -1) {
                    (void) fprintf(stderr, "get_sockaddr_info6():error\n");
                    break;
                }
                /* 送信 */
                if ((len = sendto(soc,
		                  buf,
				  strlen(buf),
				  0,
				  (struct sockaddr *) &to,
				  tolen)) == -1) {
                    /* エラー */
                    perror("sendto");
                    end = 1;
                    break;
                }
            }
	    break;
        }
        if (end) {
            break;
        }
    }
}
int
main(int argc, char *argv[])
{
    struct addrinfo hints, *res0;
    int errcode, soc, opt;
    u_int opt2;

    if (argc <= 2) {
        (void) fprintf(stderr, "u-client-m6 bind-address bind-port\n");
        return (EX_USAGE);
    }
    /* ソケットの生成 */
    if ((soc = socket(PF_INET6, SOCK_DGRAM, 0)) == -1) {
        perror("socket");
        return (EX_UNAVAILABLE);
    }
    /* ソケットオプション（再利用フラグ）設定 */
    opt = 1;
    if (setsockopt(soc, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int)) == -1) {
        perror("setsockopt");
        return(-1);
    }
    /* IPアドレスのバインド */
    /* アドレス情報のヒントをゼロクリア */
    (void) memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_DGRAM;
    /* アドレス情報の決定 */
    if ((errcode = getaddrinfo(argv[1], argv[2], &hints, &res0)) != 0) {
        (void) fprintf(stderr, "getaddrinfo():%s\n", gai_strerror(errcode));
        return (EX_UNAVAILABLE);
    }
    /* IPアドレスのバインド */
    if (bind(soc, (struct sockaddr *) res0->ai_addr, res0->ai_addrlen) == -1) {
        perror("bind");
        freeaddrinfo(res0);
        (void) close(soc);
        return (EX_UNAVAILABLE);
    }
    freeaddrinfo(res0);
    /* マルチキャスト送信インタフェースの指定 */
    opt2 = if_addrtoindex(argv[1]);
    if (setsockopt(soc,
                   IPPROTO_IPV6,
		   IPV6_MULTICAST_IF,
		   &opt2,
		   sizeof(u_int)) == -1) {
        perror("setsockopt");
        (void) close(soc);
        return (EX_UNAVAILABLE);
    }
    /* マルチキャストループバックの指定 */
    opt2 = 1;
    if (setsockopt(soc, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &opt2, sizeof(u_int))
        == -1) {
        perror("setsockopt");
        (void) close(soc);
        return (EX_UNAVAILABLE);
    }
    /* 送受信 */
    send_recv_loop(soc);
    /* ソケットクローズ */
    (void) close(soc);
    return (EX_OK);
}

