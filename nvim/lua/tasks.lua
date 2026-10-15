-- ==============================================================================
-- tasks.lua: Source-First DAG & Taskwarrior Navigator
-- Provides the unified :Tasks command for Neovim.
-- Displays in-tree source TODOs (prover) or Taskwarrior tasks in a jumpable split.
-- ==============================================================================

local M = {}

--- Find lint_tasks.py in current working directory or git root
local function find_lint_tasks()
  local cwd = vim.fn.getcwd()
  local candidates = {
    cwd .. "/scripts/lint_tasks.py",
    cwd .. "/../scripts/lint_tasks.py",
  }
  local git_root = vim.fs.root(cwd, ".git")
  if git_root then
    table.insert(candidates, git_root .. "/scripts/lint_tasks.py")
  end

  for _, path in ipairs(candidates) do
    if vim.uv.fs_stat(path) then
      return path
    end
  end
  return nil
end

--- Show in-tree Source-First TODO DAG
local function show_prover_tasks(script_path)
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
  vim.fn.setqflist({}, "a", { title = string.format("Prover Source Tasks (%d tasks in DAG)", #tasks) })
  vim.cmd("botright copen 12")
  vim.api.nvim_echo({
    { string.format("● Loaded %d in-tree tasks (Press <CR> to jump, q to close)", #tasks), "ModeMsg" }
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
--- @param target? string "prover" | "tw" | "taskwarrior" | nil
function M.tasks(target)
  target = target and vim.trim(target):lower() or ""

  if target == "tw" or target == "taskwarrior" then
    show_taskwarrior()
    return
  end

  local lint_script = find_lint_tasks()
  if lint_script and target ~= "tw" then
    show_prover_tasks(lint_script)
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
    local subcmds = { "prover", "tw", "taskwarrior" }
    return vim.tbl_filter(function(s) return vim.startswith(s, arglead) end, subcmds)
  end,
  desc = "Navigate project TODO DAG or Taskwarrior tasks in bottom split",
})

vim.api.nvim_create_user_command("Taskwarrior", function()
  M.tasks("tw")
end, { desc = "View pending Taskwarrior tasks in bottom split" })

return M
