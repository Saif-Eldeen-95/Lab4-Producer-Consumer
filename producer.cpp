#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/shm.h>
#include <sys/mman.h>

// ANSI escape sequences (\033 --> escape char in C)
#define RESET "\033[0m"
#define RED "\033[0;91m"
#define GREEN "\033[0;92m"
#define YELLOW "\033[0;93m"
#define BLUE "\033[0;94m"
#define CLEAR "\033[2J \033[1;1H" // (clear terminal and move cursor to top left)

struct Commodity
{
    char name[10];
    double priceMean;
    double priceStdDev;
    int SleepTime;
    int BufferSize;
};
struct ProducerContext
{
    const char *name = "SH_Mem"; // name of the shared memory object
    const int SIZE = 1000; // size of shared memory object
    int shm_fd; // shared memory file descriptor
    void *ptr; // pointer to shared memory object
    Commodity commodity;
};
 
int main(int argc, char *argv[]) {
    Commodity commodity;
    if (argc != 6) {
        printf(RED "Usage: %s <Commodity Name> <Price Mean> <Price Std Dev> <Sleep Time> <Buffer Size>\n" RESET, argv[0]);
        return 1;
    }
    strcpy(commodity.name, argv[1]);
    commodity.priceMean = atof(argv[2]);
    commodity.priceStdDev = atof(argv[3]);
    commodity.SleepTime = atoi(argv[4]);
    commodity.BufferSize = atoi(argv[5]);

    ProducerContext context;
    context.shm_fd = shm_open(context.name, O_CREAT | O_RDWR, 0666); // create the shared memory object
    ftruncate(context.shm_fd, context.SIZE); // configure the size of the shared memory object
    context.ptr = mmap(0, context.SIZE, PROT_WRITE, MAP_SHARED, context.shm_fd, 0);     // map the shared memory object in the address space of the process
    write_to_shared_memory("Hello from Producer!", context); // write to the shared memory object
    return 0;
}

void write_to_shared_memory(const char *message, ProducerContext &context) {

    sprintf((char *)context.ptr, "%s", message);

    printf(GREEN "Producer: Wrote to shared memory: %s\n" RESET, (char *)context.ptr);
}