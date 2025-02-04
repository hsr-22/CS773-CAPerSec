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

#define MEASUREMENT_TIME_MS   20000      // 10 seconds total measurement time
#define SAMPLING_PERIOD_MS    10          // 20ms per measurement period
#define NUM_SAMPLES           (MEASUREMENT_TIME_MS / SAMPLING_PERIOD_MS) // 15000 samples

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

///////////////////////
// Main measurement loop:
// For each sampling period (2 ms) we count how many complete sweeps (calls to probe_all_sets)
// we can perform.
///////////////////////
void perform_measurement(unsigned int *results) {
    unsigned int sample = 0;
    
    // Let’s set a reference for the first period.
    double period_start = now_ms();
    double period_end = period_start + SAMPLING_PERIOD_MS;
    
    while (sample < NUM_SAMPLES) {
        // Busy-wait until the period_start (if needed).
        while (now_ms() < period_start) { /* spin */; }
        
        unsigned int sweeps = 0;
        // Count how many sweeps we can complete in the SAMPLING_PERIOD_MS window.
        while (now_ms() < period_end) {
            probe_all_sets();
            sweeps++;
        }
        results[sample] = sweeps;
        sample++;
        
        // Update the period: shift the window by SAMPLING_PERIOD_MS.
        period_start = period_end;
        period_end = period_start + SAMPLING_PERIOD_MS;
    }
}

int main(){

    // Update these values accordingly
    char* received_msg = NULL;
    int received_msg_size = 0;
    
    // Allocate memory for received message
    received_msg = (char *)malloc(MAX_MSG_SIZE * sizeof(char));
    
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

    clock_t start_t, end_t;
    start_t = clock();
    long start = rdtsc();
    probe_all_sets();
    long end = rdtsc();
    end_t = clock();
    printf("Time taken to probe all sets in cycles: %ld\n", end - start);
    printf("Time taken to probe all sets in us: %f\n", (end_t - start_t*1.0));

    // (Optional) Do a few probes as a sanity check.
    for (unsigned int i = 0; i < 5; i++) {
        probe_all_sets();
    }

    // Allocate an array to hold the measurement results.
    unsigned int *results = (unsigned int*) malloc(NUM_SAMPLES * sizeof(unsigned int));
    if (results == NULL) {
        perror("malloc");
        free(eviction_array);
        return 1;
    }

    printf("Starting measurement for %d ms (%d samples)...\n", MEASUREMENT_TIME_MS, NUM_SAMPLES);
    fflush(stdout);

    // Perform the measurement.
    perform_measurement(results);

    // Write the results to a text file.
    FILE *outfile = fopen("cache_probe_results.txt", "w");
    if (outfile == NULL) {
        perror("fopen");
        free(eviction_array);
        free(results);
        return 1;
    }

    for (unsigned int i = 0; i < NUM_SAMPLES; i++) {
        fprintf(outfile, "%u\n", results[i]);
    }
    fclose(outfile);

    printf("Measurement complete. Results written to cache_probe_results.txt\n");

    free(eviction_array);
    free(results);

    // DO NOT MODIFY THIS LINE
    printf("Accuracy (%%): %f\n", check_accuracy(received_msg, received_msg_size)*100);
}