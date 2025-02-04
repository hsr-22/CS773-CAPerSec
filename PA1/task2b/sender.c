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
#include <math.h>

///////////////////////
// Configuration
///////////////////////

// Cache parameters (matching the JavaScript code)
#define CACHE_SETS    16384   // total number of cache sets assumed
#define CACHE_WAYS    12     // number of ways (associativity)
#define SETS_PER_PAGE 64    // eviction buffer is organized into 64 sets per page
#define SET_SKIPPING_STEP 1  // skip sets in probing (probe every 2nd set)

// Memory allocation for the eviction array.
// The JavaScript version allocates 32 MB worth of uint32_t entries.
#define BYTES_PER_MB  (1024 * 1024)
#define EVICTION_ARRAY_SIZE (256 * BYTES_PER_MB / sizeof(uint32_t))
// #define EVICTION_ARRAY_SIZE (32 * BYTES_PER_MB / sizeof(uint32_t))

///////////////////////
// Global Variables
///////////////////////

uint32_t *eviction_array = NULL;      // The big eviction array
unsigned int setHeads[SETS_PER_PAGE];   // Heads (starting indices) of the circular linked lists

// We will use a “shuffled” array for index ordering; its length is:
#define SHUFFLE_LEN  (CACHE_SETS / SETS_PER_PAGE)  // for 8192/64 = 128

///////////////////////
// Timing helper: returns current time in milliseconds as double
///////////////////////
double now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double) ts.tv_sec * 1000.0 + (double) ts.tv_nsec / 1e6;
}

///////////////////////
// Shuffle helper (Fisher–Yates)
///////////////////////
void shuffle_array(unsigned int *array, size_t n) {
    for (size_t i = n - 1; i > 0; i--) {
        size_t j = rand() % (i + 1);
        unsigned int temp = array[i];
        array[i] = array[j];
        array[j] = temp;
    }
}

///////////////////////
// Build the pointer–chasing lists in eviction_array.
//
// This mimics the JavaScript code’s createSetHeads function.
// The idea is to build, for each of the SETS_PER_PAGE sets, a circular linked
// list whose elements are indices into eviction_array. The pointer arithmetic
// is done so that the resulting addresses (if mapped in virtual memory)
// would share bits corresponding to the cache set index.
// 
// We compute a constant "allSetOffset" as log2(CACHE_SETS) + 4.
// For CACHE_SETS=8192, log2(8192)=13, plus 4 = 17.
///////////////////////
void create_set_heads() {
    unsigned int shuffle_arr[SHUFFLE_LEN];
    for (unsigned int i = 0; i < SHUFFLE_LEN; i++) {
        shuffle_arr[i] = i;
    }
    shuffle_array(shuffle_arr, SHUFFLE_LEN);

    // Compute allSetOffset = log2(CACHE_SETS) + 4.
    // We assume CACHE_SETS is a power of 2.
    unsigned int allSetOffset = (unsigned int)(log2(CACHE_SETS)) + 4; // e.g. 17

    // For each of the SETS_PER_PAGE sets, create the pointer chain.
    // The JS code uses bit arithmetic to form an index:
    //   index = (some shuffled value << 10) + (set_index << 4)  [initially]
    //
    // We mimic that below.
    for (unsigned int set_index = 0; set_index < SETS_PER_PAGE; set_index++) {
        // Compute the starting index for this set.
        unsigned int current = (shuffle_arr[0] << 10) + (set_index << 4);
        setHeads[set_index] = current;
        // For each cache line ("way")
        for (unsigned int line_index = 0; line_index < CACHE_WAYS; line_index++) {
            // For each element in the list for this way
            for (unsigned int element_index = 0; element_index < SHUFFLE_LEN - 1; element_index++) {
                // Compute next index:
                // next = (line_index << allSetOffset) + (shuffle_arr[element_index+1] << 10) + (set_index << 4);
                unsigned int next = (line_index << allSetOffset) + (shuffle_arr[element_index + 1] << 10) + (set_index << 4);
                // Link current to next.
                eviction_array[current] = next;
                current = next;
            }
            // After the inner loop, finish the current line.
            unsigned int next;
            if (line_index == CACHE_WAYS - 1) {
                // In the last line, the last pointer goes to the head of the entire set.
                next = setHeads[set_index];
            } else {
                // Otherwise, the last pointer goes to the head of the next line.
                next = ((line_index + 1) << allSetOffset) + (shuffle_arr[0] << 10) + (set_index << 4);
            }
            eviction_array[current] = next;
            current = next;
        }
    }
}

