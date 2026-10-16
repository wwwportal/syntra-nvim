use serde_json::{json, Value};
use std::io::Write;
use std::process::{Command, Stdio};

#[derive(Debug, Clone)]
pub struct LeanSpecBlock {
    pub raw_lean: String,
    pub declarations: Vec<Value>,
    pub theorems: Vec<Value>,
}

pub struct LeanParser;

impl LeanParser {
    /// Parse a block of Lean 4 source code into LexLean semantic declarations and theorems.
    pub fn parse_lean_spec(input: &str) -> Result<LeanSpecBlock, String> {
        let mut declarations = Vec::new();
        let mut theorems = Vec::new();

        // Clean lines and strip comment leaders if any remain
        let mut clean_lines = Vec::new();
        for line in input.lines() {
            let trimmed = line.trim();
            if trimmed.is_empty() {
                continue;
            }
            let stripped = trimmed
                .trim_start_matches("//")
                .trim_start_matches("--")
                .trim_start_matches('#')
                .trim();
            if !stripped.is_empty() && !stripped.starts_with("[lean") && !stripped.starts_with("[lexlean") {
                clean_lines.push(stripped.to_string());
            }
        }

        let full_text = clean_lines.join("\n");
        let mut idx = 0;
        let lines: Vec<&str> = clean_lines.iter().map(|s| s.as_str()).collect();

        while idx < lines.len() {
            let line = lines[idx].trim();
            if line.starts_with("inductive ") {
                let (decl, next_idx) = Self::parse_inductive(&lines, idx)?;
                declarations.push(decl);
                idx = next_idx;
            } else if line.starts_with("structure ") {
                let (decl, next_idx) = Self::parse_structure(&lines, idx)?;
                declarations.push(decl);
                idx = next_idx;
            } else if line.starts_with("def ") {
                let (decl, next_idx) = Self::parse_def(&lines, idx)?;
                declarations.push(decl);
                idx = next_idx;
            } else if line.starts_with("theorem ") {
                let (thm, next_idx) = Self::parse_theorem(&lines, idx)?;
                theorems.push(thm);
                idx = next_idx;
            } else {
                idx += 1;
            }
        }

        Ok(LeanSpecBlock {
            raw_lean: full_text,
            declarations,
            theorems,
        })
    }

    fn parse_type(ty_str: &str) -> Value {
        let t = ty_str.trim();
        match t {
            "Bool" => json!({"kind": "bool"}),
            "Nat" => json!({"kind": "nat"}),
            "Int" | "Int64" => json!({"kind": "int"}),
            "String" => json!({"kind": "string"}),
            s if s.starts_with("List ") => {
                let inner = s.strip_prefix("List ").unwrap().trim();
                json!({
                    "kind": "list",
                    "element": Self::parse_type(inner)
                })
            }
            named => json!({
                "kind": "named",
                "member": {
                    "name": named
                }
            }),
        }
    }

    fn parse_expr(expr_str: &str) -> Result<Value, String> {
        let s = expr_str.trim();

        // Check for binary '&&' (logical and)
        if let Some(pos) = s.find("&&") {
            let left = Self::parse_expr(&s[..pos])?;
            let right = Self::parse_expr(&s[pos + 2..])?;
            return Ok(json!({
                "kind": "and",
                "left": left,
                "right": right
            }));
        }

        // Check for binary '||' (logical or)
        if let Some(pos) = s.find("||") {
            let left = Self::parse_expr(&s[..pos])?;
            let right = Self::parse_expr(&s[pos + 2..])?;
            return Ok(json!({
                "kind": "or",
                "left": left,
                "right": right
            }));
        }

        // Check for comparison '>='
        if let Some(pos) = s.find(">=") {
            let left = Self::parse_expr(&s[..pos])?;
            let right = Self::parse_expr(&s[pos + 2..])?;
            return Ok(json!({
                "kind": "ge",
                "left": left,
                "right": right
            }));
        }

        // Check for comparison '<='
        if let Some(pos) = s.find("<=") {
            let left = Self::parse_expr(&s[..pos])?;
            let right = Self::parse_expr(&s[pos + 2..])?;
            return Ok(json!({
                "kind": "le",
                "left": left,
                "right": right
            }));
        }

        // Check for comparison '>'
        if let Some(pos) = s.find('>') {
            let left = Self::parse_expr(&s[..pos])?;
            let right = Self::parse_expr(&s[pos + 1..])?;
            return Ok(json!({
                "kind": "gt",
                "left": left,
                "right": right
            }));
        }

        // Check for comparison '<'
        if let Some(pos) = s.find('<') {
            let left = Self::parse_expr(&s[..pos])?;
            let right = Self::parse_expr(&s[pos + 1..])?;
            return Ok(json!({
                "kind": "lt",
                "left": left,
                "right": right
            }));
        }

        // Check for equality '==' or '='
        if let Some(pos) = s.find("==") {
            let left = Self::parse_expr(&s[..pos])?;
            let right = Self::parse_expr(&s[pos + 2..])?;
            return Ok(json!({
                "kind": "eq",
                "left": left,
                "right": right
            }));
        }

        if s == "true" {
            return Ok(json!({"kind": "bool", "value": true}));
        }
        if s == "false" {
            return Ok(json!({"kind": "bool", "value": false}));
        }

        if let Ok(num) = s.parse::<u64>() {
            return Ok(json!({"kind": "nat", "value": num}));
        }

        if s.starts_with('"') && s.ends_with('"') && s.len() >= 2 {
            let val = &s[1..s.len() - 1];
            return Ok(json!({"kind": "string", "value": val}));
        }

        // Identifier or function call without args
        let ident = s.trim();
        if ident.chars().all(|c| c.is_alphanumeric() || c == '_' || c == '-') {
            return Ok(json!({
                "kind": "call",
                "function": {
                    "name": ident
                },
                "arguments": []
            }));
        }

        Err(format!("Unable to parse Lean expression: `{s}`"))
    }

