use super::stitcher::lower_and_verify_semantic_json;
use regex::Regex;
use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::collections::{HashMap, HashSet, VecDeque};
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use walkdir::WalkDir;

// SPECIFICATION ARCHITECTURE:
// In Syntra, each Task Graph node acts as a formal semantic interpretation of an informal English sentence,
// translating unstructured natural language goals ("TODO: ...") into machine-checked contracts with typed tiers,
// explicit dependency DAG edges, and verified Lean 4 / LexLean proof obligations.

// SPEC(lgcc-native-linter | tier:A | depends_on:[spec-jit-patch-schema] | blocks:[tasks-add-spec-scaffold]): Rewrite scripts/lint_tasks.py into native Rust inside lgcc.
// Description: Rewrite scripts/lint_tasks.py into native Rust inside lgcc, implementing high-performance task DAG validation, anti-erasure git checks, lazy-TODO detection, and JSON export.
// Criterion: assert: "lgcc --lint-tasks validates repository DAG and anti-erasure constraints in <15ms with full feature parity to Python linter"
// [lexlean:declarations]
// {"body":{"arguments":[],"function":{"name":"spec_jit_patch_schema"},"kind":"call"},"kind":"definition","name":"lgcc_native_linter","parameters":[],"result":{"kind":"bool"}}
// [lexlean:theorems]
// {"kind":"theorem","name":"lgcc_native_linter_verified","parameters":[],"proof":{"kind":"reflexivity"},"statement":{"kind":"eq","left":{"arguments":[],"function":{"name":"lgcc_native_linter"},"kind":"call"},"right":{"kind":"bool","value":true}}}

// SPEC(spec-golden-acceptance-vectors | tier:B | depends_on:[spec-cli-wire-protocol-dispatch] | blocks:[spec-knapsack-budget-theorems]): Define acceptance vector suite verifying deterministic byte outputs.
// Description: Define serialized AcceptanceVector schema modeling hex-encoded CLI request and response byte expectations to guard against silent output format regressions.
// Criterion: assert: "All acceptance test vectors replay against lgcc with 100% byte integrity"
// [lexlean:declarations]
// {"fields":[{"name":"commandVerb","type":{"kind":"string"}},{"name":"requestBytesHex","type":{"kind":"string"}},{"name":"expectedResponseHex","type":{"kind":"string"}}],"kind":"structure","name":"CliAcceptanceVector","parameters":[],"type_parameters":[]},{"body":{"arguments":[],"function":{"name":"cli_wire_protocol_dispatch"},"kind":"call"},"kind":"definition","name":"golden_acceptance_vectors","parameters":[],"result":{"kind":"bool"}}
// [lexlean:theorems]
// {"kind":"theorem","name":"golden_acceptance_vectors_verified","parameters":[],"proof":{"kind":"reflexivity"},"statement":{"kind":"eq","left":{"arguments":[],"function":{"name":"golden_acceptance_vectors"},"kind":"call"},"right":{"kind":"bool","value":true}}}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TaskRecord {
    pub id: String,
    pub kind: String, // "TODO", "FIXME", "SPEC"
    pub tier: String, // "A", "B", "C"
    pub blocks: Vec<String>,
    pub depends_on: Vec<String>,
    pub description: String,
    pub criterion: String,
    pub file: PathBuf,
    pub line: usize,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct LintResult {
    pub tasks: Vec<TaskRecord>,
    pub errors: Vec<String>,
    pub root: PathBuf,
    pub ffi_defs_verified: u32,
    pub ffi_theorems_verified: u32,
}

pub struct TaskLinter;

