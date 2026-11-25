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
};
std::random_device rd;
std::mt19937 gen(rd());

std::normal_distribution<double> distribution(
    context.commodity.priceMean,
    context.commodity.priceStdDev
);
void write_to_shared_memory(const char *message, ProducerContext &context);

int main(int argc, char *argv[]) {
while (true){
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

    long bufferSize = strtol(argv[5], &end, 10);
    if (end == argv[5] || *end != '\0' || bufferSize <= 0) {
        printf(RED "Error: Buffer Size must be a valid integer greater than 0\n" RESET);
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
                return 1;
            }
        } else {   // if it fails for any other reason
            perror("shm_open failed");
            return 1;
        }
    } else {
        created = true;
        printf(GREEN "Shared memory object created successfully.\n" RESET);     
    }
    context.shm_Size = sizeof(SharedMemoryHeader) + context.BufferSize * sizeof(BufferEntry);
    if (created) {
        if (ftruncate(context.shm_fd, context.shm_Size  ) == -1) {
            perror("ftruncate failed");
            return 1;
        }
    }
    context.ptr = mmap(0, context.shm_Size, PROT_WRITE, MAP_SHARED, context.shm_fd, 0);
    if (context.ptr == MAP_FAILED) {
        perror("mmap failed");
        return 1;
    }
    SharedMemoryHeader *header = (SharedMemoryHeader *)context.ptr;
    if (created) {
        header->write_index = 0;
        header->read_index = 0;
        header->BufferSize = context.BufferSize;
    }
    else {
        if (header->BufferSize != context.BufferSize) {
            printf("Error: Buffer size mismatch\n");
            return 1;
        }
    }
    BufferEntry *bufferstart = (BufferEntry *)((char *)context.ptr + sizeof(SharedMemoryHeader));
    write_to_shared_memory(context, header->write_index);
    usleep(context.commodity.SleepTime * 1000);
    return 0;
}
}

void write_to_shared_memory( ProducerContext &context , int index) {
    BufferEntry *bufferstart = (BufferEntry *)((char *)context.ptr + sizeof(SharedMemoryHeader)); // Move the pointer to the start of the buffer
    BufferEntry *entry = &bufferstart[index];   // Get the entry at the current write index
    strcpy(entry->commodityName, context.commodity.name);
    entry->price = distribution(gen); // Generate a random price based on the normal distribution
    clock_gettime(CLOCK_REALTIME, &entry->timestamp); // Get the current timestamp
    fprintf(stderr,GREEN "%ld Producer: %s updated to %.2f\n" RESET,entry->timestamp.tv_sec,entry->commodityName,entry->price);    
}