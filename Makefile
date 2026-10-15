# Local gate: run before every commit. Needs only a C99 compiler and python3;
# no CCES, board or network.

.PHONY: check

check:
	$(MAKE) -C firmware/audio_loopback/dsp/test test
	python3 -m unittest discover -s host/board
	@# ADI-licensed files belong in sc589-adi (.claude/rules/vendor.md)
	@if git grep -l -E 'Analog Devices,? Inc\.?,? All Rights Reserved' -- ':!.claude'; then \
		echo 'ADI-licensed files above must not be committed here' >&2; exit 1; fi

# Build an experiment's firmware on the Windows host and boot the board with it,
# or boot it again without a build; without EXPERIMENT, host/board/board.sh
# asks which. Press reset when uart_boot.py starts waiting.
#
#     make board                   # asks; Enter takes the newest
#     make board EXPERIMENT=E01
#     make boot EXPERIMENT=E01
.PHONY: board boot

board boot:
	@host/board/board.sh $@ $(EXPERIMENT)

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