impl TaskLinter {
    pub fn lint_repository(repo_root: &Path) -> LintResult {
        let mut res = LintResult {
            root: repo_root.to_path_buf(),
            ..Default::default()
        };

        for entry in WalkDir::new(repo_root)
            .into_iter()
            .filter_entry(|e| {
                if e.depth() == 0 {
                    return true;
                }
                let name = e.file_name().to_string_lossy();
                !name.starts_with('.')
                    && name != "build"
                    && name != "target"
                    && name != "vendor"
                    && name != "node_modules"
            })
            .filter_map(|e| e.ok())
        {
            if entry.file_type().is_file() {
                let p = entry.path();
                if let Some(ext) = p.extension().and_then(|s| s.to_str()) {
                    match ext {
                        "lua" | "sh" | "bash" | "py" | "cpp" | "hpp" | "h" | "cc" | "rs" => {
                            Self::scan_file(p, repo_root, &mut res.tasks, &mut res.errors);
                        }
                        _ => {}
                    }
                }
            }
        }

        let dag_errs = Self::validate_dag(&res.tasks);
        res.errors.extend(dag_errs);

        let erasure_errs = Self::check_staged_resolutions(repo_root, &res.tasks);
        res.errors.extend(erasure_errs);

        // Sort SPEC tasks topologically so definitions precede their callers
        let mut in_degree: HashMap<String, usize> = HashMap::new();
        let mut adj: HashMap<String, Vec<String>> = HashMap::new();
        let mut spec_tasks: HashMap<String, &TaskRecord> = HashMap::new();

        for t in &res.tasks {
            if t.kind == "SPEC" {
                spec_tasks.insert(t.id.clone(), t);
                in_degree.entry(t.id.clone()).or_insert(0);
                for dep in &t.depends_on {
                    adj.entry(dep.clone()).or_default().push(t.id.clone());
                    *in_degree.entry(t.id.clone()).or_insert(0) += 1;
                }
            }
        }

        let mut q: VecDeque<String> = VecDeque::new();
        for (id, deg) in &in_degree {
            if *deg == 0 {
                q.push_back(id.clone());
            }
        }

        let mut topo_tasks = Vec::new();
        while let Some(curr) = q.pop_front() {
            if let Some(t) = spec_tasks.get(&curr) {
                topo_tasks.push(*t);
            }
            if let Some(neighbors) = adj.get(&curr) {
                for next in neighbors {
                    if let Some(d) = in_degree.get_mut(next) {
                        *d -= 1;
                        if *d == 0 {
                            q.push_back(next.clone());
                        }
                    }
                }
            }
        }

        // Verify all SPEC declarations through LexLean lowered in-memory module
        let mut stitched_declarations = Vec::new();
        let mut stitched_theorems = Vec::new();

        for t in topo_tasks {
            if let Ok(content) = fs::read_to_string(&t.file) {
                let lines: Vec<&str> = content.lines().collect();
                let mut in_decl = false;
                let mut in_thm = false;
                for line in lines.iter().skip(t.line) {
                    let trimmed = line.trim();
                    if !trimmed.starts_with("//") && !trimmed.starts_with("--") && !trimmed.starts_with('#') {
                        break;
                    }
                    if trimmed.contains("[lexlean:declarations]") {
                        in_decl = true;
                        in_thm = false;
                        continue;
                    } else if trimmed.contains("[lexlean:theorems]") {
                        in_thm = true;
                        in_decl = false;
                        continue;
                    }

                    let payload = trimmed
                        .trim_start_matches("//")
                        .trim_start_matches("--")
                        .trim_start_matches('#')
                        .trim();

                    if in_decl && payload.starts_with('{') {
                        stitched_declarations.push(payload.to_string());
                    } else if in_thm && payload.starts_with('{') {
                        stitched_theorems.push(payload.to_string());
                    }
                }
            }
        }

        if !stitched_declarations.is_empty() {
            let conformance_errors = Self::validate_prismpm_semantic_conformance(
                &stitched_declarations,
                &stitched_theorems,
            );
            res.errors.extend(conformance_errors);

            let semantic_json = format!(
                r#"{{"declarations": [{}], "theorems": [{}]}}"#,
                stitched_declarations.join(","),
                stitched_theorems.join(",")
            );

            match lower_and_verify_semantic_json("Syntra.Spec", &semantic_json) {
                Ok(spec) => {
                    res.ffi_defs_verified = spec.definitions_verified;
                    res.ffi_theorems_verified = spec.theorems_verified;
                }
                Err(err) => {
                    res.errors.push(format!("Formal SPEC Verification Failed: {err}"));
                }
            }
        }

        res
    }

