#!/bin/bash
# Parity test script - tests C version functionality

set -e

echo "=== ISAE Monitor C Test Suite ==="
echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

PASS_COUNT=0
FAIL_COUNT=0

pass() {
    echo -e "${GREEN}✓${NC} $1"
    ((PASS_COUNT++)) || true
}

fail() {
    echo -e "${RED}✗${NC} $1"
    ((FAIL_COUNT++)) || true
}

C_BINARY="./isae_monitor"

# Check if C build exists
if [ ! -f "$C_BINARY" ]; then
    echo "Building C project..."
    make
fi

# Test 1: Help output
echo "Test 1: CLI help output"
if $C_BINARY --help | grep -q "ISAE School Announcement Monitor"; then
    pass "C version shows help"
else
    fail "C version help missing"
fi

# Test 2: Version output
echo "Test 2: Version output"
if $C_BINARY --version | grep -q "1.0.0"; then
    pass "C version shows correct version"
else
    fail "C version version mismatch"
fi

# Test 3: Once mode (no API keys needed)
echo "Test 3: Once mode execution"
if $C_BINARY --once 2>&1 | grep -q "Processed"; then
    pass "Once mode works"
else
    fail "Once mode failed"
fi

# Test 4: Config from environment
echo "Test 4: Environment variable parsing"
export TELEGRAM_BOT_TOKEN="test_token"
export TELEGRAM_CHAT_IDS="-1001234567890"
if $C_BINARY --once 2>&1 | grep -q "Warning: No AI provider configured"; then
    pass "Environment variables parsed"
else
    fail "Environment variable parsing failed"
fi

# Test 5: State file creation
echo "Test 5: State file management"
export ISAE_STATE_FILE="/tmp/test_isae_state.json"
$C_BINARY --once > /dev/null 2>&1
if [ -f "/tmp/test_isae_state.json" ]; then
    pass "State file created"
    rm -f "/tmp/test_isae_state.json"
else
    fail "State file not created"
fi

echo ""
echo "=== Results ==="
echo -e "Passed: ${GREEN}${PASS_COUNT}${NC}"
echo -e "Failed: ${RED}${FAIL_COUNT}${NC}"

if [ $FAIL_COUNT -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed${NC}"
    exit 1
fi
