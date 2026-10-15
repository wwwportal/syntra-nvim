-- ==============================================================================
-- tasks.lua: Source-First DAG & Taskwarrior Navigator
-- Provides the unified :Tasks command for Neovim.
-- Displays in-tree source TODOs (prover / syntra-nvim) or Taskwarrior tasks in a jumpable split.
-- ==============================================================================

-- TODO(unified-tasks-calendar | tier:A | blocks:[syntra-plugin-bundle]): Bridge Taskwarrior, in-tree source TODOs, and Google Calendar into unified Neovim UI.
-- Description: Design and implement unified task management system in nvim bridging Taskwarrior, in-tree source TODOs, and calendar.
-- Criterion: review: ":Tasks displays integrated agenda view with calendar items and filterable tags"

-- TODO(lgcc-agent-integration | tier:A | blocks:[voice-assistant-dispatch]): Combine lgcc as a nvim plugin with ~/code/agent and whisper speech-to-speech.
-- Description: Implement voice dictation and assistant :v command with faster-whisper and OpenCode/OpenRouter backend, routing errors to Quickfix on demand.
-- Criterion: review: ":v initiates push-to-talk dictation and streams LLM response without inline highlighting artifacts"

-- TODO(voice-assistant-dispatch | tier:B | depends_on:[lgcc-agent-integration]): Implement seamless voice assistant mode switching in Neovim.
-- Description: Connect whisper transcription output to speech-to-speech assistant mode in Neovim terminal split.
-- Criterion: assert: transcription latency under 800ms and audio plays back without blocking editor

-- TODO(syntra-plugin-bundle | tier:B | depends_on:[unified-tasks-calendar]): Package and version syntra-nvim Debian release with verified task DAG.
-- Description: Regenerate syntra-nvim .deb and APT Packages index containing the unified tasks and voice modules.
-- Criterion: review: "dpkg-deb -I syntra-nvim_1.0.0_all.deb succeeds and apt repository index matches"

local M = {}

--- Get all active directories from current window/split, buffer, terminal, or cwd
local function get_active_dirs()
  local dirs = {}
  local seen = {}
  local function add(d)
    if d and d ~= "" and not seen[d] and vim.fn.isdirectory(d) == 1 then
      seen[d] = true
      table.insert(dirs, d)
    end
  end

  -- 1. Check current buffer if terminal
  local cur_buf = vim.api.nvim_get_current_buf()
  if vim.bo[cur_buf].buftype == "terminal" then
    local pid = vim.b[cur_buf].terminal_job_pid
    if pid then
      local ok, cwd = pcall(vim.uv.fs_readlink, "/proc/" .. pid .. "/cwd")
      if ok and cwd then add(cwd) end
    end
  end

  -- 2. Check current buffer if normal file
  local bufname = vim.api.nvim_buf_get_name(cur_buf)
  if bufname ~= "" and not bufname:match("^term://") then
    add(vim.fn.fnamemodify(bufname, ":p:h"))
  end

  -- 3. Check any other open buffers (files or terminals)
  for _, bufnr in ipairs(vim.api.nvim_list_bufs()) do
    if vim.api.nvim_buf_is_loaded(bufnr) then
      if vim.bo[bufnr].buftype == "terminal" then
        local pid = vim.b[bufnr].terminal_job_pid
        if pid then
          local ok, cwd = pcall(vim.uv.fs_readlink, "/proc/" .. pid .. "/cwd")
          if ok and cwd then add(cwd) end
        end
      else
        local bname = vim.api.nvim_buf_get_name(bufnr)
        if bname ~= "" and not bname:match("^term://") then
          add(vim.fn.fnamemodify(bname, ":p:h"))
        end
      end
    end
  end

  -- 4. Window-local and global cwd
  add(vim.fn.getcwd(0))
  add(vim.fn.getcwd())

  return dirs
end

--- Find lint_tasks.py in active split, buffer, terminal cwd, git root, or known repositories
--- @param target? string
local function find_lint_tasks(target)
  local default_prover = vim.fn.expand("~/code/prover/scripts/lint_tasks.py")
  local default_syntra = vim.fn.expand("~/code/syntra-nvim/scripts/lint_tasks.py")

  if target == "prover" and vim.uv.fs_stat(default_prover) then
    return default_prover
  elseif (target == "syntra" or target == "syntra-nvim") and vim.uv.fs_stat(default_syntra) then
    return default_syntra
  end

  local dirs = get_active_dirs()

  for _, dir in ipairs(dirs) do
    local candidates = {
      dir .. "/scripts/lint_tasks.py",
      dir .. "/lint_tasks.py",
      dir .. "/../scripts/lint_tasks.py",
    }
    local git_root = vim.fs.root(dir, ".git")
    if git_root then
      table.insert(candidates, git_root .. "/scripts/lint_tasks.py")
    end

    for _, path in ipairs(candidates) do
      if vim.uv.fs_stat(path) then
        return path
      end
    end

    -- If any active directory path contains "prover", check default prover path
    if dir:match("/prover") and vim.uv.fs_stat(default_prover) then
      return default_prover
    end
    -- If any active directory path contains "syntra", check default syntra path
    if dir:match("/syntra") and vim.uv.fs_stat(default_syntra) then
      return default_syntra
    end
  end

  if target == "prover" and vim.uv.fs_stat(default_prover) then
    return default_prover
  elseif (target == "syntra" or target == "syntra-nvim") and vim.uv.fs_stat(default_syntra) then
    return default_syntra
  end

  return nil
