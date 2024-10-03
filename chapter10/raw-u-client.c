#include <sys/ioctl.h>  
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <linux/if.h>                   /* 追加 */

#include <arpa/inet.h>
#include <net/ethernet.h>               /* 追加 */
#include <netinet/in.h>                 /* 追加 */
#include <netinet/ip.h>                 /* 追加 */
#include <netinet/ip6.h>                /* 追加 */
#include <netinet/tcp.h>                /* 追加 */
#include <netinet/udp.h>                /* 追加 */
#include <netinet/ip_icmp.h>            /* 追加 */
#include <netinet/if_ether.h>           /* 追加 */
#include <netinet/in.h>
#include <netpacket/packet.h>           /* 追加 */
#include <netdb.h>

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h> 
#include <string.h> 
#include <sysexits.h>   
#include <unistd.h> 

#define SOURCE_ADDR "0.0.0.0"
#define DEST_ADDR   "255.255.255.255"
#define SOURCE_PORT 22223
#define DEST_PORT   22222
/* UDPパケット格納用 */
struct udp_packet {
    struct ether_header eh; 
    struct ip ip; 
    struct udphdr udp; 
    uint8_t data[256]; 
};
/* 疑似ヘッダ */
struct pseudo_header {
    uint32_t saddr;
    uint32_t daddr;
    uint8_t reserved;
    uint8_t protocol;
    uint16_t len;
};
/* 疑似UDPパケット：チェックサム計算用 */
struct pseudo_udp {
    struct pseudo_header ip; 
    struct udphdr udp;
    uint8_t data[256]; 
};

/* RAWソケットの準備 */
int
raw_socket(const char *device)
{
    struct ifreq if_req;
    struct sockaddr_ll sa;
    int soc;
    /* ソケットの生成 */
    if ((soc = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) == -1) {
        perror("socket");
        return (-1);
    }
    /* インタフェース情報の取得 */
    (void) snprintf(if_req.ifr_name, sizeof(if_req.ifr_name), "%s", device);
    if (ioctl(soc, SIOCGIFINDEX, &if_req) == -1) {
        perror("ioctl");
        (void) close(soc);
        return (-1);
    }
    /* インタフェースをbind() */
    sa.sll_family = PF_PACKET;
    sa.sll_protocol = htons(ETH_P_IP);
    sa.sll_ifindex = if_req.ifr_ifindex;
    if (bind(soc, (struct sockaddr *) &sa, sizeof(sa)) == -1) {
        perror("bind");
        (void) close(soc);
        return (-1);
    }
    /* インタフェースのフラグ取得 */
    if (ioctl(soc, SIOCGIFFLAGS, &if_req) == -1) {
        perror("ioctl");
        (void) close(soc);
        return (-1);
    }
    /* インタフェースのフラグにUPをセット */
    if_req.ifr_flags = if_req.ifr_flags|IFF_UP;
    if (ioctl(soc, SIOCSIFFLAGS, &if_req) == -1) {
        perror("ioctl");
        (void) close(soc);
        return (-1);
    }
    return(soc);
}

/* チェックサムの計算 */
uint16_t
checksum(uint8_t *data, size_t len)
{
    uint32_t sum, c;
    uint16_t val, *ptr; 
    sum = 0; 
    ptr = (uint16_t *) data; 
    for (c = len; c > 1; c -= 2) {
        sum += (*ptr); 
        if (sum & 0x80000000) {
            sum = (sum & 0xFFFF) + (sum >> 16); 
        }
        ptr++; 
    }
    if (c == 1) {
        val = 0; 
        (void) memcpy(&val, ptr, sizeof(uint8_t)); 
        sum += val; 
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16); 
    }
    if (sum == 0xFFFF) {
        return (sum);
    } else {
        return (~sum); 
    }
    /*NOT REACHED*/
    return (sum);
}

