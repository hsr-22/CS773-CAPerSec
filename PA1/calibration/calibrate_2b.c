#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include "utils.h"
#include <math.h>

// Cache parameters
#define CACHE_SETS    8192   // total number of cache sets assumed
#define CACHE_WAYS    8     // number of ways (associativity) 8
#define SETS_PER_PAGE 64     // eviction buffer is organized into 64 sets per page
#define SET_SKIPPING_STEP 200  // skip sets in probing (probe every 2nd set)

// Memory allocation for the eviction array.
#define BYTES_PER_MB  (1024 * 1024)
#define EVICTION_ARRAY_SIZE (256 * BYTES_PER_MB / sizeof(uint32_t))

uint32_t *eviction_array = NULL;      // The big eviction array
unsigned int setHeads[SETS_PER_PAGE];   // Heads (starting indices) of the circular linked lists

// We will use a “shuffled” array for index ordering; its length is:
#define SHUFFLE_LEN  (CACHE_SETS / SETS_PER_PAGE)  // for 8192/64 = 128

//shuffle the array
void shuffle_array(unsigned int *array, size_t n) {
    for (size_t i = n - 1; i > 0; i--) {
        size_t j = rand() % (i + 1);
        unsigned int temp = array[i];
        array[i] = array[j];
        array[j] = temp;
    }
}

//Build pointer-chasing lists in eviction_array
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
    for (unsigned int set_index = 0; set_index < SETS_PER_PAGE; set_index++) {
        // Compute the starting index for this set.
        unsigned int current = (shuffle_arr[0] << 10) + (set_index << 4);
        setHeads[set_index] = current;
        // For each cache line ("way")
        for (unsigned int line_index = 0; line_index < CACHE_WAYS; line_index++) {
            // For each element in the list for this way
            for (unsigned int element_index = 0; element_index < SHUFFLE_LEN - 1; element_index++) {
                // Compute next index:
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

// Probe one circular list given its head index.
void probe_set(unsigned int head) {
    unsigned int pointer = head;
    do {
        pointer = eviction_array[pointer];
    } while(pointer != head);
    // The result of this pointer chasing is discarded.
}

// Probe all sets (using a skipping step) like the JS version.
void probe_all_sets() {
    for (unsigned int set = 0; set < SETS_PER_PAGE; set += SET_SKIPPING_STEP) {
        probe_set(setHeads[set]);
    }
}

// Perform the measurement using rdtsc. for 25 seconds
void perform_measurement_rdtsc() {
    // Let’s set a reference for the first period.
    long period_start = rdtsc();
    long period_end = period_start + (500* 2e9)/1000000 ;//every 500us
    long sum = 0;
    long samples = 0;
    long ssq = 0;
    clock_t start = clock();
    while (((double)clock() - start) / CLOCKS_PER_SEC < 5) {
        unsigned int sweeps = 0;
        samples++;
        // Count how many sweeps we can complete in the SAMPLING_PERIOD_MS window.
        while (rdtsc() < period_end) {
            probe_all_sets();
            sweeps++;
        }
        sum += sweeps;
        ssq += sweeps * sweeps;

        // Update the period: shift the window by SAMPLING_PERIOD_MS.
        period_start = period_end;
        period_end = period_start + (500* 2e9)/1000000;
    }
    printf("\t\tAverage number of sweeps: %f\n", (double)sum/samples);
    printf("\t\tStandard deviation: %f\n", sqrt((double)ssq/samples - ((double)sum/samples)*((double)sum/samples)));
}

int main(){
    
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

    // Do a few probes as a sanity check.
    for (unsigned int i = 0; i < 5; i++) {
        probe_all_sets();
    }

    // Allocate an array to hold the measurement results.
    unsigned int *results = (unsigned int*) malloc(200000000);
    if (results == NULL) {
        perror("malloc");
        free(eviction_array);
        return 1;
    }

    fflush(stdout);

    // Perform the measurement.
    perform_measurement_rdtsc();

    free(eviction_array);
    free(results);
}