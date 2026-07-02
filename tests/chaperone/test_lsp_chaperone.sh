#!/bin/bash
# Drives the Angara LSP server with a minimal JSON-RPC sequence to verify the
# Chaperone runs in the editor and publishes memory-safety diagnostics.
#
# Sends: initialize → initialized → textDocument/didOpen (with a leaky file)
# Captures: the textDocument/publishDiagnostics notification.
#
# Usage: ./test_lsp_chaperone.sh [compiler_path]

set -uo pipefail

ANGC="${1:-$(cd "$(dirname "$0")/../.." && pwd)/build/angc}"

if [ ! -x "$ANGC" ]; then
    echo "FAIL: compiler not found at $ANGC"
    exit 1
fi

# A leaky file: Buf allocated, never dropped → E501.
LEAKY='class Buf {
    let v as i64;
    func init(this, x as i64) { this.v = x; }
}
export func main() -> i64 {
    let b = Buf(42);
    return 0;
}'

# A clean file: allocated, dropped before exit → no E5xx.
CLEAN='class Buf {
    let v as i64;
    func init(this, x as i64) { this.v = x; }
}
export func main() -> i64 {
    let b = Buf(42);
    drop b;
    return 0;
}'

# Build a JSON-RPC message with Content-Length framing.
msg() {
    local body="$1"
    printf 'Content-Length: %d\r\n\r\n%s' "${#body}" "$body"
}

ID=0
next_id() { ID=$((ID + 1)); echo "$ID"; }

# --- initialize ---
INIT_ID=$(next_id)
INIT=$(cat <<EOF
{"jsonrpc":"2.0","id":$INIT_ID,"method":"initialize","params":{"capabilities":{},"processId":$$,"rootUri":null}}
EOF
)

# --- initialized (notification) ---
INITIALIZED='{"jsonrpc":"2.0","method":"notifications/initialized","params":{}}'

# Run the LSP, feed it the sequence, capture all output. Send EOF after.
run_lsp() {
    local source="$1"
    local doc_id=$(next_id)
    # didOpen with the given source
    local didopen=$(cat <<EOF
{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///test.an","languageId":"angara","version":1,"text":$(python3 -c 'import json,sys; print(json.dumps(sys.argv[1]))' "$source")}}}
EOF
)
    {
        msg "$INIT"
        msg "$INITIALIZED"
        msg "$didopen"
        sleep 0.4   # let the server analyze + publish
    } | timeout 10 "$ANGC" lsp 2>/dev/null
}

echo "════════════════════════════════════════════════════════"
echo "  LSP Chaperone integration test"
echo "════════════════════════════════════════════════════════"
echo ""

PASS=0
FAIL=0

# --- Test 1: leaky file should publish E501 ---
echo -n "  leaky file (expect E501 published): "
OUT1=$(run_lsp "$LEAKY")
if echo "$OUT1" | /bin/grep -q '"E501"'; then
    echo "PASS"
    PASS=$((PASS + 1))
else
    echo "FAIL (no E501 in published diagnostics)"
    echo "$OUT1" | /bin/grep -o '"method":"[^"]*"' | sort -u | sed 's/^/        /'
    FAIL=$((FAIL + 1))
fi

# --- Test 2: clean file should publish no E5xx ---
echo -n "  clean file (expect no E5xx): "
OUT2=$(run_lsp "$CLEAN")
if echo "$OUT2" | /bin/grep -qE '"E50[0-9]"'; then
    echo "FAIL (Chaperone false-positive in editor)"
    echo "$OUT2" | /bin/grep -oE '"E50[0-9]"' | sort -u | sed 's/^/        /'
    FAIL=$((FAIL + 1))
else
    echo "PASS"
    PASS=$((PASS + 1))
fi

echo ""
echo "════════════════════════════════════════════════════════"
echo "  Results: $PASS passed, $FAIL failed"
echo "════════════════════════════════════════════════════════"
exit $FAIL
