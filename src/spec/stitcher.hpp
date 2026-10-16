#pragma once

#include "../common.hpp"

namespace lgcc {

struct SpecFragment {
    std::string id;
    std::string tier; // "A", "B", "C"
    std::vector<std::string> blocks;
    std::vector<std::string> depends_on;
    std::string description;
    std::string criterion;
    std::string declarations_json;
    std::string theorems_json;
    fs::path source_file;
    int line = 0;
};

struct StitchResult {
    bool success = true;
    std::string error;
    std::vector<SpecFragment> fragments;
    std::vector<std::string> topo_order;
    std::string stitched_lex_tex;
    std::string stitched_semantic_json;
};

struct VerifyResult {
    bool success = false;
    std::string error;
    std::string semantic_id;
    std::string attestation;
    int modules_verified = 0;
    std::string output;
    std::string stitched_file;
};

class SpecStitcher {
public:
    static std::vector<SpecFragment> scan_file(const fs::path& filepath);
    static std::vector<SpecFragment> scan_repository(const fs::path& repo_root);
    static StitchResult stitch(const std::vector<SpecFragment>& fragments, const std::string& module_name = "Syntra.StitchedSpec");
    static VerifyResult verify_specs(const fs::path& repo_root, const std::string& module_name = "Foundation.Specification.V1.JITSpec", const fs::path& specs_dir = "", bool keep_stitched = false);
};

} // namespace lgcc

