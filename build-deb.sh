#!/usr/bin/env bash
# ==============================================================================
# build-deb.sh: Build syntra-nvim Debian package and regenerate APT repository
# ==============================================================================

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VERSION="1.0.0"
PKG_NAME="syntra-nvim"
STAGING_DIR="$REPO_ROOT/build/${PKG_NAME}_${VERSION}_all"
OUTPUT_DEB="$REPO_ROOT/${PKG_NAME}_${VERSION}_all.deb"

echo "==> Preparing build staging directory..."
rm -rf "$REPO_ROOT/build" "$OUTPUT_DEB"
mkdir -p "$STAGING_DIR/DEBIAN"
mkdir -p "$STAGING_DIR/usr/local/bin"
mkdir -p "$STAGING_DIR/usr/share/syntra-nvim/config/bin"
mkdir -p "$STAGING_DIR/etc/skel/.config/nvim/bin"

echo "==> Building native lgcc compiler..."
make all

echo "==> Bundling Neovim configuration & native lgcc compiler..."
cp -r "$REPO_ROOT/nvim/"* "$STAGING_DIR/usr/share/syntra-nvim/config/"
cp -r "$REPO_ROOT/nvim/"* "$STAGING_DIR/etc/skel/.config/nvim/"
if [ -x "$REPO_ROOT/bin/lgcc" ]; then
    cp "$REPO_ROOT/bin/lgcc" "$STAGING_DIR/usr/local/bin/lgcc"
    cp "$REPO_ROOT/bin/lgcc" "$STAGING_DIR/usr/share/syntra-nvim/config/bin/lgcc"
    cp "$REPO_ROOT/bin/lgcc" "$STAGING_DIR/etc/skel/.config/nvim/bin/lgcc"
    chmod 755 "$STAGING_DIR/usr/local/bin/lgcc" "$STAGING_DIR/usr/share/syntra-nvim/config/bin/lgcc" "$STAGING_DIR/etc/skel/.config/nvim/bin/lgcc"
fi

echo "==> Generating DEBIAN/control..."
cat << EOF > "$STAGING_DIR/DEBIAN/control"
Package: $PKG_NAME
Version: $VERSION
Section: editors
Priority: optional
Architecture: all
Depends: git, curl, ripgrep, tar, gzip, ca-certificates
Maintainer: Syntra <support@syntra.io>
Description: High-performance Neovim setup with monochrome aesthetics and tiling
 Provisions modern Neovim (v0.10+), native Rust lgcc Code Graph & Task Graph engine,
 and custom monochrome window and terminal tiling multiplexer.
EOF

echo "==> Generating DEBIAN/postinst..."
cat << 'EOF' > "$STAGING_DIR/DEBIAN/postinst"
#!/usr/bin/env bash
set -e

TARGET_USER="${SUDO_USER:-$(logname 2>/dev/null || echo "$USER")}"
TARGET_HOME="$(eval echo "~$TARGET_USER")"

echo "==> [syntra-nvim] Checking Neovim v0.10+ binary..."
NVIM_NEEDS_INSTALL=true
if command -v nvim &>/dev/null; then
    NVIM_VER="$(nvim --version | head -n1 | grep -oP 'v\K[0-9]+\.[0-9]+' || echo "0.0")"
    MAJOR="$(echo "$NVIM_VER" | cut -d. -f1)"
    MINOR="$(echo "$NVIM_VER" | cut -d. -f2)"
    if [ "$MAJOR" -ge 1 ] || [ "$MINOR" -ge 10 ]; then
        echo "==> [syntra-nvim] Compatible Neovim version detected: $(nvim --version | head -n1)"
        NVIM_NEEDS_INSTALL=false
    fi
fi

if [ "$NVIM_NEEDS_INSTALL" = true ]; then
    echo "==> [syntra-nvim] Downloading official Neovim release binary..."
    NVIM_TAR="/tmp/nvim-linux-x86_64.tar.gz"
    curl -sSfL "https://github.com/neovim/neovim/releases/latest/download/nvim-linux-x86_64.tar.gz" -o "$NVIM_TAR"
    mkdir -p /opt/nvim-linux-x86_64
    tar -xzf "$NVIM_TAR" -C /opt
    rm -f "$NVIM_TAR"
    ln -sf /opt/nvim-linux-x86_64/bin/nvim /usr/local/bin/nvim
    echo "==> [syntra-nvim] Neovim installed to /usr/local/bin/nvim ($(nvim --version | head -n1))"
fi

# Deploy configuration to target user
if [ -n "$TARGET_USER" ] && [ -d "$TARGET_HOME" ]; then
    echo "==> [syntra-nvim] Provisioning configuration for user $TARGET_USER..."
    USER_NVIM_DIR="$TARGET_HOME/.config/nvim"
    mkdir -p "$USER_NVIM_DIR"
    cp -r /usr/share/syntra-nvim/config/* "$USER_NVIM_DIR/"

    # Clone lazy.nvim if missing
    LAZY_DIR="$TARGET_HOME/.local/share/nvim/lazy/lazy.nvim"
    if [ ! -d "$LAZY_DIR" ]; then
        echo "==> [syntra-nvim] Bootstrapping lazy.nvim..."
        git clone --filter=blob:none https://github.com/folke/lazy.nvim.git --branch=stable "$LAZY_DIR" 2>/dev/null || true
    fi

    # Fix file ownership
    chown -R "$TARGET_USER:$TARGET_USER" "$USER_NVIM_DIR" 2>/dev/null || true
    chown -R "$TARGET_USER:$TARGET_USER" "$TARGET_HOME/.local/share/nvim" 2>/dev/null || true

fi

echo "==> [syntra-nvim] Installation complete! Run 'nvim' to start."
exit 0
EOF
chmod 755 "$STAGING_DIR/DEBIAN/postinst"

echo "==> Building Debian package..."
dpkg-deb --build "$STAGING_DIR" "$OUTPUT_DEB"
rm -rf "$REPO_ROOT/build"

echo "==> Regenerating APT repository metadata..."
"$REPO_ROOT/generate-repo.sh"

echo "==> Build complete! Output: $OUTPUT_DEB"
