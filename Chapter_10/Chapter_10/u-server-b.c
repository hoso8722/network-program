#include	<stdio.h>
#include	<ctype.h>
#include	<unistd.h>
#include	<stdlib.h>
#include	<string.h>
#include	<errno.h>
#include	<signal.h>
#include	<sys/param.h>
#include	<sys/types.h>
#include	<sys/socket.h>
#include	<netinet/in.h>
#include	<arpa/inet.h>
#include	<netdb.h>
#include	<sys/ioctl.h>
#include	<net/if.h>

/* ブロードキャストアドレス */
char	BroadAddr[80];
/* ユニキャストアドレス */
struct in_addr	MyAddr;

/* UDPサーバソケットの準備 */
int UDPServerSocket(char *portnm)
{
int		soc,portno,opt;
struct servent	  *se;
struct sockaddr_in	  my;
    /* アドレス情報をゼロクリア */
    memset((char *)&my,0,sizeof(my));
    my.sin_family=AF_INET;
    my.sin_addr.s_addr=htonl(INADDR_ANY);
    /* ポート番号の決定 */
    if(isdigit(portnm[0])){
        /* 先頭が数字 */
        if((portno=atoi(portnm))<=0){
            /* 数値化するとゼロ以下 */
            fprintf(stderr,"bad port no\n");
            return(-1);
        }
        my.sin_port=htons(portno);
    }
    else{
        if((se=getservbyname(portnm,"udp"))==NULL){
            /* サービスに見つからない */
            fprintf(stderr,"getservbyname():error\n");
            return(-1);
        }
        else{
            /* サービスに見つかった:該当ポート番号 */
            my.sin_port=se->s_port;
        }
    }
    fprintf(stderr,"port=%d\n",ntohs(my.sin_port));
    /* ソケットの生成 */
    if((soc=socket(AF_INET,SOCK_DGRAM,0))<0){
        perror("socket");
        return(-1);
    }
    /* ソケットオプション（再利用フラグ）設定 */
    opt=1;
    if(setsockopt(soc,SOL_SOCKET,SO_REUSEADDR,(char *)&opt,sizeof(int))!=0){
        perror("setsockopt");
        return(-1);
    }
    /* ソケットにアドレスを指定 */
    if(bind(soc,(struct sockaddr *)&my,sizeof(my))==-1){
        perror("bind");
        return(-1);
    }
    return(soc);
}

/* アドレス情報の取得 */
int GetAddress(char *hostnm,char *portnm,struct sockaddr_in *saddr,int *saddr_len)
{
struct in_addr	  addr;
struct hostent	  *host;
struct sockaddr_in	  server;
struct servent	  *se;
int		soc,portno;
    /* アドレス情報をゼロクリア */
    memset((char *)&server,0,sizeof(server));
    server.sin_family=AF_INET;
    /* ホスト名がIPアドレスと仮定してホスト情報取得 */
    if((addr.s_addr=inet_addr(hostnm))==-1){
        /* ホスト名が名称としてホスト情報取得 */
        host=gethostbyname(hostnm);
        if(host==NULL){
            /* ホストが見つからない */
            perror("gethostbyname");
            return(-1);
        }
        memcpy(&addr,(struct in_addr *)*host->h_addr_list,sizeof(struct in_addr));
    }
    fprintf(stderr,"addr=%s\n",inet_ntoa(addr));
    /* ホストアドレスのセット */
    server.sin_addr=addr;
    /* ポート番号の決定 */
    if(isdigit(portnm[0])){
        /* 先頭が数字 */
        if((portno=atoi(portnm))<=0){
            /* 数値化するとゼロ以下 */
            fprintf(stderr,"bad port no\n");
            return(-1);
        }
        server.sin_port=htons(portno);
    }
    else{
        if((se=getservbyname(portnm,"udp"))==NULL){
            /* サービスに見つからない */
            fprintf(stderr,"getservbyname():error\n");
            return(-1);
        }
        else{
            /* サービスに見つかった:該当ポート番号 */
            server.sin_port=se->s_port;
        }
    }
    fprintf(stderr,"port=%d\n",ntohs(server.sin_port));
    memcpy(saddr,&server,sizeof(struct sockaddr_in));
    *saddr_len=sizeof(struct sockaddr_in);
    return(soc);
}

