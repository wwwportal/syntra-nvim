# Syntra Neovim (`syntra-nvim`)

High-performance, minimalist Neovim environment with:
- **Monochrome Aesthetics**: Pure black and white, zero syntax distractions, light grey selections.
- **`lgcc` Logical Compiler**: Native `:Lgcc` async diagnostic engine powered by local LLM (`qwen2.5-coder-7b`) or OpenCode.
- **Tiling & Terminal Multiplexing**: Hyprland-style seamless split management (`Alt+h/j/k/l`, `Alt+q`, `Alt+z`).
- **Formal Verification Ready**: Native hooks for Lean 4 theorem proving and language servers.

---

## Installation

### Method 1: The One-Liner (Registers APT & Installs)
On any fresh Ubuntu or Debian machine:
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

## Features & Usage

### 1. Logical Error Compilation (`:Lgcc`)
* `:Lgcc`: Runs async logical error analysis on the active buffer.
* `:Lgcc spec <file>`: Analyzes logic against a formal specification document.
* `:Lgcc web`: Runs logic analysis with OpenCode web search.
* `:Lgcc clear`: Clears all inline diagnostic markers.

### 2. Window & Split Navigation
* `Alt + Left/Right/Up/Down`: Seamless focus navigation across splits.
* `Alt + h/j/k/l`: Swap active split with neighbor.
* `Alt + Shift + Left/Right/Up/Down`: Resize split borders.
* `Alt + z`: Maximize / zoom current split (press again to restore).
* `Alt + q` / `Alt + w`: Close current split.
