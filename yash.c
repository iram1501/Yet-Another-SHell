#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <readline/readline.h>

int main () {
    char *input;

    while (1) {
        input  = readline("# ");

        if (input == NULL) {
            break;
        }

        char *args[100];
        int i = 0;

        char *token = strtok(input, " ");

        while (token != NULL) {
            args[i] = token;
            i++;

            token = strtok(NULL, " ");
        }

        args[i] = NULL;

        if (i == 0) {
            free(input);
            continue;
        }

        pid_t pid = fork();

        if (pid == 0) {
            execvp(args[0], args);

            perror("execvp");
            _exit(1);
        } else {
            waitpid(pid, NULL, 0);
        }

        free(input);
    }

    return 0;
}