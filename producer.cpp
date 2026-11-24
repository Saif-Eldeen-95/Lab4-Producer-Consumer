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

int main(int argc, char *argv[]) {
    const char *name = "SH_Mem"; // name of the shared memory object
    const int SIZE = 4096; // size of shared memory object

    int shm_fd; // shared memory file descriptor
    void *ptr; // pointer to shared memory object

    // create the shared memory object
    shm_fd = shm_open(name, O_CREAT | O_RDWR, 0666);
    ftruncate(shm_fd, SIZE); // configure the size of the shared memory object

    // map the shared memory object in the address space of the process
    ptr = mmap(0, SIZE, PROT_WRITE, MAP_SHARED, shm_fd, 0);

    // write to the shared memory object
    sprintf((char *)ptr, "%s", "Hello, World!");

    printf(GREEN "Producer: Wrote to shared memory: %s\n" RESET, (char *)ptr);

    return 0;
}