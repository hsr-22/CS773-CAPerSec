#!/bin/bash

# Build all components
make all > /dev/null 2>&1

# Calibration for task2a
echo "Calibration settings for task2a:"
./calibrate_2a

echo ""

# Calibration for task2b with LLC being thrashed
echo "Calibration settings for task2b:"
echo -e "\tWhen LLC being thrashed"

# Launch the thrash program on core 0 in the background
taskset -c 0 ./thrash &
# Save the background process ID for later termination
THRASH_PID=$!

# Run calibrate_2b five times on core 2 while LLC is being thrashed
for i in {1..3}; do
    taskset -c 2 ./calibrate_2b
    echo ""
done

# Kill the thrash process
kill $THRASH_PID

# Calibration for task2b with LLC not being thrashed
echo -e "\tWhen LLC not being thrashed"

# Run calibrate_2b five times on core 2 after thrashing has stopped
for i in {1..3}; do
    taskset -c 2 ./calibrate_2b
    echo ""
done

make clean > /dev/null 2>&1