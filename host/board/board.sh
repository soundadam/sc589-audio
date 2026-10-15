#!/usr/bin/env bash
# Build an experiment's firmware and boot the board with it, or boot it again
# without a build. make board and make boot run this.
#
#     host/board/board.sh board          # asks which experiment
#     host/board/board.sh board E01
#     host/board/board.sh boot E01
#
# The experiments are the coursework/E<nn>-*/ directories; asked, Enter takes
# the newest. One with a tag e<nn> is built from that tag, any other from the
# working tree, uncommitted changes included (host/build/remote_build.sh, which
# reuses an earlier build of the same firmware). Its loader stream is kept in
# build/E<nn>/, where boot finds it. Booting goes through
# host/board/uart_boot.py: press reset when it starts waiting. It exits on the
# console's verdict and keeps the console lines in build/E<nn>/board.log, next
# to the report that reads them.

set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

LDR_NAME=adc_dac_playback_SC589_uartslave.ldr

usage() {
	echo "usage: $0 board|boot [E<nn>]" >&2
	exit 2
}

action=${1:-}
[[ $action == board || $action == boot ]] || usage
experiment=$(printf '%s' "${2:-}" | tr '[:lower:]' '[:upper:]')

ids=()
names=()
for dir in coursework/E[0-9][0-9]-*/; do
	dir=${dir%/}
	dir=${dir#coursework/}
	ids+=("${dir%%-*}")
	names+=("${dir#*-}")
done

# The tag an experiment is built from, or nothing for the working tree
rev_of() {
	local tag
	tag=$(printf '%s' "$1" | tr '[:upper:]' '[:lower:]')
	if git rev-parse -q --verify "refs/tags/$tag" >/dev/null; then
		echo "$tag"
	fi
}

if [[ -z $experiment ]]; then
	if [[ ! -t 0 ]]; then
		echo "no terminal to ask on: give EXPERIMENT, one of ${ids[*]}" >&2
		exit 2
	fi
	newest=${#ids[@]}
	for i in "${!ids[@]}"; do
		id=${ids[$i]}
		rev=$(rev_of "$id")
		source='working tree'
		[[ -n $rev ]] && source="tag $rev"
		kept=
		[[ -f build/$id/$LDR_NAME ]] && kept='loader kept'
		printf '  %d) %s  %-12s %-14s %s\n' $((i + 1)) "$id" "${names[$i]}" "$source" "$kept" >&2
	done
	read -r -p "$action which experiment? [$newest] " pick
	pick=${pick:-$newest}
	if [[ $pick =~ ^[0-9]+$ ]] && ((pick >= 1 && pick <= ${#ids[@]})); then
		experiment=${ids[$((pick - 1))]}
	else
		experiment=$(printf '%s' "$pick" | tr '[:lower:]' '[:upper:]')
	fi
fi

known=
for id in "${ids[@]}"; do
	[[ $id == "$experiment" ]] && known=1
done
if [[ -z $known ]]; then
	echo "no experiment $experiment: one of ${ids[*]}" >&2
	exit 2
fi

ldr=build/$experiment/$LDR_NAME

if [[ $action == board ]]; then
	host/build/remote_build.sh $(rev_of "$experiment")
	mkdir -p "build/$experiment"
	cp "build/$LDR_NAME" "$ldr"
fi

if [[ ! -f $ldr ]]; then
	echo "no $ldr: run make board EXPERIMENT=$experiment" >&2
	exit 1
fi
echo "booting $experiment" >&2
exec uv run --no-project --with-requirements host/board/requirements.txt \
	python host/board/uart_boot.py "$ldr" --wait 60 --log "build/$experiment/board.log"
