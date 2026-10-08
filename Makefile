# Local gate: run before every commit. Needs only a C99 compiler and python3;
# no CCES, board or network.

.PHONY: check

check:
	$(MAKE) -C apps/audio_loopback/dsp/test test
	python3 -m unittest discover tools

# Build on the Windows host (tools/remote_build.sh), then boot the board over
# UART and exit on its console verdict (tools/uart_boot.py). Press reset when
# uart_boot.py starts waiting.
.PHONY: board

board:
	tools/remote_build.sh
	uv run --no-project --with-requirements tools/requirements.txt \
		python tools/uart_boot.py build/adc_dac_playback_SC589_uartslave.ldr --wait 60
