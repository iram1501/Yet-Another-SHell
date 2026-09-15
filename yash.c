#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <readline/readline.h>
#include <errno.h>

#define MAX_JOBS 20

enum job_state {
    RUNNING,
    STOPPED,
    DONE
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

volatile sig_atomic_t child_changed = 0;

void handle_sigchld(int sig) {
    child_changed = 1;
}

int get_next_job_id() {
    int highest = 0;

    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].active && jobs[i].job_id > highest) {
            highest = jobs[i].job_id;
        }
    }

    return highest + 1;
}

void add_job(pid_t pid, pid_t pgid, char *command) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].active == 0) {
            jobs[i].job_id = get_next_job_id();
            jobs[i].active = 1;

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

struct job *find_recent_job(void);

void print_jobs() {
    struct job *recent = find_recent_job();

    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].active == 0) {
            continue;
        }

        printf("[%d]", jobs[i].job_id);

        if (recent == &jobs[i]) {
            printf ("+ ");
        } else {
            printf("- ");
        }

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

struct job *find_recent_stopped_job() {
    struct job *recent = NULL;

    for (int i = 0; i < MAX_JOBS; i++) {
        if(jobs[i].active == 0) {
            continue;
        }

        if(jobs[i].state != STOPPED) {
            continue;
        }

        if(recent == NULL || jobs[i].job_id > recent->job_id) {
            recent = &jobs[i];
        }
    }

    return recent;
}

struct job *find_recent_job() {
    struct job *recent = NULL;

    for (int i = 0; i < MAX_JOBS; i++) {
        if(jobs[i].active == 0) {
            continue;
        }

        if(jobs[i].state == DONE) {
            continue;
        }

        if (recent == NULL || jobs[i].job_id > recent->job_id) {
            recent = &jobs[i];
        }
    }

    return recent;
}

void add_pipeline_job(pid_t pid1, pid_t pid2, pid_t pgid, char *command) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if(jobs[i].active == 0) {
            jobs[i].job_id = get_next_job_id();
            jobs[i].active = 1;

            jobs[i].pgid = pgid;

            jobs[i].pid1 = pid1;
            jobs[i].pid2 = pid2;

            jobs[i].process_count = 2;
            jobs[i].finished_count = 0;

            jobs[i].state = RUNNING;

            strcpy(jobs[i].command, command);

            return;
        }
    }
}

void print_done_jobs() {
    struct job *recent = NULL;

    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].active == 0) {
            continue;
        }

        if (recent == NULL || jobs[i].job_id > recent->job_id) {
            recent = &jobs[i];
        }
    }

    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].active == 0) {
            continue;
        }

        if (jobs[i].state != DONE) {
            continue;
        }

        printf("[%d]", jobs[i].job_id);

        if (recent == &jobs[i]) {
            printf("+ ");
        } else {
            printf("- ");
        }

        printf("Done %s\n", jobs[i].command);

        remove_job(&jobs[i]);
    }
}

