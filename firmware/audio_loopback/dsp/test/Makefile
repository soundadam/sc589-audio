# Host build of the dsp/ tests. No CCES or board needed.
#
#   make            build and run the tests
#   make clean

CC      ?= cc
# -I../.. is the firmware include root, as in the CCES project
CFLAGS  ?= -std=c99 -g -O0 -Wall -Wextra -Wno-unused-function -I../..
LDLIBS  = -lm

TARGET  = test_audio_process
SOURCES = $(wildcard ../*.c ../*.h)

.PHONY: all test clean

all: test

$(TARGET): test_audio_process.c $(SOURCES)
	$(CC) $(CFLAGS) -o $@ test_audio_process.c $(LDLIBS)

test: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(TARGET) $(TARGET).dSYM
