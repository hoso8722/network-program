#include <sys/ioctl.h>                  /* 追加 */
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
/* 指定した名前のインタフェース情報の表示 */
int
show_ifreq(int soc, const char *name)
{
    char addr_str[256];
    struct ifreq ifreq;
    int i;
    struct ifaddrs *ifaddrs;
    struct ifaddrs *ifa;
    uint8_t *p;

    /* ifreq構造体に名前をセット */
    (void) snprintf(ifreq.ifr_name, sizeof(ifreq.ifr_name), "%s", name);
    /* フラグの取得・表示 */
    if (ioctl(soc, SIOCGIFFLAGS, &ifreq) == -1) {
        perror("ioctl:SIOCGIFFLAGS");
        return (-1);
    }
    if (ifreq.ifr_flags & IFF_UP)	   (void) fprintf(stdout, "UP ");
    if (ifreq.ifr_flags & IFF_BROADCAST)   (void) fprintf(stdout, "BROADCAST ");
    if (ifreq.ifr_flags & IFF_PROMISC)	   (void) fprintf(stdout, "PROMISC ");
    if (ifreq.ifr_flags & IFF_MULTICAST)   (void) fprintf(stdout, "MULTICAST ");
    if (ifreq.ifr_flags & IFF_LOOPBACK)	   (void) fprintf(stdout, "LOOPBACK ");
    if (ifreq.ifr_flags & IFF_POINTOPOINT) (void) fprintf(stdout, "P2P ");
    (void) fprintf(stdout, "\n");
    /* MTUの取得・表示 */
    if (ioctl(soc, SIOCGIFMTU, &ifreq) == -1) {
        perror("ioctl:SIOCGIFMTU");
    } else {
        (void) fprintf(stdout, "mtu=%d\n", ifreq.ifr_mtu);
    }
    /* getifaddrs()でインタフェースのアドレス一覧をifaddrsに取得 */
    if (getifaddrs(&ifaddrs) == -1) {
        perror("getifaddrs");
        return (-1);
    }
    /* ifaddrsに取得したアドレス一覧のリンクドリストを辿る */
    for (i = 0, ifa = ifaddrs; ifa != NULL; ifa = ifa->ifa_next) {
        /*
	 * getifaddrs()ではアドレスを持つインタフェース全ての情報が取得するため
	 * 指定されたインタフェース名以外は除外する
	 */
        if (strcmp(ifa->ifa_name, name) != 0) {
            continue;
        }
        if (ifa->ifa_addr == NULL) {
            continue;
        }
        if (ifa->ifa_addr->sa_family == AF_INET) {
            /* IPv4 の場合 */
            /* ユニキャストアドレスの取得・表示 */
            (void) fprintf(stdout, "addr[%d]=%s\n", i, inet_ntop(AF_INET,
	                   &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr,
			   addr_str,
			   sizeof(addr_str)));
            /* ポイント‐ポイントアドレスの取得・表示 */
            if (ifa->ifa_ifu.ifu_dstaddr)
                (void) fprintf(stdout, "dstaddr[%d]=%s\n", i, inet_ntop(AF_INET,
		    &((struct sockaddr_in *)ifa->ifa_ifu.ifu_dstaddr)->sin_addr,
	            addr_str,
		    sizeof(addr_str)));
            /* ブロードキャストアドレスの取得・表示 */
            if (ifa->ifa_ifu.ifu_broadaddr)
              (void) fprintf(stdout, "broadaddr[%d]=%s\n", i, inet_ntop(AF_INET,
		  &((struct sockaddr_in *)ifa->ifa_ifu.ifu_broadaddr)->sin_addr,
		  addr_str,
                  sizeof(addr_str)));
            /* ネットマスクの取得・表示 */
            (void) fprintf(stdout, "netmask[%d]=%s\n", i, inet_ntop(AF_INET,
	                   &((struct sockaddr_in *)ifa->ifa_netmask)->sin_addr,
			   addr_str,
			   sizeof(addr_str)));
        } else if (ifa->ifa_addr->sa_family == AF_INET6) {
            /* IPv6 の場合 */
            /* ユニキャストアドレスの取得・表示 */
            (void) fprintf(stdout, "addr6[%d]=%s\n", i, inet_ntop(AF_INET6,
	                   &((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr,
			   addr_str,
			   sizeof(addr_str)));
            /* ポイント‐ポイントアドレスの取得・表示 */
            if (ifa->ifa_ifu.ifu_dstaddr)
              (void) fprintf(stdout, "dstaddr6[%d]=%s\n", i, inet_ntop(AF_INET6,
		  &((struct sockaddr_in6 *)ifa->ifa_ifu.ifu_dstaddr)->sin6_addr,
	          addr_str,
	          sizeof(addr_str)));
            /* ネットマスクの取得・表示 */
            (void) fprintf(stdout, "netmask6[%d]=%s\n", i, inet_ntop(AF_INET6,
	                  &((struct sockaddr_in6 *)ifa->ifa_netmask)->sin6_addr,
			  addr_str, sizeof(addr_str)));
        } else {
            continue;
        }
        i++;
    }
    /* getifaddrs()が確保したメモリを解放 */
    freeifaddrs(ifaddrs);
    /* MACアドレスの取得・表示 */
    if (ioctl(soc, SIOCGIFHWADDR, &ifreq) == -1) {
       perror("ioctl:hwaddr");
    } else {
       p = (uint8_t *) &ifreq.ifr_hwaddr.sa_data;
       (void) fprintf(stdout,
                      "hwaddr=%02X:%02X:%02X:%02X:%02X:%02X\n",
		      *p,
		      *(p+1),
		      *(p+2),
		      *(p+3),
		      *(p+4),
		      *(p+5));
    }
    return (0);
}
/* 起動中の全インタフェース情報の表示 */
int
show_if(int soc)
{
    struct ifconf ifc;
    int i, if_count;
    /* 空のバッファでサイズのみ取得 */
    ifc.ifc_len = 0;
    ifc.ifc_buf = NULL;
    if (ioctl(soc, SIOCGIFCONF, &ifc) == -1) {
        perror("SIOCGIFCONF ioctl failed");
        return (-1);
    }
    (void) fprintf(stdout, "ifcl=%d\n", ifc.ifc_len);
    /* バッファ確保 */
    if ((ifc.ifc_buf = malloc(ifc.ifc_len)) == NULL) {
        perror("malloc");
        return (-1);
    }
    /* 情報の取得 */
    if (ioctl(soc, SIOCGIFCONF, &ifc) == -1) {
        perror("SIOCGIFCONF ioctl failed");
        free(ifc.ifc_req);
        return (-1);
    }
    /* 情報数の計算 */
    if_count = ifc.ifc_len / sizeof(struct ifreq);
    (void) fprintf(stdout, "if_count=%d\n", if_count);
    (void) fprintf(stdout, "\n");
    /* 個々の情報の表示 */
    for (i = 0; i < if_count; i++) {
        if (ifc.ifc_req[i].ifr_name == NULL) {
            /* 名前が無い */
            (void) fprintf(stdout, "ifr_name=null\n");
        } else {
            (void) fprintf(stdout, "ifr_name=%s\n", ifc.ifc_req[i].ifr_name);
            if (ifc.ifc_req[i].ifr_addr.sa_family
	        != AF_INET && ifc.ifc_req[i].ifr_addr.sa_family != AF_INET6) {
                /* IP以外 */
                (void) fprintf(stdout, "not IP\n");
            } else {
                /* 指定した名前のインタフェース情報の表示 */
                (void) show_ifreq(soc, ifc.ifc_req[i].ifr_name);
            }
        }
        (void) fprintf(stdout, "\n");
    }
    /* バッファ解放 */
    free(ifc.ifc_req);
    return (0);
}
int
main(int argc, char *argv[])
{
    int	i, soc;
    /* ソケット生成 */
    if ((soc = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        return (EX_OSERR);
    }
    if (argc <= 1) {
        /* 起動中の全インタフェース情報の表示 */
        (void) show_if(soc);
    } else {
        for (i = 1; i < argc; i++) {
            (void) printf("name=%s\n", argv[i]);
            /* 指定した名前のインタフェース情報の表示 */
            (void) show_ifreq(soc, argv[i]);
            (void) fprintf(stdout, "\n");
        }
    }
    /* ソケットクローズ */
    (void) close(soc);
    return (EX_OK);
}