end

--- Show in-tree Source-First TODO DAG
local function show_repo_tasks(script_path)
  local cmd = { "python3", script_path, "--json" }
  local out = vim.fn.system(cmd)
  if vim.v.shell_error ~= 0 and out == "" then
    vim.notify("tasks: Failed to execute " .. script_path, vim.log.levels.ERROR)
    return
  end

  local ok, data = pcall(vim.fn.json_decode, out)
  if not ok or not data or not data.tasks then
    vim.notify("tasks: Invalid JSON output from task linter", vim.log.levels.ERROR)
    return
  end

  local tasks = data.tasks
  local root = data.root or vim.fs.root(script_path, ".git") or vim.fn.getcwd()
  local repo_name = vim.fn.fnamemodify(root, ":t"):gsub("^%l", string.upper)
  if repo_name == "" or repo_name == "." then repo_name = "Repository" end

  -- Sort: Tier A first, then Tier B, then Tier C
  table.sort(tasks, function(a, b)
    if a.tier ~= b.tier then
      return a.tier < b.tier
    end
    return a.id < b.id
  end)

  local qf = {}
  for _, t in ipairs(tasks) do
    local blocks_str = (#t.blocks > 0) and (" -> blocks [" .. table.concat(t.blocks, ", ") .. "]") or ""
    local deps_str = (#t.depends_on > 0) and (" (depends on: " .. table.concat(t.depends_on, ", ") .. ")") or ""
    local fullpath = root .. "/" .. t.file

    table.insert(qf, {
      filename = fullpath,
      lnum = t.line,
      col = 1,
      text = string.format("[Tier %s] %s%s%s: %s", t.tier, t.id, deps_str, blocks_str, t.desc),
      type = t.tier == "A" and "E" or (t.tier == "B" and "W" or "I"),
    })
  end

  vim.fn.setqflist(qf, "r")
  vim.fn.setqflist({}, "a", { title = string.format("%s Source Tasks (%d tasks in DAG)", repo_name, #tasks) })
  vim.cmd("botright copen 12")
  vim.api.nvim_echo({
    { string.format("● Loaded %d in-tree tasks for %s (Press <CR> to jump, q to close)", #tasks, repo_name), "ModeMsg" }
  }, false, {})
end

--- Show global Taskwarrior tasks
local function show_taskwarrior()
  if vim.fn.executable("task") ~= 1 then
    vim.notify("tasks: 'task' (Taskwarrior) executable not found in PATH", vim.log.levels.ERROR)
    return
  end

  local out = vim.fn.system({ "task", "rc.verbose=nothing", "rc.color=off", "rc.json.array=on", "status:pending", "export" })
  if vim.v.shell_error ~= 0 or out == "" then
    vim.notify("tasks: No pending Taskwarrior tasks found", vim.log.levels.INFO)
    return
  end

  local ok, tasks = pcall(vim.fn.json_decode, out)
  if not ok or type(tasks) ~= "table" then
    vim.notify("tasks: Could not parse Taskwarrior output", vim.log.levels.ERROR)
    return
  end

  -- Sort by urgency descending
  table.sort(tasks, function(a, b)
    return (a.urgency or 0) > (b.urgency or 0)
  end)

  local qf = {}
  for _, t in ipairs(tasks) do
    local project = t.project and ("[" .. t.project .. "] ") or ""
    local urg = t.urgency and string.format("(Urg: %.2f) ", t.urgency) or ""
    table.insert(qf, {
      text = string.format("#%-2d %s%s%s", t.id or 0, project, urg, t.description or "No description"),
      type = (t.urgency and t.urgency > 5) and "E" or ((t.urgency and t.urgency > 2) and "W" or "I"),
    })
  end

  vim.fn.setqflist(qf, "r")
  vim.fn.setqflist({}, "a", { title = string.format("Taskwarrior Tasks (%d pending)", #tasks) })
  vim.cmd("botright copen 12")
  vim.api.nvim_echo({
    { string.format("● Loaded %d Taskwarrior tasks (q to close)", #tasks), "ModeMsg" }
  }, false, {})
end

--- Main entry point for :Tasks command
--- @param target? string "prover" | "syntra" | "syntra-nvim" | "tw" | "taskwarrior" | nil
function M.tasks(target)
  target = target and vim.trim(target):lower() or ""

  if target == "tw" or target == "taskwarrior" then
    show_taskwarrior()
    return
  end

  local lint_script = find_lint_tasks(target)
  if lint_script then
    show_repo_tasks(lint_script)
  elseif target == "prover" or target == "syntra" or target == "syntra-nvim" then
    vim.notify(string.format("tasks: Could not find %s's lint_tasks.py script", target), vim.log.levels.ERROR)
  else
    show_taskwarrior()
  end
end

-- Define user commands
vim.api.nvim_create_user_command("Tasks", function(ctx)
  M.tasks(ctx.args)
end, {
  nargs = "?",
  complete = function(arglead)
    local subcmds = { "prover", "syntra", "syntra-nvim", "tw", "taskwarrior" }
    return vim.tbl_filter(function(s) return vim.startswith(s, arglead) end, subcmds)
  end,
  desc = "Navigate project TODO DAG or Taskwarrior tasks in bottom split",
})

vim.api.nvim_create_user_command("Taskwarrior", function()
  M.tasks("tw")
end, { desc = "View pending Taskwarrior tasks in bottom split" })

return M
