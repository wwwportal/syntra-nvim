#!/usr/bin/env python3
"""
scripts/bundle_ir.py

Zero-dependency semantic AST extractor, contract skeletonizer, and dependency bundler
for the lgcc (Logical Compiler) reasoning loop and prime-router context allocator.

Capabilities:
  --extract <file>     : Returns structured JSON symbol definitions, signatures, and import edges.
  --skeletonize <file> : Strips implementation bodies to stubs while preserving type signatures
                         and docstring invariants (>70% semantic token reduction).
  --bundle <file>      : Discovers local repository dependencies of target file, skeletonizes them,
                         and outputs a hermetic multi-module IR context bundle.
  --metrics <file>     : Measures and outputs token & character reduction metrics.
"""

import sys
import os
import re
import ast
import json
import argparse
import subprocess
from pathlib import Path
from typing import Dict, List, Any, Optional, Tuple


def estimate_tokens(text: str) -> int:
    """Rough estimation of BPE tokens (~4 characters per token for source code)."""
    return max(1, len(text) // 4)


# ==============================================================================
# Python AST Extractor & Skeletonizer
# ==============================================================================

# TODO(ir-ast-extractor | tier:A | blocks:[ir-contract-skeletonizer]): Extract symbols, declarations, and import edges from Python and Lua ASTs.
# Description: Implement AST traversal using Python ast and Lua Tree-sitter to parse source files into structured declaration lists and import edges.
# Criterion: review: "scripts/bundle_ir.py --extract <file> returns JSON with symbols, signatures, and import edges"

class PyCallCollector(ast.NodeVisitor):
    def __init__(self):
        self.calls = set()

    def visit_Call(self, node):
        if isinstance(node.func, ast.Name):
            self.calls.add(node.func.id)
        elif isinstance(node.func, ast.Attribute):
            self.calls.add(node.func.attr)
        self.generic_visit(node)


def extract_python(filepath: Path) -> Dict[str, Any]:
    with open(filepath, "r", encoding="utf-8") as f:
        src = f.read()

    tree = ast.parse(src, filename=str(filepath))

    imports = []
    symbols = []

    for node in tree.body:
        if isinstance(node, ast.Import):
            for alias in node.names:
                imports.append({
                    "module": alias.name,
                    "names": [alias.name],
                    "alias": alias.asname,
                    "level": 0,
                    "line": node.lineno,
                })
        elif isinstance(node, ast.ImportFrom):
            mod = node.module or ""
            names = [alias.name for alias in node.names]
            imports.append({
                "module": mod,
                "names": names,
                "level": node.level,
                "line": node.lineno,
            })
        elif isinstance(node, ast.ClassDef):
            doc = ast.get_docstring(node)
            bases = [ast.unparse(b) for b in node.bases]
            methods = []
            for item in node.body:
                if isinstance(item, (ast.FunctionDef, ast.AsyncFunctionDef)):
                    m_doc = ast.get_docstring(item)
                    m_calls = PyCallCollector()
                    m_calls.visit(item)
                    ret = ast.unparse(item.returns) if item.returns else None
                    args = [a.arg + (f": {ast.unparse(a.annotation)}" if a.annotation else "") for a in item.args.args]
                    methods.append({
                        "name": item.name,
                        "kind": "method",
                        "line": item.lineno,
                        "end_line": getattr(item, "end_lineno", item.lineno),
                        "signature": f"def {item.name}({', '.join(args)})" + (f" -> {ret}" if ret else "") + ":",
                        "docstring": m_doc,
                        "calls": sorted(list(m_calls.calls)),
                        "params": args,
                        "returns": ret,
                    })

            symbols.append({
                "name": node.name,
                "kind": "class",
                "line": node.lineno,
                "end_line": getattr(node, "end_lineno", node.lineno),
                "bases": bases,
                "docstring": doc,
                "methods": methods,
            })
        elif isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            doc = ast.get_docstring(node)
            calls = PyCallCollector()
            calls.visit(node)
            ret = ast.unparse(node.returns) if node.returns else None
            args = [a.arg + (f": {ast.unparse(a.annotation)}" if a.annotation else "") for a in node.args.args]
            is_async = isinstance(node, ast.AsyncFunctionDef)
            prefix = "async def " if is_async else "def "
            sig = f"{prefix}{node.name}({', '.join(args)})" + (f" -> {ret}" if ret else "") + ":"

            symbols.append({
                "name": node.name,
                "kind": "function",
                "line": node.lineno,
                "end_line": getattr(node, "end_lineno", node.lineno),
                "signature": sig,
                "docstring": doc,
                "calls": sorted(list(calls.calls)),
                "params": args,
                "returns": ret,
            })

    return {
        "file": str(filepath),
        "language": "python",
        "imports": imports,
        "symbols": symbols,
    }


# TODO(ir-contract-skeletonizer | tier:A | depends_on:[ir-ast-extractor] | blocks:[lgcc-dep-bundle]): Skeletonize implementation bodies while preserving signatures and invariants.
# Description: Replace function and method bodies with stubs while retaining complete type annotations, docstring invariants, and interface definitions.
# Criterion: review: "Skeletonizer achieves >70% character and token reduction on target modules while preserving all exported signatures"

class PySkeletonizer(ast.NodeTransformer):
    def visit_FunctionDef(self, node: ast.FunctionDef) -> ast.AST:
        doc = ast.get_docstring(node)
        new_body: List[ast.stmt] = []
        if doc:
            new_body.append(ast.Expr(value=ast.Constant(value=doc)))
        new_body.append(ast.Expr(value=ast.Constant(value=Ellipsis)))
        node.body = new_body
        return node

    def visit_AsyncFunctionDef(self, node: ast.AsyncFunctionDef) -> ast.AST:
        doc = ast.get_docstring(node)
        new_body: List[ast.stmt] = []
        if doc:
            new_body.append(ast.Expr(value=ast.Constant(value=doc)))
        new_body.append(ast.Expr(value=ast.Constant(value=Ellipsis)))
        node.body = new_body
        return node

    def visit_If(self, node: ast.If) -> Optional[ast.AST]:
        # Strip `if __name__ == "__main__":` blocks completely from skeleton
        test_str = ast.unparse(node.test)
        if "__name__" in test_str and "__main__" in test_str:
            return None
        return self.generic_visit(node)


def skeletonize_python(filepath: Path) -> str:
    with open(filepath, "r", encoding="utf-8") as f:
        src = f.read()

    tree = ast.parse(src, filename=str(filepath))
    skel_tree = PySkeletonizer().visit(tree)
    ast.fix_missing_locations(skel_tree)
    return ast.unparse(skel_tree)


# ==============================================================================
# Lua Tree-sitter Extractor & Skeletonizer
# ==============================================================================

def extract_lua(filepath: Path) -> Dict[str, Any]:
    filepath = filepath.resolve()
    with open(filepath, "r", encoding="utf-8") as f:
        raw_content = f.read()

    lua_code = f"""
local f = io.open("{filepath}", "r")
if not f then return end
local content = f:read("*a")
f:close()

local parser = vim.treesitter.get_string_parser(content, "lua")
local root = parser:parse()[1]:root()

local imports = {{}}
local import_query = vim.treesitter.query.parse("lua", [[
  (function_call
    name: (identifier) @call.name
    arguments: (arguments (string) @import.module)
    (#eq? @call.name "require"))
]])
for id, node in import_query:iter_captures(root, content) do
  if import_query.captures[id] == "import.module" then
    local raw = vim.treesitter.get_node_text(node, content)
    local mod = raw:sub(2, -2)
    local srow, _, _, _ = node:range()
    table.insert(imports, {{ module = mod, line = srow + 1 }})
  end
end

local symbols = {{}}
for child in root:iter_children() do
  if child:type() == "function_declaration" then
    local srow, scol, erow, ecol = child:range()
    local name_node = child:field("name")[1]
    local param_node = child:field("parameters")[1]
    local name_text = name_node and vim.treesitter.get_node_text(name_node, content) or "anonymous"
    local param_text = param_node and vim.treesitter.get_node_text(param_node, content) or "()"
    table.insert(symbols, {{
      name = name_text,
      kind = "function",
      line = srow + 1,
      end_line = erow + 1,
      signature = "function " .. name_text .. param_text,
      parameters = param_text,
      is_top = true
    }})
  end
end

local out = {{
  file = "{filepath}",
  language = "lua",
  imports = imports,
  symbols = symbols
}}
io.stdout:write(vim.fn.json_encode(out))
"""
    try:
        res = subprocess.run(["nvim", "-l", "-"], input=lua_code, capture_output=True, text=True, check=True)
        return json.loads(res.stdout)
    except Exception:
        return extract_lua_fallback(filepath, raw_content)


def extract_lua_fallback(filepath: Path, content: str) -> Dict[str, Any]:
    lines = content.splitlines()
    imports = []
    symbols = []

    req_pattern = re.compile(r"""(?:require\s*\(?\s*['"]([^'"]+)['"]\s*\)?)""")
    fn_pattern = re.compile(r"""^\s*(local\s+function\s+([a-zA-Z0-9_]+)\s*\((.*?)\)|function\s+([a-zA-Z0-9_\.:]+)\s*\((.*?)\)|(?:local\s+)?([a-zA-Z0-9_\.:]+)\s*=\s*function\s*\((.*?)\))""")

    for idx, line in enumerate(lines):
        line_num = idx + 1
        m_req = req_pattern.search(line)
        if m_req:
            imports.append({"module": m_req.group(1), "line": line_num})

        m_fn = fn_pattern.match(line)
        if m_fn:
            name = m_fn.group(2) or m_fn.group(4) or m_fn.group(6)
            sig = line.strip()
            symbols.append({
                "name": name,
                "kind": "function",
                "line": line_num,
                "end_line": line_num,
                "signature": sig,
                "is_top": True,
            })

    return {
        "file": str(filepath),
        "language": "lua",
        "imports": imports,
        "symbols": symbols,
    }


def skeletonize_lua(filepath: Path) -> str:
    extracted = extract_lua(filepath)
    with open(filepath, "r", encoding="utf-8") as f:
        lines = f.readlines()

    symbols = [s for s in extracted.get("symbols", []) if s.get("is_top", False)]
    symbols = sorted(symbols, key=lambda s: s["line"], reverse=True)

    for sym in symbols:
        start_idx = sym["line"] - 1
        end_idx = sym["end_line"] - 1
        if start_idx < len(lines) and end_idx < len(lines) and start_idx <= end_idx:
            sig = sym["signature"]
            indent = " " * (len(lines[start_idx]) - len(lines[start_idx].lstrip()))
            if sym.get("kind") == "command":
                stub = f"{indent}-- {sig}\n"
            else:
                stub = f"{indent}{sig}\n{indent}  -- ... [stub]\n{indent}end\n"
            lines[start_idx:end_idx + 1] = [stub]

    cleaned = []
    consecutive_empty = 0
    for l in lines:
        if not l.strip():
            consecutive_empty += 1
            if consecutive_empty <= 2:
                cleaned.append(l)
        else:
            consecutive_empty = 0
            cleaned.append(l)

    return "".join(cleaned)


# ==============================================================================
# Unified Interface
# ==============================================================================

def extract_file(filepath: Path) -> Dict[str, Any]:
    ext = filepath.suffix.lower()
    if ext == ".py":
        return extract_python(filepath)
    elif ext == ".lua":
        return extract_lua(filepath)
    else:
        # Generic line fallback
        with open(filepath, "r", encoding="utf-8") as f:
            lines = f.readlines()
        return {
            "file": str(filepath),
            "language": "generic",
            "imports": [],
            "symbols": [],
            "lines": len(lines),
        }


def skeletonize_file(filepath: Path) -> str:
    ext = filepath.suffix.lower()
    if ext == ".py":
        return skeletonize_python(filepath)
    elif ext == ".lua":
        return skeletonize_lua(filepath)
    else:
        with open(filepath, "r", encoding="utf-8") as f:
            return f.read()


def locate_local_dependency(mod_name: str, base_dir: Path) -> Optional[Path]:
    """Resolve imported module name to a local repo file."""
    # Direct relative or slash path
    direct_candidates = [
        base_dir / f"{mod_name}.py",
        base_dir / f"{mod_name}.lua",
        base_dir / mod_name / "__init__.py",
        base_dir / mod_name / "init.lua",
    ]
    for c in direct_candidates:
        if c.is_file():
            return c.resolve()

    # Search in common repo subdirectories
    subdirs = ["nvim/lua", "lua", "src", "scripts", "lib"]
    repo_root = base_dir
    while repo_root.parent != repo_root:
        if (repo_root / ".git").is_dir():
            break
        repo_root = repo_root.parent

    for sub in subdirs:
        mod_rel = mod_name.replace(".", "/")
        cand1 = repo_root / sub / f"{mod_rel}.lua"
        cand2 = repo_root / sub / f"{mod_rel}.py"
        cand3 = repo_root / sub / mod_rel / "init.lua"
        cand4 = repo_root / sub / mod_rel / "__init__.py"
        for cand in [cand1, cand2, cand3, cand4]:
            if cand.is_file():
                return cand.resolve()

    return None


# TODO(lgcc-dep-bundle | tier:A | depends_on:[ir-contract-skeletonizer] | blocks:[lgcc-task-synthesizer, graph-repo-scanner]): Compile hermetic dependency context and type signatures into intermediate representation before model invocation.
# Description: Preprocess target files by bundling imported interfaces, external function stubs, and type definitions instead of raw binary streams or isolated files.
# Criterion: review: "lgcc CLI accepts --bundle flag that aggregates local imported module stubs without raw binary degradation"

def bundle_dependencies(target_path: Path) -> str:
    """Preprocess target file and its local dependencies into a hermetic bundle."""
    target_path = target_path.resolve()
    target_dir = target_path.parent
    extracted = extract_file(target_path)

    # Read target with line numbers
    with open(target_path, "r", encoding="utf-8") as f:
        target_lines = f.readlines()
    numbered_target = [f"{i+1}: {line}" for i, line in enumerate(target_lines)]

    # Discover and skeletonize local dependencies
    dep_skeletons: Dict[str, str] = {}
    for imp in extracted.get("imports", []):
        mod = imp["module"]
        dep_path = locate_local_dependency(mod, target_dir)
        if dep_path and dep_path != target_path and str(dep_path) not in dep_skeletons:
            try:
                skel = skeletonize_file(dep_path)
                dep_skeletons[str(dep_path)] = skel
            except Exception as e:
                dep_skeletons[str(dep_path)] = f"-- [error skeletonizing {dep_path}: {e}]"

    bundle = []
    bundle.append("==============================================================================")
    bundle.append(f"TARGET FILE: {target_path}")
    bundle.append("==============================================================================")
    bundle.append("".join(numbered_target))
    bundle.append("")

    if dep_skeletons:
        bundle.append("==============================================================================")
        bundle.append(f"DEPENDENCY SKELETONS & INTERFACE CONTRACTS ({len(dep_skeletons)} local module(s))")
        bundle.append("==============================================================================")
        for dep_file, skel in sorted(dep_skeletons.items()):
            bundle.append(f"\n--- INTERFACE CONTRACT: {dep_file} ---")
            bundle.append(skel.strip())
            bundle.append("--- END CONTRACT ---\n")

    return "\n".join(bundle)


# ==============================================================================
# Repository Symbol Graph & Context Router Pipeline
# ==============================================================================

# TODO(graph-repo-scanner | tier:A | depends_on:[lgcc-dep-bundle] | blocks:[graph-symbol-table]): Discover and index repository source files respecting .gitignore.
# Description: Implement repository-wide source file scanner discovering all Python and Lua files while filtering test/build/vendor noise.
# Criterion: review: "scripts/bundle_ir.py --scan-repo returns complete list of tracked source files"

# TODO(graph-symbol-table | tier:B | depends_on:[graph-repo-scanner] | blocks:[graph-edge-extractor]): Construct global symbol table mapping fully-qualified names to signatures and locations.
# Description: Build repository symbol table V mapping module.Class.method and module.func FQNs to file locations, line ranges, and signatures.
# Criterion: review: "Symbol table indexes all top-level classes, methods, and functions with unique FQNs"

# TODO(graph-edge-extractor | tier:B | depends_on:[graph-symbol-table] | blocks:[ir-graph-builder]): Extract cross-file call-graph and type-reference dependency edges.
# Description: Resolve AST call sites and type annotations against the global symbol table to generate directed dependency edges E.
# Criterion: review: "Extracted graph contains verified caller->callee edges across different modules"

# TODO(ir-graph-builder | tier:B | depends_on:[graph-edge-extractor] | blocks:[router-distance-decay]): Construct global multi-file repository symbol dependency graph.
# Description: Assemble and serialize global repository symbol DAG G=(V, E) into compact adjacency representation.
# Criterion: review: "scripts/bundle_ir.py --graph returns full repository symbol DAG with verified node and edge counts"

# TODO(router-distance-decay | tier:B | depends_on:[ir-graph-builder] | blocks:[router-personalized-pagerank]): Compute directed shortest-path distance decay weights from target symbols.
# Description: Calculate directed graph distances d(u, v) from target symbols and apply exponential/harmonic distance decay weighting.
# Criterion: review: "Distance decay assigns strictly decreasing weights to k-hop neighborhood nodes"

# TODO(router-personalized-pagerank | tier:B | depends_on:[router-distance-decay] | blocks:[router-topological-pruner]): Compute Personalized PageRank centrality rooted at target symbols.
# Description: Run power-iteration Personalized PageRank with random walk restart rooted at target symbols to score structural centrality.
# Criterion: review: "PPR converges in <=30 iterations and ranks shared utility and interface hubs higher than leaf stubs"

# TODO(router-topological-pruner | tier:B | depends_on:[router-personalized-pagerank] | blocks:[router-knapsack-solver]): Compute composite importance weighting on symbol graph.
# Description: Synthesize composite node importance scores S(v|q) combining PPR centrality and distance decay, pruning zero-relevance subgraphs.
# Criterion: review: "Router ranks 1-hop and 2-hop dependent symbols higher than unrelated repository symbols"

# TODO(router-knapsack-solver | tier:B | depends_on:[router-topological-pruner] | blocks:[router-connectivity-guard]): Solve 0/1 knapsack optimization selecting symbols under token budget.
# Description: Formulate and solve knapsack optimization selecting symbol nodes to maximize total relevance score under active context token budget.
# Criterion: review: "Greedy/DP knapsack selector terminates in <50ms and satisfies sum(token_cost) <= budget"

# TODO(router-connectivity-guard | tier:B | depends_on:[router-knapsack-solver] | blocks:[router-budget-knapsack]): Enforce path connectivity invariant on selected symbol subgraph.
# Description: Ensure all selected symbols retain valid import/call paths to target symbols to prevent disconnected orphan hallucinations.
# Criterion: review: "Allocated context contains zero orphan symbols or unresolvable type references"

# TODO(router-budget-knapsack | tier:B | depends_on:[router-connectivity-guard] | blocks:[ir-schema-formatter]): Implement knapsack context allocator bounded by token budget.
# Description: Select optimal symbol subgraph maximizing information coverage score while staying strictly within active context token budget.
# Criterion: review: "Router output token count stays under specified budget while maximizing coverage score"

# TODO(ir-schema-formatter | tier:B | depends_on:[router-budget-knapsack] | blocks:[lgcc-cli-router-hook]): Emit unified compressed context IR schema for model prompt.
# Description: Format target file, budget-allocated symbol skeletons, and dependency topology into compact prompt intermediate representation.
# Criterion: review: "Formatted IR stream preserves all type contracts while achieving >70% compression"


def show_metrics(filepath: Path) -> None:
    with open(filepath, "r", encoding="utf-8") as f:
        orig = f.read()
    skel = skeletonize_file(filepath)

    orig_chars = len(orig)
    skel_chars = len(skel)
    char_reduc = (1.0 - (skel_chars / orig_chars)) * 100 if orig_chars else 0

    orig_tok = estimate_tokens(orig)
    skel_tok = estimate_tokens(skel)
    tok_reduc = (1.0 - (skel_tok / orig_tok)) * 100 if orig_tok else 0

    orig_lines = len(orig.splitlines())
    skel_lines = len(skel.splitlines())

    print(f"Metrics for {filepath}:")
    print(f"  Original:      {orig_chars:,} chars | {orig_tok:,} tokens | {orig_lines} lines")
    print(f"  Skeleton:      {skel_chars:,} chars | {skel_tok:,} tokens | {skel_lines} lines")
    print(f"  Reduction:     {char_reduc:.1f}% chars | {tok_reduc:.1f}% tokens")


def main():
    parser = argparse.ArgumentParser(
        description="AST symbol extractor, contract skeletonizer, and dependency bundler for lgcc."
    )
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--extract", type=str, metavar="FILE", help="Extract symbols and imports as JSON")
    group.add_argument("--skeletonize", type=str, metavar="FILE", help="Output stripped contract skeleton")
    group.add_argument("--bundle", type=str, metavar="FILE", help="Generate hermetic target + dependency bundle")
    group.add_argument("--metrics", type=str, metavar="FILE", help="Print token/size reduction metrics")

    args = parser.parse_args()

    if args.extract:
        p = Path(args.extract)
        if not p.is_file():
            print(f"Error: File not found: {p}", file=sys.stderr)
            sys.exit(1)
        res = extract_file(p)
        print(json.dumps(res, indent=2))

    elif args.skeletonize:
        p = Path(args.skeletonize)
        if not p.is_file():
            print(f"Error: File not found: {p}", file=sys.stderr)
            sys.exit(1)
        print(skeletonize_file(p))

    elif args.bundle:
        p = Path(args.bundle)
        if not p.is_file():
            print(f"Error: File not found: {p}", file=sys.stderr)
            sys.exit(1)
        print(bundle_dependencies(p))

    elif args.metrics:
        p = Path(args.metrics)
        if not p.is_file():
            print(f"Error: File not found: {p}", file=sys.stderr)
            sys.exit(1)
        show_metrics(p)


if __name__ == "__main__":
    main()
