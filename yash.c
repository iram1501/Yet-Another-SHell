#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <readline/readline.h>

#define MAX_JOBS 20

enum job_state {
    RUNNING,
    STOPPED
};

struct job {
    int active;
    int job_id;

    pid_t pgid;
    pid_t pid1;
    pid_t pid2;

    int process_count;
    int finished_count;

    enum job_state state;

    char command[2001];
};

struct job jobs[MAX_JOBS] = {0};

int next_job_id = 1;

volatile sig_atomic_t child_changed = 0;

void handle_sigchld(int sig) {
    child_changed = 1;
}

void add_job(pid_t pid, pid_t pgid, char *command) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].active == 0) {
            jobs[i].active = 1;

            jobs[i].job_id = next_job_id;
            next_job_id++;

            jobs[i].pgid = pgid;

            jobs[i].pid1 = pid;
            jobs[i].pid2 = 0;

            jobs[i].process_count = 1;
            jobs[i].finished_count = 0;

            jobs[i].state = RUNNING;

            strcpy(jobs[i].command, command);

            return;
        }
    }
}

void print_jobs() {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].active == 0) {
            continue;
        }

        printf("[%d] ", jobs[i].job_id);

        if (jobs[i].state == RUNNING) {
            printf("Running ");
        } else {
            printf("Stopped ");
        }

        printf("%s\n", jobs[i].command);
    }
}

struct job *find_job_by_pid(pid_t pid) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].active == 0) {
            continue;
        }

        if (jobs[i].pid1 == pid || jobs[i].pid2 == pid) {
            return &jobs[i];
        }
    }

    return NULL;
}

void remove_job(struct job *job) {
    job->active = 0;
}

int main () {
    char *input;

    struct sigaction sa;

    sa.sa_handler = handle_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGCHLD, &sa, NULL);

    while (1) {
        if (child_changed) {
            child_changed = 0;

            int status;
            pid_t changed_pid;

            while ((changed_pid = waitpid(-1, &status, WNOHANG)) > 0) {
                struct job *job = find_job_by_pid(changed_pid);

                if (job != NULL) {
                    if (WIFEXITED(status) || WIFSIGNALED(status)) {
                        job->finished_count++;

                        if (job->finished_count == job->process_count) {
                            remove_job(job);
                        }
                    }
                }
            }
        }

        input  = readline("# ");

        if (input == NULL) {
            break;
        }

        char command[2001];
        strcpy(command, input);

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

        int background = 0;
        
        if (i > 0 && strcmp(args[i - 1], "&") == 0) {
            background = 1;
            args[i - 1] = NULL;
            i--;
        }

        char *output_file = NULL;
        char *input_file = NULL;
        char *error_file = NULL;

        int write_index = 0;

        for(int read_index = 0; read_index < i; read_index++) {
            if (strcmp(args[read_index], "<") == 0) {
                input_file = args[read_index + 1];
                read_index++;
            } else if (strcmp(args[read_index], ">") == 0) {
                output_file = args[read_index + 1];
                read_index++;
            } else if (strcmp(args[read_index], "2>") == 0) {
                error_file = args[read_index + 1];
                read_index++;
            } else {
                args[write_index] = args[read_index];
                write_index++;
            }
        }

        args[write_index] = NULL;

        if(strcmp(args[0], "jobs") == 0) {
            print_jobs();

            free(input);
            continue;
        }

        pid_t pid = fork();

        if (pid == 0) {
            if (output_file != NULL) {
                int fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);

                if (fd < 0) {
                    perror("open");
                    _exit(1);
                }

                dup2(fd, STDOUT_FILENO);
                close(fd);
            }

            if (input_file != NULL) {
                int fd = open(input_file, O_RDONLY);

                if (fd < 0) {
                    perror("open");
                    _exit(1);
                }

                dup2(fd, STDIN_FILENO);
                close(fd);
            }

            if (error_file != NULL) {
                int fd = open(error_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);

                if (fd < 0) {
                    perror("open");
                    _exit(1);
                }

                dup2(fd, STDERR_FILENO);
                close(fd);
            }

            execvp(args[0], args);

            perror("execvp");
            _exit(1);
        } else {
            if (background) {
                add_job(pid, pid, command);
                //print_jobs();
            } else {
                waitpid(pid, NULL, 0);
            }
        }

        free(input);
    }

    return 0;
}