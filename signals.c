#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

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

int main () {
    struct sigaction sa;

    sa.sa_handler = handle_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGCHLD, &sa, NULL);

    sigset_t mask;
    sigset_t oldmask;

    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);

    sigprocmask(SIG_BLOCK, &mask, &oldmask);

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");

        sigprocmask(SIG_SETMASK, &oldmask, NULL);
    } else if (pid == 0) {
        sigprocmask(SIG_SETMASK, &oldmask, NULL);

        setpgid(0, 0);
        sleep(1);
        return 0;
    } else {
        setpgid(pid, pid);
        add_job(pid, pid, "sleep 5 &");
        sigprocmask(SIG_SETMASK, &oldmask, NULL);
        print_jobs();

        for (int i = 0; i < 10; i++) {
            printf("Parent still running...\n");

            sleep(1);

            if (child_changed) {
                child_changed = 0;
                int status;
                pid_t changed_pid;

                while ((changed_pid = waitpid(-1, &status, WNOHANG)) > 0){
                    struct job *job;

                    job = find_job_by_pid(changed_pid);

                    if (job != NULL) {
                        if (WIFEXITED(status) || WIFSIGNALED(status)) {
                            job->finished_count++;

                            if(job->finished_count == job->process_count) {
                                remove_job(job);
                            }
                        }
                    }
                }
            } 
        }
    }
}