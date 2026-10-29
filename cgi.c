#include <sys/stat.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sws.h"

#define CGI_PREFIX "/cgi-bin/"
#define NENV       16
#define ENVLEN     (MAX_LINE + 2 * PATH_MAX)

int
is_cgi(const struct request *r)
{
	return cfg.cgidir != NULL &&
	    strncmp(r->uri, CGI_PREFIX, strlen(CGI_PREFIX)) == 0;
}

/* co thanh phan ".." trong duong dan khong */
static int
has_dotdot(const char *p)
{
	size_t n;

	while (*p) {
		n = strcspn(p, "/");
		if (n == 2 && p[0] == '.' && p[1] == '.')
			return 1;
		p += n;
		if (*p == '/')
			p++;
	}
	return 0;
}

static void
client_addr(const struct sockaddr *client, char *buf, size_t len)
{
	socklen_t slen;

	strcpy(buf, "");
	if (client == NULL)
		return;
	if (client->sa_family == AF_INET)
		slen = sizeof(struct sockaddr_in);
	else if (client->sa_family == AF_INET6)
		slen = sizeof(struct sockaddr_in6);
	else
		return;
	if (getnameinfo(client, slen, buf, len, NULL, 0, NI_NUMERICHOST) != 0)
		strcpy(buf, "");
}

