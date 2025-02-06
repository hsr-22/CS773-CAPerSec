#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/stat.h> // For mode constants
#include <unistd.h>
#include <string.h>
#include "utils.h"
#include <signal.h>
#include <stdbool.h>

#define BUFFER_SIZE (24UL * 1024 * 1024) // 256 MB
#define CACHE_LINE_SIZE 64                // typical cache line size in bytes

//setup handler for ctrl+C

int main(){
    
    uint8_t *buffer = (uint8_t *) malloc(BUFFER_SIZE);
    volatile uint64_t sum = 0;

    // Initialize the buffer with some data.
    for (size_t i = 0; i < BUFFER_SIZE; i++) {
        buffer[i] = (uint8_t) (i & 0xFF);
    }

    while(1){
        for (size_t i = 0; i < BUFFER_SIZE; i += CACHE_LINE_SIZE) {
            sum += buffer[i];
        }
    }
}
