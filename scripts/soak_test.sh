#!/usr/bin/env bash
# GridGuard 24-Hour Soak Test
# Monitors system stability, memory usage, and restart behavior over extended runtime

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
LOG_DIR="$PROJECT_ROOT/logs/soak_test"
DURATION_HOURS="${SOAK_TEST_HOURS:-24}"
SAMPLE_INTERVAL_SEC=60
METRICS_FILE="$LOG_DIR/metrics.csv"
WATCHDOG_PID_FILE="/tmp/gridguard_watchdog.pid"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${BLUE}[$(date '+%Y-%m-%d %H:%M:%S')]${NC} INFO: $*"
}

log_warn() {
    echo -e "${YELLOW}[$(date '+%Y-%m-%d %H:%M:%S')]${NC} WARN: $*"
}

log_error() {
    echo -e "${RED}[$(date '+%Y-%m-%d %H:%M:%S')]${NC} ERROR: $*"
}

log_success() {
    echo -e "${GREEN}[$(date '+%Y-%m-%d %H:%M:%S')]${NC} SUCCESS: $*"
}

cleanup() {
    log_info "Cleaning up soak test..."
    cd "$PROJECT_ROOT"
    make stop 2>/dev/null || true

    if [[ -f "$METRICS_FILE" ]]; then
        log_info "Generating final report..."
        generate_report
    fi

    log_success "Soak test cleanup complete"
}

trap cleanup EXIT INT TERM

