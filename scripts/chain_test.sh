#!/usr/bin/env bash
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$REPO_DIR/build/src/dist_comms"
CFG="$REPO_DIR/config"

if [[ ! -x "$BIN" ]]; then
    echo "Binary not found at $BIN — run cmake --build build first" >&2
    exit 1
fi

UTURN_PID=""
PIPE_PID=""

cleanup() {
    [[ -n "$PIPE_PID"  ]] && kill "$PIPE_PID"  2>/dev/null || true
    [[ -n "$UTURN_PID" ]] && kill "$UTURN_PID" 2>/dev/null || true
    wait "$PIPE_PID" "$UTURN_PID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

echo "==> starting uturn"
"$BIN" "$CFG/chain_uturn.yaml" &
UTURN_PID=$!

echo "==> starting pipe"
"$BIN" "$CFG/chain_pipe.yaml" &
PIPE_PID=$!

# wait for both to bind before sending
sleep 0.3

echo "==> source: sending message through chain"
echo "    route: source(42010) -> pipe(42011) -> uturn(42013) -> pipe(42012) -> source(42010)"
"$BIN" "$CFG/chain_source.yaml" send

echo "==> chain test complete"
