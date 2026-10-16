#pragma once

#include "../common.hpp"

namespace lgcc {

// TODO(ir-ast-extractor | tier:A | blocks:[ir-contract-skeletonizer]): Extract symbols, declarations, and import edges from Python and Lua ASTs.
// Description: Implement AST traversal using native C++ parser and Neovim Tree-sitter to parse source files into structured declaration lists and import edges.
// Criterion: review: "scripts/bundle_ir.py --extract <file> returns JSON with symbols, signatures, and import edges"

// SPEC(lgcc-spec-scanner | tier:B | depends_on:[tasks-add-spec-scaffold] | blocks:[lgcc-topo-spec-stitcher]): Discover and extract distributed in-tree LexLean specification fragments.
// Description: Implement in-line specification patch scanner in lgcc C++ extracting distributed LexLean fragments and dependency tags across all source files.
// Criterion: assert: "lgcc --scan-specs discovers all in-tree specification fragments with correct source file and line offsets"
// [lexlean:declarations]
// {"constructors":[{"fields":[],"name":"TierA"},{"fields":[],"name":"TierB"},{"fields":[],"name":"TierC"}],"kind":"inductive","name":"Tier","parameters":[],"type_parameters":[]},{"body":{"arguments":[],"function":{"name":"tasks_add_spec_scaffold"},"kind":"call"},"kind":"definition","name":"lgcc_spec_scanner","parameters":[],"result":{"kind":"bool"}}
// [lexlean:theorems]
// {"kind":"theorem","name":"lgcc_spec_scanner_verified","parameters":[],"proof":{"kind":"reflexivity"},"statement":{"kind":"eq","left":{"arguments":[],"function":{"name":"lgcc_spec_scanner"},"kind":"call"},"right":{"kind":"bool","value":true}}}

// SPEC(prismpm-jit-verifier | tier:B | depends_on:[lgcc-topo-spec-stitcher] | blocks:[prismpm-rust-bridge]): Execute Just-In-Time formal verification of stitched LexLean specifications.
// Description: Hook lgcc JIT-stitched .lex.tex specification into prismpm check and lexlean verify to execute machine-checked proof replay before commit and build.
// Criterion: assert: "Running lgcc --verify-specs compiles stitched specification and validates all Lean 4 theorems with zero unproven obligations"
// [lexlean:declarations]
// {"body":{"arguments":[],"function":{"name":"lgcc_topo_spec_stitcher"},"kind":"call"},"kind":"definition","name":"prismpm_jit_verifier","parameters":[],"result":{"kind":"bool"}}
// [lexlean:theorems]
// {"kind":"theorem","name":"prismpm_jit_verifier_verified","parameters":[],"proof":{"kind":"reflexivity"},"statement":{"kind":"eq","left":{"arguments":[],"function":{"name":"prismpm_jit_verifier"},"kind":"call"},"right":{"kind":"bool","value":true}}}



// TODO(prismpm-rust-bridge | tier:B | depends_on:[prismpm-jit-verifier] | blocks:[lgcc-lexlean-verifier]): Bridge prismpm-generated Rust verification library via C ABI FFI.
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
