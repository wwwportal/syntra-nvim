#pragma once

#include "../common.hpp"

namespace lgcc {

// TODO(ir-ast-extractor | tier:A | blocks:[ir-contract-skeletonizer]): Extract symbols, declarations, and import edges from Python and Lua ASTs.
// Description: Implement AST traversal using native C++ parser and Neovim Tree-sitter to parse source files into structured declaration lists and import edges.
// Criterion: review: "scripts/bundle_ir.py --extract <file> returns JSON with symbols, signatures, and import edges"

// TODO(prismpm-cpp-backend | tier:B | depends_on:[spec-lexlean-todo-schema] | blocks:[lgcc-lexlean-verifier]): Formulate native C++20 code extraction backend for prismpm.
// Description: Design and implement C++20 codegen backend for prismpm/prod-codegen translating LexLean inductive specifications, records, and pattern matches into C++20 structs, std::variant, std::optional, and concepts instead of Rust.
// Criterion: review: "prismpm CLI or test suite successfully extracts LexLean definitions into valid C++20 headers and structs"

// TODO(lgcc-lexlean-verifier | tier:B | depends_on:[prismpm-cpp-backend]): Integrate lgcc parser and pipeline with LexLean formal specification contracts.
// Description: Connect lgcc AST extractor and IR compiler with LexLean C++-generated specifications to verify formal contracts and invariants at compile and lint time.
// Criterion: review: "lgcc validates LexLean-extracted C++ contract assertions during static analysis passes"

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
