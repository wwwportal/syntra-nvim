pub mod ast;
pub mod graph;
pub mod router;
pub mod spec;

use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Symbol {
    pub name: String,
    pub kind: String, // "function", "class", "method", "struct"
    pub line: usize,
    pub end_line: usize,
    pub signature: String,
    pub docstring: String,
    pub params: Vec<String>,
    pub return_type: String,
    pub calls: Vec<String>,
    pub is_top: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Import {
    pub module: String,
    pub names: Vec<String>,
    pub line: usize,
}

pub fn estimate_tokens(text: &str) -> usize {
    std::cmp::max(1, text.len() / 4)
}

pub fn count_braces_outside_strings(line: &str) -> (i32, i32) {
    let mut open_b = 0;
    let mut close_b = 0;
    let mut in_str = false;
    let mut in_char = false;
    let mut in_raw = false;
    let chars: Vec<char> = line.chars().collect();
    let n = chars.len();
    let mut i = 0;

    while i < n {
        let c = chars[i];
        if in_raw {
            if c == ')' && i + 1 < n && chars[i + 1] == '"' {
                in_raw = false;
                i += 1;
            }
            i += 1;
            continue;
        }
        if in_str {
            if c == '\\' && i + 1 < n {
                i += 2;
                continue;
            } else if c == '"' {
                in_str = false;
            }
            i += 1;
            continue;
        }
        if in_char {
            if c == '\\' && i + 1 < n {
                i += 2;
                continue;
            } else if c == '\'' {
                in_char = false;
            }
            i += 1;
            continue;
        }
        if c == '/' && i + 1 < n && chars[i + 1] == '/' {
            break; // line comment
        }
        if c == 'R' && i + 2 < n && chars[i + 1] == '"' && chars[i + 2] == '(' {
            in_raw = true;
            i += 3;
            continue;
        }
        if c == '"' {
            in_str = true;
            i += 1;
            continue;
        }
        if c == '\'' {
            in_char = true;
            i += 1;
            continue;
        }
        if c == '{' {
            open_b += 1;
        } else if c == '}' {
            close_b += 1;
        }
        i += 1;
    }

    (open_b, close_b)
}
