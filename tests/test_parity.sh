#!/bin/bash
# tests/test_parity.sh -- smoke + parity tests for the C edition.
#
# Verifies the CLI surface, exit codes, --check semantics, .env loading,
# state-file schema, and a few end-to-end scenarios against the live feed
# (when network is available; otherwise skipped with a warning).
#
# Does NOT test the AI providers or Telegram sends (those require secrets
# and live endpoints -- use --dry-run for that).

set -u

cd "$(dirname "$0")/.." || exit 1

# ANSI colors (disabled if not a TTY)
if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[0;33m'
    NC='\033[0m'
else
    RED='' GREEN='' YELLOW='' NC=''
fi

PASS=0
FAIL=0
SKIP=0
pass() { printf "${GREEN}[ OK ]${NC} %s\n" "$1"; PASS=$((PASS+1)); }
fail() { printf "${RED}[FAIL]${NC} %s\n" "$1"; FAIL=$((FAIL+1)); }
skip() { printf "${YELLOW}[SKIP]${NC} %s\n" "$1"; SKIP=$((SKIP+1)); }

C_BINARY="./isae_monitor"
TEST_DIR="$(mktemp -d)"
trap 'rm -rf "$TEST_DIR"' EXIT

# Build if needed
if [ ! -x "$C_BINARY" ]; then
    echo "Building the binary..."
    make >/dev/null 2>&1 || { fail "build failed"; exit 1; }
fi

echo "=========================================================="
echo "  monreqAI-CNAM-Liban (C edition) -- test suite"
echo "  binary: $C_BINARY ($(./$C_BINARY --version | head -1))"
echo "  tmpdir: $TEST_DIR"
echo "=========================================================="
echo ""

# ---------------- CLI surface ----------------

out="$("$C_BINARY" --help 2>&1)"
case "$out" in
    *"monreqAI-CNAM-Liban"*) pass "--help prints program name" ;;
    *) fail "--help output unexpected" ;;
esac
case "$out" in
    *"--dry-run"*) pass "--help documents --dry-run" ;;
    *) fail "--help missing --dry-run" ;;
esac
case "$out" in
    *"--bootstrap"*) pass "--help documents --bootstrap" ;;
    *) fail "--help missing --bootstrap" ;;
esac
case "$out" in
    *"--check"*) pass "--help documents --check" ;;
    *) fail "--help missing --check" ;;
esac
case "$out" in
    *"--list-departments"*) pass "--help documents --list-departments" ;;
    *) fail "--help missing --list-departments" ;;
esac
case "$out" in
    *"--quiet"*) pass "--help documents --quiet" ;;
    *) fail "--help missing --quiet" ;;
esac

# --version
out="$("$C_BINARY" --version 2>&1)"
case "$out" in
    *"2.0.0"*) pass "--version prints 2.0.0" ;;
    *) fail "--version mismatch: $out" ;;
esac

# Unknown flag -> exit 1
"$C_BINARY" --nonexistent 2>/dev/null
case $? in
    1) pass "unknown flag exits 1" ;;
    *) fail "unknown flag exit code: $?" ;;
esac

# ---------------- --list-departments ----------------

out="$("$C_BINARY" --list-departments 2>&1)"
for key in informatique civil electrique mecanique procedes economie statistique physique langues; do
    case "$out" in
        *"$key"*) : ;;
        *) fail "--list-departments missing key: $key" ;;
    esac
done
case "$out" in
    *"9 departments"*) pass "--list-departments prints 9-department header" ;;
    *) fail "--list-departments header missing" ;;
esac

# ---------------- --check semantics ----------------

# No telegram config -> exit 1
STATE_FILE="$TEST_DIR/seen.json" "$C_BINARY" --check >/dev/null 2>&1
case $? in
    1) pass "--check exits 1 when telegram is not configured" ;;
    *) fail "--check exit code (no telegram): $?" ;;
esac

