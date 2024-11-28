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

// ソケットを生成
// エラーの場合は−１を返す

int server_socket(const char *portnm) {
	// #define NI_MAXHOST 1025
	// #define NI_MAXSERV 32
	char nbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
	// struct addrinfo {
	// 	int	ai_flags;	/* AI_PASSIVE, AI_CANONNAME, AI_NUMERICHOST */
	// 	int	ai_family;	/* PF_xxx */
	// 	int	ai_socktype;	/* SOCK_xxx */
	// 	int	ai_protocol;	/* 0 or IPPROTO_xxx for IPv4 and IPv6 */
	// 	socklen_t ai_addrlen;	/* length of ai_addr */
	// 	char	*ai_canonname;	/* canonical name for hostname */
	// 	struct	sockaddr *ai_addr;	/* binary address */
	// 	struct	addrinfo *ai_next;	/* next structure in linked list */
	// };
	struct addrinfo hints, *res0;
	int soc, opt, errcode; 
	socklen_t opt_len; //unsigned int

  	/* アドレス情報のヒントをゼロクリア */
		// memset(埋める場所, 値, サイズ)
	(void)memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET; // IP
	hints.ai_socktype = SOCK_STREAM; // TCP
	hints.ai_flags = AI_PASSIVE; // Server_socket

	/* アドレス情報の決定 */
	/* getaddinfo()にアドレス情報のヒントを与えることでsockaddr構造体を得る関数
		 1st arg -> IPaddress, hostname or NULL(ワイルドカードを指定どのインターフェースからでも受け入れるlocalhost,127.0.0.1)
		 2nd arg -> port-number or service-name(cat /etc/services) or NULL
		 3rd arg -> ヒントとなる情報が格納されたaddrinfo型構造体のポインタを渡す
		 4th arg -> 決定したアドレス情報を格納したaddrinfo型構造体を参照するためのポインタを渡す

	*/
	if ((errcode = getaddrinfo(NULL, portnm, &hints, &res0)) != 0)
	{
		(void)fprintf(stderr, "getaddrinfo():%s\n", gai_strerror(errcode));
		return (-1);
	}

	if ((errcode = getnameinfo(res0->ai_addr, res0->ai_addrlen,
							   nbuf, sizeof(nbuf),
							   sbuf, sizeof(sbuf),
							   NI_NUMERICHOST | NI_NUMERICSERV)) != 0)
	{
		(void)fprintf(stderr, "getnameinfo():%s\n", gai_strerror(errcode));
		freeaddrinfo(res0);
		return (-1);
	}
	(void)fprintf(stderr, "port=%s\n", sbuf);
	/* ソケットの生成 */
	if ((soc = socket(res0->ai_family, res0->ai_socktype, res0->ai_protocol)) == -1)
	{
		perror("socket");
		freeaddrinfo(res0);
		return (-1);
	}
	/* ソケットオプション（再利用フラグ）設定 */
	opt = 1;
	opt_len = sizeof(opt);
	if (setsockopt(soc, SOL_SOCKET, SO_REUSEADDR, &opt, opt_len) == -1)
	{
		perror("setsockopt");
		(void)close(soc);
		freeaddrinfo(res0);
		return (-1);
	}
	/* ソケットにアドレスを指定 */
	if (bind(soc, res0->ai_addr, res0->ai_addrlen) == -1)
	{
		perror("bind");
		(void)close(soc);
		freeaddrinfo(res0);
		return (-1);
	}
	/* アクセスバックログの指定 */
	if (listen(soc, SOMAXCONN) == -1)
	{
		perror("listen");
		(void)close(soc);
		freeaddrinfo(res0);
		return (-1);
	}
	freeaddrinfo(res0);
	return (soc);

}

/* アクセプトループ */
void accept_loop(int soc)
{
	char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
	struct sockaddr_storage from;
	int acc;
	socklen_t len;
	for (;;)
	{
		len = (socklen_t)sizeof(from);
		/* 接続受付 */
		if ((acc = accept(soc, (struct sockaddr *)&from, &len)) == -1)
		{
			if (errno != EINTR)
			{
				perror("accept");
			}
		}
		else
		{
			(void)getnameinfo((struct sockaddr *)&from, len,
							  hbuf, sizeof(hbuf),
							  sbuf, sizeof(sbuf),
							  NI_NUMERICHOST | NI_NUMERICSERV);
			(void)fprintf(stderr, "accept:%s:%s\n", hbuf, sbuf);
			/* 送受信ループ */
			void send_recv_loop(int acc);
			/* アクセプトソケットクローズ */
			(void)close(acc);
			acc = 0;
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

	for (pd = dst, lest = size; *pd != '\0' && lest != 0; pd++, lest--)
		;
	dlen = pd - dst;
	if (size - dlen == 0)
	{
		return (dlen + strlen(src));
	}
	pde = dst + size - 1;
	for (ps = src; *ps != '\0' && pd < pde; pd++, ps++)
	{
		*pd = *ps;
	}
	for (; pd <= pde; pd++)
	{
		*pd = '\0';
	}
	while (*ps++)
		;
	return (dlen + (ps - src - 1));
}

/* 送受信ループ */
void send_recv_loop(int acc)
{
	char buf[512], *ptr;
	ssize_t len;
	for (;;)
	{
		/* 受信 */
		if ((len = recv(acc, buf, sizeof(buf), 0)) == -1)
		{
			/* エラー */
			perror("recv");
			break;
		}
		if (len == 0)
		{
			/* エンド・オブ・ファイル */
			(void)fprintf(stderr, "recv:EOF\n");
			break;
		}
		/* 文字列化・表示 */
		buf[len] = '\0';
		if ((ptr = strpbrk(buf, "\r\n")) != NULL)
		{
			*ptr = '\0';
		}
		(void)fprintf(stderr, "[client]%s\n", buf);
		/* 応答文字列作成 */
		(void)mystrlcat(buf, ":OK\r\n", sizeof(buf));
		len = (ssize_t)strlen(buf);
		/* 応答 */
		if ((len = send(acc, buf, (size_t)len, 0)) == -1)
		{
			/* エラー */
			perror("send");
			break;
		}
	}
}
int main(int argc, char *argv[])
{
	int soc;
	/* 引数にポート番号が指定されているか？ */
	if (argc <= 1)
	{
		(void)fprintf(stderr, "server port\n");
		return (EX_USAGE);
	}
	/* サーバソケットの準備 */
	if ((soc = server_socket(argv[1])) == -1)
	{
		(void)fprintf(stderr, "server_socket(%s):error\n", argv[1]);
		return (EX_UNAVAILABLE);
	}
	(void)fprintf(stderr, "ready for accept\n");
	/* アクセプトループ */
	accept_loop(soc);
	/* ソケットクローズ */
	(void)close(soc);
	return (EX_OK);
}
