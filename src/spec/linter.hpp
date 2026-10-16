#pragma once

#include "../common.hpp"

namespace lgcc {

// SPEC(lgcc-native-linter | tier:A | depends_on:[spec-jit-patch-schema] | blocks:[tasks-add-spec-scaffold]): Rewrite scripts/lint_tasks.py into native C++20 inside lgcc.
// Description: Rewrite scripts/lint_tasks.py into native C++20 inside lgcc, implementing high-performance task DAG validation, anti-erasure git checks, lazy-TODO detection, and JSON export.
// Criterion: assert: "lgcc --lint-tasks validates repository DAG and anti-erasure constraints in <15ms with full feature parity to Python linter"
// [lexlean:declarations]
// {"body":{"arguments":[],"function":{"name":"spec_jit_patch_schema"},"kind":"call"},"kind":"definition","name":"lgcc_native_linter","parameters":[],"result":{"kind":"bool"}}
// [lexlean:theorems]
// {"kind":"theorem","name":"lgcc_native_linter_verified","parameters":[],"proof":{"kind":"reflexivity"},"statement":{"kind":"eq","left":{"arguments":[],"function":{"name":"lgcc_native_linter"},"kind":"call"},"right":{"kind":"bool","value":true}}}


struct TaskRecord {
    std::string id;
    std::string kind; // "TODO", "FIXME", "SPEC"
    std::string tier; // "A", "B", "C"
    std::vector<std::string> blocks;
    std::vector<std::string> depends_on;
    std::string description;
    std::string criterion;
    fs::path file;
    int line = 0;
};

struct LintResult {
    std::vector<TaskRecord> tasks;
    std::vector<std::string> errors;
    fs::path root;
    uint32_t ffi_defs_verified = 0;
    uint32_t ffi_theorems_verified = 0;
};


class TaskLinter {
public:
    static LintResult lint_repository(const fs::path& repo_root);
    static std::string to_json(const LintResult& res);
    static void print_report(const LintResult& res);

private:
    static void scan_file(const fs::path& filepath, const fs::path& repo_root, std::vector<TaskRecord>& tasks, std::vector<std::string>& errors);
    static std::vector<std::string> validate_dag(const std::vector<TaskRecord>& tasks);
    static std::vector<std::string> check_staged_resolutions(const fs::path& repo_root, const std::vector<TaskRecord>& active_tasks);
};

} // namespace lgcc
