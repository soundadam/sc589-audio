#!/usr/bin/env bash
# Build the SC589 audio_loopback loader streams on the Windows host from this
# working tree, and copy them into build/.
#
#     host/build/remote_build.sh
#     host/build/remote_build.sh e01        # a commit instead of the working tree
#     REMOTE_HOST=other.host host/build/remote_build.sh
#
# The working tree, uncommitted and untracked files included (or the given
# commit), is snapshotted into a commit under refs/remote-build/ without
# touching the index or HEAD,
# sent as a git bundle and checked out detached in the host's clone, which is
# then cleaned of untracked files (ignored build output stays), so files
# deleted here are deleted there too. The host clone is a mirror: edits made
# there are overwritten on the next run. Then host/build/remote_build.ps1 builds
# the three cores and runs make_ldr.ps1.
#
# The firmware files under ADI's licence are not in this repository. They live
# in the private sc589-adi repository at the same paths, listed in its
# firmware-files.txt, and are added to the snapshot from ADI_DIR (default: a
# sc589-adi checkout next to this one). The snapshot ref is local and never
# pushed.
#
# The loader streams are kept in build/ldr/, keyed by the snapshot's firmware/
# and host/build/ trees: a tree that was built before is copied from there
# without reaching the host, so edits outside those directories, or going back
# to an earlier experiment's commit, cost no build.

set -euo pipefail

HOST=${REMOTE_HOST:-noah.tail}
# Relative to the ssh login directory on the host
CLONE='workspaces\adi-cces-examples'
CLONE_SCP=workspaces/adi-cces-examples
BUNDLE=workspaces/remote-build.bundle
REF=refs/remote-build/snapshot
LDR_DIR=firmware/audio_loopback/cces/SC589/sharc/Debug

repo=$(git rev-parse --show-toplevel)
cd "$repo"
rev=${1:-}
base=$(git rev-parse --verify "${rev:-HEAD}^{commit}")

index=$(mktemp)
trap 'rm -f "$index" "$index.bundle"' EXIT
GIT_INDEX_FILE=$index git read-tree "$base"
if [ -z "$rev" ]; then
    GIT_INDEX_FILE=$index git add -A
fi
adi=${ADI_DIR:-$repo/../sc589-adi}
if [ ! -f "$adi/firmware-files.txt" ]; then
    echo "no $adi/firmware-files.txt: clone sc589-adi there or set ADI_DIR" >&2
    exit 1
fi
while IFS= read -r path; do
    # --no-filters keeps the bytes as they are, CRLF included
    blob=$(git hash-object -w --no-filters "$adi/$path")
    GIT_INDEX_FILE=$index git update-index --add --cacheinfo "100644,$blob,$path"
done < "$adi/firmware-files.txt"
tree=$(GIT_INDEX_FILE=$index git write-tree)

key=$(printf '%s %s\n' "$(git rev-parse "$tree:firmware")" "$(git rev-parse "$tree:host/build")" \
    | git hash-object --stdin | cut -c1-12)
cache=build/ldr/$key
mkdir -p build
if ls "$cache"/adc_dac_playback_SC589_*.ldr >/dev/null 2>&1; then
    cp "$cache"/adc_dac_playback_SC589_*.ldr build/
    echo "cached build $key"
    ls -l build/*.ldr
    exit 0
fi

# A fixed date makes an unchanged tree give the same commit, so nothing is sent
snapshot=$(GIT_AUTHOR_DATE='2000-01-01T00:00:00Z' GIT_COMMITTER_DATE='2000-01-01T00:00:00Z' \
    git commit-tree "$tree" -p "$base" -m 'remote build snapshot')
git update-ref "$REF" "$snapshot"

remote_head=$(ssh "$HOST" "git -C $CLONE rev-parse HEAD")
if [ "$remote_head" != "$snapshot" ]; then
    if git merge-base --is-ancestor "$remote_head" "$snapshot" 2>/dev/null; then
        git bundle create -q "$index.bundle" "$REF" "^$remote_head"
    else
        git bundle create -q "$index.bundle" "$REF"
    fi
    scp -q "$index.bundle" "$HOST:$BUNDLE"
    ssh "$HOST" "git -C $CLONE fetch -q ..\\remote-build.bundle +${REF}:${REF}"
fi
# Always check out and clean, so files the IDE or a build rewrote or left
# behind go back to the snapshot
ssh "$HOST" "git -C $CLONE checkout -q -f --detach $snapshot && git -C $CLONE clean -fdq"
echo "host at $(git rev-parse --short "$snapshot") (snapshot of $(git rev-parse --short "$base"))"

ssh "$HOST" "cd $CLONE; powershell -NoProfile -ExecutionPolicy Bypass -File host\\build\\remote_build.ps1"

mkdir -p "$cache"
scp -q "$HOST:$CLONE_SCP/$LDR_DIR/adc_dac_playback_SC589_*.ldr" "$cache/"
cp "$cache"/adc_dac_playback_SC589_*.ldr build/
ls -l build/*.ldr
