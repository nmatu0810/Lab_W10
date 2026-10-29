#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <arpa/inet.h>

#include "sws.h"

struct config cfg = { NULL, NULL, NULL, NULL, DEFAULT_PORT, 0 };

void server_loop(void);

static void
usage(void)
{
    fprintf(stderr,
        "usage: sws [-dh] [-c dir] [-i address] "
        "[-l file] [-p port] dir\n");
    exit(EXIT_FAILURE);
}

int
main(int argc, char **argv)
{
    int ch;
    struct stat st;
    char *end;
    long port;
    char *resolved_docroot;
    char *resolved_cgidir;
    char *resolved_logfile;

    while ((ch = getopt(argc, argv, "c:dhi:l:p:")) != -1) {
        switch (ch) {
        case 'c':
            cfg.cgidir = optarg;
            break;
        case 'd':
            cfg.debug = 1;
            break;
        case 'h':
            usage();
            break;
        case 'i':
            cfg.addr = optarg;
            break;
        case 'l':
            cfg.logfile = optarg;
            break;
        case 'p':
            errno = 0;
            port = strtol(optarg, &end, 10);

            if (errno != 0 || *optarg == '\0' ||
                *end != '\0' || port < 1 || port > 65535) {
                fprintf(stderr, "Invalid port: %s\n", optarg);
                return EXIT_FAILURE;
            }

            cfg.port = (int)port;
            break;
        default:
            usage();
        }
    }

    argc -= optind;
    argv += optind;

    if (argc != 1)
        usage();

    cfg.docroot = argv[0];

   resolved_docroot = realpath(cfg.docroot, NULL);

if (resolved_docroot == NULL) {
    perror("realpath docroot");
    return EXIT_FAILURE;
}

cfg.docroot = resolved_docroot;

if (cfg.cgidir != NULL) {
    resolved_cgidir = realpath(cfg.cgidir, NULL);

    if (resolved_cgidir == NULL) {
        perror("realpath CGI directory");
        return EXIT_FAILURE;
    }

    cfg.cgidir = resolved_cgidir;
}

if (cfg.logfile != NULL) {
    resolved_logfile = cfg.logfile;

    if (cfg.logfile[0] != '/') {
        char cwd[PATH_MAX];
        char path[PATH_MAX];
        int n;

        if (getcwd(cwd, sizeof(cwd)) == NULL) {
            perror("getcwd");
            return EXIT_FAILURE;
        }

        n = snprintf(path, sizeof(path), "%s/%s",
                     cwd, cfg.logfile);

        if (n < 0 || (size_t)n >= sizeof(path)) {
            fprintf(stderr, "Log path too long\n");
            return EXIT_FAILURE;
        }

        resolved_logfile = strdup(path);

        if (resolved_logfile == NULL) {
            perror("strdup");
            return EXIT_FAILURE;
        }
    }

    cfg.logfile = resolved_logfile;
}

    if (stat(cfg.docroot, &st) == -1) {
        perror("stat docroot");
        return EXIT_FAILURE;
    }

    if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, "docroot is not a directory\n");
        return EXIT_FAILURE;
    }

    if (cfg.addr != NULL) {
    struct in_addr test_addr;

    if (inet_pton(AF_INET, cfg.addr, &test_addr) != 1) {
        fprintf(stderr, "Invalid IPv4 address: %s\n",
                cfg.addr);
        return EXIT_FAILURE;
    }
}

    if (cfg.debug) {
        printf("SWS starting...\n");
        printf("Port: %d\n", cfg.port);
        printf("Address: %s\n",
               cfg.addr ? cfg.addr : "0.0.0.0");
        printf("Document root: %s\n", cfg.docroot);
        printf("CGI directory: %s\n",
               cfg.cgidir ? cfg.cgidir : "(none)");
        printf("Log file: %s\n",
               cfg.logfile ? cfg.logfile : "(none)");
        fflush(stdout);
    } else {
        if (daemon(0, 0) == -1) {
            perror("daemon");
            return EXIT_FAILURE;
        }
    }

    server_loop();

    return EXIT_SUCCESS;
}
