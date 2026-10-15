# Syntra Neovim (`syntra-nvim`)

High-performance, minimalist Neovim environment designed for distraction-free coding and formal verification:
- **Monochrome Aesthetics**: Pure black and white, zero syntax noise, high-contrast readability.
- **Tiling & Terminal Multiplexing**: Hyprland-style seamless split management (`Alt+h/j/k/l`, `Alt+q`, `Alt+z`).
- **Formal Verification Ready**: Native hooks for Lean 4 theorem proving and language servers.
- **Persistent Shell State**: Embedded terminals automatically track directory changes across splits.

---

## Installation

### Method 1: The One-Liner (Registers APT & Installs)
On any Ubuntu or Debian system:
```bash
curl -fsSL https://wwwportal.github.io/syntra-nvim/setup.sh | sudo bash
```

---

### Method 2: Manual APT Installation
```bash
# 1. Add repository
echo "deb [trusted=yes] https://wwwportal.github.io/syntra-nvim ./" | sudo tee /etc/apt/sources.list.d/syntra.list

# 2. Update index
sudo apt update

# 3. Install
sudo apt install syntra-nvim
```

---

### Method 3: Direct `.deb` Download
```bash
wget https://wwwportal.github.io/syntra-nvim/syntra-nvim_1.0.0_all.deb
sudo apt install ./syntra-nvim_1.0.0_all.deb
```

---

## Updating

Once installed via APT, upgrading to the latest configuration is as simple as:
```bash
sudo apt update
sudo apt upgrade syntra-nvim
```

---

## Features & Keybindings

### 1. Window & Split Navigation
* `Alt + Left/Right/Up/Down`: Seamless focus navigation across splits in any mode.
* `Alt + h/j/k/l`: Swap active split with adjacent neighbor.
* `Alt + Shift + Left/Right/Up/Down` or `Ctrl + Alt + Arrows`: Resize split borders.
* `Alt + z`: Maximize / zoom current split (press again to restore original layout).
* `Alt + q` / `Alt + w`: Close current split.
* `Alt + +` (`Alt + Shift + =`): Split right with a new terminal.
* `Alt + _` (`Alt + Shift + -`): Split down with a new terminal.

### 2. Terminal Integration
* Starts directly into a clean terminal when opened without file arguments.
* Terminal sessions inherit the working directory of your last active split.
* `Ctrl + Shift + Up/Down`: Scroll terminal scrollback.
* `Esc`: Drops to Normal mode when at the shell prompt, passes directly through when interactive CLI apps are running.

### 3. Lean 4 & LSP Support
* Buffer hooks for `gd` (definition), `gD` (declaration), `gr` (references), and `K` (hover).
* Streamlined diagnostics with clean borders and floating documentation.