/* UDPパケットをRAWソケットから送信 */
int
send_udp_raw(int soc, const uint8_t *data, size_t size)
{
    uint8_t buf[sizeof(struct ether_header)
                + sizeof(struct ip)
		+ sizeof(struct udphdr) + 256],
            *p; 
    struct ifreq ifreq; 
    struct udp_packet udp;
    struct pseudo_udp gudp;
    size_t total;
    /* 疑似UDPパケットにデータをセット */
    (void) memset(&gudp, 0, sizeof(gudp)); 
    (void) inet_pton(AF_INET, SOURCE_ADDR, &gudp.ip.saddr);
    (void) inet_pton(AF_INET, DEST_ADDR, &gudp.ip.daddr);
    gudp.ip.reserved = 0; 
    gudp.ip.protocol = 17;     /* UDP */
    gudp.ip.len = htons(sizeof(struct udphdr) + size); 
    gudp.udp.source = htons(SOURCE_PORT); 
    gudp.udp.dest = htons(DEST_PORT); 
    gudp.udp.len = htons(sizeof(struct udphdr) + size); 
    gudp.udp.check = 0; 
    (void) memset(gudp.data, 0, sizeof(gudp.data)); 
    (void) memcpy(gudp.data, data, size); 
    /* UDPチェックサムの計算 */
    gudp.udp.check = checksum((unsigned char *) &gudp,
                              sizeof(struct pseudo_header)
			      + sizeof(struct udphdr)
			      + size); 
    /* UDPパケットに疑似UDPパケットからデータをコピー */
    (void) memset(&udp, 0, sizeof(struct udp_packet));
    (void) memcpy(&udp.udp, &gudp.udp, sizeof(struct udphdr)); 
    (void) memcpy(udp.data, gudp.data, sizeof(udp.data)); 
    /* IPヘッダにデータをセット */
    udp.ip.ip_v = 4; 
    udp.ip.ip_hl = 5; 
    udp.ip.ip_tos = 0; 
    udp.ip.ip_len = htons(sizeof(struct ip) + sizeof(struct udphdr) + size); 
    udp.ip.ip_id = 0; 
    udp.ip.ip_off = htons(0); 
    udp.ip.ip_ttl = 64; 
    udp.ip.ip_p = 17; 
    udp.ip.ip_sum = 0; 
    (void) inet_pton(AF_INET, SOURCE_ADDR, &udp.ip.ip_src.s_addr);
    (void) inet_pton(AF_INET, DEST_ADDR, &udp.ip.ip_dst.s_addr);
    udp.ip.ip_sum = checksum((uint8_t *) &udp.ip, sizeof(struct ip)); 
    /* MACアドレスの取得 */
    (void) snprintf(ifreq.ifr_name, sizeof(ifreq.ifr_name), "eth0"); 
    if (ioctl(soc, SIOCGIFHWADDR, &ifreq) == -1) {
        perror("ioctl:hwaddr");
        return (-1);
    } else {
        p = (uint8_t *) &ifreq.ifr_hwaddr.sa_data; 
        (void) printf("hwaddr = %02X:%02X:%02X:%02X:%02X:%02X\n",
	              *p,
		      *(p+1),
		      *(p+2),
		      *(p+3),
		      *(p+4),
		      *(p+5)); 
    }
    /* イーサネットヘッダにデータをセット */
    udp.eh.ether_dhost[0] = 0xFF; 
    udp.eh.ether_dhost[1] = 0xFF; 
    udp.eh.ether_dhost[2] = 0xFF; 
    udp.eh.ether_dhost[3] = 0xFF; 
    udp.eh.ether_dhost[4] = 0xFF; 
    udp.eh.ether_dhost[5] = 0xFF; 
    udp.eh.ether_shost[0] = *p; 
    udp.eh.ether_shost[1] = *(p+1); 
    udp.eh.ether_shost[2] = *(p+2); 
    udp.eh.ether_shost[3] = *(p+3); 
    udp.eh.ether_shost[4] = *(p+4); 
    udp.eh.ether_shost[5] = *(p+5); 
    udp.eh.ether_type = htons(0x800); 
    /* バッファにデータを連結してコピー */
    (void) memset(buf, 0, sizeof(buf)); 
    p = buf; 
    (void) memcpy(p,
                  &udp.eh,
		  sizeof(struct ether_header));
    p += sizeof(struct ether_header); 
    (void) memcpy(p, &udp.ip, sizeof(struct ip)); p += sizeof(struct ip); 
    (void) memcpy(p,
                  &udp.udp,
		  sizeof(struct udphdr));
    p += sizeof(struct udphdr); 
    (void) memcpy(p, udp.data, size); p += size; 
    total = p - buf; 
    /* 送信 */
    if (send(soc, buf, total, 0) == -1) {
        perror("send");
        return (-1);
    }
    return(size); 
}

