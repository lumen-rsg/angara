#!/bin/bash
# ═══════════════════════════════════════════════════════════════
# Angara MQTT Test Suite Runner
#
# Auto-starts a Mosquitto broker on port 11883, runs all MQTT
# test programs, and reports pass/fail results.
#
# Prerequisites:
#   - angc compiler built (make)
#   - mqtt module built (make modules)
#   - mosquitto installed (brew install mosquitto)
#
# Usage:
#   bash run_mqtt_tests.sh [options]
#
# Options:
#   --compiler <path>   Path to angc (default: ../../build/angc)
#   --port <port>       MQTT broker port (default: 11883)
#   --keep-broker       Do not kill broker on exit
#   --test <name>       Run only a specific test (e.g. 02_pubsub_qos)
# ═══════════════════════════════════════════════════════════════

set -euo pipefail

# --- Colors ---
RED='\033[1;31m'
GREEN='\033[1;32m'
YELLOW='\033[1;33m'
BLUE='\033[1;34m'
MAGENTA='\033[1;35m'
CYAN='\033[1;36m'
DIM='\033[2m'
BOLD='\033[1m'
RESET='\033[0m'

# --- Defaults ---
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
ANGC="$PROJECT_DIR/build/angc"
BROKER_PORT=11883
KEEP_BROKER=false
SINGLE_TEST=""
PASS=0
FAIL=0
FAILURES=()

# --- Parse Arguments ---
while [[ $# -gt 0 ]]; do
    case "$1" in
        --compiler)  ANGC="$2"; shift 2 ;;
        --port)      BROKER_PORT="$2"; shift 2 ;;
        --keep-broker) KEEP_BROKER=true; shift ;;
        --test)      SINGLE_TEST="$2"; shift 2 ;;
        -h|--help)
            head -25 "$0" | tail -20
            exit 0
            ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

# --- Temp Directory ---
TMPDIR_TEST="$(mktemp -d /tmp/angara_mqtt_test.XXXXXX)"
MOSQUITTO_CONF="$TMPDIR_TEST/mosquitto.conf"
MOSQUITTO_PID_FILE="$TMPDIR_TEST/mosquitto.pid"

cleanup() {
    if [ "$KEEP_BROKER" = false ] && [ -f "$MOSQUITTO_PID_FILE" ]; then
        local pid
        pid=$(cat "$MOSQUITTO_PID_FILE" 2>/dev/null || true)
        if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null || true
            wait "$pid" 2>/dev/null || true
        fi
    fi
    rm -rf "$TMPDIR_TEST"
}
trap cleanup EXIT

# --- Write Mosquitto Config ---
cat > "$MOSQUITTO_CONF" <<EOF
listener $BROKER_PORT
allow_anonymous true
max_keepalive 3600
log_type none
EOF

# --- Find Mosquitto ---
MOSQUITTO_BIN=""
for candidate in mosquitto /opt/homebrew/sbin/mosquitto /usr/local/sbin/mosquitto /usr/sbin/mosquitto; do
    if command -v "$candidate" &>/dev/null; then
        MOSQUITTO_BIN="$candidate"
        break
    fi
done

if [ -z "$MOSQUITTO_BIN" ]; then
    printf "${RED}Error: mosquitto not found.${RESET}\n"
    printf "${YELLOW}Install with: brew install mosquitto${RESET}\n"
    exit 1
fi

# --- Header ---
echo ""
printf "${CYAN}════════════════════════════════════════════════════════${RESET}\n"
printf "${CYAN}  Angara MQTT Test Suite${RESET}\n"
printf "${CYAN}  Compiler: $ANGC${RESET}\n"
printf "${CYAN}  Broker:   localhost:$BROKER_PORT ($MOSQUITTO_BIN)${RESET}\n"
printf "${CYAN}════════════════════════════════════════════════════════${RESET}\n"
echo ""

if [ ! -f "$ANGC" ]; then
    printf "${RED}Error: Compiler not found at $ANGC${RESET}\n"
    printf "${YELLOW}Run 'make' first to build the compiler.${RESET}\n"
    exit 1
fi

# --- Start Mosquitto ---
printf "${DIM}Starting Mosquitto broker on port $BROKER_PORT...${RESET}\n"
"$MOSQUITTO_BIN" -c "$MOSQUITTO_CONF" -d -p "$BROKER_PORT" 2>/dev/null

# Find the PID (mosquitto -d forks, so $! may not work)
sleep 0.3
MOSQUITTO_PID=$(pgrep -f "mosquitto.*$BROKER_PORT" 2>/dev/null | head -1 || true)
if [ -n "$MOSQUITTO_PID" ]; then
    echo "$MOSQUITTO_PID" > "$MOSQUITTO_PID_FILE"
