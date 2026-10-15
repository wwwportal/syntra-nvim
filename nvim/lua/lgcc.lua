-- ==============================================================================
-- lgcc.lua: Neovim Integration for Logical Compiler (lgcc)
-- Asynchronously runs lgcc with live status ticker and bottom-split error list.
-- No intrusive pop-ups, no in-line virtual text distractions.
-- ==============================================================================

local M = {}

local active_job = nil
local ticker_timer = nil

-- Close quickfix window easily with 'q' or '<Esc>'
vim.api.nvim_create_autocmd("FileType", {
  pattern = "qf",
  callback = function(ev)
    vim.keymap.set("n", "q", "<Cmd>close<CR>", { buffer = ev.buf, silent = true, nowait = true })
    vim.keymap.set("n", "<Esc>", "<Cmd>close<CR>", { buffer = ev.buf, silent = true, nowait = true })
  end,
})

--- Start the animated status ticker in the command line
local function start_ticker(filename)
  if ticker_timer then
    pcall(function() ticker_timer:stop(); ticker_timer:close() end)
    ticker_timer = nil
  end

  local spinner = { "⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏" }
  local frame = 1
  local state = {
    token_count = 0,
    snippet = "analyzing logic...",
  }

  ticker_timer = vim.uv.new_timer()
  ticker_timer:start(0, 100, vim.schedule_wrap(function()
    if not ticker_timer then return end
    frame = (frame % #spinner) + 1
    local spin = spinner[frame]
    local msg = string.format("%s [lgcc] %s %s", spin, filename, state.snippet)
    if state.token_count > 0 then
      msg = msg .. string.format(" (%d tokens)", state.token_count)
    end
    vim.api.nvim_echo({ { msg, "ModeMsg" } }, false, {})
  end))

  return state
end

--- Stop the animated status ticker
local function stop_ticker()
  if ticker_timer then
    pcall(function() ticker_timer:stop(); ticker_timer:close() end)
    ticker_timer = nil
  end
  vim.api.nvim_echo({ { "", "" } }, false, {})
end

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

  -- Cancel existing job if running
  if active_job then
    pcall(function() active_job:kill(9) end)
    active_job = nil
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

  local file_basename = vim.fn.fnamemodify(filepath, ":t")
  local state = start_ticker(file_basename)

  local stdout_chunks = {}

  active_job = vim.system(cmd, {
    text = true,
    stdout = function(_, data)
      if data then table.insert(stdout_chunks, data) end
    end,
    stderr = function(_, data)
      if not data then return end
      -- Update live status ticker state from streamed tokens/reasoning
      local words = vim.split(data, "%s+", { trimempty = true })
      state.token_count = state.token_count + #words

      -- Clean up snippet for statusline display
      local clean = data:gsub("<thinking>", ""):gsub("</thinking>", ""):gsub("[\r\n]+", " "):gsub("^%s+", "")
      if clean ~= "" then
        if #clean > 45 then
          clean = "..." .. clean:sub(-42)
        end
        state.snippet = clean
      end
    end,
  }, function(res)
    active_job = nil

    vim.schedule(function()
      stop_ticker()

      if not vim.api.nvim_buf_is_valid(bufnr) then
        return
      end

      local out = table.concat(stdout_chunks, "")
      local qf_items = {}

      for line in out:gmatch("[^\r\n]+") do
        -- Format: <file>:<line>:<col>: error: <msg>
        local file, lnum, col, msg = line:match("^([^:]+):(%d+):(%d*):%s*error:%s*(.+)$")
        if not lnum then
          file, lnum, msg = line:match("^([^:]+):(%d+):%s*error:%s*(.+)$")
          col = "1"
        end

        if lnum and msg then
          table.insert(qf_items, {
            filename = file,
            lnum = tonumber(lnum),
            col = tonumber(col) or 1,
            text = msg,
            type = "E",
          })
        end
      end

      if #qf_items > 0 then
        -- Open bottom split with errors (zero in-line buffer pollution)
        vim.fn.setqflist(qf_items, "r")
        vim.fn.setqflist({}, "a", { title = "lgcc: " .. file_basename .. " (" .. #qf_items .. " logical errors)" })
        vim.cmd("botright copen 8")
        vim.api.nvim_echo({
          { string.format("✕ [lgcc] %d logical error(s) detected (see bottom split)", #qf_items), "DiagnosticError" },
        }, true, {})
      else
        -- Clean: close quickfix if open, echo success
        pcall(vim.cmd, "cclose")
        vim.fn.setqflist({}, "r")
        vim.api.nvim_echo({
          { "✓ [lgcc] clean (no logical errors found)", "DiagnosticOk" },
        }, true, {})
      end
    end)
  end)
end

--- Clear lgcc results and close bottom split
function M.clear()
  if active_job then
    pcall(function() active_job:kill(9) end)
    active_job = nil
  end
  stop_ticker()
  pcall(vim.cmd, "cclose")
  vim.fn.setqflist({}, "r")
  vim.api.nvim_echo({ { "lgcc: cleared", "Normal" } }, false, {})
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
  desc = "Run logical compiler (lgcc) on current buffer with status ticker and bottom split",
})

return M
