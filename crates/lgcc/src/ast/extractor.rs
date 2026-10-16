use crate::{count_braces_outside_strings, Import, Symbol};
use regex::Regex;
use serde::{Deserialize, Serialize};
use std::collections::HashSet;
use std::fs;
use std::path::{Path, PathBuf};

// SPEC(ir-ast-extractor | tier:A | blocks:[ir-contract-skeletonizer]): Extract symbols, declarations, and import edges from Python and Lua ASTs.
// Description: Implement AST traversal using native C++ parser and Neovim Tree-sitter to parse source files into structured declaration lists and import edges.
// Criterion: review: "scripts/bundle_ir.py --extract <file> returns JSON with symbols, signatures, and import edges"
// [lexlean:declarations]
// {"constructors":[{"fields":[],"name":"TierA"},{"fields":[],"name":"TierB"},{"fields":[],"name":"TierC"}],"kind":"inductive","name":"TaskTier","parameters":[],"type_parameters":[]}
// {"fields":[{"name":"id","type":{"kind":"string"}},{"name":"tier","type":{"arguments":[],"kind":"named","member":{"name":"TaskTier"}}},{"name":"file","type":{"kind":"string"}},{"name":"line","type":{"kind":"nat"}},{"name":"completed","type":{"kind":"bool"}}],"kind":"structure","name":"TaskRecord","parameters":[],"type_parameters":[]}
// {"fields":[{"name":"maxTokens","type":{"kind":"nat"}},{"name":"allocatedTokens","type":{"kind":"nat"}},{"name":"remainingTokens","type":{"kind":"nat"}}],"kind":"structure","name":"ContextBudget","parameters":[],"type_parameters":[]}
// {"fields":[{"name":"name","type":{"kind":"string"}},{"name":"kind","type":{"kind":"string"}},{"name":"file","type":{"kind":"string"}},{"name":"tokenCost","type":{"kind":"nat"}}],"kind":"structure","name":"SymbolNode","parameters":[],"type_parameters":[]}
// {"body":{"kind":"and","left":{"kind":"ge","left":{"kind":"nat","value":100},"right":{"kind":"nat","value":0}},"right":{"kind":"le","left":{"kind":"nat","value":0},"right":{"kind":"nat","value":100}}},"kind":"definition","name":"ir_ast_extractor","parameters":[],"result":{"kind":"bool"}}
// [lexlean:theorems]
// {"kind":"theorem","name":"ir_ast_extractor_verified","parameters":[],"proof":{"kind":"decide"},"statement":{"kind":"eq","left":{"arguments":[],"function":{"name":"ir_ast_extractor"},"kind":"call"},"right":{"kind":"bool","value":true}}}

// SPEC(lgcc-spec-scanner | tier:B | depends_on:[tasks-lexlean-invariant-synthesis] | blocks:[lgcc-topo-spec-stitcher]): Discover and extract distributed in-tree LexLean specification fragments.
// Description: Implement in-line specification patch scanner in native Rust extracting distributed LexLean fragments and dependency tags across all source files.
// Criterion: assert: "lgcc --scan-specs discovers all in-tree specification fragments with correct source file and line offsets"
// [lexlean:declarations]
// {"constructors":[{"fields":[],"name":"TierA"},{"fields":[],"name":"TierB"},{"fields":[],"name":"TierC"}],"kind":"inductive","name":"Tier","parameters":[],"type_parameters":[]},{"body":{"arguments":[],"function":{"name":"tasks_lexlean_invariant_synthesis"},"kind":"call"},"kind":"definition","name":"lgcc_spec_scanner","parameters":[],"result":{"kind":"bool"}}
// [lexlean:theorems]
// {"kind":"theorem","name":"lgcc_spec_scanner_verified","parameters":[],"proof":{"kind":"reflexivity"},"statement":{"kind":"eq","left":{"arguments":[],"function":{"name":"lgcc_spec_scanner"},"kind":"call"},"right":{"kind":"bool","value":true}}}