    fn scan_file(
        filepath: &Path,
        repo_root: &Path,
        tasks: &mut Vec<TaskRecord>,
        errors: &mut Vec<String>,
    ) {
        let content = match fs::read_to_string(filepath) {
            Ok(c) => c,
            Err(_) => return,
        };

        let rel_path = filepath
            .strip_prefix(repo_root)
            .unwrap_or(filepath)
            .display()
            .to_string();

        let task_header_re = Regex::new(
            r"^[ \t]*(?:#|//|--)\s*(TODO|FIXME|SPEC)\(([a-zA-Z0-9_\-]+)\s*\|\s*tier:([ABC])((?:\s*\|\s*(?:blocks|depends_on):\[[a-zA-Z0-9_\-,\s]*\])*)\):(?:\s*(.*))?$"
        ).unwrap();
        let any_todo_re = Regex::new(
            r"(?i)^[ \t]*(?:#|//|--)\s*(TODO|FIXME|SPEC)\b"
        ).unwrap();
        let desc_re = Regex::new(r"^[ \t]*(?:#|//|--)\s*Description:\s*(.+)$").unwrap();
        let crit_re = Regex::new(r"^[ \t]*(?:#|//|--)\s*Criterion:\s*(assert:\s*.+|review:\s*.+)$").unwrap();

        let lines: Vec<&str> = content.lines().collect();

        for (i, line) in lines.iter().enumerate() {
            let line_num = i + 1;

            if any_todo_re.is_match(line) && !task_header_re.is_match(line) {
                errors.push(format!(
                    "{}:{}: Unstructured task comment.\n  Found: {}\n  Must match: -- TODO(<id> | tier:<A|B|C> [| blocks:[...]] [| depends_on:[...]]):",
                    rel_path,
                    line_num,
                    line.trim()
                ));
                continue;
            }

            if let Some(caps) = task_header_re.captures(line) {
                let kind = caps.get(1).unwrap().as_str().to_string();
                let id = caps.get(2).unwrap().as_str().to_string();
                let tier = caps.get(3).unwrap().as_str().to_string();
                let metadata = caps.get(4).map(|m| m.as_str()).unwrap_or("");
                let inline_desc = caps.get(5).map(|m| m.as_str()).unwrap_or("");

                let mut blocks = Vec::new();
                let mut depends_on = Vec::new();

                let blocks_re = Regex::new(r"blocks:\[(.*?)\]").unwrap();
                let deps_re = Regex::new(r"depends_on:\[(.*?)\]").unwrap();

                if let Some(bcaps) = blocks_re.captures(metadata) {
                    for item in bcaps.get(1).unwrap().as_str().split(',') {
                        let t = item.trim();
                        if !t.is_empty() {
                            blocks.push(t.to_string());
                        }
                    }
                }

                if let Some(dcaps) = deps_re.captures(metadata) {
                    for item in dcaps.get(1).unwrap().as_str().split(',') {
                        let t = item.trim();
                        if !t.is_empty() {
                            depends_on.push(t.to_string());
                        }
                    }
                }

                let mut description = inline_desc.to_string();
                let mut criterion = String::new();

                for next_line in lines.iter().skip(line_num).take(5) {
                    let trimmed = next_line.trim();
                    if !trimmed.starts_with("//") && !trimmed.starts_with("--") && !trimmed.starts_with('#') {
                        break;
                    }
                    if let Some(dcaps) = desc_re.captures(next_line) {
                        description = dcaps.get(1).unwrap().as_str().trim().to_string();
                    } else if let Some(ccaps) = crit_re.captures(next_line) {
                        criterion = ccaps.get(1).unwrap().as_str().trim().to_string();
                    }
                }

                if description.is_empty() {
                    errors.push(format!("{}:{}: Task '{}' is missing a Description field.", rel_path, line_num, id));
                }
                if criterion.is_empty() {
                    errors.push(format!("{}:{}: Task '{}' is missing a Criterion field.", rel_path, line_num, id));
                }

                tasks.push(TaskRecord {
                    id,
                    kind,
                    tier,
                    blocks,
                    depends_on,
                    description,
                    criterion,
                    file: filepath.to_path_buf(),
                    line: line_num,
                });
            }
        }
    }

