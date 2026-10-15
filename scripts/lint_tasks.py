#!/usr/bin/env python3
"""
scripts/lint_tasks.py

Source-First Task Linter & Dependency DAG Validator for syntra-nvim.
Enforces the structured in-line TODO / FIXME contract across the codebase.

Schema:
    `-- TODO(<id> | tier:<A|B|C> [| blocks:[<id>,...]] [| depends_on:[<id>,...]]):`
    `-- Description: <text>`
    `-- Criterion: assert: ... | review: ...`
"""

import sys
import re
from pathlib import Path
from typing import Dict, List, Set, Any, Tuple

ROOT = Path(__file__).resolve().parent.parent

# Regex for the task declaration line: supports #, //, --
TASK_HEADER_RE = re.compile(
    r'^[ \t]*(?:#|//|--)\s*(TODO|FIXME)\(([a-zA-Z0-9_\-]+)\s*\|\s*tier:([ABC])'
    r'((?:\s*\|\s*(?:blocks|depends_on):\[[a-zA-Z0-9_\-,\s]*\])*)\):(?:\s*(.*))?$'
)

# Regex for untyped, lazy TODOs that violate the schema
LAZY_TODO_RE = re.compile(r'^[ \t]*(?:#|//|--)\s*(TODO|FIXME)(?!\([a-zA-Z0-9_\-]+\s*\|).*$', re.IGNORECASE)

CRITERION_RE = re.compile(r'^[ \t]*(?:#|//|--)\s*Criterion:\s*(assert:\s*.+|review:\s*.+)$')
DESCRIPTION_RE = re.compile(r'^[ \t]*(?:#|//|--)\s*Description:\s*(.+)$')

SCAN_EXTENSIONS = {'.lua', '.sh', '.bash', '.py'}
IGNORE_DIRS = {'.git', 'build', 'target', '__pycache__', '.venv', 'venv', 'vendor', '.lazy'}

def scan_tasks() -> Tuple[List[Dict[str, Any]], List[str]]:
    tasks = []
    errors = []
    
    for file_path in ROOT.rglob('*'):
        if file_path.is_dir() or file_path.suffix not in SCAN_EXTENSIONS:
            continue
        if any(part in IGNORE_DIRS for part in file_path.parts):
            continue
            
        try:
            with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
                lines = f.readlines()
        except Exception:
            continue
            
        i = 0
        while i < len(lines):
            line = lines[i]
            
            # Check for lazy TODOs
            if LAZY_TODO_RE.match(line):
                if not TASK_HEADER_RE.match(line):
                    errors.append(
                        f"{file_path.relative_to(ROOT)}:{i+1}: Unstructured task comment.\n"
                        f"  Found: {line.strip()}\n"
                        f"  Must match: -- TODO(<id> | tier:<A|B|C> [| blocks:[...]] [| depends_on:[...]]):"
                    )
                    i += 1
                    continue
                    
            m = TASK_HEADER_RE.match(line)
            if m:
                task_kind, task_id, tier, attrs_str, inline_desc = m.groups()
                
                blocks = []
                depends_on = []
                if attrs_str:
                    b_match = re.search(r'blocks:\[([a-zA-Z0-9_\-,\s]*)\]', attrs_str)
                    if b_match:
                        blocks = [b.strip() for b in b_match.group(1).split(',') if b.strip()]
                    d_match = re.search(r'depends_on:\[([a-zA-Z0-9_\-,\s]*)\]', attrs_str)
                    if d_match:
                        depends_on = [d.strip() for d in d_match.group(1).split(',') if d.strip()]
                
                # Lookahead for Description: and Criterion:
                desc = inline_desc.strip() if inline_desc and inline_desc.strip() else None
                criterion = None
                
                j = i + 1
                while j < len(lines) and j <= i + 5:
                    next_line = lines[j]
                    desc_m = DESCRIPTION_RE.match(next_line)
                    crit_m = CRITERION_RE.match(next_line)
                    if desc_m:
                        desc = desc_m.group(1).strip()
                    if crit_m:
                        criterion = crit_m.group(1).strip()
                    if desc and criterion:
                        break
                    if not re.match(r'^[ \t]*(?:#|//|--)', next_line):
                        break
                    j += 1
                    
                if not desc:
                    errors.append(f"{file_path.relative_to(ROOT)}:{i+1}: Task '{task_id}' missing 'Description: <summary>'")
                if not criterion:
                    errors.append(f"{file_path.relative_to(ROOT)}:{i+1}: Task '{task_id}' missing 'Criterion: assert: ...' or 'Criterion: review: ...'")
                    
                tasks.append({
                    "id": task_id,
                    "kind": task_kind,
                    "tier": tier,
                    "blocks": blocks,
                    "depends_on": depends_on,
                    "file": str(file_path.relative_to(ROOT)),
                    "line": i + 1,
                    "desc": desc,
                    "criterion": criterion
                })
            i += 1
            
    return tasks, errors

