# Compiler and flags
CC = gcc
CFLAGS = -Wall -g

# Source and output
SRC = main.c
OUT = gitpuller
LIB = -lncurses

# Default target: build and run
all: build run clean

# Build the executable
build:
	$(CC) $(CFLAGS) $(SRC) -o $(OUT) $(LIB)

# Run the executable
run:
	./$(OUT)

# Clean compiled files
clean:
	rm -f $(OUT)
