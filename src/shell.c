#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "circularBuffer.h"
#include "splitCommand.h"

#define BUFFER_SIZE 1024

static void reap_zombies_nonblocking(void) {
    while (waitpid(-1, NULL, WNOHANG) > 0) {
        // clean finished background children
    }
}

static int safe_read_stdin(unsigned char *buf, int n) {
    int r = (int)read(STDIN_FILENO, buf, n);
    if (r < 0) perror("read");
    return r;
}

int main(void) {
    CircularBuffer cb;
    if (buffer_init(&cb, BUFFER_SIZE) != 0) {
        perror("buffer_init");
        return 1;
    }

    unsigned char readBuffer[BUFFER_SIZE];
    int reachedEOF = 0;

    while (!reachedEOF) {
        // Reap finished background processes (CONCURRENT)
        reap_zombies_nonblocking();

        int bytesRead = safe_read_stdin(readBuffer, BUFFER_SIZE);
        if (bytesRead < 0) {
            buffer_deallocate(&cb);
            return 1;
        }
        if (bytesRead == 0) {
            reachedEOF = 1;
        }

        for (int i = 0; i < bytesRead; i++) {
            if (buffer_free_bytes(&cb) > 0) {
                buffer_push(&cb, readBuffer[i]);
            }
            // If buffer gets full, bytes would be dropped; with reasonable test lines, OK.
        }

        int lineSize;
        while ((lineSize = buffer_size_next_element(&cb, '\n', reachedEOF)) > 0) {

            char line[BUFFER_SIZE];
            int j = 0;

            for (int k = 0; k < lineSize; k++) {
                unsigned char c = buffer_pop(&cb);
                if (c != '\n') {
                    if (j < (int)sizeof(line) - 1) {
                        line[j++] = (char)c;
                    }
                }
            }
            line[j] = '\0';

            if (strcmp(line, "EXIT") == 0) {
                buffer_deallocate(&cb);
                return 0;
            }

            // Accept both PIPED (as statement) and PIPE (as examples)
            int is_single = (strcmp(line, "SINGLE") == 0);
            int is_conc   = (strcmp(line, "CONCURRENT") == 0);
            int is_piped  = (strcmp(line, "PIPED") == 0) || (strcmp(line, "PIPE") == 0);

            if (!is_single && !is_conc && !is_piped) {
                // Unknown mode -> ignore
                continue;
            }

            // Ensure command1 line is available (read more if needed)
            while ((lineSize = buffer_size_next_element(&cb, '\n', reachedEOF)) <= 0) {
                if (reachedEOF) break;
                bytesRead = safe_read_stdin(readBuffer, BUFFER_SIZE);
                if (bytesRead < 0) {
                    buffer_deallocate(&cb);
                    return 1;
                }
                if (bytesRead == 0) { reachedEOF = 1; break; }
                for (int i = 0; i < bytesRead; i++) {
                    if (buffer_free_bytes(&cb) > 0) buffer_push(&cb, readBuffer[i]);
                }
            }
            if (lineSize <= 0) break; // EOF / no command

            char command1[BUFFER_SIZE];
            j = 0;
            for (int k = 0; k < lineSize; k++) {
                unsigned char c = buffer_pop(&cb);
                if (c != '\n') {
                    if (j < (int)sizeof(command1) - 1) command1[j++] = (char)c;
                }
            }
            command1[j] = '\0';

            if (is_single || is_conc) {
                pid_t pid = fork();
                if (pid < 0) {
                    perror("fork");
                    buffer_deallocate(&cb);
                    return 1;
                }

                if (pid == 0) {
                    char **argv = split_command(command1);
                    if (!argv || !argv[0]) {
                        fprintf(stderr, "Invalid command\n");
                        free(argv);
                        _exit(1);
                    }
                    execvp(argv[0], argv);
                    perror("execvp");
                    free(argv);
                    _exit(1);
                }

                if (is_single) {
                    waitpid(pid, NULL, 0);
                }
                // CONCURRENT: do not wait here
                continue;
            }

            // PIPED: read command2
            while ((lineSize = buffer_size_next_element(&cb, '\n', reachedEOF)) <= 0) {
                if (reachedEOF) break;
                bytesRead = safe_read_stdin(readBuffer, BUFFER_SIZE);
                if (bytesRead < 0) {
                    buffer_deallocate(&cb);
                    return 1;
                }
                if (bytesRead == 0) { reachedEOF = 1; break; }
                for (int i = 0; i < bytesRead; i++) {
                    if (buffer_free_bytes(&cb) > 0) buffer_push(&cb, readBuffer[i]);
                }
            }
            if (lineSize <= 0) break; // EOF / no second command

            char command2[BUFFER_SIZE];
            j = 0;
            for (int k = 0; k < lineSize; k++) {
                unsigned char c = buffer_pop(&cb);
                if (c != '\n') {
                    if (j < (int)sizeof(command2) - 1) command2[j++] = (char)c;
                }
            }
            command2[j] = '\0';

            int pipefd[2];
            if (pipe(pipefd) < 0) {
                perror("pipe");
                buffer_deallocate(&cb);
                return 1;
            }

            pid_t pid1 = fork();
            if (pid1 < 0) {
                perror("fork");
                close(pipefd[0]); close(pipefd[1]);
                buffer_deallocate(&cb);
                return 1;
            }
            if (pid1 == 0) {
                if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
                    perror("dup2");
                    _exit(1);
                }
                close(pipefd[0]);
                close(pipefd[1]);

                char **argv1 = split_command(command1);
                if (!argv1 || !argv1[0]) {
                    fprintf(stderr, "Invalid command\n");
                    free(argv1);
                    _exit(1);
                }
                execvp(argv1[0], argv1);
                perror("execvp");
                free(argv1);
                _exit(1);
            }

            pid_t pid2 = fork();
            if (pid2 < 0) {
                perror("fork");
                close(pipefd[0]); close(pipefd[1]);
                waitpid(pid1, NULL, 0);
                buffer_deallocate(&cb);
                return 1;
            }
            if (pid2 == 0) {
                if (dup2(pipefd[0], STDIN_FILENO) < 0) {
                    perror("dup2");
                    _exit(1);
                }
                close(pipefd[1]);
                close(pipefd[0]);

                char **argv2 = split_command(command2);
                if (!argv2 || !argv2[0]) {
                    fprintf(stderr, "Invalid command\n");
                    free(argv2);
                    _exit(1);
                }
                execvp(argv2[0], argv2);
                perror("execvp");
                free(argv2);
                _exit(1);
            }

            close(pipefd[0]);
            close(pipefd[1]);

            waitpid(pid1, NULL, 0);
            waitpid(pid2, NULL, 0);
        }
    }

    buffer_deallocate(&cb);
    return 0;
}
