#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#include "circularBuffer.h"
#include "splitCommand.h"

#define BUFFER_SIZE 1024

int main() {

    CircularBuffer cb;
    if (buffer_init(&cb, BUFFER_SIZE) != 0) {
        perror("Buffer init");
        return 1;
    }

    unsigned char readBuffer[BUFFER_SIZE];
    int reachedEOF = 0;

    while (!reachedEOF) {

        int bytesRead = read(STDIN_FILENO, readBuffer, BUFFER_SIZE);

        if (bytesRead == 0) {
            reachedEOF = 1;
        }

        for (int i = 0; i < bytesRead; i++) {
            if (buffer_free_bytes(&cb) > 0) {
                buffer_push(&cb, readBuffer[i]);
            }
        }

        int lineSize;
        while ((lineSize = buffer_size_next_element(&cb, '\n', reachedEOF)) > 0) {

            char line[1024];
            int j = 0;

            for (int k = 0; k < lineSize; k++) {
                unsigned char c = buffer_pop(&cb);
                if (c != '\n')
                    line[j++] = c;
            }
            line[j] = '\0';

            if (strcmp(line, "EXIT") == 0) {
                buffer_deallocate(&cb);
                return 0;
            }

            if (strcmp(line, "SINGLE") == 0 ||
                strcmp(line, "CONCURRENT") == 0 ||
                strcmp(line, "PIPE") == 0) {

                char mode[32];
                strcpy(mode, line);

                while ((lineSize = buffer_size_next_element(&cb, '\n', reachedEOF)) <= 0) {
                    bytesRead = read(STDIN_FILENO, readBuffer, BUFFER_SIZE);
                    if (bytesRead == 0) {
                        reachedEOF = 1;
                        break;
                    }
                    for (int i = 0; i < bytesRead; i++) {
                        if (buffer_free_bytes(&cb) > 0)
                            buffer_push(&cb, readBuffer[i]);
                    }
                }

                char command1[1024];
                j = 0;
                for (int k = 0; k < lineSize; k++) {
                    unsigned char c = buffer_pop(&cb);
                    if (c != '\n')
                        command1[j++] = c;
                }
                command1[j] = '\0';

                if (strcmp(mode, "SINGLE") == 0 ||
                    strcmp(mode, "CONCURRENT") == 0) {

                    pid_t pid = fork();

                    if (pid == 0) {
                        char **argv = split_command(command1);
                        execvp(argv[0], argv);
                        perror("execvp");
                        exit(1);
                    }

                    if (strcmp(mode, "SINGLE") == 0) {
                        waitpid(pid, NULL, 0);
                    }
                }

                if (strcmp(mode, "PIPE") == 0) {

                    while ((lineSize = buffer_size_next_element(&cb, '\n', reachedEOF)) <= 0) {
                        bytesRead = read(STDIN_FILENO, readBuffer, BUFFER_SIZE);
                        if (bytesRead == 0) {
                            reachedEOF = 1;
                            break;
                        }
                        for (int i = 0; i < bytesRead; i++) {
                            if (buffer_free_bytes(&cb) > 0)
                                buffer_push(&cb, readBuffer[i]);
                        }
                    }

                    char command2[1024];
                    j = 0;
                    for (int k = 0; k < lineSize; k++) {
                        unsigned char c = buffer_pop(&cb);
                        if (c != '\n')
                            command2[j++] = c;
                    }
                    command2[j] = '\0';

                    int pipefd[2];
                    pipe(pipefd);

                    pid_t pid1 = fork();
                    if (pid1 == 0) {
                        dup2(pipefd[1], STDOUT_FILENO);
                        close(pipefd[0]);
                        close(pipefd[1]);

                        char **argv1 = split_command(command1);
                        execvp(argv1[0], argv1);
                        perror("execvp");
                        exit(1);
                    }

                    pid_t pid2 = fork();
                    if (pid2 == 0) {
                        dup2(pipefd[0], STDIN_FILENO);
                        close(pipefd[1]);
                        close(pipefd[0]);

                        char **argv2 = split_command(command2);
                        execvp(argv2[0], argv2);
                        perror("execvp");
                        exit(1);
                    }

                    close(pipefd[0]);
                    close(pipefd[1]);

                    waitpid(pid1, NULL, 0);
                    waitpid(pid2, NULL, 0);
                }
            }
        }
    }

    buffer_deallocate(&cb);
    return 0;
}
