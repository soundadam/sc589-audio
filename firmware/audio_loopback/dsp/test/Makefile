# Host build of the audio_process.c tests. No CCES or board needed.
#
#   make            build and run the tests
#   make sweep      write the frequency response to sweep.csv
#   make clean

CC      ?= cc
CFLAGS  ?= -std=c99 -g -O0 -Wall -Wextra -Wno-unused-function
LDLIBS  = -lm

TARGET  = test_audio_process

.PHONY: all test sweep clean

all: test

$(TARGET): test_audio_process.c ../audio_process.c ../audio_process.h
	$(CC) $(CFLAGS) -o $@ test_audio_process.c $(LDLIBS)

test: $(TARGET)
	./$(TARGET)

sweep: $(TARGET)
	./$(TARGET) sweep > sweep.csv
	@echo "wrote sweep.csv"

clean:
	rm -rf $(TARGET) $(TARGET).dSYM sweep.csv