///////////////////////
// Probe one circular list given its head index.
// This function “walks” the pointer–chasing list until it comes back to the head.
///////////////////////
void probe_set(unsigned int head) {
    unsigned int pointer = head;
    do {
        pointer = eviction_array[pointer];
    } while(pointer != head);
    // The result of this pointer chasing is discarded.
}

///////////////////////
// Probe all sets (using a skipping step) like the JS version.
///////////////////////
void probe_all_sets() {
    for (unsigned int set = 0; set < SETS_PER_PAGE; set += SET_SKIPPING_STEP) {
        probe_set(setHeads[set]);
    }
}


int main(){

    // ********** DO NOT MODIFY THIS SECTION **********
    FILE *fp = fopen(MSG_FILE, "r");
    if(fp == NULL){
        printf("Error opening file\n");
        return 1;
    }

    char msg[MAX_MSG_SIZE];
    int msg_size = 0;
    char c;
    while((c = fgetc(fp)) != EOF){
        msg[msg_size++] = c;
    }
    fclose(fp);

    clock_t start = clock();
    // **********************************************
    // ********** YOUR CODE STARTS HERE **********

    srand((unsigned int) time(NULL));  // Seed the random number generator

    // Allocate the eviction array.
    eviction_array = (uint32_t*) malloc(EVICTION_ARRAY_SIZE * sizeof(uint32_t));
    if (eviction_array == NULL) {
        perror("malloc");
        return 1;
    }

    // Initialize the eviction array to 0 (not really needed)
    for (unsigned int i = 0; i < EVICTION_ARRAY_SIZE; i++) {
        eviction_array[i] = 0;
    }

    // Build the pointer–chasing lists.
    create_set_heads();

    for (unsigned int i = 0; i < 5; i++) {
        probe_all_sets();
    }

    //sleep for 10 secs
    sleep(5);
    printf("Execute other now!\n");
    sleep(5);

    // treat shm_ptr as an array of integers
    int* my_array = (int *)malloc(10*L3_SIZE);

    // temporary variable to read from shared memory
    int read = 0;

    // Send 1s to signal the start of the message
    clock_t start2;
    srand(0);
    while(((double)clock() - start) / CLOCKS_PER_SEC < 10)
    {
        start2 = clock();
        while(((double)clock() - start2) / CLOCKS_PER_SEC < 1){
            probe_all_sets();
        }

        start2 = clock();
        while(((double)clock() - start2) / CLOCKS_PER_SEC < 1){
        
        }
    }

    sleep(10);

    // // Send the message
    // int bit = 0;
    // for (int i = 0; i < msg_size; i++) {
    //     for(int j = 7; j >= 0; j--){
    //         bit = (msg[i] >> j) & 1;
    //         if(bit){
    //             //Bit is 1 -> flush INDEX then dont do anything, i.e. receiver will read spike at INDEX
    //             start2 = clock();
    //             while(((double)clock() - start2) / CLOCKS_PER_SEC < 0.00004){
    //                 clflush((void *) (shared_array + INDEX));
    //             }
    //             start2 = clock();
    //             while(((double)clock() - start2) / CLOCKS_PER_SEC < 0.00004){
                    
    //             }
    //         }
    //         else{
    //             start2 = clock();
    //             //Bit is 0 -> flush INDEX2 then dont do anything, i.e. receiver will read spike at INDEX2
    //             while(((double)clock() - start2) / CLOCKS_PER_SEC < 0.00004){
    //                 clflush((void *) (shared_array + INDEX2));
    //             }
    //             start2 = clock();
    //             while(((double)clock() - start2) / CLOCKS_PER_SEC < 0.00004){
                    
    //             }
    //         }
    //         start2 = clock();
    //     }
    // }

    // ********** YOUR CODE ENDS HERE **********
    // ********** DO NOT MODIFY THIS SECTION **********
    clock_t end = clock();
    double time_taken = ((double)end - start) / CLOCKS_PER_SEC;
    printf("Message sent successfully\n");
    printf("Time taken to send the message: %f\n", time_taken);
    printf("Message size: %d\n", msg_size);
    printf("Bits per second: %f\n", msg_size * 8 / time_taken);
    // **********************************************
}
