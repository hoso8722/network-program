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
/* UDPマルチキャストサーバソケットの準備 */
int 
udp_server_socket_mcast6(const char *m_address,
                         const char *portnm,
			 const char *if_address)
{   
    char nbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
    struct addrinfo hints, *res0;
    struct ipv6_mreq mreq;
    int soc, opt, errcode;
    socklen_t opt_len;

    /* アドレス情報のヒントをゼロクリア */
    (void) memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_DGRAM;
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
    /* マルチキャストグループに参加 */
    inet_pton(AF_INET6, m_address, &mreq.ipv6mr_multiaddr);
    mreq.ipv6mr_interface = if_addrtoindex(if_address);
    if(setsockopt(soc,
                  IPPROTO_IPV6,
		  IPV6_JOIN_GROUP,
		  &mreq,
		  sizeof(mreq)) == -1){
        perror("setsockopt");
        (void) close(soc);
        freeaddrinfo(res0);
        return(-1);
    }
    /* ソケットにアドレスを指定 */
    if (bind(soc, res0->ai_addr, res0->ai_addrlen) == -1) {
        perror("bind");
        (void) close(soc);
        freeaddrinfo(res0);
        return (-1);
    }
    freeaddrinfo(res0);

   return (soc);
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
void
send_recv_loop(int soc)
{
    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
    char buf[512], *ptr;
    struct sockaddr_storage from;
    ssize_t len;
    socklen_t fromlen;
    /* 送受信 */
    for (;;) {
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
        }
        (void) getnameinfo((struct sockaddr *) &from, fromlen,
                   hbuf, sizeof(hbuf),
                   sbuf, sizeof(sbuf),
                   NI_NUMERICHOST | NI_NUMERICSERV);
        (void) fprintf(stderr, "recvfrom:%s:%s:len=%d\n", hbuf, sbuf, (int)len);
        /* 文字列化・表示 */
        buf[len] = '\0';
        if ((ptr = strpbrk(buf,"\r\n")) != NULL) {
            *ptr = '\0';
        }
        (void) fprintf(stderr, "[client]%s\n", buf);
        /* 応答文字列作成 */
        (void) mystrlcat(buf, ":OK\r\n", sizeof(buf));
        len = strlen(buf);
        /* 応答 */
        if ((len = sendto(soc,
	                  buf,
			  len,
			  0,
			  (struct sockaddr *) &from,
			  fromlen)) == -1) {
            /* エラー */
            perror("sendto");
            break;
        }
    }
}
int
main(int argc, char *argv[])
{
    struct ipv6_mreq  mreq;
    int soc;
    /* 引数にポート番号が指定されているか？ */
    if(argc<=3){
        (void) fprintf(stderr,"u-server-m6 m-address port if-address\n");
        return (EX_USAGE);
    }
    /* UDPマルチキャストサーバソケットの準備 */
    if ((soc = udp_server_socket_mcast6(argv[1], argv[2], argv[3])) == -1) {
        (void) fprintf(stderr,
	               "udp_server_socket_mcast6(%s,%s,%s):error\n",
		       argv[1],
		       argv[2],
		       argv[3]);
        return (EX_UNAVAILABLE);
    }
    (void) fprintf(stderr, "ready for recvfrom\n");
    /* 送受信 */
    send_recv_loop(soc);
    /* マルチキャストグループから脱退 */
    inet_pton(AF_INET6, argv[1], &mreq.ipv6mr_multiaddr);
    mreq.ipv6mr_interface = if_addrtoindex(argv[3]);
    if (setsockopt(soc,
                   IPPROTO_IPV6,
		   IPV6_LEAVE_GROUP,
		   &mreq,
		   sizeof(mreq)) == -1) {
        perror("setsockopt");
        (void) close(soc);
        return(EX_UNAVAILABLE);
    }
    /* ソケットクローズ */
    (void) close(soc);
    return (EX_OK);
}

