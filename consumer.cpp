#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <time.h>
#include <semaphore.h>
#include <limits.h>
#include <signal.h>

// ANSI escape sequences (\033 --> escape char in C)
#define RESET "\033[0m"
#define RED "\033[0;91m"
#define GREEN "\033[0;92m"
#define YELLOW "\033[0;93m"
#define BLUE "\033[0;94m"
#define CLEAR "\033[2J \033[1;1H"

struct BufferEntry{
    char commodityName[11];
    double price;
    struct timespec timestamp;
};
struct SharedMemoryHeader{
    int write_index;
    int read_index;
    int BufferSize;
};
struct ConsumerContext{
    const char *name = "/SH_Mem";
    size_t shm_Size = 0;
    int shm_fd = -1;
    void *ptr = MAP_FAILED;
    int BufferSize;
    sem_t *mutex = SEM_FAILED;
    sem_t *empty = SEM_FAILED;
    sem_t *full = SEM_FAILED;
};
volatile sig_atomic_t running = 1;
void handle_signal(int signal){
    running = 0;
}
void cleanup(ConsumerContext &context);
void display_dashboard(
    const char *commodities[],
    int commoditiesCount,
    double currentPrice[],
    double currentAvg[],
    double previousPrice[],
    double previousAvg[],
    int readingCount[]);

