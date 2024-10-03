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

#include <openssl/err.h>                /* 追加 */
#include <openssl/rand.h>               /* 追加 */
#include <openssl/ssl.h>                /* 追加 */
#include <openssl/x509v3.h>             /* 追加 */
/* SSLコンテキスト */
SSL_CTX *g_ctx;
/* サーバ証明書 */
#define CERTFILE        "/home/lnpb/testCA/SERVER/cert.pem"
/* 秘密鍵 */
/* PEMパスフレーズあり */
#define KEYFILE         "/home/lnpb/testCA/SERVER/server.key"
/* PEMパスフレーズなし */
/* #define KEYFILE         "/home/lnpb/testCA/SERVER/servernopass.key" */
/* PEMパスワード */
#define PEM_PASSWD      "testCA"
/* 暗号選択 */
#define CIPHER_LIST     "ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH"
#ifdef CHECK_CLIENT_CERT
/* クライアントのFQDN */
#define CLIENT_FQDN     "*.localdomain"
/* CA証明書一覧 */
/* CA証明書一覧ファイルを使用する場合 */
#define CAFILE          "/home/lnpb/testCA/CAfile.pem"
#define CADIR           NULL
/* CA証明書一覧ディレクトリを使用する場合 */
/* #define CAFILE          NULL */
/* #define CADIR           "/home/lnpb/testCA/certs" */
#endif /* CHECK_CLIENT_CERT */
#ifdef USE_DH
DH *g_dh512 = NULL;
DH *g_dh1024 = NULL;
#endif /* USE_DH */
#ifdef	USE_DH
/* ファイルからDHパラメータ読み込み */
DH *
read_dhparams_from_file(char *fname)
{
    FILE *fp;
    DH *dh;

    if ((fp = fopen(fname, "r")) == NULL) {
(void) fprintf(stderr, "Cannot read %s", fname);
        return (NULL);
    }
    dh = PEM_read_DHparams(fp, NULL, NULL, NULL);
    (void) fclose(fp);
    return (dh);
}
/* 使用するDHパラメータの準備 */
void
init_dh_params(void)
{
    if ((g_dh512 = read_dhparams_from_file("dh512.pem")) == NULL) {
(void) fprintf(stderr, "read_dhparams_from_file(%s):error\n", "dh512.pem");
    }

    if ((g_dh1024 = read_dhparams_from_file("dh1024.pem")) == NULL) {
(void) fprintf(stderr, "read_dhparams_from_file(%s):error\n", "dh1024.pem");
    }
}
/* 一時的DHコールバック */
DH *
my_tmp_dh_callback(SSL *ssl, int is_export, int keylength)
{

(void) fprintf(stderr,
	       "my_tmp_dh_callback:is_export=%d,keylength=%d\n",
	       is_export,
	       keylength);
    if (g_dh512 == NULL || g_dh1024 == NULL) {
        init_dh_params();
    }
    if (keylength == 512) {
        return (g_dh512);
    } else {
        return (g_dh1024);
    }
}
#endif /* USE_DH */
#ifdef CHECK_CLIENT_CERT
/* 検証コールバック */
int
my_verify_callback(int ok, X509_STORE_CTX *store)
{
    char data[256];
    int err, depth;
    X509 *cert;
    if (ok == 0) {
        depth = X509_STORE_CTX_get_error_depth(store);
(void) fprintf(stderr, "NG: my_verify_callback() depth : %i\n", depth);
        cert = X509_STORE_CTX_get_current_cert(store);
        X509_NAME_oneline(X509_get_issuer_name(cert), data, sizeof(data));
(void) fprintf(stderr, "  issuer = %s\n", data);
        X509_NAME_oneline(X509_get_subject_name(cert), data, sizeof(data));
(void) fprintf(stderr, "  subject = %s\n", data);
        err = X509_STORE_CTX_get_error(store);
(void) fprintf(stderr,
	       "  err %i : %s\n",
	       err,
	       X509_verify_cert_error_string (err));
    } else {
        /* 動作確認用：成功の場合なので本来表示の必要は無い */
        depth = X509_STORE_CTX_get_error_depth(store);
(void) fprintf(stderr, "OK: my_verify_callback() depth : %i\n", depth);
        cert = X509_STORE_CTX_get_current_cert(store);
        X509_NAME_oneline(X509_get_issuer_name(cert),data,sizeof(data));
(void) fprintf(stderr, "  issuer = %s\n", data);
        X509_NAME_oneline(X509_get_subject_name(cert),data,sizeof(data));
(void) fprintf(stderr, "  subject = %s\n", data);
        err=X509_STORE_CTX_get_error(store);
(void) fprintf(stderr,
	       "  err %i : %s\n",
	       err,
	       X509_verify_cert_error_string(err));
    }
    return (ok);
}
/* 大文字小文字無視ワイルドカード文字列チェック */
int
like_check_ignorecase(char *str_in, char *check_in)
{
    for (;;) {
        switch (*check_in) {
        case '\0':
            if (*str_in == '\0') {
                return (1);
            } else {
                return (0);
            }
        case '*':
            check_in++;
            if (*check_in == '\0') {
                return (1);
            }
            for (;;) {
                if (like_check_ignorecase(str_in, check_in)) {
                    return (1);
                }
                str_in++;
                if (*str_in == '\0') {
                    return (0);
                }
            }
        case '?':
            if (*str_in == '\0') {
                return (0);
            }
            break;
        default:
            if (toupper(*str_in) != toupper(*check_in)) {
                return (0);
            }
            break;
        }
        str_in++;
        check_in++;
    }
    /*NOT REACHED*/
    return (0);
}
/* 接続後証明書確認 */
long
post_connection_check(SSL *ssl, char *host)
{
    char data[256];
    int extcount, i, j, ok = 0;
    char *extstr;
    const unsigned char *ext_value_data;
    X509 *cert;
    X509_EXTENSION *ext;
    X509V3_EXT_METHOD *method;
    X509_NAME *subject;
    CONF_VALUE *cval;
    STACK_OF(CONF_VALUE) *val;
#if (OPENSSL_VERSION_NUMBER > 0x00907000L)
    ASN1_VALUE *str;
#else
    char *str;
#endif
(void) fprintf(stderr, "post_connection_check(%p,%s)\n", (void *) ssl, host);
    if (!host) {
(void) fprintf(stderr, "post_connection_check():host is null\n");
        return (X509_V_ERR_APPLICATION_VERIFICATION);
    }
    if ((cert = SSL_get_peer_certificate(ssl)) == NULL) {
(void) fprintf(stderr, "SSL_get_peer_certificate():error\n");
        return (X509_V_ERR_APPLICATION_VERIFICATION);
    }
    /*
     * 証明書の拡張領域のsubjectAltNameで
     * "DNS"というタグがついているフィールドの値の調査
    */
    if ((extcount = X509_get_ext_count(cert)) > 0) {
        for (i = 0; i < extcount; i++) {
            ext = X509_get_ext(cert, i);
            extstr =
                (char*) OBJ_nid2sn(OBJ_obj2nid(X509_EXTENSION_get_object(ext)));
            if (strncmp(extstr, "subjectAltName", 14) == 0) {
                if ((method = X509V3_EXT_get(ext)) == NULL) {
                    break;
                }
#if (OPENSSL_VERSION_NUMBER > 0x00907000L)
                ext_value_data = ext->value->data;
                if (method->it) {
                    str = ASN1_item_d2i(NULL,
					&ext_value_data,
					ext->value->length,
					ASN1_ITEM_ptr(method->it));
                } else {
                    str = method->d2i(NULL,
				      &ext_value_data,
				      ext->value->length);
                }
#else
                str = method->d2i(NULL,
                          &ext->value->data,
                          ext->value->length);
#endif
                val = method->i2v(method, str, NULL);
                for (j = 0; j < sk_CONF_VALUE_num(val); j++) {
                    cval = sk_CONF_VALUE_value(val, j);
(void) fprintf(stderr, "cval->name=%s,cval->value=%s\n",cval->name,cval->value);
                    if (strncmp(cval->name, "DNS", 3) == 0 &&
                        like_check_ignorecase(cval->value,
					      host)) {
(void) fprintf(stderr,
	       "post_connection_check():OK:cval->name=%s == DNS"
	       "&& cval->value=%s == host=%s\n",
	       cval->name,
	       cval->value,
	       host);
                        ok = 1;
                        break;
                    }
                }
            }
            if (ok) {
                break;
            }
        }
    }
    /* 証明書のcommonNameの値の調査 */
    if (!ok) {
        if ((subject = X509_get_subject_name(cert))) {
            if (X509_NAME_get_text_by_NID(subject,
					  NID_commonName,
					  data,
					  sizeof(data)) > 0) {
                data[sizeof(data) - 1] = '\0';
                if (like_check_ignorecase(host, data)!= 1) {
(void) fprintf(stderr,
	       "post_connection_check():NG:data=%s != host=%s\n",
	       data,
	       host);
                    X509_free(cert);
                    return (X509_V_ERR_APPLICATION_VERIFICATION);
                } else {
(void) fprintf(stderr,
	       "post_connection_check():OK:data=%s == host=%s\n",
	       data,
	       host);
                    ok = 1;
                }
            }
        }
    }
    X509_free(cert);
    return (SSL_get_verify_result(ssl));
}
#endif /* CHECK_CLIENT_CERT */
/* PEMパスワードコールバック */
int
my_pem_passwd_cb(char *buf, int size, int rwflag, void *userdata)
{
    (void) snprintf(buf, size, "%s", PEM_PASSWD);
    return (strlen(buf));
}
/* サーバ用SSLコンテキストの準備 */
SSL_CTX *
server_ctx(void)
{
    SSL_CTX *ctx;
    /* SSLコンテキストの生成 */
    ctx = SSL_CTX_new(SSLv23_method());
    /* PEMパスワードコールバックの指定 */
    SSL_CTX_set_default_passwd_cb(ctx, my_pem_passwd_cb);
    /* 証明書ファイルの指定 */
    if (SSL_CTX_use_certificate_chain_file(ctx, CERTFILE) != 1) {
(void) fprintf(stderr, "SSL_CTX_use_certificate_chain_file():error\n");
        return (NULL);
    }
    /* 暗号化鍵ファイルの指定 */
    if (SSL_CTX_use_PrivateKey_file(ctx, KEYFILE, SSL_FILETYPE_PEM) != 1) {
(void) fprintf(stderr, "SSL_CTX_use_PrivateKey_file():error\n");
        return (NULL);
    }
#ifdef    CHECK_CLIENT_CERT
    /* ルートCA証明書の指定 */
    if (SSL_CTX_load_verify_locations(ctx, CAFILE, CADIR) != 1) {
(void) fprintf(stderr, "SSL_CTX_load_verify_locations():error\n");
        return (NULL);
    }
    /* デフォルトCA証明書一覧を使いたい場合用 */
    /* if (SSL_CTX_set_default_verify_paths(ctx) != 1) { */
    /* (void) fprintf(stderr,"SSL_CTX_set_default_verify_paths():error\n"); */
    /*    return(NULL); */
    /* } */
    /* クライアント証明書の確認の指定 */
    SSL_CTX_set_verify(ctx,
		       SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
		       my_verify_callback);
    /* クライアント証明書の階層の深さの許容最大値の指定 */
    SSL_CTX_set_verify_depth(ctx, 9);
#endif /* CHECK_CLIENT_CERT */
#ifdef USE_DH
    /* SSLオプションの指定 */
    (void) SSL_CTX_set_options(ctx,
			       SSL_OP_ALL|SSL_OP_NO_SSLv2|SSL_OP_SINGLE_DH_USE);
    /* 一時的DHコールバックの指定 */
    SSL_CTX_set_tmp_dh_callback(ctx, my_tmp_dh_callback);
#else
    /* SSLオプションの指定 */
    (void) SSL_CTX_set_options(ctx, SSL_OP_ALL | SSL_OP_NO_SSLv2);
#endif /* USE_DH */
    /* 暗号スイートの選択 */
    if (SSL_CTX_set_cipher_list(ctx, CIPHER_LIST) != 1) {
(void) fprintf(stderr, "SSL_CTX_set_cipher_list():error\n");
        return (NULL);
    }
    return (ctx);
}
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
            (void) send_recv_loop(acc);
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
/* 送受信ループ */
int
send_recv_loop(int acc)
{
    char buf[512], *ptr;
    ssize_t len;
    unsigned long err;
    SSL *ssl;
    /* SSLの生成 */
    if ((ssl = SSL_new(g_ctx)) == NULL) {
(void) fprintf(stderr, "SSL_new():error\n");
        return (-1);
    }
    /* SSLにソケットを指定 */
    (void) SSL_set_fd(ssl, acc);
    /* SSLアクセプト処理 */
    if (SSL_accept(ssl) <= 0) {
        (void) fprintf(stderr, "SSL_accept():error\n");
        SSL_free(ssl);
        ERR_remove_state(0);
        return (-1);
    }
(void) fprintf(stderr,
	       "TCP %d: SSL_accept state=%x, finished=%x, in_init=%x/%x\n",
	       acc,
	       SSL_state(ssl),
	       SSL_is_init_finished(ssl),
	       SSL_in_init(ssl),
	       SSL_in_accept_init(ssl));
#ifdef CHECK_CLIENT_CERT
    /* 接続後証明書確認 */
    if ((err = post_connection_check(ssl, CLIENT_FQDN)) != X509_V_OK) {
(void) fprintf(stderr,
	       "post_connection_check():%s\n",
	       X509_verify_cert_error_string(err));
        SSL_free(ssl);
        ERR_remove_state(0);
        return (-1);
    }
#endif /* CHECK_CLIENT_CERT */
    /* SSL暗号化状態の表示 */
(void) fprintf(stderr,
	       "current_cipher=%s\n",
	       SSL_CIPHER_get_name(SSL_get_current_cipher(ssl)));
(void) fprintf(stderr,
	       "current_cipher=%s\n",
	       SSL_CIPHER_get_version(SSL_get_current_cipher(ssl)));
    for (;;) {
        /* 受信 */
        if ((len = SSL_read(ssl, buf, sizeof(buf))) < 0) {
            /* エラー */
            err = ERR_get_error();
(void) fprintf(stderr, "SSL_read error err=%lu %s\n", err,ERR_error_string(err,NULL));
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
        if ((len = SSL_write(ssl, buf, len)) < 0) {
            /* エラー */
            err = ERR_get_error();
(void) fprintf(stderr, "SSL_write error err=%lu %s\n", err,ERR_error_string(err,NULL));
            break;
        }
    }
    if ((SSL_get_shutdown(ssl) & SSL_RECEIVED_SHUTDOWN)) {
        /* 通常終了 */
(void) fprintf(stderr, "do SSL_shutdown()\n");
        SSL_set_shutdown(ssl, SSL_SENT_SHUTDOWN);
        SSL_shutdown(ssl);
    } else {
        /* エラー終了 */
(void) fprintf(stderr, "do SSL_clear()\n");
        SSL_clear(ssl);
    }
    /* SSL解放 */
    SSL_free(ssl);
    /* エラー情報の除去 */
    ERR_remove_state(0);
    return (0);
}
int
main(int argc, char *argv[])
{
    int soc;
    /* 引数にポート番号が指定されているか？ */
    if (argc <= 1) {
        (void) fprintf(stderr, "ssl-server port\n");
        return (EX_USAGE);
    }
#ifdef CHECK_CLIENT_CERT
(void) fprintf(stderr, "CHECK_CLIENT_CERT:ON\n");
#else
(void) fprintf(stderr, "CHECK_CLIENT_CERT:OFF\n");
#endif /* CHECK_CLIENT_CERT */

#ifdef USE_DH
(void) fprintf(stderr, "USE_DH:ON\n");
#else
(void) fprintf(stderr, "USE_DH:OFF\n");
#endif /* USE_DH */
    /* SSLライブラリの初期化 */
    if (SSL_library_init() == 0) {
(void) fprintf(stderr, "SSL_library_init():error\n");
        return (EX_UNAVAILABLE);
    }
    /* SSLエラー文字列のロード */
    SSL_load_error_strings();
    /* 乱数の種を/dev/urandomから渡す */
    if (RAND_load_file("/dev/urandom", 1024) == 0) {
(void) fprintf(stderr, "RAND_load_file():error\n");
        return (EX_UNAVAILABLE);
    }
    /* サーバ用SSLコンテキストの準備 */
    if ((g_ctx = server_ctx()) == NULL) {
(void) fprintf(stderr, "server_ctx():error\n");
        return (EX_UNAVAILABLE);
    }
    /* サーバソケットの準備 */
    if ((soc = server_socket(argv[1])) == -1) {
(void) fprintf(stderr, "server_socket(%s):error\n", argv[1]);
        return (EX_UNAVAILABLE);
    }
(void) fprintf(stderr, "ready for accept\n");
    /* アクセプトループ */
    accept_loop(soc);
    /* ソケットクローズ */
    (void) close(soc);
    /* SSLコンテキストの解放 */
    SSL_CTX_free(g_ctx);
    return (EX_OK);
}
