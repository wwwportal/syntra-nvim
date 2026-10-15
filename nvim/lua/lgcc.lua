-- ==============================================================================
-- lgcc.lua: Native Neovim Logical Compiler & Invariant Verifier
-- Tiered execution: Uses Antigravity (agy) until quota/limits run out,
-- allows agy to orchestrate opencode worker subagents, and automatically
-- fails over to opencode (OpenRouter) on quota exhaustion.
-- Streams intermediate reasoning tokens live to the command-line ticker
-- and routes verified errors to Quickfix on demand.
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

--- Speak textual summary using piper TTS asynchronously
--- @param text string
local function speak_summary(text)
  if not text or text == "" then return end
  local piper_bin = vim.fn.expand("~/myenv/bin/piper")
  local model_path = vim.fn.expand("~/code/agent/vendor/voices/en_US-kristin-medium.onnx")
  if vim.fn.executable(piper_bin) ~= 1 or not vim.uv.fs_stat(model_path) then
    return
  end
  local text_path = "/tmp/lgcc_speech_text.txt"
  local wav_path = "/tmp/lgcc_speech.wav"
  vim.fn.writefile({ text }, text_path)
  local cmd = string.format(
    "%s -m %s -i %s -f %s --sentence-silence 0.2 2>/dev/null && aplay -q %s 2>/dev/null",
    piper_bin, model_path, text_path, wav_path, wav_path
  )
  vim.fn.jobstart({ "sh", "-c", cmd })
end

