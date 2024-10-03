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

#include <openssl/err.h>                /* �ɲ� */
#include <openssl/rand.h>               /* �ɲ� */
#include <openssl/ssl.h>                /* �ɲ� */
#include <openssl/x509v3.h>             /* �ɲ� */
SSL_CTX *g_ctx;
#ifdef CHECK_CLIENT_CERT
/* ���饤����Ⱦ����� */
/* #define CERTFILE        "/home/lnpb/testCA/CLIENT/clcert.pem" */
#define CERTFILE        "/home/lnpb/testCA/SubCA/CLIENT/clcertchain.pem"
/* ��̩�� */
/* #define KEYFILE         "/home/lnpb/testCA/CLIENT/client.key" */
#define KEYFILE         "/home/lnpb/testCA/SubCA/CLIENT/client.key"
/* PEM�ѥ���� */
#define PEM_PASSWD      "SubCA"
/* #define PEM_PASSWD      "testCA" */
#endif/* CHECK_CLIENT_CERT */
/* �Ź����� */
#define CIPHER_LIST     "ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH"
#ifdef CHECK_SERVER_CERT
/* �����Ф�FQDN:��³�� */
char *g_server_fqdn = NULL;
/* CA��������� */
/* CA����������ե��������Ѥ����� */
#define CAFILE          "/home/lnpb/testCA/CAfile.pem"
#define CADIR           NULL
/* CA����������ǥ��쥯�ȥ����Ѥ����� */
/* #define CAFILE          NULL */
/* #define CADIR           "/home/lnpb/testCA/certs" */
#endif /* CHECK_SERVER_CERT */
#ifdef CHECK_SERVER_CERT
/* ���ڥ�����Хå� */
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
        /* ư���ǧ�ѡ������ξ��ʤΤ�����ɽ����ɬ�פ�̵�� */
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
#endif
/* ��ʸ����ʸ��̵��磻��ɥ�����ʸ��������å� */
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
#ifdef CHECK_SERVER_CERT
/* ��³��������ǧ */
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
     * ������γ�ĥ�ΰ��subjectAltName��
     * "DNS"�Ȥ����������Ĥ��Ƥ���ե�����ɤ��ͤ�Ĵ��
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
    /* �������commonName���ͤ�Ĵ�� */
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
#endif /* CHECK_SERVER_CERT */
#ifdef CHECK_CLIENT_CERT
/* PEM�ѥ���ɥ�����Хå� */
int
my_pem_passwd_cb(char *buf, int size, int rwflag, void *userdata)
{
    (void) snprintf(buf, size, "%s", PEM_PASSWD);
    return (strlen(buf));
}
#endif /* CHECK_CLIENT_CERT */
/* ���饤�������SSL����ƥ����Ȥν��� */
SSL_CTX *
client_ctx(void)
{
    SSL_CTX *ctx;
    /* SSL����ƥ����Ȥ����� */
    ctx = SSL_CTX_new(SSLv23_method());
#ifdef    CHECK_CLIENT_CERT
    /* PEM�ѥ���ɥ�����Хå��λ��� */
    SSL_CTX_set_default_passwd_cb(ctx, my_pem_passwd_cb);
    /* ������ե�����λ��� */
    if (SSL_CTX_use_certificate_chain_file(ctx, CERTFILE) != 1) {
(void) fprintf(stderr, "SSL_CTX_use_certificate_chain_file():error\n");
        return (NULL);
    }
    /* �Ź沽���ե�����λ��� */
    if (SSL_CTX_use_PrivateKey_file(ctx, KEYFILE, SSL_FILETYPE_PEM) != 1) {
(void) fprintf(stderr, "SSL_CTX_use_PrivateKey_file():error\n");
        return (NULL);
    }
#endif /* CHECK_CLIENT_CERT */
#ifdef CHECK_SERVER_CERT
    /* �롼��CA������λ��� */
    if (SSL_CTX_load_verify_locations(ctx, CAFILE, CADIR) != 1) {
(void) fprintf(stderr, "SSL_CTX_load_verify_locations():error\n");
        return (NULL);
    }
    /* �ǥե����CA�����������ͭ���� */
    if (SSL_CTX_set_default_verify_paths(ctx) != 1) {
 (void) fprintf(stderr, "SSL_CTX_set_default_verify_paths():error\n");
        return (NULL);
    }
    /* �����о�����γ�ǧ�λ��� */
    SSL_CTX_set_verify(ctx,
		       SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
		       my_verify_callback);
    /* �����о�����γ��ؤο����ε��ƺ����ͤλ��� */
    SSL_CTX_set_verify_depth(ctx, 9);
#endif /* CHECK_SERVER_CERT */
    /* SSL���ץ����λ��� */
(void) SSL_CTX_set_options(ctx, SSL_OP_ALL | SSL_OP_NO_SSLv2);
    /* �Ź楹�����Ȥ����� */
    if (SSL_CTX_set_cipher_list(ctx, CIPHER_LIST) != 1) {
(void) fprintf(stderr, "SSL_CTX_set_cipher_list():error\n");
        return (NULL);
    }
    return (ctx);
}
/* �����Ф˥����å���³ */
int
client_socket(const char *hostnm, const char *portnm)
{

    char nbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
    struct addrinfo hints, *res0;
    int soc, errcode;

    /* ���ɥ쥹����Υҥ�Ȥ򥼥��ꥢ */
    (void) memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    /* ���ɥ쥹����η��� */
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
    /* �����åȤ����� */
    if ((soc = socket(res0->ai_family, res0->ai_socktype, res0->ai_protocol))
        == -1) {
        perror("socket");
        freeaddrinfo(res0);
        return (-1);
    }

    /* ���ͥ��� */
    if (connect(soc, res0->ai_addr, res0->ai_addrlen) == -1) {
        perror("connect");
        (void) close(soc);
        freeaddrinfo(res0);
        return (-1);
    }
    freeaddrinfo(res0);
    return (soc);
}
/* ���������� */
int
send_recv_loop(int soc)
{
    char buf[512];
    struct timeval timeout;
    int end, width;
    ssize_t len;
    fd_set mask, ready;
    SSL *ssl;
    unsigned long err;
    /* SSL������ */
    if ((ssl = SSL_new(g_ctx)) == NULL) {
(void) fprintf(stderr, "SSL_new():error\n");
        return (-1);
    }
    /* SSL�˥����åȤ���� */
    (void) SSL_set_fd(ssl, soc);
    /* SSL��³���� */
    if (SSL_connect(ssl) <= 0) {
(void) fprintf(stderr, "SSL_connect():error\n");
        SSL_free(ssl);
        ERR_remove_state(0);
        return (-1);
    }
#ifdef CHECK_SERVER_CERT
    /* ��³��������ǧ */
    if ((err = post_connection_check(ssl, g_server_fqdn)) != X509_V_OK) {
(void) fprintf(stderr,
	       "post_connection_check():%s\n",
	       X509_verify_cert_error_string(err));
        SSL_free(ssl);
        ERR_remove_state(0);
        return (-1);
    }
#endif /* CHECK_SERVER_CERT */
    /* SSL�Ź沽���֤�ɽ�� */
(void) fprintf(stderr,
	       "current_cipher=%s\n",
	       SSL_CIPHER_get_name(SSL_get_current_cipher(ssl)));
(void) fprintf(stderr,
	       "current_cipher=%s\n",
	       SSL_CIPHER_get_version(SSL_get_current_cipher(ssl)));
    /* select()�ѥޥ��� */
    FD_ZERO(&mask);
    /* �����åȥǥ�������ץ��򥻥å� */
    FD_SET(soc, &mask);
    /* ɸ�����Ϥ򥻥å� */
    FD_SET(0, &mask);
    width = soc + 1;
    for (end = 0;; ) {
        /* �ޥ��������� */
        ready = mask;
        /* �����ॢ�����ͤΥ��å� */
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        switch (select(width, (fd_set *) &ready, NULL, NULL, &timeout)) {
        case -1:
            /* ���顼 */
            perror("select");
            break;
        case 0:
            /* �����ॢ���� */
            break;
        default:
            /* ��ǥ�ͭ�� */
            /* �����åȥ�ǥ� */
            if (FD_ISSET(soc, &ready)) {
                /* ���� */
                if ((len = SSL_read(ssl, buf, sizeof(buf))) < 0) {
                    /* ���顼 */
                    err = ERR_get_error();
(void) fprintf(stderr,
	       "SSL_read error err=%lu %s\n",
	       err,
	       ERR_error_string(err, NULL));
                    end = 1;
                    break;
                }
                if (len == 0) {
                    /* ����ɡ����֡��ե����� */
(void) fprintf(stderr, "recv:EOF\n");
                    end = 2;
                    break;
                }
                /* ʸ���󲽡�ɽ�� */
                buf[len]='\0';
                (void) printf("> %s", buf);
            }
        /* ɸ�����ϥ�ǥ� */
            if (FD_ISSET(0, &ready)) {
                /* ɸ�����Ϥ��飱���ɤ߹��� */
                (void) fgets(buf,sizeof(buf),stdin);
                if (feof(stdin)) {
                    end = 2;
                    break;
                }
                /* ���� */
                if ((len = SSL_write(ssl, buf, strlen(buf))) < 0) {
                    /* ���顼 */
                    err = ERR_get_error();
(void) fprintf(stderr,
	       "SSL_write error err=%lu %s\n",
	       err,
	       ERR_error_string(err, NULL));
                    end  = 1;
                    break;
                }
            }
        }
        if (end) {
            break;
        }
    }
    if (end == 2) {
        /* �̾ｪλ */
(void) fprintf(stderr, "do SSL_shutdown()\n");
        (void) SSL_shutdown(ssl);
    } else {
        /* ���顼��λ */
(void) fprintf(stderr, "do SSL_clear()\n");
        (void) SSL_clear(ssl);
    }
    /* SSL���� */
    SSL_free(ssl);
    /* ���顼����ν��� */
    ERR_remove_state(0);
    return (0);
}
int
main(int argc, char *argv[])
{
    int soc;
    /* �����˥ۥ���̾���ݡ����ֹ椬���ꤵ��Ƥ��뤫�� */
    if (argc <= 2) {
(void) fprintf(stderr,"ssl-client server-host port\n");
        return (EX_USAGE);
    }
#ifdef CHECK_SERVER_CERT
    /* ������FQDN����³������ */
    g_server_fqdn = argv[1];
(void) fprintf(stderr, "CHECK_SERVER_CERT:ON\n");
#else
(void) fprintf(stderr, "CHECK_SERVER_CERT:OFF\n");
#endif /* CHECK_SERVER_CERT */
#ifdef CHECK_CLIENT_CERT
(void) fprintf(stderr, "CHECK_CLIENT_CERT:ON\n");
#else
(void)fprintf(stderr, "CHECK_CLIENT_CERT:OFF\n");
#endif /* CHECK_CLIENT_CERT */
    /* SSL�饤�֥��ν���� */
    if (SSL_library_init() == 0) {
(void) fprintf(stderr, "SSL_library_init():error\n");
        return (EX_UNAVAILABLE);
    }
    /* SSL���顼ʸ����Υ��� */
    SSL_load_error_strings();
    /* ����μ��/dev/urandom�����Ϥ� */
    if (RAND_load_file("/dev/urandom", 1024) == 0) {
(void) fprintf(stderr, "RAND_load_file():error\n");
        return (EX_UNAVAILABLE);
    }
    /* ���饤�������SSL����ƥ����Ȥν��� */
    if ((g_ctx = client_ctx()) == NULL) {
(void) fprintf(stderr, "client_ctx():error\n");
        return (EX_UNAVAILABLE);
    }
    /* �����Ф˥����å���³ */
    if ((soc = client_socket(argv[1], argv[2])) == -1) {
(void) fprintf(stderr,"client_socket():error\n");
        return (EX_UNAVAILABLE);
    }
    /* ���������� */
    (void) send_recv_loop(soc);
    /* �����åȥ����� */
    (void) close(soc);
    /* SSL����ƥ����Ȥβ��� */
    SSL_CTX_free(g_ctx);
    return (EX_OK);
}