int main () {
    char *input;

    sigset_t child_mask;

    sigemptyset(&child_mask);
    sigaddset(&child_mask, SIGCHLD);

    struct sigaction sa;

    sa.sa_handler = handle_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    sigaction(SIGCHLD, &sa, NULL);

    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);

    while (1) {
        if (child_changed) {
            child_changed = 0;

            int status;
            pid_t changed_pid;

            while ((changed_pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
                struct job *job = find_job_by_pid(changed_pid);

                if (job != NULL) {
                    if (WIFEXITED(status) || WIFSIGNALED(status)) {
                        job->finished_count++;

                        if (job->finished_count == job->process_count) {
                            job->state = DONE;
                        }
                    } else if (WIFSTOPPED(status)) {
                    job->state = STOPPED;
                    } else if (WIFCONTINUED(status)) {
                    job->state = RUNNING;
                    }
                }  
            }
        }

        input  = readline("# ");

        print_done_jobs();

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

        int pipe_index = -1;

        for (int j = 0; j < i; j++) {
            if(strcmp(args[j], "|") == 0) {
                pipe_index = j;
                break;
            }
        }

        if (pipe_index != -1) {
            args[pipe_index] = NULL;

            char **left_args = args;
            char **right_args = &args[pipe_index + 1];

            int pipefd[2];

            if (pipe(pipefd) < 0) {
                perror("pipe");
                free(input);
                continue;
            }

            sigset_t old_mask;

            sigprocmask(SIG_BLOCK, &child_mask, &old_mask);

            pid_t left_pid = fork();

            if (left_pid < 0) {
                perror("fork");
                close(pipefd[0]);
                close(pipefd[1]);

                sigprocmask(SIG_SETMASK, &old_mask, NULL);

                free(input);
                continue;
            } else if (left_pid == 0) {
                sigprocmask(SIG_SETMASK, &old_mask, NULL);

                setpgid(0, 0);

                signal(SIGINT, SIG_DFL);
                signal(SIGTSTP, SIG_DFL);
                signal(SIGTTOU, SIG_DFL);

                dup2(pipefd[1], STDOUT_FILENO);

                close(pipefd[0]);
                close(pipefd[1]);

                execvp(left_args[0], left_args);

                perror("execvp");
                _exit(1);
            } else {
                setpgid(left_pid, left_pid);
            }

            pid_t right_pid = fork();

            if(right_pid < 0) {
                perror("fork");

                close(pipefd[0]);
                close(pipefd[1]);

                sigprocmask(SIG_SETMASK, &old_mask, NULL);
                
                free(input);
                continue;
            } else if (right_pid == 0) {
                sigprocmask(SIG_SETMASK, &old_mask, NULL);

                setpgid(0, left_pid);

                signal(SIGINT, SIG_DFL);
                signal(SIGTSTP, SIG_DFL);
                signal(SIGTTOU, SIG_DFL);

                dup2(pipefd[0], STDIN_FILENO);

                close(pipefd[0]);
                close(pipefd[1]);

                execvp(right_args[0], right_args);

                perror("execvp");
                _exit(1);
            } else {
                setpgid(right_pid, left_pid);

                close(pipefd[0]);
                close(pipefd[1]);

                sigprocmask(SIG_SETMASK, &old_mask, NULL);

                int status;
                int finished = 0;
                int stopped = 0;

                tcsetpgrp(STDIN_FILENO, left_pid);

                while (finished + stopped < 2) {
                    pid_t changed_pid = waitpid(-left_pid, &status, WUNTRACED);

                    if(changed_pid < 0) {
                        break;
                    }

                    if(WIFEXITED(status) || WIFSIGNALED(status)) {
                        finished++;
                    } else if  (WIFSTOPPED(status)) {
                        stopped++;
                    }
                }

                tcsetpgrp(STDIN_FILENO, getpgrp());

                if (stopped > 0) {
                    add_pipeline_job(left_pid, right_pid, left_pid, command);

                    struct job *job = find_job_by_pid(left_pid);

                    if (job != NULL) {
                        job->state = STOPPED;
                        job->finished_count = finished;
                    }

                    printf("\n");
                } 

                free(input);
                continue;
            }
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

        if(strcmp(args[0], "bg") == 0) {
            struct job *job = find_recent_stopped_job();

            if(job != NULL) {
                kill(-job->pgid, SIGCONT);
                job->state = RUNNING;

                size_t len = strlen(job->command);

                if (len == 0 || job->command[len - 1] != '&') {
                    strcat(job->command, " &");
                }

                printf("[%d]+ Running %s\n", job->job_id, job->command);
            }

            free(input);
            continue;
        }

        if(strcmp(args[0], "fg") == 0) {
            struct job *job = find_recent_job();

            if (job != NULL) {
                int status;
                int finished = job->finished_count;
                int stopped = 0;

                printf("%s\n", job->command);

                tcsetpgrp(STDIN_FILENO, job->pgid);

                if (job->state == STOPPED) {
                    kill(-job->pgid, SIGCONT);
                    job->state = RUNNING;
                }

                while (finished < job->process_count) {
                    pid_t changed_pid = waitpid(-job->pgid, &status, WUNTRACED);

                    if(changed_pid < 0) {
                        if (errno == EINTR) {
                            continue;
                        }
                        perror("waitpid");
                        break;
                    }

                    if (WIFEXITED(status)) {
                        finished++;
                    } else if (WIFSIGNALED(status)) {
                        finished++;

                        if (WTERMSIG(status) == SIGINT) {
                            printf("^C\n");
                        }
                    } else if (WIFSTOPPED(status)) {
                        stopped = 1;
                        break;
                    }
                }

                tcsetpgrp(STDIN_FILENO, getpgrp());

                if (stopped) {
                    job->state = STOPPED;
                    job->finished_count = finished;
                    printf("\n");
                } else {
                    remove_job(job);
                }
            }

            free(input);
            continue;
        }

        sigset_t old_mask;

        sigprocmask(SIG_BLOCK, &child_mask, &old_mask);

        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");
            sigprocmask(SIG_SETMASK, &old_mask, NULL);
        }else if (pid == 0) {
            sigprocmask(SIG_SETMASK, &old_mask, NULL);

            setpgid(0, 0);

            signal(SIGINT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);
            signal(SIGTTOU, SIG_DFL);

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
            setpgid(pid, pid);

            if (background) {
                add_job(pid, pid, command);
                
                sigprocmask(SIG_SETMASK, &old_mask, NULL);
            } else {
                sigprocmask(SIG_SETMASK, &old_mask, NULL);

                int status;

                tcsetpgrp(STDIN_FILENO, pid);

                waitpid(pid, &status, WUNTRACED);

                tcsetpgrp(STDIN_FILENO, getpgrp());

                if(WIFSTOPPED(status)) {
                    add_job(pid, pid, command);

                    struct job *job = find_job_by_pid(pid);

                    if(job != NULL) {
                        job->state = STOPPED;
                    }

                    printf("\n");
                }else if(WIFSIGNALED(status)) {
                    if (WTERMSIG(status) == SIGINT){
                        printf("^C\n");
                    }
                }
            }
        }

        free(input);
    }

    return 0;
}