#pragma once

#include "../common.hpp"

namespace lgcc {

// TODO(ir-ast-extractor | tier:A | blocks:[ir-contract-skeletonizer]): Extract symbols, declarations, and import edges from Python and Lua ASTs.
// Description: Implement AST traversal using native C++ parser and Neovim Tree-sitter to parse source files into structured declaration lists and import edges.
// Criterion: review: "scripts/bundle_ir.py --extract <file> returns JSON with symbols, signatures, and import edges"

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
