#include <errno.h>
#include <netinet/ip.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include "eprintf.h"
#include "respond.h"

enum {
    MUX = 4,
};

static int port, sockfd, cfd;
static sem_t multiplex;

static void SigIntHandler(int signo) {
    (void)signo;
    eprintf("interupt");
}

static void SigChldHandler(int signo) {
    (void)signo;
    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0) {
        if (sem_post(&multiplex) != 0)
            eprintf("posting on semaphore:");
    }
    errno = saved_errno;
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
    /* Initialize semaphore */
    if (sem_init(&multiplex, 1, MUX) != 0)
        eprintf("initializing semaphore:");
    /* Setup SIGINT and SIGCHLD handlers */
    struct sigaction sigact;
    sigact.sa_handler = SigIntHandler;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = SA_RESTART;
    if (sigaction(SIGINT, &sigact, NULL) == -1)
        eprintf("setting sigint handler:");
    sigact.sa_handler = SigChldHandler;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sigact, NULL) == -1)
        eprintf("setting sigchld handler:");
    /* Setup server socket */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
        eprintf("creating socket:");
    int reuseaddr_opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_opt, sizeof(reuseaddr_opt)) == -1)
        eprintf("set socket options:");
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
        eprintf("binding on 127.0.0.1:%d:", port);
    if (listen(sockfd, 0) == -1)
        eprintf("listen on socket:");
    weprintf("[%d]: listening on http://127.0.0.1:%d ...", getpid(), port);
    /* Main loop */
    while ((cfd = accept(sockfd, NULL, NULL)) != -1) {
        weprintf("new connection accepted");
        if (sem_wait(&multiplex) != 0)
            eprintf("waiting on semaphore:");
        pid_t pid;
        if ((pid = fork()) == -1)
            eprintf("fork:");
        else if (pid == 0) {
            /* child */
            Respond(cfd);
            close(cfd);
            exit(0);
        } else {
            /* parent */
        }
    }
    eprintf("accept connection:");
    /* not reached */
}