int main(int argc, char *argv[]){
    char *end;
    // ******************************* validations of consumer inputs and error handling *****************************************************

    if (argc != 2){
        printf(RED "Usage: %s <Buffer Size>\n" RESET, argv[0]);
        return 1;
    }
    long bufferSize = strtol(argv[1], &end, 10);
    if (end == argv[1] || *end != '\0' || bufferSize <= 0 || bufferSize > INT_MAX){
        printf(RED "Error: Buffer Size must be a valid integer greater than 0\n" RESET);
        return 1;
    }
    // ******************************* validations of context and error handling *****************************************************
    
    ConsumerContext context;
    context.BufferSize = (int)bufferSize;
    sem_t *init_lock = sem_open("/init_lock", O_CREAT, 0666, 1);
    if (init_lock == SEM_FAILED){
        perror("init_lock sem_open failed");
        return 1;
    }
    if (sem_wait(init_lock) == -1){
        perror("sem_wait init_lock failed");
        sem_close(init_lock);
        return 1;
    }
    context.shm_fd = shm_open(context.name, O_RDWR, 0666);
    if (context.shm_fd == -1){
        perror("shm_open failed. Start a Producer first");
        sem_post(init_lock);
        sem_close(init_lock);
        return 1;
    }
    struct stat shmStat;
    if (fstat(context.shm_fd, &shmStat) == -1){
        perror("fstat failed");
        cleanup(context);
        sem_post(init_lock);
        sem_close(init_lock);
        return 1;
    }
    context.shm_Size = shmStat.st_size;
    if (context.shm_Size < sizeof(SharedMemoryHeader)){
        printf(RED "Error: Invalid shared memory size\n" RESET);
        cleanup(context);
        sem_post(init_lock);
        sem_close(init_lock);
        return 1;
    }
    context.ptr = mmap(0, context.shm_Size,
                       PROT_READ | PROT_WRITE,
                       MAP_SHARED, context.shm_fd, 0);
    if (context.ptr == MAP_FAILED){
        perror("mmap failed");
        cleanup(context);
        sem_post(init_lock);
        sem_close(init_lock);
        return 1;
    }
    SharedMemoryHeader *header = (SharedMemoryHeader *)context.ptr;
    if (context.shm_Size != sizeof(SharedMemoryHeader) + context.BufferSize * sizeof(BufferEntry) || header->BufferSize != context.BufferSize){
        printf(RED "Error: Buffer size mismatch\n" RESET);
        cleanup(context);
        sem_post(init_lock);
        sem_close(init_lock);
        return 1;
    }

    context.mutex = sem_open("/mutex", 0);
    context.empty = sem_open("/empty", 0);
    context.full = sem_open("/full", 0);

    if (context.mutex == SEM_FAILED ||
        context.empty == SEM_FAILED ||
        context.full == SEM_FAILED){
        perror("sem_open failed");
        cleanup(context);
        sem_post(init_lock);
        sem_close(init_lock);
        return 1;
    }
    sem_post(init_lock);
    sem_close(init_lock);

    // ******************************* initialization of dashboard variables *****************************************************
    const char *commodities[] = {
        "ALUMINIUM",
        "COTTON",
        "COPPER",
        "CRUDEOIL",
        "GOLD",
        "LEAD",
        "MENTHAOIL",
        "NATURALGAS",
        "NICKEL",
        "SILVER",
        "ZINC"};
    int commoditiesCount = sizeof(commodities) / sizeof(commodities[0]);

    double currentPrice[11] = {};
    double previousPrice[11] = {};

    double currentAvg[11] = {};
    double previousAvg[11] = {};

    double prices[11][5] = {};

    int readingCount[11] = {};
    int readingIndex[11] = {};

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    // ******************************* Consumer loop to read from shared memory *****************************************************
    display_dashboard(
        commodities, commoditiesCount,
        currentPrice, currentAvg,
        previousPrice, previousAvg,
        readingCount);
    BufferEntry *bufferstart =
        (BufferEntry *)((char *)context.ptr + sizeof(SharedMemoryHeader));
    while (running)
    {
        sem_wait(context.full);
        sem_wait(context.mutex);
        BufferEntry entry = bufferstart[header->read_index];
        header->read_index = (header->read_index + 1) % context.BufferSize;
        sem_post(context.mutex);
        sem_post(context.empty);
        int index = -1;
        for (int i = 0; i < commoditiesCount; i++){
            if (strcmp(entry.commodityName, commodities[i]) == 0){
                index = i;
                break;
            }
        }
        if (index != -1){
            previousPrice[index] = currentPrice[index];
            previousAvg[index] = currentAvg[index];
            currentPrice[index] = entry.price;
            prices[index][readingIndex[index]] = entry.price;
            readingIndex[index] = (readingIndex[index] + 1) % 5;
            if (readingCount[index] < 5){
                readingCount[index]++;
            }
            double sum = 0;
            for (int i = 0; i < readingCount[index]; i++){
                sum += prices[index][i];
            }
            currentAvg[index] = sum / readingCount[index];
        }
        display_dashboard(
            commodities, commoditiesCount,
            currentPrice, currentAvg,
            previousPrice, previousAvg,
            readingCount);
        if(!running){
            break;
        }
    }
    cleanup(context);
    return 0;
}
void display_dashboard(
    const char *commodities[],
    int commoditiesCount,
    double currentPrice[],
    double currentAvg[],
    double previousPrice[],
    double previousAvg[],
    int readingCount[]){
    printf(CLEAR);
    printf(BLUE "====================== COMMODITIES DASHBOARD ======================\n" RESET);
    printf("%-12s | %10s | %10s\n", "COMMODITY", "PRICE", "AVG PRICE");
    printf("--------------------------------------------------------------------\n");
    for (int i = 0; i < commoditiesCount; i++)
    {
        const char *priceArrow = "-";
        const char *avgArrow = "-";
        if (readingCount[i] > 1)
        {
            if (currentPrice[i] > previousPrice[i])
                priceArrow = GREEN "↑" RESET;
            else if (currentPrice[i] < previousPrice[i])
                priceArrow = RED "↓" RESET;
            else
                priceArrow = YELLOW "→" RESET;

            if (currentAvg[i] > previousAvg[i])
                avgArrow = GREEN "↑" RESET;
            else if (currentAvg[i] < previousAvg[i])
                avgArrow = RED "↓" RESET;
            else
                avgArrow = YELLOW "→" RESET;
        }
        printf("%-12s | %7.2lf %s | %7.2lf %s\n",
               commodities[i],
               currentPrice[i],
               priceArrow,
               currentAvg[i],
               avgArrow);
    }
    printf("--------------------------------------------------------------------\n");
    fflush(stdout);
}
void cleanup(ConsumerContext &context)
{
    if (context.mutex != SEM_FAILED){
        sem_close(context.mutex);
        context.mutex = SEM_FAILED;
    }
    if (context.empty != SEM_FAILED){
        sem_close(context.empty);
        context.empty = SEM_FAILED;
    }
    if (context.full != SEM_FAILED){
        sem_close(context.full);
        context.full = SEM_FAILED;
    }
    if (context.ptr != MAP_FAILED){
        munmap(context.ptr, context.shm_Size);
        context.ptr = MAP_FAILED;
    }
    if (context.shm_fd != -1){
        close(context.shm_fd);
        context.shm_fd = -1;
    }
}
