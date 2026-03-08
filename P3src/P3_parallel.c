#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "parsePGM.h"

#define BUFF_SIZE 1024

typedef struct {
    const char* path;
    int offset;
    int bytesToRead;
    int nBins;
    unsigned int* local_histogram;
} ThreadInfo;

static void* worker(void* arg) {
    ThreadInfo* info = (ThreadInfo*)arg;
// Cada hilo abre el fichero por separado
    int fd = open(info->path, O_RDONLY);
    if (fd < 0) {
        perror("open");
        pthread_exit(NULL);
    }

    if (lseek(fd, info->offset, SEEK_SET) < 0) {
        perror("lseek");
        close(fd);
        pthread_exit(NULL);
    }

    unsigned char buffer[BUFF_SIZE];
    int remaining = info->bytesToRead;

  // bucle de lectura hasta consumir todos los bytes
    while (remaining > 0) {
        int toRead = (remaining > BUFF_SIZE) ? BUFF_SIZE : remaining;
        int n = read(fd, buffer, toRead);

        if (n < 0) {
            perror("read");
            break;
        }
        if (n == 0) {
            break; // EOF inesperado
        }

        for (int i = 0; i < n; i++) {
            unsigned char px = buffer[i];
            info->local_histogram[px]++;// incrementamos el bin
        }

        remaining -= n;
    }

    close(fd);
    pthread_exit(NULL);
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: histogram pathToImage pathToHistogramOut nThreads\n");
        return 1;
    }

    const char* imagePath = argv[1];
    const char* outPath   = argv[2];
    int nThreads          = atoi(argv[3]);

    if (nThreads <= 0) {
        fprintf(stderr, "Error: nThreads must be > 0\n");
        return 1;
    }

    int width, height, maxval;
    int headerSize = parse_pgm_header(imagePath, &width, &height, &maxval);
    if (headerSize < 0) {
        fprintf(stderr, "Invalid PGM header\n");
        return 1;
    }
    if (maxval > 255) {
        fprintf(stderr, "Expecting 1 byte pixels (maxval <= 255). Got maxval=%d\n", maxval);
        return 1;
    }

    int nPixels = width * height;
    int nBins = maxval + 1;

    pthread_t threads[nThreads];
    ThreadInfo info[nThreads];

    unsigned int* global_hist = (unsigned int*)calloc(nBins, sizeof(unsigned int));
    if (!global_hist) {
        perror("calloc global_hist");
        return 1;
    }

    int baseChunk = nPixels / nThreads;
    int remainder = nPixels % nThreads;
    int offset = headerSize;

    for (int t = 0; t < nThreads; t++) {
        int chunk = baseChunk + ((t == nThreads - 1) ? remainder : 0);

        info[t].path = imagePath;
        info[t].offset = offset;
        info[t].bytesToRead = chunk;
        info[t].nBins = nBins;

        info[t].local_histogram = (unsigned int*)calloc(nBins, sizeof(unsigned int));
        if (!info[t].local_histogram) {
            perror("calloc local_histogram");
            free(global_hist);
            return 1;
        }

        if (pthread_create(&threads[t], NULL, worker, &info[t]) != 0) {
            perror("pthread_create");
            free(info[t].local_histogram);
            free(global_hist);
            return 1;
        }

        offset += chunk;
    }

    for (int t = 0; t < nThreads; t++) {
        pthread_join(threads[t], NULL);
    }

    for (int t = 0; t < nThreads; t++) {
        for (int i = 0; i < nBins; i++) {
            global_hist[i] += info[t].local_histogram[i];
        }
        free(info[t].local_histogram);
    }

    int fd_out = open(outPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_out < 0) {
        perror("open output");
        free(global_hist);
        return 1;
    }

    for (int i = 0; i < nBins; i++) {
        char line[64];
        int len = snprintf(line, sizeof(line), "%d,%u\n", i, global_hist[i]);

        if (len > 0) {
            ssize_t w = write(fd_out, line, (size_t)len);
            if (w < 0) {
                perror("write");
                close(fd_out);
                free(global_hist);
                return 1;
            }
        }
    }

    close(fd_out);
    free(global_hist);
    return 0;
}