--- Start the animated status ticker in the command line
--- @param filename string
--- @param engine? string "agy" | "opencode" | "local"
local function start_ticker(filename, engine)
  engine = engine or "agy"
  if ticker_timer then
    pcall(function() ticker_timer:stop(); ticker_timer:close() end)
    ticker_timer = nil
  end

  local spinner = { "⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏" }
  local frame = 1
  local state = {
    engine = engine,
    raw_tail = "starting...",
  }

  ticker_timer = vim.uv.new_timer()
  ticker_timer:start(0, 100, vim.schedule_wrap(function()
    if not ticker_timer then return end
    frame = (frame % #spinner) + 1
    local spin = spinner[frame]
    local tail = state.raw_tail or "analyzing..."
    local msg = string.format("%s [lgcc:%s] %s: %s", spin, state.engine, filename, tail)
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

--- Parse LLM diagnostic output into Quickfix items
--- @param raw string
--- @param filepath string
local function parse_diagnostics(raw, filepath)
  local qf_items = {}
  for line in raw:gmatch("[^\r\n]+") do
    -- Standard format: <file>:<line>:<col>: error: <msg> or <file>:<line>: error: <msg>
    local file, lnum, col, msg = line:match("^([^:]+):(%d+):(%d*):%s*error:%s*(.+)$")
    if not lnum then
      file, lnum, msg = line:match("^([^:]+):(%d+):%s*error:%s*(.+)$")
    end

    -- Alt format: [line N] msg or Line N: msg
    if not lnum then
      local lnum_alt, msg_alt = line:match("^%[?line%s*(%d+)%]?%s*:?%s*(.+)$")
      if lnum_alt then
        file = filepath
        lnum = lnum_alt
        msg = msg_alt
      end
    end

    if lnum and msg then
      table.insert(qf_items, {
        filename = (file and file ~= "") and file or filepath,
        lnum = tonumber(lnum),
        col = tonumber(col) or 1,
        text = vim.trim(msg),
        type = "E",
      })
    end
  end
  return qf_items
end

--- Run native logical compiler on target file or active buffer
--- @param opts? { file?: string, spec?: string, engine?: string, speak?: boolean, bufnr?: number }
function M.lint(opts)
  opts = opts or {}
  local bufnr = opts.bufnr or vim.api.nvim_get_current_buf()
  local filepath = (opts.file and opts.file ~= "") and vim.fn.expand(opts.file) or vim.api.nvim_buf_get_name(bufnr)

  if filepath == "" or not vim.uv.fs_stat(filepath) then
    vim.notify("lgcc: Target file not found or buffer not saved: " .. (filepath ~= "" and filepath or "[unnamed]"), vim.log.levels.WARN)
    return
  end

  -- Cancel existing job if running
  if active_job then
    pcall(function() active_job:kill(9) end)
    active_job = nil
  end

  -- Auto-save if buffer is modified
  if bufnr and vim.api.nvim_buf_is_valid(bufnr) and vim.api.nvim_get_option_value("modified", { buf = bufnr }) then
    vim.cmd("silent write")
  end

  -- Read file and prefix line numbers
  local lines = vim.fn.readfile(filepath)
  local numbered_lines = {}
  for i, line in ipairs(lines) do
    table.insert(numbered_lines, string.format("%d: %s", i, line))
  end
  local numbered_src = table.concat(numbered_lines, "\n")

  local file_basename = vim.fn.fnamemodify(filepath, ":t")

  -- Build prompt
  local spec_section = ""
  if opts.spec and opts.spec ~= "" then
    local spec_path = vim.fn.expand(opts.spec)
    if vim.fn.filereadable(spec_path) == 1 then
      spec_section = string.format("--- SPECIFICATION DOCUMENT ---\n%s\n\n", table.concat(vim.fn.readfile(spec_path), "\n"))
    end
  end

  local system_prompt = [[You are the Logical Compiler (lgcc).
Analyze the provided source code for semantic bugs, invariant violations, and logical flaws.
Ignore syntax, formatting, and naming conventions.
If you need heavy dependency context or external code exploration, you may run `opencode run "<query>"` to delegate sub-worker analysis.
If flaws exist, output each flaw strictly on its own line in this format:
<file>:<line>: error: <clear description of flaw>
If there are no logic errors, output: CLEAN]]

  local full_prompt = string.format("%s\n\n%s--- SOURCE CODE (prefixed with line numbers) ---\n%s\n\nFile: %s",
    system_prompt, spec_section, numbered_src, filepath)

  local initial_engine = opts.engine or "agy"
  local state = start_ticker(file_basename, initial_engine)

  local function execute_run(engine_name)
    state.engine = engine_name
    state.raw_tail = "dispatching to " .. engine_name .. "..."

    local cmd = {}
    if engine_name == "agy" then
      cmd = { "agy", "-p", full_prompt, "--dangerously-skip-permissions", "--output-format", "text" }
    elseif engine_name == "opencode" then
      cmd = { "opencode", "run", full_prompt }
    else
      -- Local fallback
      local model = vim.fn.expand("~/code/agent/vendor/models/qwen2.5-coder-7b/qwen2.5-coder-7b-instruct-q6_k.gguf")
      cmd = { "llama-cli", "-m", model, "-ngl", "12", "-t", "8", "-c", "8192", "--temp", "0.0", "-p", full_prompt }
    end

    local stdout_chunks = {}
    local raw_stream = ""

    active_job = vim.system(cmd, {
      text = true,
      stdout = function(_, data)
        if not data then return end
        table.insert(stdout_chunks, data)
        raw_stream = raw_stream .. data

        local clean = raw_stream:gsub("\27%[[0-9;]*[a-zA-Z]", ""):gsub("[\r\n]+", " "):gsub("%s+", " "):gsub("^%s+", "")
        if #clean > 55 then
          state.raw_tail = "..." .. clean:sub(-52)
        elseif #clean > 0 then
          state.raw_tail = clean
        end
      end,
      stderr = function(_, data)
        if not data then return end
        raw_stream = raw_stream .. data
        local clean = raw_stream:gsub("\27%[[0-9;]*[a-zA-Z]", ""):gsub("[\r\n]+", " "):gsub("%s+", " "):gsub("^%s+", "")
        if #clean > 55 then
          state.raw_tail = "..." .. clean:sub(-52)
        elseif #clean > 0 then
          state.raw_tail = clean
        end
      end,
    }, function(res)
      active_job = nil

      local out = table.concat(stdout_chunks, "")

      -- Failover condition: If agy failed (exit non-zero, quota exceeded, or empty output)
      local is_quota_error = out:match("RESOURCE_EXHAUSTED") or out:match("quota") or out:match("429")
      if engine_name == "agy" and (res.code ~= 0 or is_quota_error or vim.trim(out) == "") then
        vim.schedule(function()
          vim.notify("lgcc: agy quota/execution limit reached, failing over to opencode...", vim.log.levels.WARN)
          execute_run("opencode")
        end)
        return
      end

      vim.schedule(function()
        stop_ticker()

        local qf_items = parse_diagnostics(out, filepath)

        if #qf_items > 0 then
          vim.fn.setqflist(qf_items, "r")
          vim.fn.setqflist({}, "a", { title = string.format("lgcc [%s]: %s (%d logical errors)", engine_name, file_basename, #qf_items) })
          vim.cmd("botright copen 8")
          vim.api.nvim_echo({
            { string.format("✕ [lgcc:%s] %d logical error(s) detected (see bottom split)", engine_name, #qf_items), "DiagnosticError" },
          }, true, {})

          if opts.speak then
            local summary = string.format("Logical compiler found %d error%s in %s. First error at line %d: %s",
              #qf_items, #qf_items == 1 and "" or "s", file_basename, qf_items[1].lnum, qf_items[1].text)
            speak_summary(summary)
          end
        else
          pcall(vim.cmd, "cclose")
          vim.fn.setqflist({}, "r")
          vim.api.nvim_echo({
            { string.format("✓ [lgcc:%s] clean (no logical errors found in %s)", engine_name, file_basename), "DiagnosticOk" },
          }, true, {})

          if opts.speak then
            speak_summary("Logical compiler check clean. No logical errors found in " .. file_basename)
          end
        end
      end)
    end)
  end

  execute_run(initial_engine)
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

-- Unified :Lgcc Command
vim.api.nvim_create_user_command("Lgcc", function(ctx)
  local fargs = ctx.fargs or {}
  local speak = false
  local spec_path = nil
  local engine = "agy" -- default to agy, with automatic opencode failover
  local target_file = nil
  local i = 1

  while i <= #fargs do
    local arg = fargs[i]
    if arg == "-s" or arg == "--speak" or arg == "speak" then
      speak = true
    elseif arg == "--opencode" or arg == "opencode" then
      engine = "opencode"
    elseif arg == "--agy" or arg == "agy" then
      engine = "agy"
    elseif arg == "-l" or arg == "--local" or arg == "local" then
      engine = "local"
    elseif arg == "clear" then
      M.clear()
      return
    elseif arg == "--spec" or arg == "spec" then
      i = i + 1
      spec_path = fargs[i]
    elseif not target_file and arg ~= "" and not arg:match("^-") then
      target_file = arg
    end
    i = i + 1
  end

  M.lint({
    file = target_file,
    spec = spec_path,
    engine = engine,
    speak = speak,
  })
end, {
  nargs = "*",
  complete = function(arglead, cmdline)
    local parts = vim.split(vim.trim(cmdline), "%s+")
    if #parts <= 2 and not cmdline:match("%s+$") then
      local subcmds = { "clear", "-s", "--speak", "spec", "--opencode", "--agy", "-l", "--local" }
      return vim.tbl_filter(function(item)
        return vim.startswith(item, arglead)
      end, subcmds)
    elseif parts[#parts - 1] == "spec" or parts[#parts - 1] == "--spec" then
      return vim.fn.getcompletion(arglead, "file")
    else
      return vim.fn.getcompletion(arglead, "file")
    end
  end,
  desc = "Run logical compiler (lgcc) with live status ticker, Quickfix results, and agy/opencode failover; use -s for voice summary",
})

return M
