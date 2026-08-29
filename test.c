#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

void restore_child_signals () {
    struct sigaction sa;

    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTSTP, &sa, NULL);
}

int main() {
    struct sigaction sa;

    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);

    pid_t pid = fork();

    if (pid == 0) {
        struct sigaction default_sa;

        default_sa.sa_handler = SIG_DFL;
        sigemptyset(&default_sa.sa_mask);
        default_sa.sa_flags = 0;

        sigaction(SIGINT, &default_sa, NULL);

        printf("Child running\n");

        while (1) {
            sleep(1);
        }
    } else {
        waitpid(pid, NULL, 0);

        printf("\nParent survived\n");
    }

    return 0;
}