generate_report() {
    local report_file="$LOG_DIR/soak_test_report.txt"

    cat > "$report_file" <<EOF
═══════════════════════════════════════════════════════════════
  GRIDGUARD SOAK TEST REPORT
  Duration: ${DURATION_HOURS}h
  Generated: $(date '+%Y-%m-%d %H:%M:%S')
═══════════════════════════════════════════════════════════════

EOF

    if [[ ! -f "$METRICS_FILE" ]]; then
        echo "ERROR: No metrics collected" >> "$report_file"
        return
    fi

    # Parse metrics
    local samples=$(wc -l < "$METRICS_FILE")
    local total_restarts=$(awk -F',' '{sum+=$6} END {print sum}' "$METRICS_FILE" 2>/dev/null || echo "0")
    local max_rss=$(awk -F',' 'BEGIN{max=0} {if($2>max) max=$2} END {print max}' "$METRICS_FILE")
    local avg_rss=$(awk -F',' '{sum+=$2; count++} END {print int(sum/count)}' "$METRICS_FILE")
    local max_cpu=$(awk -F',' 'BEGIN{max=0} {if($3>max) max=$3} END {print max}' "$METRICS_FILE")
    local avg_cpu=$(awk -F',' '{sum+=$3; count++} END {printf "%.1f", sum/count}' "$METRICS_FILE")

    cat >> "$report_file" <<EOF
STABILITY METRICS
─────────────────────────────────────────────────────────────
  Samples collected:       $samples
  Total restarts:          $total_restarts
  Uptime stability:        $(echo "scale=2; (1 - $total_restarts/$samples) * 100" | bc)%

MEMORY USAGE (RSS)
─────────────────────────────────────────────────────────────
  Peak memory:             $(numfmt --to=iec-i --suffix=B $((max_rss * 1024)))
  Average memory:          $(numfmt --to=iec-i --suffix=B $((avg_rss * 1024)))
  Memory growth:           $(calculate_memory_growth)

CPU USAGE
─────────────────────────────────────────────────────────────
  Peak CPU:                ${max_cpu}%
  Average CPU:             ${avg_cpu}%

PROCESS HEALTH
─────────────────────────────────────────────────────────────
EOF

    # Check for memory leaks (>10% growth over duration)
    local leak_detected=$(calculate_memory_growth | grep -q "LEAK" && echo "YES" || echo "NO")
    echo "  Memory leak detected:    $leak_detected" >> "$report_file"

    # Check for crashes
    local crash_count=$(grep -c "CRASH\|FATAL" "$PROJECT_ROOT"/logs/*.log 2>/dev/null || echo "0")
    echo "  Crash events:            $crash_count" >> "$report_file"

    # Check heartbeat failures
    local hb_failures=$(grep -c "frozen\|heartbeat timeout" "$PROJECT_ROOT"/logs/watchdog.log 2>/dev/null || echo "0")
    echo "  Heartbeat failures:      $hb_failures" >> "$report_file"

    cat >> "$report_file" <<EOF

VERDICT
─────────────────────────────────────────────────────────────
EOF

    if [[ "$total_restarts" -eq 0 && "$crash_count" -eq 0 && "$leak_detected" == "NO" ]]; then
        echo "  Status: ✅ PRODUCTION READY" >> "$report_file"
        echo "  All metrics within acceptable ranges." >> "$report_file"
    elif [[ "$total_restarts" -lt 3 && "$crash_count" -eq 0 ]]; then
        echo "  Status: ⚠️  ACCEPTABLE WITH MONITORING" >> "$report_file"
        echo "  Minor instability detected, recommend extended monitoring." >> "$report_file"
    else
        echo "  Status: ❌ NOT PRODUCTION READY" >> "$report_file"
        echo "  Critical issues detected, investigate logs." >> "$report_file"
    fi

    echo "" >> "$report_file"
    echo "Full metrics: $METRICS_FILE" >> "$report_file"
    echo "Logs: $PROJECT_ROOT/logs/" >> "$report_file"

    cat "$report_file"
}

calculate_memory_growth() {
    local first_rss=$(head -2 "$METRICS_FILE" | tail -1 | awk -F',' '{print $2}')
    local last_rss=$(tail -1 "$METRICS_FILE" | awk -F',' '{print $2}')

    if [[ -z "$first_rss" || -z "$last_rss" || "$first_rss" -eq 0 ]]; then
        echo "N/A"
        return
    fi

    local growth=$(echo "scale=2; (($last_rss - $first_rss) / $first_rss) * 100" | bc)

    if (( $(echo "$growth > 10" | bc -l) )); then
        echo "+${growth}% (LEAK SUSPECTED)"
    else
        echo "+${growth}%"
    fi
}

collect_metrics() {
    local watchdog_pid=$(cat "$WATCHDOG_PID_FILE" 2>/dev/null || echo "")

    if [[ -z "$watchdog_pid" ]]; then
        log_error "Watchdog PID not found"
        return 1
    fi

    # Get watchdog + children PIDs
    local pids=$(pgrep -P "$watchdog_pid" | tr '\n' ',' | sed 's/,$//')
    pids="${watchdog_pid},${pids}"

    # Aggregate RSS (memory) and CPU for all processes
    local total_rss=0
    local total_cpu=0
    local process_count=0

    for pid in $(echo "$pids" | tr ',' ' '); do
        if [[ -d "/proc/$pid" ]]; then
            local rss=$(awk '/^VmRSS:/ {print $2}' "/proc/$pid/status" 2>/dev/null || echo "0")
            local cpu=$(ps -p "$pid" -o %cpu= 2>/dev/null | tr -d ' ' || echo "0")

            total_rss=$((total_rss + rss))
            total_cpu=$(echo "$total_cpu + $cpu" | bc)
            process_count=$((process_count + 1))
        fi
    done

    # Get restart count from metrics endpoint
    local restart_count=$(curl -s http://localhost:8080/metrics 2>/dev/null | grep -o '"restart_count":[0-9]*' | awk -F':' '{print $2}' || echo "0")

    # Get open file descriptors
    local fd_count=$(find /proc/"$watchdog_pid"/fd/ -type l 2>/dev/null | wc -l)

    # Timestamp,RSS_KB,CPU_percent,ProcessCount,FD_Count,RestartCount
    echo "$(date +%s),$total_rss,$total_cpu,$process_count,$fd_count,$restart_count" >> "$METRICS_FILE"
}

run_stress_test() {
    log_info "Running concurrent API stress test..."

    local token=$(cat "$PROJECT_ROOT/jwt_token.txt" 2>/dev/null || echo "")
    if [[ -z "$token" ]]; then
        log_warn "No JWT token found, skipping stress test"
        return
    fi

    # Send 10 concurrent forecast requests
    for i in {1..10}; do
        curl -s -H "Authorization: Bearer $token" http://localhost:8080/forecast > /dev/null 2>&1 &
    done
    wait

    log_success "Stress test complete"
}

main() {
    log_info "GridGuard Soak Test Starting"
    log_info "Duration: ${DURATION_HOURS} hours"
    log_info "Sample interval: ${SAMPLE_INTERVAL_SEC} seconds"

    # Setup
    mkdir -p "$LOG_DIR"
    rm -f "$METRICS_FILE"
    echo "timestamp,rss_kb,cpu_percent,process_count,fd_count,restart_count" > "$METRICS_FILE"

    # Start system
    cd "$PROJECT_ROOT"
    log_info "Starting GridGuard system..."
    make dev > "$LOG_DIR/startup.log" 2>&1 &

    # Wait for startup
    sleep 10

    if [[ ! -f "$WATCHDOG_PID_FILE" ]]; then
        log_error "Watchdog failed to start, check $LOG_DIR/startup.log"
        exit 1
    fi

    log_success "System started (Watchdog PID: $(cat "$WATCHDOG_PID_FILE"))"

    # Main monitoring loop
    local end_time=$(($(date +%s) + DURATION_HOURS * 3600))
    local iteration=0
    local stress_interval=300  # Run stress test every 5 minutes

    while [[ $(date +%s) -lt $end_time ]]; do
        collect_metrics

        # Periodic stress test
        if [[ $((iteration % (stress_interval / SAMPLE_INTERVAL_SEC))) -eq 0 ]]; then
            run_stress_test
        fi

        # Progress indicator
        local elapsed=$(($(date +%s) - (end_time - DURATION_HOURS * 3600)))
        local progress=$((elapsed * 100 / (DURATION_HOURS * 3600)))
        log_info "Progress: ${progress}% (${elapsed}s / $((DURATION_HOURS * 3600))s)"

        iteration=$((iteration + 1))
        sleep "$SAMPLE_INTERVAL_SEC"
    done

    log_success "Soak test completed successfully!"
}

main "$@"