static int
write_all(int fd, const char *buf, size_t len)
{
	ssize_t n;

	while (len > 0) {
		if ((n = write(fd, buf, len)) < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		buf += n;
		len -= n;
	}
	return 0;
}

/* tim cuoi phan header cua script, tra ve vi tri bat dau body */
static char *
end_of_headers(char *buf, size_t len)
{
	size_t i;

	for (i = 0; i + 1 < len; i++) {
		if (buf[i] == '\n' && buf[i + 1] == '\n')
			return buf + i + 2;
		if (i + 3 < len && memcmp(buf + i, "\r\n\r\n", 4) == 0)
			return buf + i + 4;
	}
	return NULL;
}

static void
run_script(const char *dir, const char *file, const char *script_name,
    const char *path_info, struct request *r, const struct sockaddr *client)
{
	char env[NENV][ENVLEN];
	char *envp[NENV + 1];
	char host[256], addr[NI_MAXHOST], prog[PATH_MAX];
	int i = 0;

	if (gethostname(host, sizeof(host)) != 0)
		strcpy(host, "localhost");
	client_addr(client, addr, sizeof(addr));

	snprintf(env[i++], ENVLEN, "GATEWAY_INTERFACE=CGI/1.1");
	snprintf(env[i++], ENVLEN, "SERVER_PROTOCOL=HTTP/1.0");
	snprintf(env[i++], ENVLEN, "SERVER_SOFTWARE=%s", SERVER_NAME);
	snprintf(env[i++], ENVLEN, "SERVER_NAME=%s", host);
	snprintf(env[i++], ENVLEN, "SERVER_PORT=%d", cfg.port);
	snprintf(env[i++], ENVLEN, "REQUEST_METHOD=%s", r->method);
	snprintf(env[i++], ENVLEN, "QUERY_STRING=%s", r->query);
	snprintf(env[i++], ENVLEN, "SCRIPT_NAME=%s", script_name);
	snprintf(env[i++], ENVLEN, "SCRIPT_FILENAME=%s/%s", dir, file);
	snprintf(env[i++], ENVLEN, "PATH_INFO=%s", path_info);
	snprintf(env[i++], ENVLEN, "REMOTE_ADDR=%s", addr);
	snprintf(env[i++], ENVLEN, "DOCUMENT_ROOT=%s",
	    cfg.docroot ? cfg.docroot : "");
	snprintf(env[i++], ENVLEN, "PATH=/usr/local/bin:/usr/bin:/bin");
	for (int j = 0; j < i; j++)
		envp[j] = env[j];
	envp[i] = NULL;

	snprintf(prog, sizeof(prog), "./%s", file);
	if (chdir(dir) != 0)
		_exit(127);
	execle(prog, prog, (char *)NULL, envp);
	_exit(127);
}

void
serve_cgi(int fd, struct request *r, const struct sockaddr *client)
{
	char dir[PATH_MAX], file[PATH_MAX], script_name[PATH_MAX + 16];
	char path_info[PATH_MAX], date[64], hdr[512];
	char buf[MAX_LINE], *body;
	const char *rest, *slash;
	struct stat st;
	size_t len = 0;
	ssize_t n;
	time_t now;
	pid_t pid;
	int p[2], devnull, eof = 0;

	/* /cgi-bin/script/path/info -> script + PATH_INFO */
	rest = r->uri + strlen(CGI_PREFIX);
	if (*rest == '\0' || has_dotdot(rest)) {
		send_error(fd, r, 403);
		return;
	}
	if ((slash = strchr(rest, '/')) != NULL) {
		snprintf(file, sizeof(file), "%.*s", (int)(slash - rest), rest);
		snprintf(path_info, sizeof(path_info), "%s", slash);
	} else {
		snprintf(file, sizeof(file), "%s", rest);
		path_info[0] = '\0';
	}
	snprintf(script_name, sizeof(script_name), "%s%s", CGI_PREFIX, file);

	if (realpath(cfg.cgidir, dir) == NULL) {
		send_error(fd, r, 500);
		return;
	}
	if (strlen(dir) + strlen(file) + 2 > sizeof(buf)) {
		send_error(fd, r, 403);
		return;
	}
	snprintf(buf, sizeof(buf), "%s/%s", dir, file);
	if (stat(buf, &st) != 0) {
		send_error(fd, r, errno == ENOENT ? 404 : 403);
		return;
	}
	if (!S_ISREG(st.st_mode) || access(buf, X_OK) != 0) {
		send_error(fd, r, 403);
		return;
	}

	if (pipe(p) != 0) {
		send_error(fd, r, 500);
		return;
	}
	if ((pid = fork()) < 0) {
		close(p[0]);
		close(p[1]);
		send_error(fd, r, 500);
		return;
	}
	if (pid == 0) {
		close(p[0]);
		if ((devnull = open("/dev/null", O_RDWR)) >= 0)
			dup2(devnull, STDIN_FILENO);
		dup2(p[1], STDOUT_FILENO);
		close(p[1]);
		run_script(dir, file, script_name, path_info, r, client);
	}
	close(p[1]);

	/* doc phan header cua script */
	while (len < sizeof(buf) &&
	    (n = read(p[0], buf + len, sizeof(buf) - len)) != 0) {
		if (n < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		len += n;
		if (end_of_headers(buf, len) != NULL)
			break;
	}
	if ((body = end_of_headers(buf, len)) == NULL) {
		close(p[0]);
		kill(pid, SIGTERM);
		waitpid(pid, NULL, 0);
		send_error(fd, r, 500);
		return;
	}

	now = time(NULL);
	strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S GMT", gmtime(&now));
	snprintf(hdr, sizeof(hdr), "HTTP/1.0 200 OK\r\nDate: %s\r\nServer: %s\r\n",
	    date, SERVER_NAME);
	r->status = 200;
	if (write_all(fd, hdr, strlen(hdr)) != 0 ||
	    write_all(fd, buf, body - buf) != 0 || r->is_head)
		goto done;

	/* body */
	if (write_all(fd, body, len - (body - buf)) != 0)
		goto done;
	r->sent += len - (body - buf);
	while ((n = read(p[0], buf, sizeof(buf))) != 0) {
		if (n < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (write_all(fd, buf, n) != 0)
			break;
		r->sent += n;
	}
	eof = (n == 0);

done:
	close(p[0]);
	if (!eof)
		kill(pid, SIGTERM);
	waitpid(pid, NULL, 0);
}
