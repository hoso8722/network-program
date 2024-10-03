#include <sys/param.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#define SEND_COUNT      50
#define LOOP_NO         50
#define NUM_CHILD       1000

/* 子スレッド情報 */
struct {
    pthread_t thread_id;
    int active;
    double connect_speed;
    double send_recv_speed;
} g_children[NUM_CHILD];

/* 開始フラグ */
volatile sig_atomic_t g_start_flag = 0;

/* mainの引数 */
char **argv_;

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
    //(void) fprintf(stderr, "addr=%s\n", nbuf);
    //(void) fprintf(stderr, "port=%s\n", sbuf);
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

/* 送受信処理 */
int
send_recv_loop(int soc)
{
    char buf[512], rbuf[512], *ptr;
    int count = 0, rv = 0;
    ssize_t len;
    
    /* 送受信 */
    for (;;) {
        count++;
        (void) snprintf(buf, sizeof(buf), "%d:%d\n", (int)pthread_self(),count);
        /* 送信 */
        if ((len = send(soc, buf, strlen(buf), 0)) == -1) {
            /* エラー */
            perror("send");
            rv = -1;
            break;
        }
        /* 受信 */
        if ((len = recv(soc, rbuf, sizeof(rbuf), 0)) == -1) {
            /* エラー */
            perror("recv");
            rv = -1;
            break;
        } else if (len == 0) {
            /* エンド・オブ・ファイル */
            //(void) fprintf(stderr, "recv:EOF\n");
            rv = -1;
            break;
        }
        /* 文字列化・表示 */
        rbuf[(size_t) len] = '\0';
        //(void) fprintf(stderr, "> %s", buf);
        if ((ptr = strpbrk(buf, "\r\n")) != NULL) {
            *ptr='\0';
        }
        (void) mystrlcat(buf, ":OK\r\n", sizeof(buf));
        /* 送った文字列と、期待する文字列の比較：違っていたらサーバプログラムのバグ */
        if (strcmp(buf, rbuf) != 0) {
            (void) fprintf(stderr,
	                   "recv string error.\n\tsend=%d:%d\n\trecv=%s",
			   (int) pthread_self(),
			   count,
			   rbuf);
            rv = -1;
            break;
        }
        if (count > SEND_COUNT) {
            //(void) fprintf(stderr, "%d:end\n", (int) pthread_self());
            break;
        }
    }

    return (rv);
}

/* 接続・送受信 */
static int
worker_core(int my_no)
{
    int soc;
    struct timezone tzp;
    struct timeval start_tp, end_tp, lapsed_tp;

    /* 接続開始時間 */
    (void) gettimeofday(&start_tp, &tzp);
    /* サーバにソケット接続 */
    if ((soc = client_socket(argv_[1], argv_[2])) == -1) {
        (void) fprintf(stderr, "client_socket():error\n");
        return (-1);
    }
    /* 接続終了時間 */
    (void) gettimeofday(&end_tp, &tzp);
    if (start_tp.tv_usec > end_tp.tv_usec) {
        end_tp.tv_usec += 1000 * 1000;
        end_tp.tv_sec--;
    }
    lapsed_tp.tv_usec = end_tp.tv_usec - start_tp.tv_usec;
    lapsed_tp.tv_sec = end_tp.tv_sec - start_tp.tv_sec;
    g_children[my_no].connect_speed
	+= (double) lapsed_tp.tv_sec + (double) lapsed_tp.tv_usec / 1000000.0;
    /* 送受信開始時間 */
    (void) gettimeofday(&start_tp, &tzp);
    /* 送受信処理 */
    if (send_recv_loop(soc) == -1) {
        return (-1);
    }
    /* ソケットクローズ */
    (void) close(soc);
    /* 送受信終了時間 */
    (void) gettimeofday(&end_tp, &tzp);
    if (start_tp.tv_usec > end_tp.tv_usec) {
        end_tp.tv_usec += 1000 * 1000;
        end_tp.tv_sec--;
    }
    lapsed_tp.tv_usec = end_tp.tv_usec - start_tp.tv_usec;
    lapsed_tp.tv_sec = end_tp.tv_sec - start_tp.tv_sec;
    g_children[my_no].send_recv_speed
	+= (double) lapsed_tp.tv_sec + (double) lapsed_tp.tv_usec / 1000000.0;

    return (0);
}

/* テストスレッド */
void *
worker(void *arg)
{
    int i, my_no;

    pthread_detach(pthread_self());
    my_no = (int) arg;
    g_children[my_no].active = 1;
    g_children[my_no].connect_speed = 0.0;
    g_children[my_no].send_recv_speed = 0.0;
    for (;;) {
        (void) usleep(1000);
        if (g_start_flag) {
            break;
        }
    }
    for (i = 0; i < LOOP_NO; i++) {
        /* 接続・送受信 */
        if (worker_core(my_no) == -1) {
            exit(EX_IOERR);
        }
    }
    /* 平均の計算 */
    g_children[my_no].connect_speed /= LOOP_NO;
    g_children[my_no].send_recv_speed /= LOOP_NO;
    /* 終了をセット */
    g_children[my_no].active = 0;

    pthread_exit((void *) NULL);
    /*NOT REACHED*/
    return ((void *) NULL);
}

int
main(int argc, char *argv[])
{
    int i, count;
    double connect_speed, send_recv_speed;
    struct rlimit rl;

    (void) getrlimit(RLIMIT_NOFILE,&rl);
    rl.rlim_cur = 8192;
    rl.rlim_max = 8192;
    if (setrlimit(RLIMIT_NOFILE, &rl) == -1) {
        return (EX_OSERR);
    }
    (void) getrlimit(RLIMIT_NOFILE, &rl);
    
    /* 引数にホスト名、ポート番号が指定されているか？ */
    if (argc <= 2) {
        (void) fprintf(stderr, "testclient server-host port\n");
        return (EX_USAGE);
    }
    argv_ = argv;
    /* スレッド生成 */
    for (i = 0; i < NUM_CHILD; i++) {
        if(pthread_create(&g_children[i].thread_id, NULL, worker, (void *) i)
	    != 0) {
            perror("pthread_create");
        } else {
            /* 開始をセット */
            //(void) fprintf(stderr,
	    //               "pthread_create:create:thread_id=%d\n",
	    //		     (int) g_children[i].thread_id);
        }
    }
    /* 開始フラグをセット */
    g_start_flag = 1;
    /* 全処理の終了を待つ */
    for (;;) {
        (void) sleep(10);
        count = 0;
        for (i = 0; i < NUM_CHILD; i++) {
            count += g_children[i].active;
            if (count > 0) {
                break;
            }
        }
        if (count == 0) {
            break;
        }
    }
    /* 全スレッドの平均速度の計算 */
    connect_speed = 0.0;
    send_recv_speed = 0.0;
    for (i = 0; i < NUM_CHILD; i++) {
        connect_speed += g_children[i].connect_speed;
        send_recv_speed += g_children[i].send_recv_speed;
    }
    connect_speed /= NUM_CHILD;
    send_recv_speed /= NUM_CHILD;
    /* 結果表示 */
    (void) fprintf(stderr, "%g\n",connect_speed);
    (void) fprintf(stderr, "%g\n",send_recv_speed);

    return (EX_OK);
}

