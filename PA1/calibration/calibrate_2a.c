#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/stat.h> // For mode constants
#include <unistd.h>
#include <string.h>
#include <math.h>
#include "utils.h"

int main(){
    
    // Reference time
    clock_t start = clock();

    long t_1, t_2, delta; // Variables to store the time-stamp counter values
    long ctr = 0; // Variable to store the number of iterations in the inner loop
    clock_t start2;
    int y = 0;
    int *arr = (int*)malloc(1000*sizeof(int));
    long sum1=0, sum2 = 0, ssq1 = 0, ssq2 = 0;

    //Start receiving the message
    while(((double)clock() - start) / CLOCKS_PER_SEC < 4)
    {
        start2 = clock();
        while(((double)clock() - start2) / CLOCKS_PER_SEC < 3){
            ctr++;

            clflush((void *)(arr + INDEX)); // Flush the cache line containing INDEX);
            t_1 = rdtsc();
            volatile int read = arr[INDEX];
            t_2 = rdtsc();
            delta=t_2 - t_1;
            sum1+=delta;
            ssq1 += delta*delta;

            t_1 = rdtsc();
            read = y;
            t_2 = rdtsc();
            delta=t_2 - t_1;
            sum2+=delta;
            ssq2 += delta*delta;
        }
    }

    printf("\tAverage time-stamp counter difference for clflush: %f\n", (double)sum1/ctr);
    printf("\tStandard deviation for clflush: %f\n\n", sqrt((double)ssq1/ctr - ((double)sum1/ctr)*((double)sum1/ctr)));
    printf("\tAverage time-stamp counter difference for LLC hit: %f\n", (double)sum2/ctr);
    printf("\tStandard deviation for LLC hit: %f\n", sqrt((double)ssq2/ctr - ((double)sum2/ctr)*((double)sum2/ctr)));;
}