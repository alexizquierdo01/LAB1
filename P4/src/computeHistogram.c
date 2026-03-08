#include "parsePGM.h"
#include <pthread.h>

#define BLOCK_SIZE (1024 * 16)
#define HIST_SIZE 256

typedef struct {
    unsigned char *data;
    int size;
} block_t;

block_t *buffer;
int sizeBuffer;

int in = 0;
int out = 0;
int elementsInBuffer = 0;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t notFull = PTHREAD_COND_INITIALIZER;
pthread_cond_t notEmpty = PTHREAD_COND_INITIALIZER;

int producers_finished = 0;
int active_producers = 0;

int histogram[HIST_SIZE] = {0};
pthread_mutex_t hist_mutex = PTHREAD_MUTEX_INITIALIZER;

int readPos;
pthread_mutex_t lock_read = PTHREAD_MUTEX_INITIALIZER;

char *filePath;

void *Producer(void *arg)
{
    int fd = open(filePath, O_RDONLY);
    int nBytesRead;
    int readPosLocal;

    while (1) {
        pthread_mutex_lock(&lock_read);
        readPosLocal = readPos;
        readPos += BLOCK_SIZE;
        pthread_mutex_unlock(&lock_read);

        lseek(fd, readPosLocal, SEEK_SET);

        unsigned char *buff = malloc(BLOCK_SIZE);
        nBytesRead = read(fd, buff, BLOCK_SIZE);

        if (nBytesRead <= 0) {
            free(buff);
            break;
        }

        pthread_mutex_lock(&mutex);

        while (elementsInBuffer == sizeBuffer)
            pthread_cond_wait(&notFull, &mutex);

        buffer[in].data = buff;
        buffer[in].size = nBytesRead;
        in = (in + 1) % sizeBuffer;
        elementsInBuffer++;

        pthread_cond_signal(&notEmpty);
        pthread_mutex_unlock(&mutex);
    }

    close(fd);

    pthread_mutex_lock(&mutex);
    active_producers--;
    if (active_producers == 0) {
        producers_finished = 1;
        pthread_cond_broadcast(&notEmpty);
    }
    pthread_mutex_unlock(&mutex);

    return NULL;
}

void *Consumer(void *arg)
{
    while (1) {

        pthread_mutex_lock(&mutex);

        while (elementsInBuffer == 0 && !producers_finished)
            pthread_cond_wait(&notEmpty, &mutex);

        if (elementsInBuffer == 0 && producers_finished) {
            pthread_mutex_unlock(&mutex);
            break;
        }

        block_t item = buffer[out];
        out = (out + 1) % sizeBuffer;
        elementsInBuffer--;

        pthread_cond_signal(&notFull);
        pthread_mutex_unlock(&mutex);

        pthread_mutex_lock(&hist_mutex);
        for (int i = 0; i < item.size; i++)
            histogram[item.data[i]]++;
        pthread_mutex_unlock(&hist_mutex);

        free(item.data);
    }

    return NULL;
}

int main(int argc, char *argv[])
{
    if (argc != 6) {
        printf("Usage:\n");
        printf("./computeHistogram file.pgm histogram.txt N_producers N_consumers sizeBuffer\n");
        return 1;
    }

    filePath = argv[1];
    char *outputPath = argv[2];
    int nProducers = atoi(argv[3]);
    int nConsumers = atoi(argv[4]);
    sizeBuffer = atoi(argv[5]);

    buffer = malloc(sizeof(block_t) * sizeBuffer);

    int width, height, maxval;
    int headerSize = parse_pgm_header(filePath, &width, &height, &maxval);

    if (headerSize < 0) {
        printf("Invalid PGM file\n");
        return 1;
    }

    readPos = headerSize;
    active_producers = nProducers;

    pthread_t producers[nProducers];
    pthread_t consumers[nConsumers];

    for (int i = 0; i < nProducers; i++)
        pthread_create(&producers[i], NULL, Producer, NULL);

    for (int i = 0; i < nConsumers; i++)
        pthread_create(&consumers[i], NULL, Consumer, NULL);

    for (int i = 0; i < nProducers; i++)
        pthread_join(producers[i], NULL);

    for (int i = 0; i < nConsumers; i++)
        pthread_join(consumers[i], NULL);
        
    FILE *f = fopen(outputPath, "w");
    for (int i = 0; i < HIST_SIZE; i++)
        fprintf(f, "%d,%d\n", i, histogram[i]);
    fclose(f);

    free(buffer);
    return 0;
}