def validate_dag(tasks: List[Dict[str, Any]]) -> List[str]:
    errors = []
    task_ids = {t["id"]: t for t in tasks}
    
    seen = set()
    for t in tasks:
        tid = t["id"]
        if tid in seen:
            errors.append(f"Duplicate task ID '{tid}' found at {t['file']}:{t['line']}")
        seen.add(tid)
        
    for t in tasks:
        for b in t["blocks"]:
            if b not in task_ids:
                errors.append(f"{t['file']}:{t['line']}: Task '{t['id']}' blocks non-existent task '{b}'")
        for d in t["depends_on"]:
            if d not in task_ids:
                errors.append(f"{t['file']}:{t['line']}: Task '{t['id']}' depends on non-existent task '{d}'")
                
    adj = {t["id"]: set(t["blocks"]) for t in tasks}
    visited = {}
    
    def dfs(node, path):
        visited[node] = 1
        for nxt in adj.get(node, []):
            if nxt in visited and visited[nxt] == 1:
                errors.append(f"Cyclic dependency detected: {' -> '.join(path + [nxt])}")
            elif nxt not in visited or visited[nxt] == 0:
                dfs(nxt, path + [nxt])
        visited[node] = 2

    for tid in task_ids:
        if tid not in visited or visited[tid] == 0:
            dfs(tid, [tid])
            
    return errors

def check_staged_resolutions(active_tasks: List[Dict[str, Any]]) -> List[str]:
    """
    Anti-erasure check: Ensures a task cannot disappear silently in git.
    Modifying a task in-place (same ID) is allowed; deleting requires a resolution log.
    """
    import subprocess
    errors = []
    try:
        diff = subprocess.check_output(
            ["git", "diff", "--cached", "-U0"],
            cwd=str(ROOT),
            text=True,
            errors="ignore"
        )
    except Exception:
        return errors
        
    raw_deleted_todos = []
    for line in diff.splitlines():
        if line.startswith("-") and not line.startswith("---"):
            m = re.search(r'(?:TODO|FIXME)\(([a-zA-Z0-9_\-]+)\s*\|', line)
            if m:
                raw_deleted_todos.append(m.group(1))

    active_task_ids = {t["id"] for t in active_tasks}
    # Only consider tasks that no longer exist anywhere in the codebase
    deleted_todos = [tid for tid in raw_deleted_todos if tid not in active_task_ids]
                
    if deleted_todos:
        try:
            staged_files = subprocess.check_output(
                ["git", "diff", "--cached", "--name-only"],
                cwd=str(ROOT),
                text=True
            ).splitlines()
        except Exception:
            staged_files = []
            
        staged_log_files = [ROOT / f for f in staged_files if f.startswith("logs/")]
        staged_test_files = [ROOT / f for f in staged_files if f.startswith("tests/")]
        
        for tid in deleted_todos:
            if not staged_log_files and not staged_test_files:
                errors.append(
                    f"Anti-Erasure Violation: Task '{tid}' is being removed, but no test or log artifact in logs/ "
                    f"is staged to prove resolution. Revert the deletion or stage the proof log."
                )
                continue
                
            task_referenced = False
            for file_path in staged_log_files + staged_test_files:
                if not file_path.exists() or file_path.stat().st_size == 0:
                    continue
                try:
                    content = file_path.read_text(encoding="utf-8", errors="ignore")
                    if tid in content:
                        task_referenced = True
                        break
                except Exception:
                    continue
                    
            if not task_referenced:
                errors.append(
                    f"Anti-Erasure Violation: Task '{tid}' is being removed, but none of the staged log/test files "
                    f"mention task ID '{tid}'. The resolution log must explicitly reference '{tid}' and contain non-empty data."
                )
                
    return errors

def main():
    tasks, parse_errors = scan_tasks()
    dag_errors = validate_dag(tasks)
    resolution_errors = check_staged_resolutions(tasks)
    
    all_errors = parse_errors + dag_errors + resolution_errors
    
    if "--json" in sys.argv:
        import json
        print(json.dumps({"tasks": tasks, "errors": all_errors, "root": str(ROOT)}))
        sys.exit(0 if not all_errors else 1)

    print("=" * 70)
    print("SOURCE-FIRST TASK & INVARIANT LINTER (syntra-nvim)")
    print("=" * 70)
    
    if all_errors:
        print(f"\n[FAILED] Found {len(all_errors)} task specification errors:\n")
        for err in all_errors:
            print(f"  ✖ {err}")
        print("\nPlease fix these in code comments before committing.")
        sys.exit(1)
        
    print(f"\n[PASS] Verified {len(tasks)} in-line task contracts across codebase.")
    print("\nLive In-Tree Task DAG:")
    for tier in ['A', 'B', 'C']:
        tier_tasks = [t for t in tasks if t['tier'] == tier]
        if tier_tasks:
            print(f"\n--- Tier {tier} ---")
            for t in tier_tasks:
                blocks_info = f" -> blocks [{', '.join(t['blocks'])}]" if t['blocks'] else ""
                deps_info = f" (depends on: {', '.join(t['depends_on'])})" if t['depends_on'] else ""
                print(f"  ● {t['id']} ({t['file']}:{t['line']}){deps_info}{blocks_info}")
                print(f"    Desc: {t['desc']}")
                print(f"    Crit: {t['criterion']}")
                
    print("\n" + "=" * 70)

if __name__ == "__main__":
    main()
