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
int perform_measurement_rdtsc(unsigned int *results, char* received_msg) {
    unsigned int sample = 0;
    
    // Let’s set a reference for the first period.
    long period_start = rdtsc();
    long period_end = period_start + (500* 2e9)/1000000 ;//every 500us

    clock_t start = clock();
    while (((double)clock() - start) / CLOCKS_PER_SEC < 25) {
        unsigned int sweeps = 0;
        // Count how many sweeps we can complete in the SAMPLING_PERIOD_MS window.
        while (rdtsc() < period_end) {
            probe_all_sets();
            sweeps++;
        }
 
        results[sample] = sweeps;
        sample++;

        // Update the period: shift the window by SAMPLING_PERIOD_MS.
        period_start = period_end;
        period_end = period_start + (500* 2e9)/1000000;
    }
    return sample;
}

// Check edge quality
int edgeQ(const int window[]) {
    int sum1 = 0, sum2 = 0;
    // Note: Python window[3:8] covers indices 3,4,5,6,7.
    for (int i = 3; i < 8; i++) {
        sum1 += window[i];
    }
    // Python window[8:13] covers indices 8,9,10,11,12.
    for (int i = 8; i < 13; i++) {
        sum2 += window[i];
    }
    return abs(sum1 - sum2);
}

// Return bit from edge 0/1
int bit_from_edge(const int window[]) {
    int sum1 = 0, sum2 = 0;
    for (int i = 3; i < 8; i++) {
        sum1 += window[i];
    }
    for (int i = 8; i < 13; i++) {
        sum2 += window[i];
    }
    return (sum1 - sum2) > 0 ? 0 : 1;
}

// Find the edge position
int edgePos(const int window[]) {
    int max_delta = 0;
    int max_delta_pos = 0;
    // Loop from i=4 to 12 (Python: range(4, 13))
    for (int i = 4; i < 13; i++) {
        int delta = abs(window[i-2] + window[i-1] - window[i] - window[i+1]);
        if (delta > max_delta) {
            max_delta = delta;
            max_delta_pos = i;
        }
    }
    return max_delta_pos;
}

// Decipher the message
// This block basicallu uses a Delay Locked Loop to keep track
// of the bits and then deciphers the message
// The DLL works by using Early/In-Phase and Late windows to
// keep the edge position in the center of the window
int decipher_msg(unsigned int *data, int size, char *received_msg)
{
    int total_bytes = 0;
    int latch = 0;
    int no_added = 0;
    int global_ctr = 0;
    char ch = 0;
    int bits_done = 0;
    int curr_len = 0;   

    int curr_window[17];
    for (int i = 0; i < 17; i++) {
        curr_window[i] = data[0];
    }

    while (global_ctr + 1 < size) {
        // Save the current window as early_window.
        int early_window[17];
        memcpy(early_window, curr_window, sizeof(curr_window));
        
        // Shift curr_window left by one element and append data[global_ctr]
        for (int i = 0; i < 17 - 1; i++) {
            curr_window[i] = curr_window[i+1];
        }
        curr_window[17 - 1] = data[global_ctr];
        
        // Construct late_window by shifting curr_window (after update) left by one
        // and appending data[global_ctr+1]
        int late_window[17];
        for (int i = 0; i < 17 - 1; i++) {
            late_window[i] = curr_window[i+1];
        }
        late_window[17 - 1] = data[global_ctr+1];
        
        global_ctr++;
        no_added++;
        
        // If latch is active and we haven’t added a full window, skip the rest.
        if (latch && no_added != 17) {
            continue;
        } else if (!latch && edgeQ(curr_window) > 5*DIFFERENCE_THRESHOLD) {
            // When latch is not set and the edge quality exceeds threshold:
            ch = (ch << 1) | 0;
            bits_done++;
            latch = 1;
            no_added = 0;
            continue;
        } else if (!latch) {
            continue;
        }
        
        // Check if the range in curr_window is very small (i.e. signal flat); if so, break.
        int max_val = curr_window[0], min_val = curr_window[0];
        for (int i = 1; i < 17; i++) {
            if (curr_window[i] > max_val) max_val = curr_window[i];
            if (curr_window[i] < min_val) min_val = curr_window[i];
        }
        if (max_val - min_val <= 3) {
            break;
        }
        
        // Compute edge position.
        int edgepos = edgePos(curr_window);
        
        // Decide which window (early, curr, or late) to use based on edge position.
        char chosen[10] = "";
        if (edgepos > 8) {
            // Update curr_window to be late_window.
            memcpy(curr_window, late_window, sizeof(late_window));
            global_ctr++;  // Advance an extra step.
            strcpy(chosen, "late");
        } else if (edgepos < 8) {
            // Revert curr_window to early_window.
            memcpy(curr_window, early_window, sizeof(early_window));
            global_ctr--;  // Move back one step.
            strcpy(chosen, "early");
        }

        ch = (ch << 1) | bit_from_edge(curr_window);
        bits_done++;

        if(bits_done == 8){
            received_msg[curr_len++] = ch;
            // printf("Received: %c\n", ch);
            bits_done = 0;
            ch = 0;
            total_bytes++;
        }
        
        no_added = 0;
    }

    return total_bytes;
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
    int max = perform_measurement_rdtsc(results, received_msg);
    received_msg_size = decipher_msg(results, max, received_msg);

    free(eviction_array);
    free(results);

    // DO NOT MODIFY THIS LINE
    printf("Accuracy (%%): %f\n", check_accuracy(received_msg, received_msg_size)*100);
}