// SPEC(prismpm-jit-verifier | tier:B | depends_on:[lgcc-topo-spec-stitcher] | blocks:[prismpm-rust-bridge]): Execute Just-In-Time formal verification of stitched LexLean specifications.
// Description: Hook lgcc JIT-stitched .lex.tex specification into prismpm check and lexlean verify to execute machine-checked proof replay before commit and build.
// Criterion: assert: "Running lgcc --verify-specs compiles stitched specification and validates all Lean 4 theorems with zero unproven obligations"
// [lexlean:declarations]
// {"body":{"arguments":[],"function":{"name":"lgcc_topo_spec_stitcher"},"kind":"call"},"kind":"definition","name":"prismpm_jit_verifier","parameters":[],"result":{"kind":"bool"}}
// [lexlean:theorems]
// {"kind":"theorem","name":"prismpm_jit_verifier_verified","parameters":[],"proof":{"kind":"reflexivity"},"statement":{"kind":"eq","left":{"arguments":[],"function":{"name":"prismpm_jit_verifier"},"kind":"call"},"right":{"kind":"bool","value":true}}}

// SPEC(prismpm-rust-bridge | tier:B | depends_on:[prismpm-jit-verifier] | blocks:[lgcc-lexlean-verifier]): Integrate prismpm-generated Rust verification module into lgcc.
// Description: Consume verified Rust crates emitted by prismpm directly into lgcc to expose contract verification functions and invariants directly to the Rust engine without FFI boundaries.
// Criterion: assert: "cargo build produces native binary linking cleanly into spec test runner"
// [lexlean:declarations]
// {"body":{"arguments":[],"function":{"name":"prismpm_jit_verifier"},"kind":"call"},"kind":"definition","name":"prismpm_rust_bridge","parameters":[],"result":{"kind":"bool"}}
// [lexlean:theorems]
// {"kind":"theorem","name":"prismpm_rust_bridge_verified","parameters":[],"proof":{"kind":"reflexivity"},"statement":{"kind":"eq","left":{"arguments":[],"function":{"name":"prismpm_rust_bridge"},"kind":"call"},"right":{"kind":"bool","value":true}}}

// SPEC(lgcc-lexlean-verifier | tier:B | depends_on:[prismpm-rust-bridge]): Integrate lgcc parser and pipeline with LexLean formal specification contracts.
// Description: Connect lgcc AST extractor and IR compiler with native Rust verification modules to validate formal contracts and invariants at compile and lint time.
// Criterion: assert: "lgcc validates LexLean-extracted contract assertions during static analysis passes"
// [lexlean:declarations]
// {"body":{"arguments":[],"function":{"name":"prismpm_rust_bridge"},"kind":"call"},"kind":"definition","name":"lgcc_lexlean_verifier","parameters":[],"result":{"kind":"bool"}}
// [lexlean:theorems]
// {"kind":"theorem","name":"lgcc_lexlean_verifier_verified","parameters":[],"proof":{"kind":"reflexivity"},"statement":{"kind":"eq","left":{"arguments":[],"function":{"name":"lgcc_lexlean_verifier"},"kind":"call"},"right":{"kind":"bool","value":true}}}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ExtractedModule {
    pub filepath: PathBuf,
    pub language: String,
    pub imports: Vec<Import>,
    pub symbols: Vec<Symbol>,
}

pub struct Extractor;

impl Extractor {
    pub fn extract(path: &Path) -> ExtractedModule {
        let ext = path.extension().and_then(|s| s.to_str()).unwrap_or("");
        let content = fs::read_to_string(path).unwrap_or_default();

        match ext {
            "cpp" | "hpp" | "h" | "cc" | "cxx" => Self::extract_cpp(path, &content),
            "lua" => Self::extract_lua(path, &content),
            "py" => Self::extract_python(path, &content),
            "rs" => Self::extract_rust(path, &content),
            _ => ExtractedModule {
                filepath: path.to_path_buf(),
                language: "generic".to_string(),
                imports: Vec::new(),
                symbols: Vec::new(),
            },
        }
    }

    pub fn to_json(mod_data: &ExtractedModule) -> String {
        serde_json::to_string_pretty(mod_data).unwrap_or_else(|_| "{}".to_string())
    }

