#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sws.h"

void
server_loop(void)
{
	/* TODO: socket, bind, listen, accept, fork moi ket noi, SIGCHLD */
}

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
	close(fd);
}

void
log_request(const struct sockaddr *client, const struct request *r)
{
	/* TODO: ip, thoi gian, dong request, status, size */
	(void)client;
	(void)r;
}
