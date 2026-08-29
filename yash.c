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
#define MAX_ARGS 1001

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
    (void)sig;
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

        if (recent == NULL || jobs[i].job_id > recent->job_id) {
            recent = &jobs[i];
        }
    }

    return recent;
}

void add_pipeline_job(pid_t pid1, pid_t pid2, pid_t pgid, char *command) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if(jobs[i].active == 0) {
            jobs[i].active = 1;
            jobs[i].job_id = next_job_id;
            next_job_id++;

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

void parse_redirection(char **args, char **input_file, char **output_file, char **error_file) {
	int read_index = 0;
	int write_index = 0;

	while (args[read_index] != NULL) {
		if (strcmp(args[read_index], "<") == 0) {
			*input_file = args[read_index + 1];
			read_index += 2;
		} else if (strcmp(args[read_index], ">") == 0) {
			*output_file = args[read_index + 1];
			read_index += 2;
		} else if (strcmp(args[read_index], "2>") == 0) {
			*error_file = args[read_index + 1];
			read_index += 2;
		} else {
			args[write_index] = args[read_index];
			write_index++;
			read_index++;
		}
	}

	args[write_index] = NULL;
}

void update_jobs() {
    int status;
    pid_t changed_pid;

    child_changed = 0;

    while((changed_pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        struct job *job = find_job_by_pid(changed_pid);

        if (job != NULL) {
            if (WIFEXITED(status) || WIFSIGNALED(status)) {
                job->finished_count++;

                if(job->finished_count == job->process_count) {
                    remove_job(job);
                }
            } else if (WIFSTOPPED(status)) {
                job->state = STOPPED;
            } else if (WIFCONTINUED(status)) {
                job->state = RUNNING;
            }
        }
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
        input  = readline("# ");

        if (child_changed) {
            child_changed = 0;
            update_jobs();
        }

        if (input == NULL) {
            for (int i = 0; i < MAX_JOBS; i++) {
                if (jobs[i].active) {
                    kill(-jobs[i].pgid, SIGKILL);
                }
            }

            while (waitpid(-1, NULL, 0) > 0){
                
            }

            break;
        }

        char command[2001];
        strcpy(command, input);

        char *args[MAX_ARGS];
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

        int pipe_index = -1;

        for (int j = 0; j < i; j++) {
            if(strcmp(args[j], "|") == 0) {
                pipe_index = j;
                break;
            }
        }

        if (pipe_index == -1) {
            parse_redirection(args, &input_file, &output_file, &error_file);
        } else if (pipe_index != -1) {
            args[pipe_index] = NULL;

            char **left_args = args;
            char **right_args = &args[pipe_index + 1];

            char *left_input = NULL;
            char *left_output = NULL;
            char *left_error = NULL;

            char *right_input = NULL;
            char *right_output = NULL;
            char *right_error = NULL;

            parse_redirection(left_args, &left_input, &left_output, &left_error);
            parse_redirection(right_args, &right_input, &right_output, &right_error);

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

                if(left_input != NULL) {
                    int fd = open(left_input, O_RDONLY, 0644);

                    if (fd < 0) {
                        perror("open");
                        _exit(1);
                    }

                    dup2(fd, STDIN_FILENO);
                    close(fd);
                }

                if(left_output != NULL) {
                    int fd = open(left_output, O_WRONLY | O_CREAT | O_TRUNC, 0644);

                    if (fd < 0) {
                        perror("open");
                        _exit(1);
                    }

                    dup2(fd, STDOUT_FILENO);
                    close(fd);
                }

                if(left_error != NULL) {
                    int fd = open(left_error, O_WRONLY | O_CREAT | O_TRUNC, 0644);

                    if (fd < 0) {
                        perror("open");
                        _exit(1);
                    }

                    dup2(fd, STDERR_FILENO);
                    close(fd);
                }

                close(pipefd[0]);
                close(pipefd[1]);

                execvp(left_args[0], left_args);

                write(STDOUT_FILENO, "\n", 1);
                _exit(1);
            } else {
                setpgid(left_pid, left_pid);
            }

            pid_t right_pid = fork();

            if(right_pid < 0) {
                perror("fork");

                close(pipefd[0]);
                close(pipefd[1]);

                kill(-left_pid, SIGKILL);
                waitpid(left_pid, NULL, 0);

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

                if (right_input != NULL) {
                    int fd = open(right_input, O_RDONLY);

                    if (fd < 0) {
                        perror("open");
                        _exit(1);
                    }

                    dup2(fd, STDIN_FILENO);
                    close(fd);
                }

                if (right_output != NULL) {
                    int fd = open(right_output, O_WRONLY | O_CREAT | O_TRUNC, 0644);

                    if (fd < 0) {
                        perror("open");
                        _exit(1);
                    }

                    dup2(fd, STDOUT_FILENO);
                    close(fd);
                }

                if (right_error != NULL) {
                    int fd = open(right_error, O_WRONLY | O_CREAT | O_TRUNC, 0644);

                    if (fd < 0) {
                        perror("open");
                        _exit(1);
                    }

                    dup2(fd, STDERR_FILENO);
                    close(fd);
                }

                close(pipefd[0]);
                close(pipefd[1]);

                execvp(right_args[0], right_args);

                write(STDOUT_FILENO, "\n", 1);
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
                        if (errno == EINTR) {
                            continue;
                        }

                        perror("waitpid");
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

                printf("[%d] Running %s\n", job->job_id, job->command);
            }

            free(input);
            continue;
        }

        if(strcmp(args[0], "fg") == 0) {
            struct job *job = find_recent_job();

            if (job != NULL) {
                int status;
                int finished = job->finished_count;
                int stopped_count = 0;
                int signaled = 0;
                int wait_failed = 0;

                printf("%s\n", job->command);

                tcsetpgrp(STDIN_FILENO, job->pgid);

                if (job->state == STOPPED) {
                    kill(-job->pgid, SIGCONT);
                    job->state = RUNNING;
                }

                while (finished + stopped_count < job->process_count) {
                    pid_t changed_pid = waitpid(-job->pgid, &status, WUNTRACED);

                    if(changed_pid < 0) {
                        if (errno == EINTR) {
                            continue;
                        }

                        perror("waitpid");
                        wait_failed = 1;
                        break;
                    }

                    if (WIFEXITED(status)) {
                        finished++;
                    } else if (WIFSIGNALED(status)) {
                        finished++;
                        signaled = 1;
                    } else if (WIFSTOPPED(status)) {
                        stopped_count++;
                    }
                }

                tcsetpgrp(STDIN_FILENO, getpgrp());

                if (stopped_count > 0) {
                    job->state = STOPPED;
                    job->finished_count = finished;
                    printf("\n");
                } else if (!wait_failed) {
                    remove_job(job);

                    if(signaled) {
                        printf("\n");
                    }
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

            write(STDOUT_FILENO, "\n", 1);
            _exit(1);
        } else {
            setpgid(pid, pid);

            if (background) {
                add_job(pid, pid, command);
                
                sigprocmask(SIG_SETMASK, &old_mask, NULL);
            } else {
                sigprocmask(SIG_SETMASK, &old_mask, NULL);

                int status;
                int wait_failed = 0;

                tcsetpgrp(STDIN_FILENO, pid);

                while (waitpid(pid, &status, WUNTRACED) < 0) {
                    if (errno == EINTR) {
                        continue;
                    }

                    perror("waitpid");
                    wait_failed = 1;
                    break;
                }

                tcsetpgrp(STDIN_FILENO, getpgrp());

                if(!wait_failed){
                    if(WIFSTOPPED(status)) {
                        add_job(pid, pid, command);

                        struct job *job = find_job_by_pid(pid);

                        if(job != NULL) {
                            job->state = STOPPED;
                        }

                        printf("\n");
                    } else if(WIFSIGNALED(status)) {
                        printf("\n");
                    }
                }
            }
        }

        free(input);
    }

    return 0;
}