#pragma once

#include "../common.hpp"

namespace lgcc {

// TODO(ir-ast-extractor | tier:A | blocks:[ir-contract-skeletonizer]): Extract symbols, declarations, and import edges from Python and Lua ASTs.
// Description: Implement AST traversal using native C++ parser and Neovim Tree-sitter to parse source files into structured declaration lists and import edges.
// Criterion: review: "scripts/bundle_ir.py --extract <file> returns JSON with symbols, signatures, and import edges"

// TODO(prismpm-rust-bridge | tier:B | depends_on:[spec-lexlean-todo-schema] | blocks:[lgcc-lexlean-verifier]): Bridge prismpm-generated Rust verification library via C ABI FFI.
// Description: Consume verified Rust crates emitted by prismpm through its C ABI layer (prod_codegen::CBindings / extern "C") to expose contract verification functions and invariants directly to C++.
// Criterion: review: "cargo build produces static/dynamic library with C headers linking cleanly into C++ test runner"

// TODO(lgcc-lexlean-verifier | tier:B | depends_on:[prismpm-rust-bridge]): Integrate lgcc parser and pipeline with LexLean formal specification contracts.
// Description: Connect lgcc AST extractor and IR compiler with prismpm Rust/C FFI verifier to validate formal contracts and invariants at compile and lint time.
// Criterion: review: "lgcc validates LexLean-extracted contract assertions via FFI during static analysis passes"

struct ExtractedModule {
    fs::path filepath;
    std::string language;
    std::vector<Import> imports;
    std::vector<Symbol> symbols;
};

class Extractor {
public:
    static ExtractedModule extract(const fs::path& filepath);
    static std::string to_json(const ExtractedModule& mod);

private:
    static ExtractedModule extract_cpp(const fs::path& filepath, const std::string& content);
    static ExtractedModule extract_lua(const fs::path& filepath, const std::string& content);
    static ExtractedModule extract_python(const fs::path& filepath, const std::string& content);
};

} // namespace lgcc