fi

# Wait for port readiness
PORT_READY=false
for i in $(seq 1 25); do
    if nc -z localhost "$BROKER_PORT" 2>/dev/null; then
        PORT_READY=true
        break
    fi
    sleep 0.2
done

if [ "$PORT_READY" = false ]; then
    printf "${RED}Error: Mosquitto broker did not start on port $BROKER_PORT${RESET}\n"
    exit 1
fi
printf "${DIM}Broker ready (PID: $MOSQUITTO_PID).${RESET}\n\n"

# --- Helper: Find compiled binary ---
find_binary() {
    local test_name="$1"
    local tmp_path="$2"
    if [ -f "$tmp_path" ]; then
        echo "$tmp_path"
        return
    fi
    local cwd_path="$PROJECT_DIR/$test_name"
    if [ -f "$cwd_path" ]; then
        mv "$cwd_path" "$tmp_path"
        echo "$tmp_path"
        return
    fi
    echo ""
}

# --- Determine Test Files ---
if [ -n "$SINGLE_TEST" ]; then
    TEST_FILES=("$SCRIPT_DIR/${SINGLE_TEST}.an")
else
    mapfile -t TEST_FILES < <(find "$SCRIPT_DIR" -maxdepth 1 -name '*.an' | sort)
fi

# --- Run Tests ---
for test_file in "${TEST_FILES[@]}"; do
    [ -f "$test_file" ] || continue
    test_name="$(basename "$test_file" .an)"
    binary="$TMPDIR_TEST/${test_name}"

    # Determine group label
    if [[ "$test_name" == iot_* ]]; then
        GROUP="${MAGENTA}IoT${RESET}  "
    else
        GROUP="${BLUE}Core${RESET} "
    fi

    printf "  ${GROUP} ${BOLD}${test_name}${RESET}: "

    # Compile (compiler outputs binary to CWD, ignoring -o)
    compile_output=$(cd "$PROJECT_DIR" && "$ANGC" "$test_file" 2>&1) && compile_rc=$? || compile_rc=$?

    if [ $compile_rc -ne 0 ]; then
        printf "${RED}COMPILE ERROR${RESET}\n"
        echo "$compile_output" | grep -i "error" | head -3 | sed 's/^/         /'
        FAIL=$((FAIL + 1))
        FAILURES+=("$test_name (compile error)")
        continue
    fi

    # Find binary (compiler outputs to CWD which is project root)
    if [ -f "$PROJECT_DIR/$test_name" ]; then
        mv "$PROJECT_DIR/$test_name" "$binary"
        actual_binary="$binary"
    else
        actual_binary=""
    fi
    if [ -z "$actual_binary" ]; then
        printf "${RED}NO BINARY${RESET}\n"
        FAIL=$((FAIL + 1))
        FAILURES+=("$test_name (binary not found)")
        continue
    fi

    # Run
    # Set DYLD_LIBRARY_PATH so the binary can find native modules (linked with relative paths)
    run_output=$(DYLD_LIBRARY_PATH="$PROJECT_DIR/build/modules${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}" "$actual_binary" 2>&1) && run_rc=$? || run_rc=$?

    if [ $run_rc -ne 0 ]; then
        if [ $run_rc -eq 139 ]; then
            printf "${RED}SEGFAULT${RESET}\n"
        elif [ $run_rc -eq 134 ]; then
            printf "${RED}ABORT${RESET}\n"
        else
            printf "${RED}FAIL${RESET} (exit $run_rc)\n"
        fi
        # Show last few lines of output for debugging
        echo "$run_output" | tail -5 | sed 's/^/         /'
        FAIL=$((FAIL + 1))
        FAILURES+=("$test_name (exit $run_rc)")
        continue
    fi

    printf "${GREEN}PASS${RESET}\n"
    PASS=$((PASS + 1))
done

# --- Summary ---
TOTAL=$((PASS + FAIL))
echo ""
printf "${CYAN}════════════════════════════════════════════════════════${RESET}\n"
printf "  ${BOLD}Results: ${GREEN}${PASS} passed${RESET}"
if [ $FAIL -gt 0 ]; then
    printf ", ${RED}${FAIL} failed${RESET}"
fi
printf " out of ${TOTAL} tests\n"
printf "${CYAN}════════════════════════════════════════════════════════${RESET}\n"

if [ ${#FAILURES[@]} -gt 0 ]; then
    echo ""
    printf "${YELLOW}Failed tests:${RESET}\n"
    for f in "${FAILURES[@]}"; do
        printf "  ${RED}- ${f}${RESET}\n"
    done
fi

echo ""
exit $FAIL
