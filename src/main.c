#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include "eprintf.h"
#include "respond.h"
#include "magicfile.h"

static int port, sockfd, cfd;

static void SigintHandler(int signo) {
    (void)signo;
    eprintf("interupt");
}

int main(int argc, char *argv[]) {
    setprogname(argv[0]);
    if (argc > 1) {
        port = atoi(argv[1]);
        if (port <= 0 || port >= (1<<16))
            eprintf("invalid port number");
    } else {
        port = 2222;
    }
    InitMagic();
    if (signal(SIGINT, SigintHandler) == SIG_ERR)
        eprintf("setting signal handler:");
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
        eprintf("creating socket:");
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
        eprintf("binding on 127.0.0.1:%d:", port);
    if (listen(sockfd, 0) == -1)
        eprintf("listen on socket:");
    weprintf("[%d]: listening on 127.0.0.1:%d...", getpid(), port);
    while ((cfd = accept(sockfd, NULL, NULL)) != -1) {
        weprintf("new connection");
        Respond(cfd);
        close(cfd);
    }
    eprintf("accept connection:");
    /* not reached */
}