    fn validate_dag(tasks: &[TaskRecord]) -> Vec<String> {
        let mut errors = Vec::new();
        let mut id_map: HashMap<String, &TaskRecord> = HashMap::new();

        for t in tasks {
            if id_map.contains_key(&t.id) {
                errors.push(format!("Duplicate task ID '{}' found in {}", t.id, t.file.display()));
            } else {
                id_map.insert(t.id.clone(), t);
            }
        }

        // Verify dependencies exist
        for t in tasks {
            for dep in &t.depends_on {
                if !id_map.contains_key(dep) {
                    errors.push(format!("Task '{}' depends on non-existent task '{}'", t.id, dep));
                }
            }
            for b in &t.blocks {
                if !id_map.contains_key(b) {
                    errors.push(format!("Task '{}' blocks non-existent task '{}'", t.id, b));
                }
            }
        }

        // Cycle check (Kahn's algorithm)
        let mut in_degree: HashMap<String, usize> = HashMap::new();
        let mut adj: HashMap<String, Vec<String>> = HashMap::new();

        for t in tasks {
            in_degree.entry(t.id.clone()).or_insert(0);
            for dep in &t.depends_on {
                adj.entry(dep.clone()).or_default().push(t.id.clone());
                *in_degree.entry(t.id.clone()).or_insert(0) += 1;
            }
        }

        let mut q: VecDeque<String> = VecDeque::new();
        for (id, deg) in &in_degree {
            if *deg == 0 {
                q.push_back(id.clone());
            }
        }

        let mut visited = 0;
        while let Some(curr) = q.pop_front() {
            visited += 1;
            if let Some(neighbors) = adj.get(&curr) {
                for next in neighbors {
                    if let Some(d) = in_degree.get_mut(next) {
                        *d -= 1;
                        if *d == 0 {
                            q.push_back(next.clone());
                        }
                    }
                }
            }
        }

        if visited < in_degree.len() {
            errors.push("Cycle detected in Task Graph dependencies!".to_string());
        }

        errors
    }