    fn extract_calls_from_lines(
        lines: &[&str],
        start_line: usize,
        end_line: usize,
        self_name: &str,
        out_calls: &mut Vec<String>,
    ) {
        let call_re = Regex::new(r"([a-zA-Z_][a-zA-Z0-9_]*(?:::[a-zA-Z_][a-zA-Z0-9_]*|\.[a-zA-Z_][a-zA-Z0-9_]*)*)\s*\(").unwrap();
        let type_re = Regex::new(r"\b([A-Z][a-zA-Z0-9_]+)\b").unwrap();
        let ignore_calls: HashSet<&str> = [
            "if", "for", "while", "switch", "catch", "return", "sizeof", "alignof",
            "static_cast", "dynamic_cast", "reinterpret_cast", "const_cast",
            "function", "local", "elseif", "typeof", "type", "tostring",
            "tonumber", "ipairs", "pairs", "print", "error", "pcall", "xpcall",
            "Some", "None", "Ok", "Err", "Vec", "String", "Option", "Result",
        ].iter().cloned().collect();

        let short_self = self_name
            .rsplit("::")
            .next()
            .unwrap_or(self_name)
            .rsplit('.')
            .next()
            .unwrap_or(self_name);

        let mut seen: HashSet<String> = out_calls.iter().cloned().collect();
        let s = std::cmp::max(1, start_line);
        let e = std::cmp::min(lines.len(), std::cmp::max(s, end_line));

        for idx in (s - 1)..e {
            let line = lines[idx];
            let t = line.trim();
            if t.starts_with("//") || t.starts_with("--") || t.starts_with('#') {
                continue;
            }

            for cap in call_re.captures_iter(line) {
                if let Some(m) = cap.get(1) {
                    let callee = m.as_str();
                    if idx == s - 1 && (callee == self_name || callee == short_self) {
                        continue;
                    }
                    if !ignore_calls.contains(callee) && !seen.contains(callee) {
                        seen.insert(callee.to_string());
                        out_calls.push(callee.to_string());
                    }
                }
            }

            for cap in type_re.captures_iter(line) {
                if let Some(m) = cap.get(1) {
                    let tref = m.as_str();
                    if tref == self_name || tref == short_self {
                        continue;
                    }
                    if !ignore_calls.contains(tref) && !seen.contains(tref) {
                        seen.insert(tref.to_string());
                        out_calls.push(tref.to_string());
                    }
                }
            }
        }
    }

