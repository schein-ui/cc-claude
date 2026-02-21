#!/bin/bash
# Install cc (Claude Conversations) to ~/.local/bin

set -e

INSTALL_DIR="${HOME}/.local/bin"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Create install directory if needed
mkdir -p "$INSTALL_DIR"

# Copy the script
cp "$SCRIPT_DIR/cc" "$INSTALL_DIR/cc"
chmod +x "$INSTALL_DIR/cc"

# Check if ~/.local/bin is on PATH
if ! echo "$PATH" | grep -q "$INSTALL_DIR"; then
    echo ""
    echo "Add this to your shell profile (~/.zshrc or ~/.bashrc):"
    echo ""
    echo "  export PATH=\"\$HOME/.local/bin:\$PATH\""
    echo ""
fi

echo "Installed cc to $INSTALL_DIR/cc"
echo "Run 'cc' to list your Claude Code sessions."
