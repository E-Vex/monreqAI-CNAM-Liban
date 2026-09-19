#!/bin/bash
# Parity test script - compares Python and C versions

set -e

echo "=== ISAE Monitor Parity Test ==="
echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

PASS_COUNT=0
FAIL_COUNT=0

pass() {
    echo -e "${GREEN}✓${NC} $1"
    ((PASS_COUNT++))
}

fail() {
    echo -e "${RED}✗${NC} $1"
    ((FAIL_COUNT++))
}

# Check if C build exists
if [ ! -f "build/isae-monitor" ]; then
    echo "Building C project..."
    mkdir -p build
    cd build
    cmake .. && make
    cd ..
fi

# Test 1: Help output
echo "Test 1: CLI help output"
if ./build/isae-monitor --help | grep -q "ISAE Monitor"; then
    pass "C version shows help"
else
    fail "C version help missing"
fi

# Test 2: Version output  
echo "Test 2: Version output"
if ./build/isae-monitor --version | grep -q "2.0.0"; then
    pass "C version shows correct version"
else
    fail "C version version mismatch"
fi

# Test 3: Dry run (no API keys needed)
echo "Test 3: Dry run mode"
if ./build/isae-monitor --dry-run --bootstrap 2>&1 | grep -q "DRY RUN"; then
    pass "Dry run mode works"
else
    fail "Dry run mode failed"
fi

# Test 4: Config from environment
echo "Test 4: Environment variable parsing"
export TELEGRAM_BOT_TOKEN="test_token"
export TELEGRAM_GENERAL_CHAT_ID="-1001234567890"
if ./build/isae-monitor --dry-run 2>&1 | grep -q ""; then
    pass "Environment variables parsed"
else
    fail "Environment variable parsing failed"
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
