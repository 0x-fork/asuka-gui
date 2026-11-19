#!/bin/sh
# Runs bench/ui's benches (asuka test --bench --json) several times with
# one or two CLIs, interleaved, and tabulates them (bench/tools/benchtab).
#
#   bench/ui/run.sh 3 after=./asuka                      # one CLI, 3 runs
#   bench/ui/run.sh 3 before=/tmp/asuka-old:/tmp/old-core after=./asuka
#
# A CLI may name the core it builds with after a colon (ASUKA_CORE): an
# older checkout's core/, to compare with what it was.
set -e
runs=$1; shift
out=${BENCH_OUT:-$(mktemp -d)}
root=$(cd "$(dirname "$0")/../.." && pwd)
args=""
for i in $(seq 1 "$runs"); do
	for set in "$@"; do
		label=${set%%=*}; spec=${set#*=}
		cli=${spec%%:*}; core=""
		[ "$cli" != "$spec" ] && core=${spec#*:}
		f="$out/$label-$i.json"
		(cd "$root" && ASUKA_CORE=$core $cli test --bench --json bench/ui > "$f")
	done
done
for set in "$@"; do
	label=${set%%=*}
	files=$(ls "$out/$label"-*.json | tr '\n' ',' | sed 's/,$//')
	args="$args $label=$files"
done
cd "$root" && go run ./bench/tools/benchtab $args
