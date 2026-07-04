#!/usr/bin/env bash
#
# scripts/test_driver.sh
#
# End-to-end automated test for the mydevice character driver.
# Builds the module, loads it, runs the userspace test app, captures
# kernel log evidence, then cleanly unloads. Intended to be run on a
# real Linux machine or VM (NOT inside a container without kernel headers).
#
# Usage:
#   sudo ./scripts/test_driver.sh
#
# Exit code 0 = all steps passed, non-zero = failure (see printed step).

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DRIVER_DIR="$ROOT_DIR/driver"
APP_DIR="$ROOT_DIR/app"
LOG_FILE="$ROOT_DIR/test_evidence.log"

pass() { echo "[PASS] $1"; }
fail() { echo "[FAIL] $1"; exit 1; }

if [[ $EUID -ne 0 ]]; then
	echo "This script needs root (insmod/rmmod). Re-run with sudo." >&2
	exit 1
fi

echo "=== mydevice driver test run: $(date) ===" | tee "$LOG_FILE"

echo "--- Step 1: build kernel module ---" | tee -a "$LOG_FILE"
make -C "$DRIVER_DIR" clean >>"$LOG_FILE" 2>&1 || true
if make -C "$DRIVER_DIR" >>"$LOG_FILE" 2>&1; then
	pass "module built (hello.ko)" | tee -a "$LOG_FILE"
else
	fail "module build failed - check $LOG_FILE"
fi

echo "--- Step 2: build userspace test app ---" | tee -a "$LOG_FILE"
if gcc -Wall -o "$APP_DIR/test" "$APP_DIR/test.c" >>"$LOG_FILE" 2>&1; then
	pass "test app built" | tee -a "$LOG_FILE"
else
	fail "test app build failed - check $LOG_FILE"
fi

echo "--- Step 3: unload any previous instance ---" | tee -a "$LOG_FILE"
rmmod hello 2>/dev/null || true

echo "--- Step 4: insmod ---" | tee -a "$LOG_FILE"
if insmod "$DRIVER_DIR/hello.ko"; then
	pass "insmod succeeded"
else
	fail "insmod failed"
fi
sleep 0.3

echo "--- Step 5: verify /dev/mydevice exists ---" | tee -a "$LOG_FILE"
if [[ -c /dev/mydevice ]]; then
	pass "/dev/mydevice created"
else
	fail "/dev/mydevice not found"
fi

echo "--- Step 6: run userspace test app ---" | tee -a "$LOG_FILE"
if "$APP_DIR/test"; then
	pass "test app ran successfully"
else
	fail "test app returned non-zero"
fi

echo "--- Step 7: capture dmesg evidence ---" | tee -a "$LOG_FILE"
dmesg | grep mydevice | tail -n 30 | tee -a "$LOG_FILE"

echo "--- Step 8: rmmod ---" | tee -a "$LOG_FILE"
if rmmod hello; then
	pass "rmmod succeeded"
else
	fail "rmmod failed"
fi

echo "--- Step 9: verify /dev/mydevice removed ---" | tee -a "$LOG_FILE"
if [[ ! -e /dev/mydevice ]]; then
	pass "/dev/mydevice cleaned up"
else
	fail "/dev/mydevice still present after rmmod"
fi

echo "=== ALL TESTS PASSED ===" | tee -a "$LOG_FILE"
echo "Full log saved to: $LOG_FILE"