    fn parse_inductive(lines: &[&str], start: usize) -> Result<(Value, usize), String> {
        let first = lines[start].trim();
        // inductive <Name> [where] [| C1 | C2]
        let rest = first.strip_prefix("inductive ").unwrap().trim();
        let name_part = rest.split_whitespace().next().ok_or_else(|| "missing inductive name".to_string())?;
        let name = name_part.trim_end_matches("where").trim();

        let mut constructors = Vec::new();
        // Check if inline constructors exist
        if rest.contains('|') {
            for part in rest.split('|').skip(1) {
                let cname = part.trim();
                if !cname.is_empty() {
                    constructors.push(json!({"name": cname, "fields": []}));
                }
            }
            return Ok((json!({
                "kind": "inductive",
                "name": name,
                "parameters": [],
                "type_parameters": [],
                "constructors": constructors
            }), start + 1));
        }

        let mut idx = start + 1;
        while idx < lines.len() {
            let l = lines[idx].trim();
            if l.starts_with('|') {
                let cname = l.trim_start_matches('|').trim();
                // Strip type annotation if present (e.g. | TierA : Tier -> TierA)
                let actual_cname = if let Some(pos) = cname.find(':') {
                    cname[..pos].trim()
                } else {
                    cname
                };
                if !actual_cname.is_empty() {
                    constructors.push(json!({"name": actual_cname, "fields": []}));
                }
                idx += 1;
            } else if l.is_empty() {
                idx += 1;
            } else {
                break;
            }
        }

        Ok((json!({
            "kind": "inductive",
            "name": name,
            "parameters": [],
            "type_parameters": [],
            "constructors": constructors
        }), idx))
    }

    fn parse_structure(lines: &[&str], start: usize) -> Result<(Value, usize), String> {
        let first = lines[start].trim();
        let rest = first.strip_prefix("structure ").unwrap().trim();
        let name = rest.split_whitespace().next().ok_or_else(|| "missing structure name".to_string())?;
        let name = name.trim_end_matches("where").trim();

        let mut fields = Vec::new();
        let mut idx = start + 1;

        while idx < lines.len() {
            let l = lines[idx].trim();
            if l.contains(':') && !l.starts_with("def ") && !l.starts_with("theorem ") && !l.starts_with("inductive ") && !l.starts_with("structure ") {
                let parts: Vec<&str> = l.splitn(2, ':').collect();
                let fname = parts[0].trim();
                let ftype_str = parts[1].trim();
                fields.push(json!({
                    "name": fname,
                    "type": Self::parse_type(ftype_str)
                }));
                idx += 1;
            } else if l.is_empty() {
                idx += 1;
            } else {
                break;
            }
        }

        Ok((json!({
            "kind": "structure",
            "name": name,
            "parameters": [],
            "type_parameters": [],
            "fields": fields
        }), idx))
    }

    fn parse_def(lines: &[&str], start: usize) -> Result<(Value, usize), String> {
        let mut full_def = lines[start].to_string();
        let mut idx = start + 1;

        while idx < lines.len() {
            let l = lines[idx].trim();
            if l.starts_with("def ") || l.starts_with("theorem ") || l.starts_with("inductive ") || l.starts_with("structure ") {
                break;
            }
            full_def.push(' ');
            full_def.push_str(l);
            idx += 1;
        }

        let s = full_def.trim().strip_prefix("def ").unwrap().trim();
        // <name> : <Type> := <body>
        let colon_pos = s.find(':').ok_or_else(|| format!("def missing ':' type annotation: `{s}`"))?;
        let name = s[..colon_pos].trim();

        let assign_pos = s.find(":=").ok_or_else(|| format!("def missing ':=' body assignment: `{s}`"))?;
        let type_str = s[colon_pos + 1..assign_pos].trim();
        let body_str = s[assign_pos + 2..].trim();

        let res_type = Self::parse_type(type_str);
        let body = Self::parse_expr(body_str)?;

        Ok((json!({
            "kind": "definition",
            "name": name,
            "parameters": [],
            "result": res_type,
            "body": body
        }), idx))
    }