/* 送受信 */
int SendRecvLoop(int soc)
{
int		len;
struct sockaddr_in	  from,to;
char	buf[512],*ptr;
char	port[80];
int		fromlen,tolen;
    /* 送受信 */
    while(1){
        /* 受信 */
        fromlen=sizeof(from);
        if((len=recvfrom(soc,buf,sizeof(buf),0,(struct sockaddr *)&from,&fromlen))<0){
            /* エラー */
            perror("recvfrom");
        }
        printf("recvfrom:%s:%d:len=%d\n",inet_ntoa(from.sin_addr),
        ntohs(from.sin_port),len);
        if(from.sin_addr.s_addr==MyAddr.s_addr){
                printf("From oneself:ignore\n");
                continue;
        }
        /* 文字列化・表示 */
        buf[len]='\0';
        if((ptr=strpbrk(buf,"\r\n"))!=NULL){
            *ptr='\0';
        }
        printf("[client]%s\n",buf);
        /* 応答文字列作成 */
        strcat(buf,":OK\r\n");
        len=strlen(buf);
        sprintf(port,"%d",ntohs(from.sin_port));
        /* 応答アドレス情報の取得 */
        if(GetAddress(BroadAddr,port,&to,&tolen)==-1){
            fprintf(stderr,"GetAddress():error\n");
            break;
        }
        /* 応答 */
        if((len=sendto(soc,buf,len,0,(struct sockaddr *)&to,tolen))<0){
            /* エラー */
            perror("sendto");
            break;
        }
    }
    return(0);
}

int main(int argc,char *argv[])
{
int	   soc;
int	   on;
struct ifreq	ifreq;
struct sockaddr_in		addr;
    /* 引数にポート番号・インタフェース名が指定されているか？ */
    if(argc<=2){
        fprintf(stderr,"u-server-b port if-name\n");
        return(1);
    }
    /* UDPサーバソケットの準備 */
    soc=UDPServerSocket(argv[1]);
    if(soc==-1){
        fprintf(stderr,"UDPServerSocket(%s):error\n",argv[1]);
        return(-1);
    }
    /* ブロードキャストの許可 */
    on=1;
    if(setsockopt(soc,SOL_SOCKET,SO_BROADCAST,&on,sizeof(on))==-1){
        perror("setsockopt");
        close(soc);
        return(-1);
    }
    /* ブロードキャストアドレスの調査 */
    strcpy(ifreq.ifr_name,argv[2]);
    if(ioctl(soc,SIOCGIFBRDADDR,&ifreq)==-1){
        perror("ioctl:broadaddr");
        close(soc);
        return(-1);
    }
    else{
        memcpy(&addr,&ifreq.ifr_broadaddr,sizeof(struct sockaddr_in));
        strcpy(BroadAddr,inet_ntoa(addr.sin_addr));
        printf("%s:BroadAddr=%s\n",argv[2],BroadAddr);
    }
    /* ユニキャストアドレスの取得・表示 */
    if(ioctl(soc,SIOCGIFADDR,&ifreq)==-1){
        perror("ioctl:addr");
    }
    else if(ifreq.ifr_addr.sa_family!=AF_INET){
        /* IPv4以外 */
        printf("%s:not AF_INET\n",argv[2]);
        close(soc);
        return(-1);
    }
    else{
        memcpy(&addr,&ifreq.ifr_addr,sizeof(struct sockaddr_in));
        MyAddr=addr.sin_addr;
        printf("%s:MyAddr=%s\n",argv[2],inet_ntoa(MyAddr));
    }
    fprintf(stderr,"ready for recvfrom\n");
    /* 送受信 */
    SendRecvLoop(soc);
    /* ソケットクローズ */
    close(soc);
    return(0);
}