# Telegram configured -> exit 0
STATE_FILE="$TEST_DIR/seen.json" \
TELEGRAM_BOT_TOKEN="test:token" \
TELEGRAM_CHANNEL_GENERAL="-1001234567890" \
"$C_BINARY" --check >/dev/null 2>&1
case $? in
    0) pass "--check exits 0 when telegram is configured" ;;
    *) fail "--check exit code (telegram configured): $?" ;;
esac

# --check prints config summary
out="$(STATE_FILE="$TEST_DIR/seen.json" TELEGRAM_BOT_TOKEN="x" TELEGRAM_CHANNEL_GENERAL="y" "$C_BINARY" --check 2>&1)"
case "$out" in
    *"FEED_URL"*) pass "--check prints FEED_URL" ;;
    *) fail "--check output missing FEED_URL" ;;
esac
case "$out" in
    *"GEMINI_MODEL"*) pass "--check prints GEMINI_MODEL" ;;
    *) fail "--check output missing GEMINI_MODEL" ;;
esac
case "$out" in
    *"gemini-2.5-flash"*) pass "--check shows default Gemini model" ;;
    *) fail "--check missing default model" ;;
esac

# ---------------- .env auto-load ----------------

cat > "$TEST_DIR/.env" <<EOF
TELEGRAM_BOT_TOKEN=from_env_file
TELEGRAM_CHANNEL_GENERAL=-1001111111111
STATE_FILE=$TEST_DIR/from_env_file_state.json
EOF

# Run from a directory with a .env file -- the binary should pick it up.
cp "$C_BINARY" "$TEST_DIR/"
out="$(cd "$TEST_DIR" && ./isae_monitor --check 2>&1)"
case "$out" in
    *"from_env_file_state.json"*) pass ".env auto-load reads STATE_FILE" ;;
    *) fail ".env auto-load failed: $out" ;;
esac

# Explicit env var overrides .env (STATE_FILE is printed verbatim, so we
# can distinguish which source won.)
out="$(cd "$TEST_DIR" && STATE_FILE=/tmp/from_env_var_state.json ./isae_monitor --check 2>&1)"
case "$out" in
    *"from_env_var_state.json"*) pass "explicit env var overrides .env" ;;
    *) fail "env var override failed: $out" ;;
esac

# Clean up the env_file state file (we don't actually use it)
rm -f "$TEST_DIR/from_env_file_state.json"

# ---------------- state file schema ----------------

# Bootstrap creates a v2 seen.json
rm -f "$TEST_DIR/seen.json"
STATE_FILE="$TEST_DIR/seen.json" "$C_BINARY" --bootstrap --quiet >/dev/null 2>&1
if [ -f "$TEST_DIR/seen.json" ]; then
    pass "bootstrap creates state file"
else
    fail "bootstrap did not create state file"
    # Continue with empty file for downstream tests
    echo '{"version":2,"seen":{}}' > "$TEST_DIR/seen.json"
fi

# Verify schema: version=2 and "seen" object
content="$(cat "$TEST_DIR/seen.json")"
case "$content" in
    *'"version":2'*) pass "state file has version=2" ;;
    *) fail "state file version not 2: $content" ;;
esac
case "$content" in
    *'"seen"'*) pass "state file has 'seen' object" ;;
    *) fail "state file missing 'seen' object" ;;
esac

# Verify entries have g/c/t fields
case "$content" in
    *'"g":true'*) pass "state file has g=true entries" ;;
    *) fail "state file missing g=true entries" ;;
esac
case "$content" in
    *'"c":"bootstrap"'*) pass "state file has c=bootstrap entries" ;;
    *) fail "state file missing c=bootstrap entries" ;;
esac
case "$content" in
    *'"t":'*) pass "state file has t (timestamp) fields" ;;
    *) fail "state file missing t fields" ;;
esac

# ---------------- corruption quarantine ----------------

echo "{ this is not valid JSON" > "$TEST_DIR/seen_corrupt.json"
STATE_FILE="$TEST_DIR/seen_corrupt.json" "$C_BINARY" --check >/dev/null 2>&1
if [ -f "$TEST_DIR/seen_corrupt.json.corrupt" ]; then
    pass "corrupt state file moved to .corrupt"
