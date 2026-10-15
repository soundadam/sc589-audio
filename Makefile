# Local gate: run before every commit. Needs only a C99 compiler and python3;
# no CCES, board or network.

.PHONY: check

check:
	$(MAKE) -C firmware/audio_loopback/dsp/test test
	python3 -m unittest discover -s host/board
	@# ADI-licensed files belong in sc589-adi (.claude/rules/vendor.md)
	@if git grep -l -E 'Analog Devices,? Inc\.?,? All Rights Reserved' -- ':!.claude'; then \
		echo 'ADI-licensed files above must not be committed here' >&2; exit 1; fi

# Build on the Windows host (host/build/remote_build.sh), then boot the board over
# UART and exit on its console verdict (host/board/uart_boot.py). Press reset when
# uart_boot.py starts waiting. The console lines are kept in
# build/$(EXPERIMENT)/board.log, next to the report that reads them, and the
# loader stream in build/$(EXPERIMENT)/, where make boot starts it again without
# a build.
#
#     make board                   # the working tree, E02
#     make board EXPERIMENT=E01    # the firmware at tag e01
#     make boot EXPERIMENT=E01     # the loader stream the last make board kept
.PHONY: board boot

EXPERIMENT = E02
# The commit each experiment's firmware is built from; empty: the working tree
REV_E01 = e01
REV_E02 =
LDR = build/$(EXPERIMENT)/adc_dac_playback_SC589_uartslave.ldr

board:
	host/build/remote_build.sh $(REV_$(EXPERIMENT))
	mkdir -p build/$(EXPERIMENT)
	cp build/adc_dac_playback_SC589_uartslave.ldr $(LDR)
	$(MAKE) boot

boot:
	@test -f $(LDR) || { echo "no $(LDR): run make board EXPERIMENT=$(EXPERIMENT)" >&2; exit 1; }
	uv run --no-project --with-requirements host/board/requirements.txt \
		python host/board/uart_boot.py $(LDR) --wait 60 \
		--log build/$(EXPERIMENT)/board.log

# Lab reports: each experiment's figures.py writes its figures and numbers into
# build/<E0n>/, then XeLaTeX builds build/<E0n>/report.pdf. The byline is
# REPORT_AUTHOR from the untracked .env (or the environment when .env does not
# set it), so a name and student number stay out of the repository.
-include .env
REPORT_AUTHOR ?= soundadam

# $(1): build/ subdirectory, $(2): coursework/ directory
define report
	uv run coursework/$(2)/figures.py
	printf '%s\n' '\newcommand{\ReportAuthor}{$(REPORT_AUTHOR)}' > build/$(1)/author.tex
	cd coursework/$(2) && TEXINPUTS=../common//: latexmk -xelatex -interaction=nonstopmode -halt-on-error \
		-outdir=$(CURDIR)/build/$(1) report.tex
endef

# E01 reads the impulse responses and cycle counts in build/E01/board.log, from
# make board EXPERIMENT=E01
.PHONY: e01 e02

e01:
	$(call report,E01,E01-fir-iir)

# E02 runs the dsp/ sources on the Mac (host/dsplab/cdsp.py) and adds the board's
# numbers when build/E02/board.log is there
e02:
	$(call report,E02,E02-multirate)
