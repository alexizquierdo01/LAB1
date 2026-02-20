#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parsePGM.h"
#define BUFF_SIZE 1024




typedef struct {
    char* path;
    int offset;
    int bytesToRead;
    int maxval;
    unsigned int* local_histogram;
} ThreadInfo;



void* worker(void* arg) {
    ThreadInfo* info = (ThreadInfo*)arg;

    int fd = open(info->path, O_RDONLY);
    if (fd < 0) {
        perror("open");
        pthread_exit(NULL);
    }

    lseek(fd, info->offset, SEEK_SET);

    unsigned char buffer[BUFF_SIZE];
    int remaining = info->bytesToRead;

    while (remaining > 0) {
        int toRead = remaining > BUFF_SIZE ? BUFF_SIZE : remaining;
        int n = read(fd, buffer, toRead);
        if (n <= 0) break;

        for (int i = 0; i < n; i++) {
            info->local_histogram[buffer[i]]++;
        }

        remaining -= n;
    }
    close(fd);
    pthread_exit(NULL);
}




int main(int argc, char* argv[]) {
    if (argc != 4) {
        printf("Usage: histogram pathToImage pathToHistogramOut nThreads\n");
        return 1;
    }

    char* imagePath = argv[1];
    char* outPath = argv[2];
    int nThreads = atoi(argv[3]);

    int width, height, maxval;
    int headerSize = parse_pgm_header(imagePath, &width, &height, &maxval);
    if (headerSize < 0 || maxval > 255) {
        perror("Invalid PGM file");
        return 1;
    }



    int nPixels = width * height;
    pthread_t threads[nThreads];
    ThreadInfo threadInfo[nThreads];
    unsigned int* global_histogram = calloc(maxval, sizeof(unsigned int));
    int baseChunk = nPixels / nThreads;
    int remainder = nPixels % nThreads;
    int offset = headerSize;



    for (int i = 0; i < nThreads; i++) {
        int chunk = baseChunk;
        if (i == nThreads - 1)
            chunk += remainder;

        threadInfo[i].path = imagePath;
        threadInfo[i].offset = offset;
        threadInfo[i].bytesToRead = chunk;
        threadInfo[i].maxval = maxval;
        threadInfo[i].local_histogram = calloc(maxval, sizeof(unsigned int));

        pthread_create(&threads[i], NULL, worker, &threadInfo[i]);

        offset += chunk;
    }

    for (int i = 0; i < nThreads; i++) {
        pthread_join(threads[i], NULL);
    }


    for (int t = 0; t < nThreads; t++) {
        for (int i = 0; i < maxval; i++) {
            global_histogram[i] += threadInfo[t].local_histogram[i];
        }
        free(threadInfo[t].local_histogram);
    }

    int fd_out = open(outPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_out < 0) {
        perror("open output");
        return 1;
    }

    
    for (int i = 0; i < maxval; i++) {
        char line[64];
        int len = sprintf(line, "%d,%d\n", i, global_histogram[i]);
        write(fd_out, line, len);
    }

    close(fd_out);
    free(global_histogram);

    return 0;
}