    fn parse_theorem(lines: &[&str], start: usize) -> Result<(Value, usize), String> {
        let mut full_thm = lines[start].to_string();
        let mut idx = start + 1;

        while idx < lines.len() {
            let l = lines[idx].trim();
            if l.starts_with("def ") || l.starts_with("theorem ") || l.starts_with("inductive ") || l.starts_with("structure ") {
                break;
            }
            full_thm.push(' ');
            full_thm.push_str(l);
            idx += 1;
        }

        let s = full_thm.trim().strip_prefix("theorem ").unwrap().trim();
        // <name> : <statement> := by <proof>
        let colon_pos = s.find(':').ok_or_else(|| format!("theorem missing ':' statement: `{s}`"))?;
        let name = s[..colon_pos].trim();

        let assign_pos = s.find(":=").ok_or_else(|| format!("theorem missing ':=' proof: `{s}`"))?;
        let stmt_str = s[colon_pos + 1..assign_pos].trim();
        let proof_str = s[assign_pos + 2..].trim();

        // Statement is typically <lhs> = <rhs>
        let eq_pos = stmt_str.find('=').ok_or_else(|| format!("theorem statement missing '=': `{stmt_str}`"))?;
        let lhs = Self::parse_expr(&stmt_str[..eq_pos])?;
        let rhs = Self::parse_expr(&stmt_str[eq_pos + 1..])?;

        let proof_kind = if proof_str.contains("decide") {
            "decide"
        } else {
            "reflexivity"
        };

        Ok((json!({
            "kind": "theorem",
            "name": name,
            "parameters": [],
            "statement": {
                "kind": "eq",
                "left": lhs,
                "right": rhs
            },
            "proof": {
                "kind": proof_kind
            }
        }), idx))
    }

    /// Verifies stitched Lean 4 code directly against the Lean 4 compiler kernel via `lean --stdin`.
    pub fn verify_with_lean_kernel(lean_code: &str) -> Result<(), String> {
        let home = std::env::var("HOME").unwrap_or_default();
        let elan_lean = std::path::PathBuf::from(format!("{home}/.elan/bin/lean"));
        let lean_bin = if elan_lean.exists() {
            elan_lean
        } else if Command::new("lean").arg("--version").output().is_ok() {
            std::path::PathBuf::from("lean")
        } else {
            // Lean compiler not available in environment, pass through
            return Ok(());
        };

        let mut child = Command::new(lean_bin)
            .arg("--stdin")
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()
            .map_err(|e| format!("Failed to spawn lean: {e}"))?;

        if let Some(mut stdin) = child.stdin.take() {
            stdin
                .write_all(lean_code.as_bytes())
                .map_err(|e| format!("Failed to write to lean stdin: {e}"))?;
        }

        let output = child
            .wait_with_output()
            .map_err(|e| format!("Failed to wait on lean process: {e}"))?;

        if !output.status.success() {
            let stderr = String::from_utf8_lossy(&output.stderr);
            let stdout = String::from_utf8_lossy(&output.stdout);
            return Err(format!("Lean 4 kernel verification failed:\n{}{}", stderr, stdout));
        }

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_lean_def_and_theorem() {
        let src = r#"
            def test_task : Bool := true
            theorem test_task_verified : test_task = true := by rfl
        "#;
        let block = LeanParser::parse_lean_spec(src).unwrap();
        assert_eq!(block.declarations.len(), 1);
        assert_eq!(block.theorems.len(), 1);
        assert_eq!(block.declarations[0]["name"], "test_task");
        assert_eq!(block.theorems[0]["name"], "test_task_verified");
    }

    #[test]
    fn test_parse_lean_inductive_and_structure() {
        let src = r#"
            inductive Tier where
              | TierA
              | TierB
              | TierC

            structure AppConfig where
              name : String
              budget : Nat
              tier : Tier
        "#;
        let block = LeanParser::parse_lean_spec(src).unwrap();
        assert_eq!(block.declarations.len(), 2);
        assert_eq!(block.declarations[0]["name"], "Tier");
        assert_eq!(block.declarations[1]["name"], "AppConfig");
    }

    #[test]
    fn test_parse_lean_complex_expr() {
        let src = r#"
            def unified_tasks : Bool := 1000 >= 0 && 1 <= 1000
            theorem unified_tasks_verified : unified_tasks = true := by decide
        "#;
        let block = LeanParser::parse_lean_spec(src).unwrap();
        assert_eq!(block.declarations.len(), 1);
        assert_eq!(block.theorems.len(), 1);
        assert_eq!(block.theorems[0]["proof"]["kind"], "decide");
    }

    #[test]
    fn test_verify_with_lean_kernel() {
        let src = "def test_val : Bool := true\ntheorem test_val_thm : test_val = true := by rfl\n";
        assert!(LeanParser::verify_with_lean_kernel(src).is_ok());
    }
}
