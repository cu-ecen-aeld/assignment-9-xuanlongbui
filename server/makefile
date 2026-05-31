# Makefile for building a program with -lrt and -pthread

# Compiler to use
CC ?= $(CROSS_COMPILE)gcc

# Compiler flags
CFLAGS ?= -Wall -g -I../aesd-char-driver

# Linker flags (add -lrt for real-time library and -pthread for threading support)
LDFLAGS ?= -lrt -pthread

# Source files
SRC = main.c aesdsocket.c

# Output executable
TARGET ?= aesdsocket

# Default target
all: $(TARGET)

# Rule to build the executable
$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

# Clean rule to remove generated files
clean:
	rm -f $(TARGET)
