#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "sws.h"

/* Thu don cac tien trinh con da ket thuc */
static void
reap_children(int sig)
{
    int saved_errno = errno;
    (void)sig;

    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;

    errno = saved_errno;
}

/*
 * Tao socket lang nghe. Khong co -i thi dung IPv6 nhan ca IPv4,
 * may khong ho tro IPv6 thi quay ve IPv4.
 */
static int
open_socket(void)
{
    int fd;
    int opt = 1;
    int off = 0;
    int use6;
    struct sockaddr_in addr4;
    struct sockaddr_in6 addr6;

    memset(&addr4, 0, sizeof(addr4));
    memset(&addr6, 0, sizeof(addr6));
    addr4.sin_family = AF_INET;
    addr4.sin_port = htons((unsigned short)cfg.port);
    addr6.sin6_family = AF_INET6;
    addr6.sin6_port = htons((unsigned short)cfg.port);

    if (cfg.addr == NULL) {
        addr4.sin_addr.s_addr = htonl(INADDR_ANY);
        addr6.sin6_addr = in6addr_any;
        use6 = 1;
    } else if (inet_pton(AF_INET, cfg.addr, &addr4.sin_addr) == 1) {
        use6 = 0;
    } else if (inet_pton(AF_INET6, cfg.addr, &addr6.sin6_addr) == 1) {
        use6 = 1;
    } else {
        fprintf(stderr, "Invalid address: %s\n", cfg.addr);
        exit(EXIT_FAILURE);
    }

    fd = socket(use6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
    if (fd < 0 && use6 && cfg.addr == NULL) {
        use6 = 0;
        fd = socket(AF_INET, SOCK_STREAM, 0);
    }
    if (fd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                   &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR");
        close(fd);
        exit(EXIT_FAILURE);
    }

    if (use6)
        setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));

    if ((use6 ? bind(fd, (struct sockaddr *)&addr6, sizeof(addr6))
              : bind(fd, (struct sockaddr *)&addr4, sizeof(addr4))) < 0) {
        perror("bind");
        close(fd);
        exit(EXIT_FAILURE);
    }

    return fd;
}

/* Tao socket va cho client ket noi */
void
server_loop(void)
{
    int server_fd;
    int client_fd;
    struct sockaddr_storage client_addr;
    socklen_t client_len;
    struct sigaction sa;
    struct timeval timeout;
    pid_t pid;

    server_fd = open_socket();

    if (listen(server_fd, 20) < 0) {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = reap_children;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;

    if (sigaction(SIGCHLD, &sa, NULL) < 0) {
        perror("sigaction");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    signal(SIGPIPE, SIG_IGN);

    if (cfg.debug) {
        printf("Server listening on port %d\n", cfg.port);
        fflush(stdout);
    }

    while (1) {
        client_len = sizeof(client_addr);

        client_fd = accept(server_fd,
                           (struct sockaddr *)&client_addr,
                           &client_len);

        if (client_fd < 0) {
            if (errno == EINTR)
                continue;

            perror("accept");
            continue;
        }

        timeout.tv_sec = CONN_TIMEOUT;
        timeout.tv_usec = 0;

        if (setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO,
                       &timeout, sizeof(timeout)) < 0 ||
            setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO,
                       &timeout, sizeof(timeout)) < 0) {
            perror("connection timeout");
            close(client_fd);
            continue;
        }

        pid = fork();

        if (pid < 0) {
            perror("fork");
            close(client_fd);
            continue;
        }

        if (pid == 0) {
            close(server_fd);
            signal(SIGCHLD, SIG_DFL);

            handle_client(client_fd,
                          (struct sockaddr *)&client_addr);

            _exit(EXIT_SUCCESS);
        }

        close(client_fd);
    }
}

/*
 * Dong ket noi: neu client con du lieu chua doc (vd request loi) ma
 * close() ngay thi kernel gui RST va client co the mat response.
 */
static void
close_conn(int fd)
{
    struct timeval tv;
    char buf[1024];

    tv.tv_sec = 1;
    tv.tv_usec = 0;

    shutdown(fd, SHUT_WR);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (read(fd, buf, sizeof(buf)) > 0)
        ;

    close(fd);
}

/* Xu ly mot ket noi HTTP */
void
handle_client(int fd, const struct sockaddr *client)
{
    struct request r;
    int err;

    memset(&r, 0, sizeof(r));

    if ((err = parse_request(fd, &r)) != 0)
        send_error(fd, &r, err);
    else if (is_cgi(&r))
        serve_cgi(fd, &r, client);
    else
        serve_path(fd, &r);

    log_request(client, &r);
    close_conn(fd);
}

/* Ghi log request */
void
log_request(const struct sockaddr *client,
            const struct request *r)
{
    char ip[INET6_ADDRSTRLEN] = "unknown";
    char date[64];
    char line[10240];
    time_t now;
    struct tm tm_info;
    int log_fd;
    int len;

    if (client != NULL && client->sa_family == AF_INET) {
        const struct sockaddr_in *in =
            (const struct sockaddr_in *)client;

        inet_ntop(AF_INET, &in->sin_addr,
                  ip, sizeof(ip));
    } else if (client != NULL && client->sa_family == AF_INET6) {
        const struct sockaddr_in6 *in6 =
            (const struct sockaddr_in6 *)client;

        inet_ntop(AF_INET6, &in6->sin6_addr,
                  ip, sizeof(ip));

        /* client IPv4 qua socket IPv6: ::ffff:1.2.3.4 -> 1.2.3.4 */
        if (strncmp(ip, "::ffff:", 7) == 0 &&
            strchr(ip + 7, '.') != NULL)
            memmove(ip, ip + 7, strlen(ip + 7) + 1);
    }

    now = time(NULL);
    localtime_r(&now, &tm_info);

    strftime(date, sizeof(date),
             "%Y-%m-%d %H:%M:%S", &tm_info);

    len = snprintf(line, sizeof(line),
                   "[%s] %s \"%s\" %d %lld\n",
                   date, ip, r->first_line,
                   r->status, (long long)r->sent);

    if (len < 0)
        return;

    if ((size_t)len >= sizeof(line))
        len = sizeof(line) - 1;

    if (cfg.logfile != NULL) {
        log_fd = open(cfg.logfile,
                      O_WRONLY | O_CREAT | O_APPEND, 0644);

        if (log_fd >= 0) {
            (void)write(log_fd, line, (size_t)len);
            close(log_fd);
        }
    } else if (cfg.debug) {
        (void)write(STDOUT_FILENO, line, (size_t)len);
    }
}
