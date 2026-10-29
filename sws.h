#ifndef SWS_H
#define SWS_H

#include <sys/types.h>
#include <sys/socket.h>
#include <limits.h>
#include <time.h>

#define SERVER_NAME   "sws/1.0"
#define DEFAULT_PORT  8080
#define MAX_LINE      8192
#define CONN_TIMEOUT  30

struct config {
	char *docroot;
	char *cgidir;   /* -c */
	char *addr;     /* -i */
	char *logfile;  /* -l */
	int   port;     /* -p */
	int   debug;    /* -d */
};

struct request {
	char   method[8];
	char   uri[PATH_MAX];
	char   path[PATH_MAX];
	char   query[MAX_LINE];
	char   first_line[MAX_LINE];
	time_t ims;       /* If-Modified-Since */
	int    is_head;
	int    status;
	off_t  sent;
};

extern struct config cfg;

/* main.c, net.c */
void handle_client(int fd, const struct sockaddr *client);
void log_request(const struct sockaddr *client, const struct request *r);

/* request.c - tra ve 0 neu ok, -1 neu client khong gui gi, con lai la ma loi */
int  parse_request(int fd, struct request *r);

/* response.c */
const char *status_text(int status);
void send_headers(int fd, struct request *r, int status, const char *ctype,
                  off_t len, time_t mtime);
void send_error(int fd, struct request *r, int status);

/* serve.c */
void serve_path(int fd, struct request *r);
const char *mime_type(const char *path);

/* cgi.c */
int  is_cgi(const struct request *r);
void serve_cgi(int fd, struct request *r, const struct sockaddr *client);

#endif
