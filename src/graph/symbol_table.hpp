#pragma once

#include "../common.hpp"
#include "../ast/extractor.hpp"

namespace lgcc {

// SPEC(graph-repo-scanner | tier:A | depends_on:[lgcc-dep-bundle] | blocks:[graph-symbol-table]): Discover and index repository source files respecting .gitignore.
// Description: Implement repository-wide source file scanner discovering all Python and Lua files while filtering test/build/vendor noise.
// Criterion: review: "scripts/bundle_ir.py --scan-repo returns complete list of tracked source files"
// [lexlean:declarations]
// {"body":{"arguments":[],"function":{"name":"lgcc_dep_bundle"},"kind":"call"},"kind":"definition","name":"graph_repo_scanner","parameters":[],"result":{"kind":"bool"}}
// [lexlean:theorems]
// {"kind":"theorem","name":"graph_repo_scanner_verified","parameters":[],"proof":{"kind":"reflexivity"},"statement":{"kind":"eq","left":{"arguments":[],"function":{"name":"graph_repo_scanner"},"kind":"call"},"right":{"kind":"bool","value":true}}}

// SPEC(graph-symbol-table | tier:B | depends_on:[graph-repo-scanner] | blocks:[graph-edge-extractor]): Construct global symbol table mapping fully-qualified names to signatures and locations.
// Description: Build repository symbol table V mapping module.Class.method and module.func FQNs to file locations, line ranges, and signatures.
// Criterion: review: "Symbol table indexes all top-level classes, methods, and functions with unique FQNs"
// [lexlean:declarations]
// {"body":{"arguments":[],"function":{"name":"graph_repo_scanner"},"kind":"call"},"kind":"definition","name":"graph_symbol_table","parameters":[],"result":{"kind":"bool"}}
// [lexlean:theorems]
// {"kind":"theorem","name":"graph_symbol_table_verified","parameters":[],"proof":{"kind":"reflexivity"},"statement":{"kind":"eq","left":{"arguments":[],"function":{"name":"graph_symbol_table"},"kind":"call"},"right":{"kind":"bool","value":true}}}

struct GlobalSymbol {
    std::string fqn; // Fully-qualified name (e.g., "lgcc.lint" or "tasks.setup")
    fs::path filepath;
    int line = 0;
    int end_line = 0;
    std::string signature;
    std::string kind;
    int token_cost = 0;
    std::vector<std::string> calls;
};

class SymbolTable {
public:
    static std::vector<fs::path> scan_repository(const fs::path& repo_root);
    static std::unordered_map<std::string, GlobalSymbol> build_table(const fs::path& repo_root);

    static std::string to_json(const std::unordered_map<std::string, GlobalSymbol>& table);
};

} // namespace lgcc
