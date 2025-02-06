#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/stat.h> // For mode constants
#include <unistd.h>
#include <string.h>
#include "utils.h"

#define TIME_INTERVAL 0.000001 //0.00001

//comparator function for qsort
int compare(const void *a, const void *b) {
    return (*(int*)a - *(int*)b);
}

// Function to check if windows is low barring outliers
int check_low(int arr[8]) {
    int sorted_arr[8];
    for (int i = 0; i < 8; i++) {
        sorted_arr[i] = arr[i];  // Copy original array to sort
    }
    
    // Sort the array to find the median
    qsort(sorted_arr, 8, sizeof(int), compare);
    
    // Median is the 4th element in the sorted array (index 3)
    int median = sorted_arr[3];
    
    int max_deviation = 0, outlier_index = 0;

    // Find the outlier with the largest deviation from the median
    for (int i = 0; i < 8; i++) {
        int deviation = abs(arr[i] - median);
        if (deviation > max_deviation) {
            max_deviation = deviation;
            outlier_index = i;
        }
    }

    // Calculate the sum after excluding the outlier
    int sum = 0;
    for (int i = 0; i < 8; i++) {
        if (i != outlier_index) {
            sum += arr[i];
        }
    }

    // Return true if the sum is less than or equal to 1050
    return (sum <= 7 * THRESHOLD_LOW);
}

int main(){

    // Update these values accordingly
    char* received_msg = NULL;
    int received_msg_size = 0;

    // Open shared memory
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        return 1;
    }

    // Set the size of shared memory
    if (ftruncate(shm_fd, SHM_SIZE) == -1) {
        perror("ftruncate");
        return 1;
    }

    // Map shared memory
    void *shm_ptr = mmap(0, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_ptr == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    
    // Allocate memory for received message
    received_msg = (char *)malloc(MAX_MSG_SIZE * sizeof(char));
    
    // treat shm_ptr as an array of integers
    int* shared_array = (int *)shm_ptr;

    // temporary variable to read from shared memory
    int read = 0;
    
    // Reference time
    clock_t start = clock();

    long t_1, t_2, delta; // Variables to store the time-stamp counter values
    double average = 0.0; // Variable to store the average time-stamp counter difference
    double average2 = 0.0; // Variable to store the average time-stamp counter difference
    int curr_win_ones[8] = {0}; // Array to store the last 8 time-stamp counter differences for 1s
    int curr_win_zeros[8] = {0}; // Array to store the last 8 time-stamp counter differences for 0s
    int curr_win_mask[8] = {0}; // Array to store the mask for the last 8 time-stamp counter differences
    char byte = 0; // Variable to store the received byte
    int latch = 0; // Variable to store the latch state i.e. when we succefully received one byte
    int byte_ctr = 0; // Variable to store the number of bits received in the current byte
    long ctr = 0; // Variable to store the number of iterations in the inner loop
    clock_t start2;

    //Start receiving the message
    while(((double)clock() - start) / CLOCKS_PER_SEC < 4)
    {
        ctr = 0;
        start2 = clock();
        while(((double)clock() - start2) / CLOCKS_PER_SEC < TIME_INTERVAL){
            ctr++;

            t_1 = rdtsc();
            read = shared_array[INDEX];
            t_2 = rdtsc();
            delta = t_2 - t_1;
            average += (delta - average) / ctr; // Calculate the average time-stamp counter difference for INDEX

            t_1 = rdtsc();
            read = shared_array[INDEX2];
            t_2 = rdtsc();
            delta = t_2 - t_1;
            average2 += (delta - average2) / ctr; // Calculate the average time-stamp counter difference for INDEX2

            // Store the values of average and average2 in a file
            FILE *file = fopen("average_output_file.txt", "a");
            if (file != NULL) {
                fprintf(file, "average: %f, average2: %f\n", average, average2);
                fclose(file);
            } else {
                perror("fopen");
            }

        }
        
        // Shift the window by 1
        for(int i = 0; i < 7; i++){
            curr_win_mask[i] = curr_win_mask[i+1];
            curr_win_ones[i] = curr_win_ones[i+1];
            curr_win_zeros[i] = curr_win_zeros[i+1];
        }
        curr_win_mask[7] = 1;
        curr_win_ones[7] = (int)average;
        curr_win_zeros[7] = (int)average2;

        if(curr_win_mask[3] == 1 && curr_win_mask[4] == 1 &&        // Check if we have previously processed those bits
            (curr_win_ones[3] >= THRESHOLD_HIGH && curr_win_ones[4] >= THRESHOLD_HIGH) && // Delay of INDEX access should be high
            check_low(curr_win_zeros)) // Delay of INDEX2 access should be low barring outliers [AMD issue]
        {
            byte = (byte << 1) | 1; // Append 1 to the byte
            byte_ctr++; // Increment the bit counter

            // Reset the mask array
            for(int i = 0; i < 8; i++){
                curr_win_mask[i] = 0;
            }
        }
        if(curr_win_mask[3] == 1 && curr_win_mask[4] == 1 &&   // Check if we have previously processed those bits
            (curr_win_zeros[3] >= THRESHOLD_HIGH && curr_win_zeros[4] >= THRESHOLD_HIGH) && // Delay of INDEX2 access should be high
            check_low(curr_win_ones)) // Delay of INDEX access should be low barring outliers [AMD issue]
        {
            byte = (byte << 1); // Append 0 to the byte
            byte_ctr++; // Increment the bit counter

            // Reset the mask array
            for(int i = 0; i < 8; i++){
                curr_win_mask[i] = 0;
            }
        }

        // Check if we have received a full byte
        if(byte_ctr == 8){
            if(latch || (byte>>7 == 0)) // Check if we have received the start of the message or we are already latched on
            {
                received_msg[received_msg_size++] = byte; // Append the byte to the message
                byte = 0; // Reset the byte
                byte_ctr = 0; // Reset the bit counter
                latch = 1; // Set the latch
            }
            else{
                byte_ctr = 7; // Reset the bit counter to 7
            }
        }
    }

    // Clean up
    munmap(shm_ptr, SHM_SIZE);
    close(shm_fd);
    

    // DO NOT MODIFY THIS LINE
    printf("Accuracy (%%): %f\n", check_accuracy(received_msg, received_msg_size)*100);
}