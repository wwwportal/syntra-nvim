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
local ns_lgcc = vim.api.nvim_create_namespace("lgcc_diagnostics")

-- Setup custom highlight groups
vim.api.nvim_set_hl(0, "LgccStreamGray", { fg = "#7e8294", italic = true, default = true })
vim.api.nvim_set_hl(0, "LgccSpinner", { fg = "#7aa2f7", bold = true, default = true })
vim.api.nvim_set_hl(0, "LgccInlineError", { fg = "#f7768e", bold = true, default = true })

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

  local function render()
    frame = (frame % #spinner) + 1
    local spin = spinner[frame]
    local tail = state.raw_tail or "analyzing..."
    local prefix = string.format("%s [lgcc:%s] %s: ", spin, state.engine, filename)
    local cols = vim.o.columns or 80
    local max_tail = math.max(15, cols - #prefix - 4)
    local display_tail = tail
    if #display_tail > max_tail then
      display_tail = "..." .. display_tail:sub(#display_tail - max_tail + 4)
    end
    vim.api.nvim_echo({
      { prefix, "LgccSpinner" },
      { display_tail, "LgccStreamGray" },
    }, false, {})
  end

  state.render = render

  ticker_timer = vim.uv.new_timer()
  ticker_timer:start(0, 80, vim.schedule_wrap(function()
    if not ticker_timer then return end
    render()
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

--- Locate native lgcc Rust binary
local function find_lgcc_bin()
  local exepath = vim.fn.exepath("lgcc")
  if exepath ~= "" and vim.fn.executable(exepath) == 1 then
    return exepath
  end
  local candidates = {
    vim.fn.expand("~/code/syn-nvim/bin/lgcc"),
    vim.fn.expand("~/code/syntra-nvim/bin/lgcc"),
    vim.fn.expand("~/.local/bin/lgcc"),
    vim.fn.expand("~/.config/nvim/bin/lgcc"),
    "/usr/local/bin/lgcc",
    "/usr/bin/lgcc",
  }
  for _, p in ipairs(candidates) do
    if vim.fn.executable(p) == 1 or vim.uv.fs_stat(p) then return p end
  end
  return nil
end

--- Known model context windows and budget profiles
local ENGINE_BUDGET_PROFILES = {
  ["local"] = {
    max_context = 8192,
    headroom = 2048, -- reserve for model reasoning/generation output
    default_dep_budget = 2048,
  },
  ["opencode"] = {
    max_context = 64000,
    headroom = 8000,
    default_dep_budget = 16000,
  },
  ["agy"] = {
    max_context = 128000,
    headroom = 16000,
    default_dep_budget = 32000,
  },
}

--- Calculate optimal dependency context budget dynamically based on model window
local function get_automatic_budget(engine, target_file_chars, user_budget)
  if user_budget and user_budget > 0 then
    return user_budget
  end
  local prof = ENGINE_BUDGET_PROFILES[engine] or ENGINE_BUDGET_PROFILES["agy"]
  local target_tokens = math.max(1, math.floor(target_file_chars / 4))
  local available = prof.max_context - target_tokens - prof.headroom
  if available < 500 then
    return 500
  end
  return math.min(available, prof.default_dep_budget)
end

--- Run native logical compiler on target file, directory, or active buffer
--- @param opts? { file?: string, spec?: string, engine?: string, speak?: boolean, bundle?: boolean, budget?: number, bufnr?: number }
function M.lint(opts)
  opts = opts or {}
  local bufnr = opts.bufnr or vim.api.nvim_get_current_buf()
  local filepath = (opts.file and opts.file ~= "") and vim.fn.expand(opts.file) or vim.api.nvim_buf_get_name(bufnr)

  local stat = (filepath ~= "") and vim.uv.fs_stat(filepath) or nil
  if filepath == "" or not stat then
    vim.notify("lgcc: Target path not found or buffer not saved: " .. (filepath ~= "" and filepath or "[unnamed]"), vim.log.levels.WARN)
    return
  end
  local is_dir = (stat.type == "directory")

  -- Cancel existing job if running
  if active_job then
    pcall(function() active_job:kill(9) end)
    active_job = nil
  end

  -- Auto-save if buffer is modified
  if not is_dir and bufnr and vim.api.nvim_buf_is_valid(bufnr) and vim.api.nvim_get_option_value("modified", { buf = bufnr }) then
    vim.cmd("silent write")
  end

  local lines = is_dir and {} or vim.fn.readfile(filepath)
  local target_chars = is_dir and 0 or #table.concat(lines, "\n")
  local initial_engine = opts.engine or "agy"
  local auto_budget = get_automatic_budget(initial_engine, target_chars, opts.budget)

  -- Assemble source code or hermetic Code Graph dependency bundle
  -- SPEC(lgcc-compressed-ir | tier:B | depends_on:[lgcc-cli-router-hook] | blocks:[lgcc-project-scan]): Design compressed semantic intermediate representation preserving global cross-module invariants.
  -- Description: Implement a compact AST/interface representation (symbol dependency graph and skeletonized type stubs) to keep full repository context in the model's active window without blowing up token limits.
  -- Criterion: review: "Intermediate representation compacts multi-file repository context by >70% while retaining all exported types, signatures, and call invariants"
  -- [lean:spec]
  -- def lgcc_compressed_ir : Bool := lgcc_cli_router_hook
  -- theorem lgcc_compressed_ir_verified : lgcc_compressed_ir = true := by rfl
  local code_section = ""
  local used_budget = nil

  -- Enable smart Code Graph routing by default or when bundle/directory mode is active
  if is_dir or opts.bundle ~= false then
    local lgcc_bin = find_lgcc_bin()
    if lgcc_bin then
      local res = vim.system({ lgcc_bin, "--route", filepath, "--budget", tostring(auto_budget), "--format", "ir" }, { text = true }):wait()
      if res.code == 0 and res.stdout and res.stdout ~= "" then
        code_section = res.stdout
        used_budget = auto_budget
      end
    end
  end

  if code_section == "" and not is_dir then
    local numbered_lines = {}
    for i, line in ipairs(lines) do
      table.insert(numbered_lines, string.format("%d: %s", i, line))
    end
    code_section = string.format("--- SOURCE CODE (prefixed with line numbers) ---\n%s\n\nFile: %s",
      table.concat(numbered_lines, "\n"), filepath)
  end

  -- Build prompt
  local spec_section = ""
  if opts.spec and opts.spec ~= "" then
    local spec_path = vim.fn.expand(opts.spec)
    if vim.fn.filereadable(spec_path) == 1 then
      spec_section = string.format("--- SPECIFICATION DOCUMENT ---\n%s\n\n", table.concat(vim.fn.readfile(spec_path), "\n"))
    end
  end

  local system_prompt = [[You are the Logical Compiler (lgcc).
Analyze the provided source code, Code Graph IR, and Task Graph specifications for semantic bugs, invariant violations, and cross-module logical flaws.
- CODE GRAPH TOPOLOGY G=(V, E) provides the symbol call graph, type edges, and line ranges across the repository.
- TASK GRAPH FORMAL SPECIFICATIONS & INVARIANTS provides the formal `SPEC` contracts, acceptance criteria, and LexLean theorems that the code must satisfy.
- ROUTED INTERFACE CONTRACTS provides the skeletonized module interfaces and full implementations of high-centrality dependencies.
Ignore syntax, formatting, and naming conventions.
If you need heavy dependency context or external code exploration, you may run `opencode run "<query>"` to delegate sub-worker analysis.
If flaws or specification violations exist, output each strictly on its own line in this format:
<file>:<line>: error: <clear description of logical flaw or invariant violation>
If there are no logic errors, output: CLEAN]]

  local full_prompt = string.format("%s\n\n%s%s", system_prompt, spec_section, code_section)

  local clean_path = filepath:gsub("/+$", "")
  local file_basename = is_dir and (vim.fn.fnamemodify(clean_path, ":t") .. "/") or vim.fn.fnamemodify(clean_path, ":t")
  local budget_label = used_budget and string.format(" [+%dtok]", used_budget) or (opts.bundle and " [+deps]" or "")
  local state = start_ticker(file_basename .. budget_label, initial_engine)

  local function execute_run(engine_name)
    state.engine = engine_name
    state.raw_tail = "dispatching to " .. engine_name .. "..."

    local cmd = {}
    if engine_name == "agy" then
      cmd = { "agy", "-p", full_prompt, "--dangerously-skip-permissions", "--disable-slash-commands", "--output-format", "stream-json" }
    elseif engine_name == "opencode" then
      cmd = { "opencode", "run", "--auto", full_prompt }
    else
      -- Local fallback
      local model = vim.fn.expand("~/code/agent/vendor/models/qwen2.5-coder-7b/qwen2.5-coder-7b-instruct-q6_k.gguf")
      cmd = { "llama-cli", "-m", model, "-ngl", "12", "-t", "8", "-c", "8192", "--temp", "0.0", "-p", full_prompt }
    end

    local stdout_chunks = {}
    local raw_stream = ""
    local job_timeout = nil

    -- 35-second hard timeout guard to prevent hangs
    job_timeout = vim.uv.new_timer()
    job_timeout:start(35000, 0, vim.schedule_wrap(function()
      if active_job then
        vim.notify("lgcc: " .. engine_name .. " reasoning timed out after 35s. Cancelling.", vim.log.levels.WARN)
        pcall(function() active_job:kill(9) end)
        active_job = nil
        stop_ticker()
      end
      if job_timeout then
        pcall(function() job_timeout:stop(); job_timeout:close() end)
        job_timeout = nil
      end
    end))

    active_job = vim.system(cmd, {
      text = true,
      stdout = function(_, data)
        if not data then return end
        table.insert(stdout_chunks, data)

        -- Parse stream-json text_delta or raw streaming text
        for line in data:gmatch("[^\r\n]+") do
          local delta = line:match('"text_delta"%s*:%s*"(.-)"')
          if delta then
            delta = delta:gsub("\\n", " "):gsub('\\"', '"'):gsub("\\\\", "\\")
            raw_stream = raw_stream .. delta
            state.raw_tail = raw_stream
          elseif not line:find('^{"event"') then
            local clean = line:gsub("\27%[[0-9;]*[a-zA-Z]", ""):gsub("[\r\n]+", " "):gsub("%s+", " "):gsub("^%s+", "")
            if clean ~= "" then
              raw_stream = raw_stream .. " " .. clean
              state.raw_tail = raw_stream
            end
          end
        end

        vim.schedule(function()
          if ticker_timer and state.render then
            state.render()
          end
        end)
      end,
      stderr = function(_, data)
        if not data then return end
        raw_stream = raw_stream .. data
        local clean = raw_stream:gsub("\27%[[0-9;]*[a-zA-Z]", ""):gsub("[\r\n]+", " "):gsub("%s+", " "):gsub("^%s+", "")
        state.raw_tail = clean
        vim.schedule(function()
          if ticker_timer and state.render then
            state.render()
          end
        end)
      end,
    }, function(res)
      active_job = nil
      if job_timeout then
        pcall(function() job_timeout:stop(); job_timeout:close() end)
        job_timeout = nil
      end

      local raw_out = table.concat(stdout_chunks, "")
      local out = ""
      -- Extract final response from stream-json if present
      for line in raw_out:gmatch("[^\r\n]+") do
        local resp = line:match('"response"%s*:%s*"(.-)"')
        if resp then
          out = out .. resp:gsub("\\n", "\n"):gsub('\\"', '"'):gsub("\\\\", "\\")
        end
      end
      if out == "" then
        out = raw_out
      end

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

        -- Clear previous in-line diagnostics
        vim.diagnostic.reset(ns_lgcc)
        for _, b in ipairs(vim.api.nvim_list_bufs()) do
          pcall(vim.api.nvim_buf_clear_namespace, b, ns_lgcc, 0, -1)
        end

        local qf_items = parse_diagnostics(out, filepath)

        if #qf_items > 0 then
          local diags_by_buf = {}
          for _, item in ipairs(qf_items) do
            local fname = item.filename
            local b = vim.fn.bufnr(fname)
            if b == -1 or not vim.api.nvim_buf_is_loaded(b) then
              b = vim.fn.bufadd(fname)
              pcall(vim.fn.bufload, b)
            end
            diags_by_buf[b] = diags_by_buf[b] or {}
            local lnum = math.max(0, (item.lnum or 1) - 1)
            table.insert(diags_by_buf[b], {
              bufnr = b,
              lnum = lnum,
              col = math.max(0, (item.col or 1) - 1),
              severity = vim.diagnostic.severity.ERROR,
              source = "lgcc:" .. engine_name,
              message = item.text,
            })

            -- Attach in-line red comment directly at the end of the line
            pcall(vim.api.nvim_buf_set_extmark, b, ns_lgcc, lnum, 0, {
              virt_text = { { "  ✖ " .. item.text, "DiagnosticError" } },
              virt_text_pos = "eol",
              hl_mode = "combine",
            })
          end

          for b, diags in pairs(diags_by_buf) do
            vim.diagnostic.set(ns_lgcc, b, diags, {
              signs = true,
              underline = true,
              virtual_text = false,
            })
          end

          vim.fn.setqflist(qf_items, "r")
          vim.fn.setqflist({}, "a", { title = string.format("lgcc [%s]: %s (%d logical errors)", engine_name, file_basename, #qf_items) })
          if opts.qf then
            vim.cmd("botright copen 8")
          end

          vim.api.nvim_echo({
            { string.format("● [lgcc:%s] %d logical error(s) flagged in-line in red (navigate with ]d / [d)", engine_name, #qf_items), "DiagnosticError" },
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

--- Clear lgcc results and remove in-line diagnostics
function M.clear()
  if active_job then
    pcall(function() active_job:kill(9) end)
    active_job = nil
  end
  stop_ticker()
  pcall(vim.cmd, "cclose")
  vim.fn.setqflist({}, "r")
  vim.diagnostic.reset(ns_lgcc)
  for _, b in ipairs(vim.api.nvim_list_bufs()) do
    pcall(vim.api.nvim_buf_clear_namespace, b, ns_lgcc, 0, -1)
  end
  vim.api.nvim_echo({ { "lgcc: in-line diagnostics cleared", "Normal" } }, false, {})
end

-- SPEC(lgcc-cli-router-hook | tier:B | depends_on:[ir-schema-formatter] | blocks:[lgcc-compressed-ir]): Connect budget router directly into Neovim lgcc execution loop.
-- Description: Integrate bundle_ir.py --route --budget into lgcc.lua with optional --budget argument for dynamic context allocation.
-- Criterion: review: ":Lgcc --budget 4000 dynamically queries router and passes knapsack-allocated IR context"
-- [lean:spec]
-- def lgcc_cli_router_hook : Bool := ir_schema_formatter
-- theorem lgcc_cli_router_hook_verified : lgcc_cli_router_hook = true := by rfl

-- Unified :Lgcc Command
vim.api.nvim_create_user_command("Lgcc", function(ctx)
  local fargs = ctx.fargs or {}
  local speak = false
  local bundle = true -- default to true for smart context routing
  local user_budget = nil
  local spec_path = nil
  local open_qf = false
  local engine = "agy" -- default to agy, with automatic opencode failover
  local target_file = nil
  local i = 1

  while i <= #fargs do
    local arg = fargs[i]
    if arg == "-s" or arg == "--speak" or arg == "speak" then
      speak = true
    elseif arg == "-b" or arg == "--bundle" or arg == "bundle" then
      bundle = true
    elseif arg == "--no-bundle" or arg == "no-bundle" then
      bundle = false
    elseif arg == "--qf" or arg == "qf" then
      open_qf = true
    elseif arg == "--budget" or arg == "budget" then
      i = i + 1
      user_budget = tonumber(fargs[i])
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
    bundle = bundle,
    budget = user_budget,
    qf = open_qf,
  })
end, {
  nargs = "*",
  complete = function(arglead, cmdline)
    local parts = vim.split(vim.trim(cmdline), "%s+")
    if #parts <= 2 and not cmdline:match("%s+$") then
      local subcmds = { "clear", "-s", "--speak", "-b", "--bundle", "--no-bundle", "--budget", "spec", "--opencode", "--agy", "-l", "--local" }
      return vim.tbl_filter(function(item)
        return vim.startswith(item, arglead)
      end, subcmds)
    elseif parts[#parts - 1] == "spec" or parts[#parts - 1] == "--spec" then
      return vim.fn.getcompletion(arglead, "file")
    else
      return vim.fn.getcompletion(arglead, "file")
    end
  end,
  desc = "Run logical compiler (lgcc) with live status ticker, Quickfix results, and agy/opencode failover; use -s for voice summary, -b for dependency bundling",
})

-- SPEC(lgcc-project-scan | tier:C | depends_on:[lgcc-compressed-ir]): Enable lgcc directory-wide and project-level logical analysis.
-- Description: Extend lgcc to scan entire directories and multi-file projects using the compressed intermediate representation to detect cross-module invariant violations and systemic flaws.
-- Criterion: review: "lgcc directory execution passes compressed project context and populates Quickfix with multi-file error locations"
-- [lean:spec]
-- def lgcc_project_scan : Bool := lgcc_compressed_ir
-- theorem lgcc_project_scan_verified : lgcc_project_scan = true := by rfl

return M