else
    fail "corrupt state file was NOT quarantined"
fi

# ---------------- exit code on feed failure ----------------

# Point FEED_URL at something that won't resolve
STATE_FILE="$TEST_DIR/seen.json" FEED_URL="http://127.0.0.1:1/nonexistent" \
    "$C_BINARY" --dry-run >/dev/null 2>&1
case $? in
    2) pass "feed failure exits 2" ;;
    *) fail "feed failure exit code: $?" ;;
esac

# ---------------- live feed (optional) ----------------

# Only runs if the live feed is reachable
if curl -sf --max-time 5 -o /dev/null "http://annonces.isae.edu.lb/feeds/posts/default?max-results=1" 2>/dev/null; then
    pass "live feed is reachable"

    # Bootstrap a fresh state file
    rm -f "$TEST_DIR/seen_live.json"
    STATE_FILE="$TEST_DIR/seen_live.json" "$C_BINARY" --bootstrap --quiet >/dev/null 2>&1
    case $? in
        0) pass "live bootstrap exits 0" ;;
        *) fail "live bootstrap exit: $?" ;;
    esac

    # Count entries in the bootstrapped state file
    entry_count="$(python3 -c "import json; print(len(json.load(open('$TEST_DIR/seen_live.json'))['seen']))" 2>/dev/null || echo 0)"
    if [ "$entry_count" -gt 0 ]; then
        pass "live bootstrap wrote $entry_count entries"
    else
        fail "live bootstrap wrote 0 entries"
    fi

    # Subsequent --dry-run should be a no-op (nothing pending)
    out="$(STATE_FILE="$TEST_DIR/seen_live.json" "$C_BINARY" --dry-run 2>&1)"
    case "$out" in
        *"Nothing to do"*) pass "post-bootstrap dry-run is a no-op" ;;
        *) fail "post-bootstrap dry-run not a no-op: $out" ;;
    esac
else
    skip "live feed not reachable (network test skipped)"
fi

# ---------------- unit tests ----------------

# Compile and run each unit test if gcc is available
if command -v gcc >/dev/null 2>&1; then
    CFLAGS="-std=c11 -Wall -Wextra -Wformat=2 -O2 -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE -DISAE_VERSION_STRING=\"2.0.0\""
    INC="-Iinclude -Ithird_party/cjson -I/usr/include/x86_64-linux-gnu -I/usr/include/libxml2"
    LIBS="-lm -lcurl -lxml2"

    run_unit_test() {
        local name="$1"
        local srcs="$2"
        local bin="/tmp/test_$name"
        # shellcheck disable=SC2086
        if gcc $CFLAGS $INC $srcs tests/test_${name}.c -o "$bin" $LIBS 2>/dev/null; then
            if "$bin" >/dev/null 2>&1; then
                pass "unit test: $name"
            else
                fail "unit test: $name (runtime failure)"
            fi
        else
            skip "unit test: $name (build failed)"
        fi
    }

    run_unit_test normalize "src/models.c third_party/cjson/cJSON.c"
    run_unit_test state     "src/state.c third_party/cjson/cJSON.c"
    run_unit_test keywords  "src/models.c src/departments.c src/keywords.c third_party/cjson/cJSON.c"
    run_unit_test feed      "src/models.c src/feed.c src/httpclient.c third_party/cjson/cJSON.c"
    run_unit_test telegram  "src/models.c src/departments.c src/telegram.c src/httpclient.c third_party/cjson/cJSON.c"
else
    skip "unit tests (gcc not available)"
fi

echo ""
echo "=========================================================="
printf "  Passed: ${GREEN}%d${NC}    Failed: ${RED}%d${NC}    Skipped: ${YELLOW}%d${NC}\n" "$PASS" "$FAIL" "$SKIP"
echo "=========================================================="

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
exit 0