/* 応答パケットのチェック */
void
check_packet(uint8_t *data, size_t len)
{
    char buf[512]; 
    struct ether_header eh;
    struct ip ip;
    struct udphdr udphdr; 
    uint8_t *ptr; 
    ptr = data;
    /* イーサネットヘッダ取得 */
    (void) memcpy(&eh, ptr, sizeof(struct ether_header)); 
    ptr += sizeof(struct ether_header); 
    len -= sizeof(struct ether_header); 
    if (ntohs(eh.ether_type) == ETHERTYPE_IP) {
        /* IPパケット */
        /* IPヘッダ取得 */
        (void) memcpy(&ip, ptr, sizeof(struct ip)); 
        ptr += sizeof(struct ip); 
        len -= sizeof(struct ip); 
        if (ip.ip_p == IPPROTO_UDP) {
               /* UDPヘッダ取得 */
               (void) memcpy(&udphdr, ptr, sizeof(struct udphdr)); 
               ptr += sizeof(struct udphdr); 
               len -= sizeof(struct udphdr); 
               if (ntohs(udphdr.dest) == SOURCE_PORT) {
                   /* 応答パケット */
                   (void) printf("recvfrom:%s:%d:len = %d\n",
		                 inet_ntoa(ip.ip_src),
				 ntohs(udphdr.source),
				 len); 
                   /* 文字列化・表示 */
                   (void) memcpy(buf, ptr, len); 
                   buf[len] = '\0'; 
                   (void) printf("> %s", buf); 
               }
        
        }
    }
}

/* 送受信処理 */
void
send_recv_loop(int soc)
{
    char buf[512];
    struct timeval timeout;
    int end, width;
    ssize_t len;
    fd_set      mask, ready;
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
                /* ソケットレディ */
                /* 受信 */
                if((len = recv(soc, buf, sizeof(buf), 0)) == -1){
                    /* エラー */
                    perror("recv"); 
                    break; 
                }
                /* 応答パケットのチェック */
                check_packet((uint8_t *) buf, len); 
            }
            /* 標準入力レディ */
            if (FD_ISSET(0, &ready)) {
                /* 標準入力から１行読み込み */
                (void) fgets(buf, sizeof(buf), stdin);
                if (feof(stdin)) {
                    end = 1;
                    break;
                }
                if ((len = send_udp_raw(soc, (uint8_t *) buf, strlen(buf))) == -1) {
                    /* エラー */
                    perror("send_udp_raw"); 
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

int
main(int argc, char *argv[])
{
    int soc; 
    if (argc <= 1){
        (void) fprintf(stderr, "raw-u-client device-name\n"); 
        return (EX_USAGE);
    }
    /* RAW-IPソケットの生成 */
    if ((soc = raw_socket(argv[1])) == -1) {
        (void) fprintf(stderr, "raw_socket(%s):error\n", argv[1]); 
        return (EX_UNAVAILABLE);
    }
    /* 送受信 */
    send_recv_loop(soc); 
    /* ソケットクローズ */
    (void) close(soc); 
    return (EX_OK);
}
