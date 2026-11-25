#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <math.h>
#include <time.h>
#include <random>
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

struct BufferEntry {
    char commodityName[11];
    double price;
    struct timespec timestamp;
};
struct SharedMemoryHeader {
    int write_index;
    int read_index;
    int BufferSize;
};
struct Commodity
{
    char name[11];
    double priceMean;
    double priceStdDev;
    int SleepTime;
};
struct ProducerContext
{
    const char *name = "/SH_Mem";
    size_t shm_Size;
    int shm_fd;
    void *ptr;
    int BufferSize;
    Commodity commodity;
    sem_t *mutex = SEM_FAILED;           // ensures mutual exclusion (no 2 processes can access buffer at the same time)
    sem_t *empty = SEM_FAILED;           // number of empty places in buffer (no producer can write in a full buffer) 
    sem_t *full = SEM_FAILED;            // number of elements in buffer (no consumer can read from an empty buffer)
};
volatile sig_atomic_t running = 1;
void handle_signal(int signal) {
    running = 0;
}
void cleanup(ProducerContext &context);
void write_to_shared_memory( ProducerContext &context , int index , std::normal_distribution<double> &distribution, std::mt19937 &gen);

int main(int argc, char *argv[]) {
    Commodity commodity;
    char *end;

// ******************************* validations of commodity inputs and error handling *****************************************************
    if (argc != 6) {
        printf(RED "Usage: %s <Commodity Name> <Price Mean> <Price Std Dev> <Sleep Time> <Buffer Size>\n" RESET, argv[0]);
        return 1;
    }

    if (strlen(argv[1]) > 10) {
        printf(RED "Error: Commodity Name must be at most 10 characters long\n" RESET);
        return 1;
    } else {
        strcpy(commodity.name, argv[1]);
    }

    double mean = strtod(argv[2], &end);
    if (end == argv[2] || *end != '\0' || !isfinite(mean) || mean < 0) {
        printf(RED "Error: Price Mean must be a valid number greater than or equal to 0\n" RESET);
        return 1;
    } else {
        commodity.priceMean = mean;
    }

    double stdDev = strtod(argv[3], &end);
    if (end == argv[3] || *end != '\0' || !isfinite(stdDev) || stdDev < 0) {
        printf(RED "Error: Price Std Dev must be a valid number greater than or equal to 0\n" RESET);
        return 1;
    } else {
        commodity.priceStdDev = stdDev;
    }

    long sleepTime = strtol(argv[4], &end, 10);
    if (end == argv[4] || *end != '\0' || sleepTime <= 0) {
        printf(RED "Error: Sleep Time must be a valid integer greater than 0\n" RESET);
        return 1;
    } else {
        commodity.SleepTime = (int)sleepTime;
    }
// ******************************* validations of context and error handling *****************************************************
    ProducerContext context;
    context.commodity = commodity;    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::normal_distribution<double> distribution(
        context.commodity.priceMean,
        context.commodity.priceStdDev
    );
    long bufferSize = strtol(argv[5], &end, 10);
    if (end == argv[5] || *end != '\0' || bufferSize <= 0 || bufferSize > INT_MAX) {
        printf(RED "Error: Buffer Size must be a valid integer greater than 0\n" RESET);
        return 1;
    }
    sem_t *init_lock = sem_open("/init_lock", O_CREAT, 0666, 1);
    if (init_lock == SEM_FAILED) {
        perror("init_lock sem_open failed");
        return 1;
    }
    if (sem_wait(init_lock) == -1) {
        perror("sem_wait init_lock failed");
        sem_close(init_lock);
        return 1;
    }
    context.BufferSize = (int)bufferSize;
    bool created = false;
    context.shm_fd = shm_open(context.name, O_CREAT | O_EXCL | O_RDWR, 0666); // try to create shared memory object with read/write permissions
    if (context.shm_fd == -1) {   // if it fails........
        if (errno == EEXIST) {    // if it fails because it's already exists
            printf(YELLOW "Shared memory object already exists.\n" RESET); 
            context.shm_fd = shm_open(context.name, O_RDWR, 0666);  // try to open the existing one with read/write permissions
            if (context.shm_fd == -1) {
                perror("shm_open existing object failed");
                sem_post(init_lock);
                sem_close(init_lock);
                return 1;
            }
        } else {   // if it fails for any other reason
            perror("shm_open failed");
            sem_post(init_lock);
            sem_close(init_lock);
            return 1;
        }
    } else {
        created = true;
        printf(GREEN "Shared memory object created successfully.\n" RESET);     
    }
    struct stat shmStat;
    if (fstat(context.shm_fd, &shmStat) == -1) {
        perror("fstat failed");    // if it fails to get the status of the shared memory object
        sem_post(init_lock);
        sem_close(init_lock);
        return 1;
    }
    if (created) {
    context.shm_Size = sizeof(SharedMemoryHeader) + context.BufferSize * sizeof(BufferEntry);
    if (ftruncate(context.shm_fd, context.shm_Size) == -1) {
        perror("ftruncate failed");
        sem_post(init_lock);
        sem_close(init_lock);
        return 1;
    }
    } else {
        context.shm_Size = shmStat.st_size;
    }
    context.ptr = mmap(0, context.shm_Size, PROT_READ | PROT_WRITE, MAP_SHARED, context.shm_fd, 0);
    if (context.ptr == MAP_FAILED) {
        perror("mmap failed");
        sem_post(init_lock);
        sem_close(init_lock);
        return 1;
    }
    SharedMemoryHeader *header = (SharedMemoryHeader *)context.ptr;
    if (created) {
        header->write_index = 0;
        header->read_index = 0;
        header->BufferSize = context.BufferSize;
        context.mutex = sem_open("/mutex", O_CREAT | O_EXCL , 0666, 1);
        context.empty = sem_open("/empty", O_CREAT | O_EXCL , 0666, context.BufferSize);
        context.full  = sem_open("/full", O_CREAT | O_EXCL , 0666, 0);
    } else {
    if (context.shm_Size != sizeof(SharedMemoryHeader) + context.BufferSize * sizeof(BufferEntry) || header->BufferSize != context.BufferSize) {
        printf(RED "Error: Buffer size mismatch\n" RESET);
        sem_post(init_lock);
        sem_close(init_lock);
        return 1;
    }
    bool mutexCreated = false;
    bool emptyCreated = false;
    bool fullCreated = false;

    context.mutex = sem_open("/mutex", 0);
    mutexCreated = (context.mutex != SEM_FAILED);

    context.empty = sem_open("/empty", 0);
    emptyCreated = (context.empty != SEM_FAILED);

    context.full = sem_open("/full", 0);
    fullCreated = (context.full != SEM_FAILED);

    if (context.mutex == SEM_FAILED || context.empty == SEM_FAILED || context.full == SEM_FAILED) {
        perror("sem_open failed");
        cleanup(context);
        if (mutexCreated)
            sem_unlink("/mutex");
        if (emptyCreated)
            sem_unlink("/empty");
        if (fullCreated)
            sem_unlink("/full");
        if (created)
            shm_unlink(context.name);
        sem_post(init_lock);
        sem_close(init_lock);
        return 1;
    }
    }
    if (context.mutex == SEM_FAILED || context.empty == SEM_FAILED || context.full == SEM_FAILED) {
        perror("sem_open failed");
        sem_post(init_lock);
        sem_close(init_lock);
        return 1;
    }
    sem_post(init_lock);
    sem_close(init_lock);
    // ******************************** Producer loop to write to shared memory *****************************************************
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    while (running) {    
        sem_wait(context.empty); // Wait for an empty slot in the buffer
        sem_wait(context.mutex); // Lock the buffer for exclusive access
        write_to_shared_memory(context, header->write_index, distribution, gen);
        header->write_index = (header->write_index + 1) % context.BufferSize; // Update the write index in a circular manner
        sem_post(context.mutex); // Unlock the buffer
        sem_post(context.full);  // Signal that a new item has been added to the buffer
        usleep(context.commodity.SleepTime * 1000);
    }
    cleanup(context);
    return 0;
}
void cleanup(ProducerContext &context) {
    if (context.mutex != SEM_FAILED) {
        sem_close(context.mutex);
        context.mutex = SEM_FAILED;
    }
    if (context.empty != SEM_FAILED) {
        sem_close(context.empty);
        context.empty = SEM_FAILED;
    }
    if (context.full != SEM_FAILED) {
        sem_close(context.full);
        context.full = SEM_FAILED;
    }
    if (context.ptr != MAP_FAILED) {
        munmap(context.ptr, context.shm_Size);
        context.ptr = MAP_FAILED;
    }
    if (context.shm_fd != -1) {
        close(context.shm_fd);
        context.shm_fd = -1;
    }
}
void write_to_shared_memory( ProducerContext &context , int index, std::normal_distribution<double> &distribution, std::mt19937 &gen) {
    BufferEntry *bufferstart = (BufferEntry *)((char *)context.ptr + sizeof(SharedMemoryHeader)); // Move the pointer to the start of the buffer
    BufferEntry *entry = &bufferstart[index];   // Get the entry at the current write index
    strcpy(entry->commodityName, context.commodity.name);
    entry->price = (context.commodity.priceStdDev == 0) ? context.commodity.priceMean : distribution(gen); // Generate a random price based on the normal distribution
    clock_gettime(CLOCK_REALTIME, &entry->timestamp); // Get the current timestamp
    
    fprintf(stderr,GREEN "%ld Producer: %s updated to %.2f\n" RESET,
        entry->timestamp.tv_sec,
        entry->commodityName,
        entry->price);    
}