# Makefile pour tests host-side TS_SYNC
# Usage: make test

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -Istubs -I../src -I../include
LDFLAGS =

# Target
TARGET = test_ts_sync
SRC = test_ts_sync.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
	@echo "Build OK: $(TARGET)"

test: $(TARGET)
	@echo "Running tests..."
	./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all test clean
