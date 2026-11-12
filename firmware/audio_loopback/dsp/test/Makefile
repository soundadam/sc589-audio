# Host build of the dsp/ tests. No CCES or board needed.
#
#   make            build and run the tests
#   make clean

CC      ?= cc
# -I../.. is the firmware include root, as in the CCES project
CFLAGS  ?= -std=c99 -g -O0 -Wall -Wextra -Wno-unused-function -I../..
LDLIBS  = -lm

TARGETS = test_audio_process test_ols
SOURCES = $(wildcard ../*.c ../*.h)

.PHONY: all test clean

all: test

$(TARGETS): %: %.c $(SOURCES)
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

test: $(TARGETS)
	./test_audio_process
	./test_ols

clean:
	rm -rf $(TARGETS) $(addsuffix .dSYM,$(TARGETS))
