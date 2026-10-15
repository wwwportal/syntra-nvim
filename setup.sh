#!/usr/bin/env bash
# ==============================================================================
# setup.sh: 1-Line Installer for Syntra Neovim
# Registers the Syntra APT repository and runs 'sudo apt install syntra-nvim'
#
# Usage:
#   curl -fsSL https://wwwportal.github.io/syntra-nvim/setup.sh | sudo bash
# ==============================================================================

set -euo pipefail

echo "=========================================================="
echo "          Syntra Neovim APT Repository Setup              "
echo "=========================================================="

REPO_URL="https://wwwportal.github.io/syntra-nvim"
LIST_FILE="/etc/apt/sources.list.d/syntra.list"

echo "==> [1/3] Adding Syntra repository to APT sources..."
echo "deb [trusted=yes] $REPO_URL ./" | tee "$LIST_FILE" > /dev/null

echo "==> [2/3] Updating package index..."
apt-get update -qq

echo "==> [3/3] Installing syntra-nvim via APT..."
apt-get install -y syntra-nvim

echo "=========================================================="
echo "  SUCCESS: Syntra Neovim is installed and ready!          "
echo "  Run 'nvim' to begin.                                    "
echo "=========================================================="
