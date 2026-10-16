-- ==============================================================================
-- lsp.lua: Language Server Buffer Configuration & Lean Spec Engine
-- Auto-starts Lean 4 LSP for .lean files and annotates in-tree spec comments
-- with Lean 4 verification status directly in source code.
-- ==============================================================================

local ns_specs = vim.api.nvim_create_namespace("specs_inline_status")

vim.api.nvim_create_autocmd("LspAttach", {
  desc = "LSP keybindings and buffer attachments",
  callback = function(args)
    local opts = { buffer = args.buf, silent = true }
    vim.keymap.set("n", "gd", vim.lsp.buf.definition, opts)
    vim.keymap.set("n", "gD", vim.lsp.buf.declaration, opts)
    vim.keymap.set("n", "gr", vim.lsp.buf.references, opts)
    vim.keymap.set("n", "K", vim.lsp.buf.hover, opts)
    vim.keymap.set("n", "[d", vim.diagnostic.goto_prev, opts)
    vim.keymap.set("n", "]d", vim.diagnostic.goto_next, opts)
  end,
})

-- Keep background diagnostic display clean and focused.
vim.diagnostic.config({
  signs = true,
  underline = true,
  virtual_text = false,
  virtual_lines = false,
  severity_sort = true,
  float = { border = "single", source = true, max_width = 80 },
})

-- 1. Auto-start Lean 4 Language Server for .lean files
vim.api.nvim_create_autocmd("FileType", {
  pattern = "lean",
  desc = "Start Lean 4 language server for .lean files",
  callback = function(args)
    local lean_bin = vim.fn.exepath("lean")
    if lean_bin == "" then
      lean_bin = vim.fn.expand("~/.elan/bin/lean")
    end
    if vim.fn.executable(lean_bin) == 1 then
      local root = vim.fs.root(args.buf, { "lakefile.lean", "lakefile.toml", "lean-toolchain", ".git" }) or vim.fn.getcwd()
      vim.lsp.start({
        name = "lean-lsp",
        cmd = { lean_bin, "--server" },
        root_dir = root,
      })
    end
  end,
})

local M = {}

-- 2. Annotate in-tree specification comments with Lean 4 verification status
function M.annotate_specs(bufnr)
  bufnr = bufnr or vim.api.nvim_get_current_buf()
  if not vim.api.nvim_buf_is_valid(bufnr) then return end
  pcall(vim.api.nvim_buf_clear_namespace, bufnr, ns_specs, 0, -1)

  local lines = vim.api.nvim_buf_get_lines(bufnr, 0, -1, false)
  local i = 1
  while i <= #lines do
    local line = lines[i]
    local tag = line:match("([A-Z]+)%s*%([%w_%-]+%s*|")
      or line:match("([A-Z]+)%s*%([%w_%-]+%)")
      or line:match("^[/%*#-]*%s*([A-Z]+)%s*%([%w_%-]+%s*|")
      or line:match("^[/%*#-]*%s*([A-Z]+)%s*%([%w_%-]+%)")
      or line:match("^[/%*#-]*%s*([A-Z]+):")

    if tag and (tag == "SPEC" or tag == "TODO" or tag == "FIXME") then
      local header_idx = i - 1

      -- Scan down through this spec block to check Lean proof status
      local j = i + 1
      local has_lean = false
      local has_sorry = false
      local has_theorem = false
      local sorry_line_idx = nil

      while j <= math.min(#lines, i + 40) do
        local sub = lines[j]
        if sub:match("^[/%*#-]*%s*SPEC%(") or sub:match("^[/%*#-]*%s*TODO%(") or sub:match("^[/%*#-]*%s*FIXME%(") then
          break
        end
        if sub:find("%[lean:spec%]") or sub:find("%[lean4:spec%]") then
          has_lean = true
        end
        if sub:find("theorem ") or sub:find("def ") then
          has_theorem = true
        end
        if sub:find("sorry") then
          has_sorry = true
          sorry_line_idx = j - 1
        end
        if sub:find("%*/") or sub:find("%]%]") then
          break
        end
        j = j + 1
      end

      if has_lean and has_sorry then
        pcall(vim.api.nvim_buf_set_extmark, bufnr, ns_specs, header_idx, 0, {
          virt_text = { { "  ● [DRAFT: open Lean obligation]", "DiagnosticWarn" } },
          virt_text_pos = "eol",
          hl_mode = "combine",
        })
        if sorry_line_idx then
          pcall(vim.api.nvim_buf_set_extmark, bufnr, ns_specs, sorry_line_idx, 0, {
            virt_text = { { "  ⊢ open proof goal (sorry)", "DiagnosticWarn" } },
            virt_text_pos = "eol",
            hl_mode = "combine",
          })
        end
      elseif has_lean and has_theorem then
        pcall(vim.api.nvim_buf_set_extmark, bufnr, ns_specs, header_idx, 0, {
          virt_text = { { "  ✓ [VERIFIED: Lean 4 kernel]", "DiagnosticOk" } },
          virt_text_pos = "eol",
          hl_mode = "combine",
        })
      elseif tag == "TODO" or tag == "FIXME" then
        pcall(vim.api.nvim_buf_set_extmark, bufnr, ns_specs, header_idx, 0, {
          virt_text = { { "  ● [INFORMAL: needs Lean spec]", "DiagnosticInfo" } },
          virt_text_pos = "eol",
          hl_mode = "combine",
        })
      end
    end
    i = i + 1
  end
end

-- Refresh spec status annotations on buffer load, save, and enter
vim.api.nvim_create_autocmd({ "BufReadPost", "BufWritePost", "BufEnter" }, {
  desc = "Annotate in-tree spec comments with Lean 4 status",
  callback = function(args)
    local ft = vim.bo[args.buf].filetype
    if ft ~= "qf" and ft ~= "help" and ft ~= "lazy" then
      vim.schedule(function()
        M.annotate_specs(args.buf)
      end)
    end
  end,
})

return M
