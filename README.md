# Syntra Neovim (`syntra-nvim`)

High-performance, minimalist Neovim environment designed for distraction-free coding, **Code Graph** context routing for LLM static analysis, and **Task Graph** formal specification tracking:
- **Monochrome Aesthetics**: Pure black and white, zero syntax noise, high-contrast readability.
- **Tiling & Terminal Multiplexing**: Hyprland-style seamless split management (`Alt+h/j/k/l`, `Alt+q`, `Alt+z`).
- **Code Graph & Logic Compiler (`:Lgcc`)**: Zero-overhead C++20 AST/symbol graph builder and knapsack context router that bundles target source code with compressed dependency skeletons into a single IR stream for LLM reasoning.
- **Task Graph & Formal Specs (`:Tasks`)**: In-tree structured task/specification DAG (`TODO` and `SPEC` comments) bridging repository dependencies, Taskwarrior, and LexLean/PrismPM proof verification.
- **Persistent Shell State**: Embedded terminals automatically track directory changes across splits.

---

## Installation

On any fresh Ubuntu or Debian system, run:
```bash
curl -fsSL https://wwwportal.github.io/syntra-nvim/setup.sh | sudo bash
```

---

## Updating

Once installed, updating to the latest version is handled directly through standard APT:
```bash
sudo apt update && sudo apt upgrade syntra-nvim
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

### 3. Code Graph & Logic Compiler (`:Lgcc`)
* `:Lgcc`: Builds the repository **Code Graph** (`src/ast/`, `src/graph/`, `src/router/`) and feeds the target source code + routed dependency skeletons into an LLM to detect cross-file logical flaws.
* **Automated Model-Aware Budgeting**: Dynamically calculates and allocates token budgets based on the active LLM backend (`local` $\le$ 2k, `opencode` $\le$ 16k, `agy` $\le$ 32k).
* **C++20 Knapsack Router (`bin/lgcc --graph` / `--route`)**: Emits a single Code Graph IR bundle with >80% token compression by extracting interface skeletons and resolving multi-hop call/import edges.

### 4. Task Graph & Formal Specifications (`:Tasks`)
* `:Tasks`: Navigates and manages the in-tree **Task Graph** (`src/spec/`) built from structured `TODO(...)` and `SPEC(...)` comments (`depends_on` / `blocks`).
* **Anti-Erasure Linter (`bin/lgcc --lint-tasks`)**: Enforces topological DAG validity, criterion verification, and commit proof logs.
* **LexLean / PrismPM Proof Replay (`bin/lgcc --verify-specs` / `--emit-c-abi`)**: Stitches distributed `SPEC` blocks in topological order and verifies formal `[lexlean:declarations]` and `[lexlean:theorems]`.

### 5. Language Server & Diagnostics
* Buffer hooks for `gd` (definition), `gD` (declaration), `gr` (references), and `K` (hover).
* Streamlined diagnostics with clean borders and floating documentation.
