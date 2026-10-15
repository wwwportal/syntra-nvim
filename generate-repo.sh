#!/usr/bin/env bash
# ==============================================================================
# generate-repo.sh: Generate Debian APT repository metadata (Packages & Release)
# ==============================================================================

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$REPO_ROOT"

echo "==> Generating Packages index..."
dpkg-scanpackages -m . /dev/null > Packages
gzip -k -f Packages

echo "==> Generating Release file..."
cat << EOF > Release
Origin: Syntra
Label: Syntra Neovim Repository
Suite: stable
Codename: stable
Architectures: all amd64 arm64
Components: main
Description: Syntra Neovim APT Repository
Date: $(date -Ru)
MD5Sum:
 $(md5sum Packages | awk '{print $1}') $(stat -c%s Packages) Packages
 $(md5sum Packages.gz | awk '{print $1}') $(stat -c%s Packages.gz) Packages.gz
SHA256:
 $(sha256sum Packages | awk '{print $1}') $(stat -c%s Packages) Packages
 $(sha256sum Packages.gz | awk '{print $1}') $(stat -c%s Packages.gz) Packages.gz
EOF

echo "==> APT repository metadata updated successfully."
