/* Generated C ABI header for syntra-spec-bridge (prod_codegen::CBindings + LexLean FFI verifier) */
#ifndef SYNTRA_SPEC_BRIDGE_H
#define SYNTRA_SPEC_BRIDGE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Boolean arguments and results use uint8_t: 0 is false, nonzero is true. */
typedef int32_t prod_status_t;
enum {
    PROD_STATUS_OK = 0,
    PROD_STATUS_ADD_OVERFLOW = 1,
    PROD_STATUS_MUL_OVERFLOW = 2,
    PROD_STATUS_SHIFT_OVERFLOW = 3,
    PROD_STATUS_POW_OVERFLOW = 4,
    PROD_STATUS_SHIFT_EXPONENT_TOO_LARGE = 5,
    PROD_STATUS_POW_EXPONENT_TOO_LARGE = 6,
    PROD_STATUS_OUTPUT_TOO_SMALL = 7
};

/* Static prod_codegen::CBindings contract entry points */
uint8_t prod_syntra_spec_tasks_add_spec_scaffold(void);
uint8_t prod_syntra_spec_lgcc_spec_scanner(void);
uint8_t prod_syntra_spec_lgcc_topo_spec_stitcher(void);
uint8_t prod_syntra_spec_prismpm_jit_verifier(void);
uint8_t prod_syntra_spec_prismpm_rust_bridge(void);
uint8_t prod_syntra_spec_lgcc_lexlean_verifier(void);

/* Dynamic LexLean semantic module -> prod_ir / prod_codegen::CBindings FFI verifier */
int32_t syntra_ffi_verify_semantic_module(
    const char* semantic_json,
    uint32_t* out_defs_checked,
    uint32_t* out_theorems_checked,
    char* err_buf,
    size_t err_cap
);

/* Generate C header and Rust extern "C" bindings from stitched LexLean semantic JSON */
int32_t syntra_ffi_generate_c_abi(
    const char* module_name,
    const char* semantic_json,
    char* out_header,
    size_t header_cap,
    char* out_rust,
    size_t rust_cap,
    char* err_buf,
    size_t err_cap
);

#ifdef __cplusplus
}
#endif

#endif /* SYNTRA_SPEC_BRIDGE_H */