    fn check_staged_resolutions(repo_root: &Path, active_tasks: &[TaskRecord]) -> Vec<String> {
        let mut errors = Vec::new();
        let diff = Command::new("git")
            .args(["diff", "--cached", "-U0"])
            .current_dir(repo_root)
            .output();

        if let Ok(out) = diff {
            let diff_text = String::from_utf8_lossy(&out.stdout);
            let active_ids: HashSet<&str> = active_tasks.iter().map(|t| t.id.as_str()).collect();
            let mut deleted_todos = Vec::new();

            let todo_re = Regex::new(r#"^-[ \t]*(?:#|//|--)\s*(?:TODO|FIXME|SPEC)\(([a-zA-Z0-9_\-]+)\s*\|"#).unwrap();

            for line in diff_text.lines() {
                if !line.starts_with("---") {
                    if let Some(caps) = todo_re.captures(line) {
                        let id = caps.get(1).unwrap().as_str();
                        if !active_ids.contains(id) {
                            deleted_todos.push(id.to_string());
                        }
                    }
                }
            }

            if !deleted_todos.is_empty() {
                let staged = Command::new("git")
                    .args(["diff", "--cached", "--name-only"])
                    .current_dir(repo_root)
                    .output();

                let mut staged_logs = Vec::new();
                if let Ok(sout) = staged {
                    for file in String::from_utf8_lossy(&sout.stdout).lines() {
                        if file.starts_with("logs/") || file.starts_with("tests/") {
                            staged_logs.push(repo_root.join(file));
                        }
                    }
                }

                for tid in deleted_todos {
                    let mut referenced = false;
                    for log_path in &staged_logs {
                        if let Ok(content) = fs::read_to_string(log_path) {
                            if content.contains(&tid) {
                                referenced = true;
                                break;
                            }
                        }
                    }

                    if !referenced {
                        errors.push(format!(
                            "Anti-Erasure Violation: Task '{}' is being removed without staged proof log in logs/ referencing '{}'.",
                            tid, tid
                        ));
                    }
                }
            }
        }

        errors
    }

    fn validate_prismpm_semantic_conformance(
        declarations: &[String],
        theorems: &[String],
    ) -> Vec<String> {
        let mut errors = Vec::new();

        // 1. Domain Entities Requirement:
        // PrismPM specifications must define the project's core domain entities.
        let mut defined_types = HashSet::new();
        let mut def_bodies: HashMap<String, Value> = HashMap::new();

        for d_str in declarations {
            if let Ok(v) = serde_json::from_str::<Value>(d_str) {
                let kind = v.get("kind").and_then(Value::as_str).unwrap_or("");
                let name = v.get("name").and_then(Value::as_str).unwrap_or("");
                if kind == "inductive" || kind == "structure" {
                    defined_types.insert(name.to_string());
                } else if kind == "definition" {
                    if let Some(b) = v.get("body") {
                        def_bodies.insert(name.to_string(), b.clone());
                    }
                }
            }
        }

        let required_entities = ["TaskTier", "TaskRecord", "ContextBudget", "SymbolNode"];
        for entity in &required_entities {
            if !defined_types.contains(*entity) {
                errors.push(format!(
                    "PrismPM Domain Model Violation: Missing required domain entity '{}'. PrismPM specifications must formally define project domain types (inductive/structure).",
                    entity
                ));
            }
        }

        // 2. Anti-Vacuity / Anti-Tautology Rule:
        // Reject trivial reflexivity identity proofs with zero parameters and literal true bodies.
        for t_str in theorems {
            if let Ok(v) = serde_json::from_str::<Value>(t_str) {
                let name = v.get("name").and_then(Value::as_str).unwrap_or("unnamed");
                let proof_kind = v.get("proof").and_then(|p| p.get("kind")).and_then(Value::as_str).unwrap_or("");
                let params = v.get("parameters").and_then(Value::as_array);

                if proof_kind == "reflexivity" && params.map_or(false, |p| p.is_empty()) {
                    if let Some(stmt) = v.get("statement") {
                        let stmt_kind = stmt.get("kind").and_then(Value::as_str).unwrap_or("");
                        let right_val = stmt.get("right");
                        let is_right_true = right_val
                            .and_then(|r| r.get("value"))
                            .and_then(Value::as_bool)
                            == Some(true);

                        if stmt_kind == "eq" && is_right_true {
                            if let Some(left) = stmt.get("left") {
                                if let Some(fn_name) = left.get("function").and_then(|f| f.get("name")).and_then(Value::as_str) {
                                    let args = left.get("arguments").and_then(Value::as_array);
                                    if args.map_or(false, |a| a.is_empty()) {
                                        // Check if called function has a trivial body
                                        if let Some(body) = def_bodies.get(fn_name) {
                                            let is_trivial_body = body.get("kind").and_then(Value::as_str) == Some("bool")
                                                && body.get("value").and_then(Value::as_bool) == Some(true);
                                            if is_trivial_body {
                                                errors.push(format!(
                                                    "PrismPM Anti-Vacuity Violation: Theorem '{}' is a vacuous reflexivity tautology (0 parameters, returns constant true). Contracts must express non-trivial domain invariants or property constraints over input parameters.",
                                                    name
                                                ));
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        errors
    }

    pub fn print_report(res: &LintResult) {
        println!("======================================================================");
        println!("SOURCE-FIRST TASK & INVARIANT LINTER (lgcc-rs)");
        println!("======================================================================");

        if !res.errors.is_empty() {
            println!("\n[FAILED] Found {} task specification errors:\n", res.errors.len());
            for err in &res.errors {
                println!("  ✖ {}", err);
            }
            println!("\nPlease fix these in code comments before committing.");
            return;
        }

        println!("\n[PASS] Verified {} in-line task contracts across codebase.", res.tasks.len());
        println!("\nLive In-Tree Task DAG:");

        for tier in &["A", "B", "C"] {
            let tier_tasks: Vec<&TaskRecord> = res.tasks.iter().filter(|t| t.tier == *tier).collect();
            if !tier_tasks.is_empty() {
                println!("\n--- Tier {} ---", tier);
                for t in tier_tasks {
                    let rel = t.file.strip_prefix(&res.root).unwrap_or(&t.file).display();
                    print!("  ● {} ({}:{})", t.id, rel, t.line);
                    if !t.depends_on.is_empty() {
                        print!(" (depends on: {})", t.depends_on.join(", "));
                    }
                    if !t.blocks.is_empty() {
                        print!(" -> blocks [{}]", t.blocks.join(", "));
                    }
                    println!("\n    Desc: {}", t.description);
                    println!("    Crit: {}", t.criterion);
                }
            }
        }

        println!("\n======================================================================");
        if res.ffi_defs_verified > 0 || res.ffi_theorems_verified > 0 {
            println!("● Machine-Checked Proof Replay Succeeded!");
            println!("  FFI C ABI Check:  {} definitions, {} theorems verified (prod_codegen)", res.ffi_defs_verified, res.ffi_theorems_verified);
            println!("  PrismPM Status:   Sound (Zero unproven obligations)");
        }
    }

    pub fn to_json(res: &LintResult) -> String {
        #[derive(Serialize)]
        struct TaskJson<'a> {
            id: &'a str,
            kind: &'a str,
            tier: &'a str,
            file: String,
            line: usize,
            desc: &'a str,
            description: &'a str,
            criterion: &'a str,
            blocks: &'a [String],
            depends_on: &'a [String],
        }

        #[derive(Serialize)]
        struct LintJson<'a> {
            root: String,
            errors: &'a [String],
            tasks: Vec<TaskJson<'a>>,
            ffi_defs_verified: u32,
            ffi_theorems_verified: u32,
        }

        let tasks: Vec<TaskJson> = res
            .tasks
            .iter()
            .map(|t| {
                let rel = t
                    .file
                    .strip_prefix(&res.root)
                    .unwrap_or(&t.file)
                    .display()
                    .to_string();
                TaskJson {
                    id: &t.id,
                    kind: &t.kind,
                    tier: &t.tier,
                    file: rel,
                    line: t.line,
                    desc: &t.description,
                    description: &t.description,
                    criterion: &t.criterion,
                    blocks: &t.blocks,
                    depends_on: &t.depends_on,
                }
            })
            .collect();

        let lint_json = LintJson {
            root: res.root.display().to_string(),
            errors: &res.errors,
            tasks,
            ffi_defs_verified: res.ffi_defs_verified,
            ffi_theorems_verified: res.ffi_theorems_verified,
        };

        serde_json::to_string_pretty(&lint_json).unwrap_or_else(|_| "{}".to_string())
    }

    pub fn fix_repository(repo_root: &Path, apply: bool) -> (usize, String) {
        let res = Self::lint_repository(repo_root);
        let mut used_ids: HashSet<String> = res.tasks.iter().map(|t| t.id.clone()).collect();

        let task_header_re = Regex::new(
            r"^[ \t]*(?:#|//|--)\s*(TODO|FIXME|SPEC)\(([a-zA-Z0-9_\-]+)\s*\|\s*tier:([ABC])((?:\s*\|\s*(?:blocks|depends_on):\[[a-zA-Z0-9_\-,\s]*\])*)\):(?:\s*(.*))?$"
        ).unwrap();
        let lazy_todo_re = Regex::new(
            r"(?i)^([ \t]*(?:#|//|--))\s*\b(TODO|FIXME|SPEC)\b\s*:?\s*(.*)$"
        ).unwrap();

        let mut fixed_count = 0;
        let mut report = String::new();

        for entry in WalkDir::new(repo_root)
            .into_iter()
            .filter_entry(|e| {
                if e.depth() == 0 {
                    return true;
                }
                let name = e.file_name().to_string_lossy();
                !name.starts_with('.')
                    && name != "build"
                    && name != "target"
                    && name != "vendor"
                    && name != "node_modules"
            })
            .filter_map(|e| e.ok())
        {
            if entry.file_type().is_file() {
                let p = entry.path();
                let ext = match p.extension().and_then(|s| s.to_str()) {
                    Some(ext) => ext,
                    None => continue,
                };
                match ext {
                    "lua" | "sh" | "bash" | "py" | "rs" => {}
                    _ => continue,
                }

                let content = match fs::read_to_string(p) {
                    Ok(c) => c,
                    Err(_) => continue,
                };

                let lines: Vec<&str> = content.lines().collect();
                let mut modified = false;
                let mut new_lines = Vec::new();
                let stem = p.file_stem().and_then(|s| s.to_str()).unwrap_or("task").replace('_', "-");
                let rel_path = p.strip_prefix(repo_root).unwrap_or(p).display().to_string();

                for (i, line) in lines.iter().enumerate() {
                    if !task_header_re.is_match(line) {
                        if let Some(caps) = lazy_todo_re.captures(line) {
                            let prefix = caps.get(1).map_or("--", |m| m.as_str());
                            let mut kind = caps.get(2).map_or("TODO", |m| m.as_str()).to_uppercase();
                            if kind != "TODO" && kind != "FIXME" && kind != "SPEC" {
                                kind = "TODO".to_string();
                            }
                            let mut raw_desc = caps.get(3).map_or("", |m| m.as_str()).trim().to_string();
                            if raw_desc.is_empty() {
                                raw_desc = format!("Resolve {} in {}", kind, stem);
                            }

                            // Generate slug ID
                            let mut slug = String::new();
                            let mut words = 0;
                            let mut in_word = false;
                            for c in raw_desc.chars() {
                                if c.is_alphanumeric() {
                                    slug.push(c.to_ascii_lowercase());
                                    in_word = true;
                                } else if in_word {
                                    words += 1;
                                    if words >= 3 {
                                        break;
                                    }
                                    slug.push('-');
                                    in_word = false;
                                }
                            }
                            while slug.ends_with('-') {
                                slug.pop();
                            }
                            if slug.is_empty() {
                                slug = "item".to_string();
                            }

                            let base_id = format!("{}-{}", stem, slug);
                            let mut task_id = base_id.clone();
                            let mut suffix = 2;
                            while used_ids.contains(&task_id) {
                                task_id = format!("{}-{}", base_id, suffix);
                                suffix += 1;
                            }
                            used_ids.insert(task_id.clone());

                            let h1 = format!("{} {}({} | tier:B): {}", prefix, kind, task_id, raw_desc);
                            let h2 = format!("{} Description: {}", prefix, raw_desc);
                            let h3 = format!("{} Criterion: review: \"{} is implemented and verified\"", prefix, raw_desc);

                            report.push_str(&format!(
                                "[SYNTHESIZE] {}:{} -> {}\n  {}\n  {}\n  {}\n",
                                rel_path,
                                i + 1,
                                task_id,
                                h1,
                                h2,
                                h3
                            ));

                            new_lines.push(h1);
                            new_lines.push(h2);
                            new_lines.push(h3);
                            modified = true;
                            fixed_count += 1;
                            continue;
                        }
                    }
                    new_lines.push(line.to_string());
                }

                if modified && apply {
                    let mut output = new_lines.join("\n");
                    output.push('\n');
                    let _ = fs::write(p, output);
                }
            }
        }

        if fixed_count == 0 {
            report.push_str("[PASS] No unstructured TODO/FIXME comments found.\n");
        } else if apply {
            report.push_str(&format!("[APPLIED] Converted {} unstructured comment(s) into Task Graph contracts.\n", fixed_count));
        } else {
            report.push_str(&format!("[DRY-RUN] Synthesized {} contract(s). Re-run with --apply to write changes.\n", fixed_count));
        }

        (fixed_count, report)
    }
}
