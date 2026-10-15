# Local gate: run before every commit. Needs only a C99 compiler and python3;
# no CCES, board or network.

.PHONY: check

check:
	$(MAKE) -C firmware/audio_loopback/dsp/test test
	python3 -m unittest discover -s host/board

# Build on the Windows host (host/build/remote_build.sh), then boot the board over
# UART and exit on its console verdict (host/board/uart_boot.py). Press reset when
# uart_boot.py starts waiting. The console lines are kept in build/board.log.
.PHONY: board

board:
	host/build/remote_build.sh
	uv run --no-project --with-requirements host/board/requirements.txt \
		python host/board/uart_boot.py build/adc_dac_playback_SC589_uartslave.ldr --wait 60 \
		--log build/board.log

# E01 report: figures and numbers from the design and build/board.log (make board),
# then XeLaTeX. Output in build/E01/report.pdf. The byline is REPORT_AUTHOR from
# the untracked .env (or the environment when .env does not set it), so a name and
# student number stay out of the repository.
-include .env
REPORT_AUTHOR ?= soundadam
.PHONY: e01

e01:
	uv run coursework/E01-fir-iir/figures.py
	printf '%s\n' '\newcommand{\ReportAuthor}{$(REPORT_AUTHOR)}' > build/E01/author.tex
	cd coursework/E01-fir-iir && TEXINPUTS=../common//: latexmk -xelatex -interaction=nonstopmode -halt-on-error \
		-outdir=$(CURDIR)/build/E01 report.tex
