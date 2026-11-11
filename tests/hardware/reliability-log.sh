#!/bin/bash
# Evidence logger for the physical-Dial reliability checks (hardware checklist:
# reconnect cycles, out of range, suspend/resume, 8 h idle). It only reads:
# `opendialctl --json status` and /proc of the running opendiald.
#
# Usage: tests/hardware/reliability-log.sh [OUT.csv] [INTERVAL_SECONDS]
# Stop with Ctrl+C; a summary is printed. Sampling gaps far longer than the
# interval are reported as a suspend (the machine was asleep).
set -u
out=${1:-$HOME/opendial-reliability-$(date +%Y%m%d-%H%M%S).csv}
iv=${2:-2}
tick=$(getconf CLK_TCK)
echo "epoch,connected,grabbed,pid,rss_kb,cpu_s" > "$out"
echo "logging to $out every ${iv}s; Ctrl+C to stop"

prev="" connects=0 disconnects=0 suspends=0 restarts=0
first_rss="" last_rss="" last_t="" start=$(date +%s) lastpid=""

summary() {
    now=$(date +%s)
    echo
    echo "duration: $((now - start))s"
    echo "connects=$connects disconnects=$disconnects suspends=$suspends daemon_restarts=$restarts"
    [ -n "$first_rss" ] && echo "rss first=${first_rss}kB last=${last_rss}kB delta=$((last_rss - first_rss))kB"
    echo "csv: $out"
    exit 0
}
trap summary INT TERM

while :; do
    t=$(date +%s)
    if [ -n "$last_t" ] && [ $((t - last_t)) -gt $((iv * 5 + 10)) ]; then
        suspends=$((suspends + 1))
        echo "$(date +%T) gap of $((t - last_t))s (suspend?)"
    fi
    last_t=$t
    pid=$(pgrep -x opendiald | head -1)
    if [ -n "$pid" ] && [ -n "$lastpid" ] && [ "$pid" != "$lastpid" ]; then
        restarts=$((restarts + 1)); echo "$(date +%T) opendiald restarted ($lastpid -> $pid)"
    fi
    lastpid=$pid
    rss=0 cpu=0
    if [ -n "$pid" ]; then
        rss=$(awk '/VmRSS/{print $2}' /proc/$pid/status 2>/dev/null || echo 0)
        cpu=$(awk -v k="$tick" '{printf "%.2f", ($14+$15)/k}' /proc/$pid/stat 2>/dev/null || echo 0)
    fi
    state=$(opendialctl --json status 2>/dev/null | python3 -c '
import json,sys
try:
    d=json.load(sys.stdin)["device"]; print(int(d["connected"]), int(d["grabbed"]))
except Exception: print("? ?")')
    set -- $state; conn=$1 grab=$2
    echo "$t,$conn,$grab,${pid:-},$rss,$cpu" >> "$out"
    if [ -n "$prev" ] && [ "$conn" != "$prev" ]; then
        if [ "$conn" = 1 ]; then connects=$((connects + 1)); echo "$(date +%T) connected (#$connects, grabbed=$grab)"
        elif [ "$conn" = 0 ]; then disconnects=$((disconnects + 1)); echo "$(date +%T) disconnected (#$disconnects)"; fi
    fi
    prev=$conn
    [ "$rss" != 0 ] && { [ -z "$first_rss" ] && first_rss=$rss; last_rss=$rss; }
    sleep "$iv"
done
