#!/bin/sh
# Instructions retired (macOS's /usr/bin/time -l) by asuka commands on the
# repository's packages: the median of runs runs, for one CLI or two (a
# loaded machine moves a time, not this count).
#
#   bench/tools/instr.sh 5 ./asuka-old ./asuka-new
set -e
runs=$1; shift
root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
med() { sort -n | awk '{a[NR]=$1} END {print a[int((NR+1)/2)]}'; }
for cmd in "version" "check examples/counter" "check std/base" "check std/template" "check examples/usage" "check examples/gallery" "fmt -l std/base" "test examples/counter"; do
	line=$(printf '%-28s' "$cmd")
	for cli in "$@"; do
		v=$(for i in $(seq 1 "$runs"); do /usr/bin/time -l $cli $cmd 2>&1 >/dev/null | awk '/instructions retired/ {print $1}'; done | med)
		line="$line $(printf '%10.0fM' "$(echo "$v / 1000000" | bc -l)")"
	done
	echo "$line"
done
