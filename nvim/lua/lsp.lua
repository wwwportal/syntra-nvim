-- ==============================================================================
-- lsp.lua: Language Server Buffer Configuration
-- Provides keybindings and buffer hooks when LSP attaches
-- ==============================================================================

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
