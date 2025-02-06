CC = gcc
CFLAGS = -Wall

# Targets
all: calibrate_2a calibrate_2b thrash

# Build sender executable
calibrate_2a: calibrate_2a.o utils.o
	$(CC) $(CFLAGS) -fno-prefetch-loop-arrays -o calibrate_2a calibrate_2a.o utils.o -lm

calibrate_2b: calibrate_2b.o utils.o
	$(CC) $(CFLAGS) -fno-prefetch-loop-arrays -o calibrate_2b calibrate_2b.o utils.o -lm

thrash: thrash.o utils.o
	$(CC) $(CFLAGS) -fno-prefetch-loop-arrays -o thrash thrash.o utils.o -lm

# Compile utils.c to utils.o
utils.o: utils.c utils.h
	$(CC) $(CFLAGS) -fno-prefetch-loop-arrays -c utils.c -o utils.o

# Compile receiver.c to receiver.o
calibrate_2a.o: calibrate_2a.c utils.h
	$(CC) $(CFLAGS) -fno-prefetch-loop-arrays -c calibrate_2a.c -o calibrate_2a.o

calibrate_2b.o: calibrate_2b.c utils.h
	$(CC) $(CFLAGS) -fno-prefetch-loop-arrays -c calibrate_2b.c -o calibrate_2b.o

thrash.o: thrash.c utils.h
	$(CC) $(CFLAGS) -fno-prefetch-loop-arrays -c thrash.c -o thrash.o

# Clean up build artifacts
clean:
	rm -f calibrate_2a calibrate_2b thrash *.o