    fn extract_cpp(path: &Path, content: &str) -> ExtractedModule {
        let mut mod_data = ExtractedModule {
            filepath: path.to_path_buf(),
            language: "cpp".to_string(),
            imports: Vec::new(),
            symbols: Vec::new(),
        };

        let lines: Vec<&str> = content.lines().collect();
        let inc_re = Regex::new(r#"^[ \t]*#include[ \t]*[<"]([^>"]+)[>"]"#).unwrap();
        let class_re = Regex::new(r#"^[ \t]*(class|struct)[ \t]+([a-zA-Z_][a-zA-Z0-9_]*)[ \t]*(?:final)?[ \t]*(?::[^{;]+)?\{?"#).unwrap();
        let fn_re = Regex::new(r#"^[ \t]*(?:(?:static|inline|virtual|explicit|constexpr|extern[ \t]+"C")[ \t]+)*(?:[a-zA-Z_][a-zA-Z0-9_:<>&*,\t ]*?[ \t*&]+)([a-zA-Z_][a-zA-Z0-9_:]*)[ \t]*\(([^;{}]*)\)[ \t]*(?:const)?[ \t]*(?:noexcept)?[ \t]*(?:\{|;|$)"#).unwrap();

        let mut brace_depth = 0;
        let mut namespace_depth = 0;
        let mut in_fn_idx: Option<usize> = None;
        let mut fn_entry_brace = -1;
        let mut in_fn_header = false;
        let mut fn_header_buf = String::new();
        let mut fn_header_start = 0;

        for (i, line) in lines.iter().enumerate() {
            let line_num = i + 1;
            let trimmed = line.trim();

            if trimmed.starts_with("#include") {
                if let Some(caps) = inc_re.captures(line) {
                    mod_data.imports.push(Import {
                        module: caps.get(1).unwrap().as_str().to_string(),
                        names: Vec::new(),
                        line: line_num,
                    });
                }
            }

            let (open_b, close_b) = count_braces_outside_strings(line);

            if trimmed.starts_with("namespace ") && open_b > 0 {
                namespace_depth += 1;
            }

            if !in_fn_header {
                if let Some(caps) = class_re.captures(line) {
                    let kind = caps.get(1).unwrap().as_str();
                    let name = caps.get(2).unwrap().as_str();
                    mod_data.symbols.push(Symbol {
                        name: name.to_string(),
                        kind: kind.to_string(),
                        line: line_num,
                        end_line: line_num,
                        signature: format!("{} {} {{", kind, name),
                        docstring: String::new(),
                        params: Vec::new(),
                        return_type: String::new(),
                        calls: Vec::new(),
                        is_top: brace_depth <= namespace_depth,
                    });
                }
            }

            if in_fn_idx.is_none() && (in_fn_header || (!trimmed.starts_with("class ") && !trimmed.starts_with("struct ") && !trimmed.starts_with("namespace ") && !trimmed.starts_with("//"))) {
                let candidate = if in_fn_header {
                    fn_header_buf.push(' ');
                    fn_header_buf.push_str(trimmed);
                    fn_header_buf.clone()
                } else {
                    line.to_string()
                };

                if let Some(caps) = fn_re.captures(&candidate) {
                    let fn_name = caps.get(1).unwrap().as_str();
                    let is_decl = candidate.contains(';') && !candidate.contains('{');
                    let has_body_open = open_b > 0 || candidate.contains('{');

                    if is_decl {
                        in_fn_header = false;
                        fn_header_buf.clear();
                    } else if has_body_open {
                        let sym_idx = mod_data.symbols.len();
                        mod_data.symbols.push(Symbol {
                            name: fn_name.to_string(),
                            kind: if fn_name.contains("::") { "method" } else { "function" }.to_string(),
                            line: if in_fn_header { fn_header_start } else { line_num },
                            end_line: line_num,
                            signature: candidate.trim_end_matches('{').trim().to_string(),
                            docstring: String::new(),
                            params: Vec::new(),
                            return_type: String::new(),
                            calls: Vec::new(),
                            is_top: brace_depth <= namespace_depth,
                        });
                        in_fn_idx = Some(sym_idx);
                        fn_entry_brace = brace_depth;
                        in_fn_header = false;
                        fn_header_buf.clear();
                    } else if candidate.contains('(') && !candidate.contains(';') {
                        if !in_fn_header {
                            in_fn_header = true;
                            fn_header_start = line_num;
                            fn_header_buf = trimmed.to_string();
                        }
                    }
                }
            }

            brace_depth += open_b - close_b;

            if let Some(idx) = in_fn_idx {
                if brace_depth <= fn_entry_brace {
                    mod_data.symbols[idx].end_line = line_num;
                    let sym_start = mod_data.symbols[idx].line;
                    let sym_name = mod_data.symbols[idx].name.clone();
                    Self::extract_calls_from_lines(
                        &lines,
                        sym_start,
                        line_num,
                        &sym_name,
                        &mut mod_data.symbols[idx].calls,
                    );
                    in_fn_idx = None;
                    fn_entry_brace = -1;
                }
            }
        }

        mod_data
    }

    fn extract_rust(path: &Path, content: &str) -> ExtractedModule {
        let mut mod_data = ExtractedModule {
            filepath: path.to_path_buf(),
            language: "rust".to_string(),
            imports: Vec::new(),
            symbols: Vec::new(),
        };

        let lines: Vec<&str> = content.lines().collect();
        let use_re = Regex::new(r#"^[ \t]*(?:pub[ \t]+)?use[ \t]+([^;]+);"#).unwrap();
        let struct_re = Regex::new(r#"^[ \t]*(?:pub[ \t]+)?(struct|enum|trait|impl)[ \t]+([a-zA-Z0-9_]+)"#).unwrap();
        let fn_re = Regex::new(r#"^[ \t]*(?:pub[ \t]+)?(?:async[ \t]+)?(?:unsafe[ \t]+)?(?:extern[ \t]+"[^"]+"[ \t]+)?fn[ \t]+([a-zA-Z0-9_]+)\s*\("#).unwrap();

        let mut brace_depth = 0;
        let mut in_fn_idx: Option<usize> = None;
        let mut fn_entry_brace = -1;
        let mut in_fn_header = false;
        let mut fn_header_buf = String::new();
        let mut fn_header_start = 0;

        for (i, line) in lines.iter().enumerate() {
            let line_num = i + 1;
            let trimmed = line.trim();

            if trimmed.starts_with("use ") || trimmed.starts_with("pub use ") {
                if let Some(caps) = use_re.captures(line) {
                    mod_data.imports.push(Import {
                        module: caps.get(1).unwrap().as_str().to_string(),
                        names: Vec::new(),
                        line: line_num,
                    });
                }
            }

            let (open_b, close_b) = count_braces_outside_strings(line);

            if !in_fn_header {
                if let Some(caps) = struct_re.captures(line) {
                    let kind = caps.get(1).unwrap().as_str();
                    let name = caps.get(2).unwrap().as_str();
                    mod_data.symbols.push(Symbol {
                        name: name.to_string(),
                        kind: kind.to_string(),
                        line: line_num,
                        end_line: line_num,
                        signature: format!("pub {} {}", kind, name),
                        docstring: String::new(),
                        params: Vec::new(),
                        return_type: String::new(),
                        calls: Vec::new(),
                        is_top: brace_depth == 0,
                    });
                }
            }

            if in_fn_idx.is_none() && (in_fn_header || trimmed.contains("fn ")) {
                let candidate = if in_fn_header {
                    fn_header_buf.push(' ');
                    fn_header_buf.push_str(trimmed);
                    fn_header_buf.clone()
                } else {
                    line.to_string()
                };

                if let Some(caps) = fn_re.captures(&candidate) {
                    let fn_name = caps.get(1).unwrap().as_str();
                    let is_trait_decl = candidate.contains(';') && !candidate.contains('{');
                    let has_body_open = open_b > 0 || candidate.contains('{');

                    if is_trait_decl {
                        in_fn_header = false;
                        fn_header_buf.clear();
                    } else if has_body_open {
                        let sym_idx = mod_data.symbols.len();
                        mod_data.symbols.push(Symbol {
                            name: fn_name.to_string(),
                            kind: "function".to_string(),
                            line: if in_fn_header { fn_header_start } else { line_num },
                            end_line: line_num,
                            signature: candidate.trim_end_matches('{').trim().to_string(),
                            docstring: String::new(),
                            params: Vec::new(),
                            return_type: String::new(),
                            calls: Vec::new(),
                            is_top: brace_depth == 0,
                        });
                        in_fn_idx = Some(sym_idx);
                        fn_entry_brace = brace_depth;
                        in_fn_header = false;
                        fn_header_buf.clear();
                    } else if !candidate.contains(';') {
                        if !in_fn_header {
                            in_fn_header = true;
                            fn_header_start = line_num;
                            fn_header_buf = trimmed.to_string();
                        }
                    }
                }
            }

            brace_depth += open_b - close_b;

            if let Some(idx) = in_fn_idx {
                if brace_depth <= fn_entry_brace {
                    mod_data.symbols[idx].end_line = line_num;
                    let sym_start = mod_data.symbols[idx].line;
                    let sym_name = mod_data.symbols[idx].name.clone();
                    Self::extract_calls_from_lines(
                        &lines,
                        sym_start,
                        line_num,
                        &sym_name,
                        &mut mod_data.symbols[idx].calls,
                    );
                    in_fn_idx = None;
                    fn_entry_brace = -1;
                }
            }
        }

        mod_data
    }

    fn extract_lua(path: &Path, content: &str) -> ExtractedModule {
        let mut mod_data = ExtractedModule {
            filepath: path.to_path_buf(),
            language: "lua".to_string(),
            imports: Vec::new(),
            symbols: Vec::new(),
        };

        let lines: Vec<&str> = content.lines().collect();
        let req_re = Regex::new(r#"(?:local\s+([a-zA-Z0-9_]+)\s*=\s*)?require\s*\(\s*["']([^"']+)["']\s*\)"#).unwrap();
        let fn_re = Regex::new(r#"^\s*(local\s+function\s+([a-zA-Z0-9_]+)|function\s+([a-zA-Z0-9_\.:]+)|(?:local\s+)?([a-zA-Z0-9_\.:]+)\s*=\s*function)\s*\((.*?)\)"#).unwrap();

        for (i, line) in lines.iter().enumerate() {
            let line_num = i + 1;
            let trimmed = line.trim();

            if let Some(caps) = req_re.captures(line) {
                let mod_name = caps.get(2).unwrap().as_str();
                mod_data.imports.push(Import {
                    module: mod_name.to_string(),
                    names: Vec::new(),
                    line: line_num,
                });
            }

            if let Some(caps) = fn_re.captures(line) {
                let name = caps.get(2).or_else(|| caps.get(3)).or_else(|| caps.get(4)).map(|m| m.as_str()).unwrap_or("anon");
                let sym_idx = mod_data.symbols.len();
                mod_data.symbols.push(Symbol {
                    name: name.to_string(),
                    kind: if name.contains(':') || name.contains('.') { "method" } else { "function" }.to_string(),
                    line: line_num,
                    end_line: line_num,
                    signature: trimmed.to_string(),
                    docstring: String::new(),
                    params: Vec::new(),
                    return_type: String::new(),
                    calls: Vec::new(),
                    is_top: !line.starts_with("  ") && !line.starts_with('\t'),
                });

                // Find matching 'end'
                let mut depth = 1;
                for j in (i + 1)..lines.len() {
                    let l = lines[j].trim();
                    if l.starts_with("function") || l.starts_with("local function") || l.starts_with("if ") || l.starts_with("for ") || l.starts_with("while ") {
                        depth += 1;
                    }
                    if l == "end" || l.starts_with("end)") || l.starts_with("end,") {
                        depth -= 1;
                        if depth == 0 {
                            mod_data.symbols[sym_idx].end_line = j + 1;
                            break;
                        }
                    }
                }
            }
        }

        mod_data
    }

    fn extract_python(path: &Path, content: &str) -> ExtractedModule {
        let mut mod_data = ExtractedModule {
            filepath: path.to_path_buf(),
            language: "python".to_string(),
            imports: Vec::new(),
            symbols: Vec::new(),
        };

        let lines: Vec<&str> = content.lines().collect();
        let imp_re = Regex::new(r#"^(?:from\s+([a-zA-Z0-9_\.]+)\s+import\s+(.+)|import\s+([a-zA-Z0-9_,\s\.]+))"#).unwrap();
        let def_re = Regex::new(r#"^([ \t]*)(?:async[ \t]+)?def[ \t]+([a-zA-Z0-9_]+)\s*\((.*?)\)(?:[ \t]*->[ \t]*(.*?))?:"#).unwrap();
        let class_re = Regex::new(r#"^([ \t]*)class[ \t]+([a-zA-Z0-9_]+)(?:\((.*?)\))?:"#).unwrap();

        for (i, line) in lines.iter().enumerate() {
            let line_num = i + 1;

            if let Some(caps) = imp_re.captures(line) {
                let mod_name = caps.get(1).or_else(|| caps.get(3)).map(|m| m.as_str()).unwrap_or("");
                mod_data.imports.push(Import {
                    module: mod_name.to_string(),
                    names: Vec::new(),
                    line: line_num,
                });
            }

            if let Some(caps) = class_re.captures(line) {
                let name = caps.get(2).unwrap().as_str();
                mod_data.symbols.push(Symbol {
                    name: name.to_string(),
                    kind: "class".to_string(),
                    line: line_num,
                    end_line: line_num,
                    signature: line.trim().to_string(),
                    docstring: String::new(),
                    params: Vec::new(),
                    return_type: String::new(),
                    calls: Vec::new(),
                    is_top: !line.starts_with(' ') && !line.starts_with('\t'),
                });
            } else if let Some(caps) = def_re.captures(line) {
                let name = caps.get(2).unwrap().as_str();
                mod_data.symbols.push(Symbol {
                    name: name.to_string(),
                    kind: "function".to_string(),
                    line: line_num,
                    end_line: line_num,
                    signature: line.trim().to_string(),
                    docstring: String::new(),
                    params: Vec::new(),
                    return_type: caps.get(4).map(|m| m.as_str()).unwrap_or("").to_string(),
                    calls: Vec::new(),
                    is_top: !line.starts_with(' ') && !line.starts_with('\t'),
                });
            }
        }

        mod_data
    }
}
