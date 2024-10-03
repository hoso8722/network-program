#include <sys/epoll.h>                  /* 追加 */
#include <sys/ioctl.h>                  /* 追加 */
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>                      /* 追加 */
#include <poll.h>                       /* 追加 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <time.h>                       /* 追加 */
#include <unistd.h>
/* 受信タイムアウト時間 */
#define TIMEOUT_SEC     (10)
/* モード：n:ノンブロッキング, s:select(), p:poll(), e:EPOLL, i:ioctl(), o:setsockopt() */
char g_mode=' ';
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
            send_recv_loop(acc);
            /* アクセプトソケットクローズ */
            (void) close(acc);
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
/* ブロッキングモードのセット */
int
set_block(int fd, int flag)
{
    int flags;

    if ((flags = fcntl(fd, F_GETFL, 0)) == -1) {
        perror("fcntl");
        return (-1);
    }
    if (flag == 0) {
        /* ノンブロッキング */
        (void) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    } else if (flag == 1) {
        /* ブロッキング */
        (void) fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    }
    return (0);
}
/* タイムアウト付き受信(ノンブロッキング) */
ssize_t
recv_with_timeout_by_nonblocking(int soc, char *buf, size_t bufsize, int flag)
{
    int end;
    ssize_t len, rv;
    time_t start_time;
    /* ノンブロッキングモード */
    set_block(soc, 0);
    start_time = time(NULL);
    do {
        end = 0;
        if (time(NULL) - start_time > TIMEOUT_SEC) {
            (void) fprintf(stderr, "Timeout\n");
            rv = -1;
            end = 1;
        }
        if ((len = recv(soc, buf, bufsize, flag)) == -1) {
            /* エラー */
            if (errno == EAGAIN) {
                (void) fprintf(stderr, ".");
                (void) usleep(100000);
            } else {
                perror("recv");
                rv = -1;
                end = 1;
            }
        } else {
            rv = len;
            end = 1;
        }
    } while(end == 0);
    /* ブロッキングモード */
    (void) set_block(soc, 1);
    return (rv);
}
/* タイムアウト付き受信(select) */
ssize_t
recv_with_timeout_by_select(int soc, char *buf, size_t bufsize, int flag)
{
    struct timeval timeout;
    fd_set mask;
    int width, end;
    ssize_t len, rv;
    /* select()用マスクの作成 */
    FD_ZERO(&mask);
    FD_SET(soc,&mask);
    width = soc + 1;
    timeout.tv_sec = TIMEOUT_SEC;
    timeout.tv_usec = 0;
    do {
        end = 0;
        switch (select(width, &mask, NULL, NULL, &timeout)) {
            case -1:
                if (errno != EINTR) {
                    perror("select");
                    rv = -1;
                    end = 1;
                }
                break;
            case 0:
                (void) fprintf(stderr,"Timeout\n");
                rv = -1;
                end = 1;
            default:
                if (FD_ISSET(soc, &mask)) {
                    if ((len = recv(soc, buf, bufsize, flag)) == -1) {
                        perror("recv");
                        rv = -1;
                        end = 1;
                    } else {
                        rv = len;
                        end = 1;
                    }
                }
                break;
        }
    } while(end == 0);
    return(rv);
}
/* タイムアウト付き受信(poll) */
ssize_t
recv_with_timeout_by_poll(int soc, char *buf, size_t bufsize, int flag)
{
    struct pollfd targets[1];
    int nready, end;
    ssize_t len, rv;
    /* poll()用データの作成 */
    targets[0].fd = soc;
    targets[0].events = POLLIN;
    do {
        end = 0;
        switch ((nready=poll(targets, 1, TIMEOUT_SEC * 1000))) {
            case -1:
                if (errno != EINTR) {
                    perror("poll");
                    rv = -1;
                    end = 1;
                }
                break;
            case 0:
                (void) fprintf(stderr, "Timeout\n");
                rv = -1;
                end = 1;
                break;
            default:
                if (targets[0].revents&(POLLIN|POLLERR)) {
                    if ((len = recv(soc,buf,bufsize,flag)) == -1) {
                        perror("recv");
                        rv = -1;
                        end = 1;
                    } else {
                        rv = len;
                        end = 1;
                    }
                }
                break;
        }
    } while(end == 0);
    return(rv);
}
/* タイムアウト付き受信(EPOLL) */
ssize_t
recv_with_timeout_by_epoll(int soc, char *buf, size_t bufsize, int flag)
{
    struct epoll_event ev, event;
    int nfds, end, epollfd;
    ssize_t len, rv;
    /* EPOLL用データの作成 */
    if ((epollfd = epoll_create(1)) == -1) {
        perror("epoll_create");
        return (-1);
    }
    ev.data.fd = soc;
    ev.events = EPOLLIN;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, soc, &ev) == -1) {
        perror("epoll_ctl");
        (void) close(epollfd);
        return (-1);
    }
    do {
        end = 0;
        switch ((nfds = epoll_wait(epollfd, &event, 1, TIMEOUT_SEC * 1000))) {
            case -1:
                if (errno != EINTR) {
                    perror("epoll");
                    rv = -1;
                    end = 1;
                }
                break;
            case 0:
                (void) fprintf(stderr, "Timeout\n");
                rv = -1;
                end = 1;
                break;
            default:
                if (event.events&(EPOLLIN|EPOLLERR)) {
                    if ((len = recv(soc,buf,bufsize,flag)) == -1) {
                        perror("recv");
                        rv = -1;
                        end = 1;
                    } else {
                        rv = len;
                        end = 1;
                    }
                }
                break;
        }
    } while(end == 0);
    if (epoll_ctl(epollfd, EPOLL_CTL_DEL, soc, &ev) == -1) {
        perror("epoll_ctl");
    }   
    (void) close(epollfd);
    return(rv);
}
/* タイムアウト付き受信(ioctl) */
ssize_t
recv_with_timeout_by_ioctl(int soc, char *buf, size_t bufsize, int flag)
{
    int end;
    ssize_t len, rv, nread;
    time_t start_time;
    start_time = time(NULL);
    do {
        end = 0;
        if (time(NULL) - start_time > TIMEOUT_SEC) {
            (void) fprintf(stderr, "Timeout\n");
            rv = -1;
            end = 1;
        } else {
            if (ioctl(soc, FIONREAD, &nread) == -1) {
                perror("ioctl");
                rv = -1;
                end = 1;
            }
            if (nread <= 0) {
                (void) fprintf(stderr, ".");
                (void) usleep(100000);
            } else {
                if ((len = recv(soc, buf, bufsize, flag)) == -1) {
                    perror("recv");
                    rv = -1;
                    end = 1;
                } else {
                    rv = len;
                    end = 1;
                }
            }
        }
    } while(end==0);
    return(rv);
}
/* タイムアウト付き受信(setsockopt) */
ssize_t
recv_with_timeout_by_setsockopt(int soc, char *buf, size_t bufsize, int flag)
{
    struct timeval tv;
    int end;
    ssize_t len;
    tv.tv_sec = TIMEOUT_SEC;
    tv.tv_usec = 0;
    if (setsockopt(soc, SOL_SOCKET, SO_RCVTIMEO, (char *) &tv, sizeof(tv))==-1) {
        perror("setsockopt");
        return (-1);
    }
    do {
        end = 0;
        if ((len = recv(soc, buf, bufsize, flag)) == -1) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                (void) fprintf(stderr, "Timeout\n");
            } else {
                perror("recv");
            }
            len = -1;
            end = 1;
        } else {
            end = 1;
        }
    } while (end == 0);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    if (setsockopt(soc, SOL_SOCKET, SO_RCVTIMEO, (char *) &tv, sizeof(tv))==-1) {
        perror("setsockopt");
    }
    return (len);
}
/* タイムアウト付き受信 */
ssize_t
recv_with_timeout(int soc, char *buf, size_t bufsize, int flag)
{
    switch (g_mode) {
    case 'n':
        return (recv_with_timeout_by_nonblocking(soc, buf, bufsize, flag));
        /*NOT REACHED*/
        break;
    case 's':
        return (recv_with_timeout_by_select(soc, buf, bufsize, flag));
        /*NOT REACHED*/
        break;
    case 'p':
        return (recv_with_timeout_by_poll(soc, buf, bufsize, flag));
        /*NOT REACHED*/
        break;
    case 'e':
        return (recv_with_timeout_by_epoll(soc, buf, bufsize, flag));
        /*NOT REACHED*/
        break;
    case 'i':
        return (recv_with_timeout_by_ioctl(soc, buf, bufsize, flag));
        /*NOT REACHED*/
        break;
    case 'o':
        return (recv_with_timeout_by_setsockopt(soc, buf, bufsize, flag));
        /*NOT REACHED*/
        break;
    default:
        return (-1);
        /*NOT REACHED*/
        break;
    }
    /*NOT REACHED*/
    return (-1);
}
/* 送受信ループ */
void
send_recv_loop(int acc)
{
    char buf[512], *ptr;
    ssize_t len;
    for (;;) {
        /* 受信 */
        if ((len = recv_with_timeout(acc, buf, sizeof(buf), 0)) == -1) {
            /* エラー */
            (void) fprintf(stderr, "recv:ERROR\n");
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
    int soc;
    /* 引数にポート番号が指定されているか？ */
    if (argc <= 2) {
        (void) fprintf(stderr, "timeout port <[N]onblocking/[S]elect/[P]oll/[E]POLL/[I]octl/setsock[O]pt>\n");
        return (EX_USAGE);
    }
    /* モードオプションの判定 */
    if (toupper(argv[2][0]) == 'N'){
        (void) fprintf(stderr,"Nonblocking mode\n");
        g_mode='n';
    } else if (toupper(argv[2][0]) == 'S') {
        (void) fprintf(stderr,"Select mode\n");
        g_mode='s';
    } else if (toupper(argv[2][0])=='P') {
        (void) fprintf(stderr,"Poll mode\n");
        g_mode='p';
    } else if (toupper(argv[2][0])=='E') {
        (void) fprintf(stderr,"EPOLL mode\n");
        g_mode='e';
    } else if (toupper(argv[2][0])=='I') {
        (void) fprintf(stderr,"ioctl mode\n");
        g_mode='i';
    } else if (toupper(argv[2][0])=='O') {
        (void) fprintf(stderr,"setsockopt mode\n");
        g_mode='o';
    } else {
        (void) fprintf(stderr, "mode error (%s)\n",argv[2]);
        return (EX_USAGE);
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

