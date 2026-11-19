#!/bin/sh
# Instructions per iteration of each bench in bench/ui, as the CPU counts
# them (macOS's /usr/bin/time -l: "instructions retired"): a measure that
# a loaded machine doesn't disturb, unlike time. Each bench runs k and
# 2k iterations in the runner asuka test builds (k enough for about 400
# million instructions, from a first estimate); the difference over k is
# one iteration's.
#
#   bench/ui/instr.sh ./asuka [core] [pattern]   # core: ASUKA_CORE
set -e
cli=$1; core=${2:-}; pattern=${3:-}
root=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d)
cd "$root"
# The runner: asuka test builds it into a temporary directory and runs
# it; it's copied from there while it runs.
ASUKA_CORE=$core $cli test --bench --benchtime 100000000x --run "select a row of 1k" bench/ui >/dev/null 2>&1 &
pid=$!
exe=""
for i in $(seq 1 600); do
	p=$(pgrep -f 'asuka-run -bench .*-benchn 100000000' | head -1)
	if [ -n "$p" ]; then exe=$(ps -o command= -p "$p" | awk '{print $1}'); cp "$exe" "$work/runner" && break; fi
	sleep 0.1
done
pkill -f 'asuka-run -bench .*-benchn 100000000' || true
wait $pid 2>/dev/null || true
[ -x "$work/runner" ] || { echo "no runner" >&2; exit 1; }
instr() {
	/usr/bin/time -l "$work/runner" -bench -no-diag -benchn "$2" -cases 100 -diag-json -test "$1" -aot 2>&1 >/dev/null | awk '/instructions retired/ {print $1}'
}
grep -h '^bench "' bench/ui/*.asuka | sed 's/^bench "\(.*\)" {$/\1/' | while read -r name; do
	case "$name" in *"$pattern"*) ;; *) continue ;; esac
	a=$(instr "$name" 1); b=$(instr "$name" 11)
	est=$(( (b - a) / 10 )); [ "$est" -lt 1000 ] && est=1000
	k=$(( 400000000 / est )); [ "$k" -lt 4 ] && k=4; [ "$k" -gt 20000 ] && k=20000
	a=$(instr "$name" "$k"); b=$(instr "$name" $((k * 2)))
	echo "$name	$(( (b - a) / k ))"
done
rm -rf "$work"
