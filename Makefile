# Local gate: run before every commit. Needs only a C99 compiler and python3;
# no CCES, board or network.

.PHONY: check

check:
	$(MAKE) -C apps/audio_loopback/host_test test
	python3 -m unittest discover tools
