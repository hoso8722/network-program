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
/* getaddrinfo() の情報に基づき作るソケットの最大数 */
#define MAX_SOCKET      8
/* サーバソケットの準備 */
int
server_socket_afindepend(const char *portnm, int *soc, int len)
{
    char nbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
    struct addrinfo hints, *res0, *res;
    int i, opt, errcode;
    socklen_t opt_len;

    /* アドレス情報のヒントをゼロクリア */
    (void) memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;	/* アドレスファミリに依存させない */
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((errcode = getaddrinfo(NULL, portnm, &hints, &res0)) != 0) {
        (void) fprintf(stderr, "getaddrinfo():%s\n", gai_strerror(errcode));
        return (-1);
    }
    i = 0;
    for (res = res0; res && i < len; res = res->ai_next) {
        if ((errcode = getnameinfo(res->ai_addr, res->ai_addrlen,
				   nbuf, sizeof(nbuf),
				   sbuf, sizeof(sbuf),
				   NI_NUMERICHOST | NI_NUMERICSERV)) != 0) {
		(void) fprintf(stderr,
                               "getnameinfo():%s\n",
			       gai_strerror(errcode));
		freeaddrinfo(res0);
		return (-1);
	}
	(void) fprintf(stderr, "addr=%s\n", nbuf);
	(void) fprintf(stderr, "port=%s\n", sbuf);
	/* ソケットの生成 */
	if ((soc[i] = socket(res->ai_family, res->ai_socktype, res->ai_protocol))
	    == -1) {
		perror("socket");
		/* ソケットの生成に失敗しても、他にアドレス情報がある可能性があるので継続 */
		continue;
	}
	/* ソケットオプション（再利用フラグ）設定 */
	opt = 1;
	opt_len = sizeof(opt);
	if (setsockopt(soc[i], SOL_SOCKET, SO_REUSEADDR, &opt, opt_len) == -1) {
		perror("setsockopt");
		(void) close(soc[i]);
		continue;
	}
	/* ソケットにアドレスを指定 */
	if (bind(soc[i], res->ai_addr, res->ai_addrlen) == -1) {
		perror("bind");
		(void) close(soc[i]);
		continue;
	}
	/* アクセスバックログの指定 */
	if (listen(soc[i], SOMAXCONN) == -1) {
		perror("listen");
		(void) close(soc[i]);
		continue;
	}
	i++;
    }
    if (i == 0) {
	    (void) fprintf(stderr, "no socket.\n");
	    return (-1);
    }
    freeaddrinfo(res0);
    return (i);
}
void
accept_loop(int *soc, int soc_len)
{
    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
    struct sockaddr_storage from;
    struct timeval timeout;
    int i, acc, width;
    socklen_t len;
    fd_set mask;

    for (;;) {
        /* select()用マスクの作成 */
        FD_ZERO(&mask);
	for (i = 0; i < soc_len; i++) {
	    FD_SET(soc[i], &mask);
	}
        width = soc[soc_len - 1] + 1;
        /* select()用タイムアウト値のセット */
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;
        switch (select(width, (fd_set *) &mask, NULL, NULL, &timeout)) {
            /* エラー */
            perror("select");
            break;
        case 0:
            /* タイムアウト */
            break;
        default:
            /* レディ有り */
            for (i = 0; i < soc_len; i++) {
                if (FD_ISSET(soc[i], &mask)) {
		    len = (socklen_t) sizeof(from);
		    /* 接続受付 */
		    if ((acc = accept(soc[i], (struct sockaddr *) &from, &len)) == -1) {
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
			send_recv_loop(acc);
			/* アクセプトソケットクローズ */
			close(acc);
			acc = 0;
		    }
		}
	    }
	    break;
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

/* 送受信ループ */
void
send_recv_loop(int acc)
{
    char buf[512], *ptr;
    ssize_t len;
    for (;;) {
        /* 受信 */
        if ((len = recv(acc, buf, sizeof(buf), 0)) == -1) {
            /* エラー */
            perror("recv");
            break;
        }
        if (len == 0) {
            /* エンド・オブ・ファイル */
            (void) fprintf(stderr, "recv:EOF\n");
            break;
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
            break;
        }
    }
}
int
main(int argc, char *argv[])
{
    int soc[MAX_SOCKET], soc_len, i;
    /* 引数にポート番号が指定されているか？ */
    if (argc <= 1) {
        (void) fprintf(stderr,"server-afindepend port\n");
        return (EX_USAGE);
    }
    /* サーバソケットの準備 */
    (void) memset(soc, 0, sizeof(int) * MAX_SOCKET);
    if ((soc_len = server_socket_afindepend(argv[1], soc, MAX_SOCKET)) == -1) {
        (void) fprintf(stderr,
		       "server_socket_afindepend(%s,%p,%d):error\n",
		       argv[1],
		       soc,
		       MAX_SOCKET);
        return (EX_UNAVAILABLE);
    }
    (void) fprintf(stderr, "ready for accept\n");
    /* アクセプトループ */
    accept_loop(soc, soc_len);
    /* ソケットクローズ */
    for (i = 0; i < soc_len; i++) {
        (void) close(soc[i]);
    }
    return (EX_OK);
}

