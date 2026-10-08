# Local gate: run before every commit. Needs only a C99 compiler and python3;
# no CCES, board or network.

.PHONY: check

check:
	$(MAKE) -C firmware/audio_loopback/dsp/test test
	python3 -m unittest discover -s host/board

# Build on the Windows host (host/build/remote_build.sh), then boot the board over
# UART and exit on its console verdict (host/board/uart_boot.py). Press reset when
# uart_boot.py starts waiting.
.PHONY: board

board:
	host/build/remote_build.sh
	uv run --no-project --with-requirements host/board/requirements.txt \
		python host/board/uart_boot.py build/adc_dac_playback_SC589_uartslave.ldr --wait 60
