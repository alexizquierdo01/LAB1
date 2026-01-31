#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "circularBuffer.h"

int main(int argc, char *argv[]) {

    if (argc != 4) {
        fprintf(stderr, "Usage: %s binary|text path bufferSize\n", argv[0]);
        return 1;
    }

    int is_binary = strcmp(argv[1], "binary") == 0;
    int is_text   = strcmp(argv[1], "text") == 0;

    if (!is_binary && !is_text) {
        fprintf(stderr, "First argument must be 'binary' or 'text'\n");
        return 1;
    }

    int fd = open(argv[2], O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    int bufferSize = atoi(argv[3]);
    unsigned char *readBuffer = malloc(bufferSize);
    if (!readBuffer) {
        perror("malloc");
        return 1;
    }

    long long sum = 0;


    if (is_binary) {

        int adjustedSize = (bufferSize / sizeof(int)) * sizeof(int);
        if (adjustedSize == 0)
            adjustedSize = sizeof(int);

        int bytesRead;
        while ((bytesRead = read(fd, readBuffer, adjustedSize)) > 0) {
            int *numbers = (int *)readBuffer;
            int count = bytesRead / sizeof(int);

            for (int i = 0; i < count; i++) {
                sum += numbers[i];
            }
        }
    }


    if (is_text) {

        CircularBuffer cb;
        if (buffer_init(&cb, bufferSize) != 0) {
            fprintf(stderr, "Error initializing circular buffer\n");
            return 1;
        }

        int reachedEOF = 0;
        int bytesRead;

        while (!reachedEOF) {

            bytesRead = read(fd, readBuffer, bufferSize);
            if (bytesRead == 0)
                reachedEOF = 1;

            for (int i = 0; i < bytesRead; i++) {
                if (buffer_free_bytes(&cb) > 0) {
                    buffer_push(&cb, readBuffer[i]);
                }
            }

            int elemSize;
            while ((elemSize = buffer_size_next_element(&cb, ',', reachedEOF)) > 0) {

                char numberStr[64];
                int j = 0;

                for (int k = 0; k < elemSize; k++) {
                    unsigned char c = buffer_pop(&cb);
                    if (c != ',' && c != '\n') {
                        numberStr[j++] = c;
                    }
                }

                numberStr[j] = '\0';
                sum += atoll(numberStr);
            }
        }

        buffer_deallocate(&cb);
    }

    printf("%lld\n", sum);

    free(readBuffer);
    close(fd);

    return 0;
}