-- ==============================================================================
-- lgcc.lua: Neovim Integration for Logical Compiler (lgcc)
-- Asynchronously runs lgcc and populates native vim.diagnostic
-- ==============================================================================

local M = {}

local lgcc_ns = vim.api.nvim_create_namespace("lgcc")

-- Configure namespace-specific display (underlines + virtual text + signs)
-- even if global LSP diagnostics are disabled.
vim.diagnostic.config({
  signs = true,
  underline = true,
  virtual_text = {
    prefix = "▲ ",
    spacing = 2,
  },
  severity_sort = true,
}, lgcc_ns)

local active_jobs = {}

--- Run lgcc on the given or current buffer
--- @param opts? { spec?: string, web?: boolean, bufnr?: number }
function M.lint(opts)
  opts = opts or {}
  local bufnr = opts.bufnr or vim.api.nvim_get_current_buf()
  local filepath = vim.api.nvim_buf_get_name(bufnr)

  if filepath == "" or not vim.uv.fs_stat(filepath) then
    vim.notify("lgcc: Buffer must be saved to a file first", vim.log.levels.WARN)
    return
  end

  -- Cancel existing job for this buffer if still running
  if active_jobs[bufnr] then
    pcall(function() active_jobs[bufnr]:kill(9) end)
    active_jobs[bufnr] = nil
  end

  -- Auto-save if buffer is modified
  if vim.api.nvim_get_option_value("modified", { buf = bufnr }) then
    vim.cmd("silent write")
  end

  local cmd = { "lgcc" }
  if opts.spec and opts.spec ~= "" then
    table.insert(cmd, "-s")
    table.insert(cmd, vim.fn.expand(opts.spec))
  end
  if opts.web then
    table.insert(cmd, "-w")
  end
  table.insert(cmd, filepath)

  vim.notify("lgcc: compiling logic for " .. vim.fn.fnamemodify(filepath, ":t") .. "...", vim.log.levels.INFO)

  local stdout_chunks = {}
  local stderr_chunks = {}

  local job = vim.system(cmd, {
    text = true,
    stdout = function(_, data)
      if data then table.insert(stdout_chunks, data) end
    end,
    stderr = function(_, data)
      if data then table.insert(stderr_chunks, data) end
    end,
  }, function(res)
    active_jobs[bufnr] = nil

    vim.schedule(function()
      if not vim.api.nvim_buf_is_valid(bufnr) then
        return
      end

      local out = table.concat(stdout_chunks, "")
      local diagnostics = {}

      for line in out:gmatch("[^\r\n]+") do
        -- Pattern: <file>:<line>:<col>: error: <msg>
        local file, lnum, col, msg = line:match("^([^:]+):(%d+):(%d*):%s*error:%s*(.+)$")
        if not lnum then
          -- Fallback: <file>:<line>: error: <msg>
          file, lnum, msg = line:match("^([^:]+):(%d+):%s*error:%s*(.+)$")
          col = "1"
        end

        if lnum and msg then
          table.insert(diagnostics, {
            bufnr = bufnr,
            lnum = math.max(0, tonumber(lnum) - 1),
            col = math.max(0, (tonumber(col) or 1) - 1),
            severity = vim.diagnostic.severity.ERROR,
            message = msg,
            source = "lgcc",
          })
        end
      end

      -- Populate vim.diagnostic for this buffer
      vim.diagnostic.set(lgcc_ns, bufnr, diagnostics)

      if #diagnostics > 0 then
        vim.notify(string.format("lgcc: %d logical error(s) detected", #diagnostics), vim.log.levels.ERROR)
      else
        vim.notify("lgcc: clean (no logical errors found)", vim.log.levels.INFO)
      end
    end)
  end)

  active_jobs[bufnr] = job
end

--- Clear lgcc diagnostics for the current buffer
function M.clear(bufnr)
  bufnr = bufnr or vim.api.nvim_get_current_buf()
  vim.diagnostic.reset(lgcc_ns, bufnr)
  vim.notify("lgcc: diagnostics cleared", vim.log.levels.INFO)
end

-- Unified :lgcc / :Lgcc Command
vim.api.nvim_create_user_command("Lgcc", function(ctx)
  local fargs = ctx.fargs or {}
  local sub = fargs[1]

  if not sub or sub == "" then
    M.lint()
  elseif sub == "clear" then
    M.clear()
  elseif sub == "web" then
    M.lint({ web = true })
  elseif sub == "spec" then
    local spec_path = fargs[2]
    if not spec_path or spec_path == "" then
      vim.notify("lgcc: missing specification file argument (usage: :Lgcc spec <file>)", vim.log.levels.ERROR)
      return
    end
    M.lint({ spec = spec_path })
  else
    vim.notify("lgcc: unknown subcommand '" .. sub .. "'. Valid options: clear, web, spec <file>", vim.log.levels.WARN)
  end
end, {
  nargs = "*",
  complete = function(arglead, cmdline)
    local parts = vim.split(vim.trim(cmdline), "%s+")
    if #parts <= 2 and not cmdline:match("%s+$") then
      local subcmds = { "clear", "web", "spec" }
      return vim.tbl_filter(function(item)
        return vim.startswith(item, arglead)
      end, subcmds)
    elseif parts[2] == "spec" then
      return vim.fn.getcompletion(arglead, "file")
    end
    return {}
  end,
  desc = "Run logical compiler (lgcc) on current buffer",
})

return M
