-- ==============================================================================
-- tasks.lua: Source-First DAG & Taskwarrior Navigator
-- Provides the unified :Tasks command for Neovim.
-- Displays in-tree source TODOs (prover / syntra-nvim) or Taskwarrior tasks in a jumpable split.
-- ==============================================================================

-- TODO(unified-tasks | tier:A | blocks:[syntra-plugin-bundle]): Bridge Taskwarrior and in-tree source TODOs into unified Neovim UI.
-- Description: Design and implement unified task management system in nvim bridging Taskwarrior and in-tree source TODOs.
-- Criterion: review: ":Tasks displays integrated agenda view with source TODOs and Taskwarrior tasks"

-- TODO(ir-ast-extractor | tier:A | blocks:[ir-contract-skeletonizer]): Extract symbols, declarations, and import edges from Python and Lua ASTs.
-- Description: Implement AST traversal using Python ast and Lua Tree-sitter to parse source files into structured declaration lists and import edges.
-- Criterion: review: "scripts/bundle_ir.py --extract <file> returns JSON with symbols, signatures, and import edges"

-- TODO(ir-contract-skeletonizer | tier:A | depends_on:[ir-ast-extractor] | blocks:[lgcc-dep-bundle]): Skeletonize implementation bodies while preserving signatures and invariants.
-- Description: Replace function and method bodies with stubs while retaining complete type annotations, docstring invariants, and interface definitions.
-- Criterion: review: "Skeletonizer achieves >70% character and token reduction on target modules while preserving all exported signatures"

-- TODO(lgcc-dep-bundle | tier:A | depends_on:[ir-contract-skeletonizer] | blocks:[lgcc-task-synthesizer, ir-graph-builder]): Compile hermetic dependency context and type signatures into intermediate representation before model invocation.
-- Description: Preprocess target files by bundling imported interfaces, external function stubs, and type definitions instead of raw binary streams or isolated files.
-- Criterion: review: "lgcc CLI accepts --bundle flag that aggregates local imported module stubs without raw binary degradation"

-- TODO(ir-graph-builder | tier:B | depends_on:[lgcc-dep-bundle] | blocks:[router-topological-pruner]): Construct global multi-file repository symbol dependency graph.
-- Description: Traverse all tracked repository source files to build a global symbol dependency graph linking callers, callees, and imported types.
-- Criterion: review: "scripts/bundle_ir.py --graph returns full repository symbol DAG with verified node and edge counts"

-- TODO(router-topological-pruner | tier:B | depends_on:[ir-graph-builder] | blocks:[router-budget-knapsack]): Compute topological distance and PageRank importance weighting on symbol graph.
-- Description: Given target file, assign importance weights W(v|q) to graph nodes using distance decay and Personalized PageRank.
-- Criterion: review: "Router ranks 1-hop and 2-hop dependent symbols higher than unrelated repository symbols"

-- TODO(router-budget-knapsack | tier:B | depends_on:[router-topological-pruner] | blocks:[lgcc-compressed-ir]): Implement knapsack context allocator bounded by token budget.
-- Description: Select optimal symbol subgraph maximizing information coverage score while staying strictly within active context token budget.
-- Criterion: review: "Router output token count stays under specified budget while maximizing coverage score"

-- TODO(lgcc-compressed-ir | tier:B | depends_on:[router-budget-knapsack] | blocks:[lgcc-project-scan]): Design compressed semantic intermediate representation preserving global cross-module invariants.
-- Description: Implement a compact AST/interface representation (symbol dependency graph and skeletonized type stubs) to keep full repository context in the model's active window without blowing up token limits.
-- Criterion: review: "Intermediate representation compacts multi-file repository context by >70% while retaining all exported types, signatures, and call invariants"

-- TODO(lgcc-task-synthesizer | tier:B | depends_on:[lgcc-dep-bundle]): Guided transformation of informal TODO/FIXME tags into schema-compliant contracts using constrained typesafe generation.
-- Description: Integrate lgcc and prime-router / typesafe schema enforcement to analyze informal code TODOs and auto-synthesize valid tier, description, and verifiable criterion.
-- Criterion: review: ":Tasks fix or lgcc --fix-tasks interactively converts unstructured TODOs into valid in-tree contracts with user confirmation"

-- TODO(syntra-plugin-bundle | tier:B | depends_on:[unified-tasks]): Package and version syntra-nvim Debian release with verified task DAG.
-- Description: Regenerate syntra-nvim .deb and APT Packages index containing the unified tasks and voice modules.
-- Criterion: review: "dpkg-deb -I syntra-nvim_1.0.0_all.deb succeeds and apt repository index matches"

-- TODO(lgcc-project-scan | tier:C | depends_on:[lgcc-compressed-ir]): Enable lgcc directory-wide and project-level logical analysis.
-- Description: Extend lgcc to scan entire directories and multi-file projects using the compressed intermediate representation to detect cross-module invariant violations and systemic flaws.
-- Criterion: review: "lgcc directory execution passes compressed project context and populates Quickfix with multi-file error locations"

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

--- Set up buffer-local keymaps in the quickfix window to route jumps to the calling split
--- @param origin_win number
local function setup_qf_keymaps(origin_win)
  local qf_win = vim.api.nvim_get_current_win()
  local qf_buf = vim.api.nvim_win_get_buf(qf_win)

  -- 'q' closes the quickfix window and returns focus to origin_win
  vim.keymap.set("n", "q", function()
    vim.cmd("cclose")
    if vim.api.nvim_win_is_valid(origin_win) then
      vim.api.nvim_set_current_win(origin_win)
    end
  end, { buffer = qf_buf, silent = true, noremap = true, desc = "Close Tasks window" })

  -- Function to perform the jump into origin_win
  local function jump_in_origin(close_qf)
    local idx = vim.fn.line(".")
    local qf_list = vim.fn.getqflist()
    local item = qf_list[idx]
    if not item then return end

    local filepath = (item.filename and item.filename ~= "") and item.filename
    if not filepath or filepath == "" then
      if item.bufnr and item.bufnr > 0 and vim.api.nvim_buf_is_valid(item.bufnr) then
        filepath = vim.api.nvim_buf_get_name(item.bufnr)
      end
    end

    if not filepath or filepath == "" then
      vim.notify("Selected task has no associated file location", vim.log.levels.INFO)
      return
    end

    local target_win = origin_win
    if not vim.api.nvim_win_is_valid(target_win) or target_win == qf_win then
      -- Fallback: find any non-quickfix window
      target_win = nil
      for _, win in ipairs(vim.api.nvim_list_wins()) do
        if win ~= qf_win then
          target_win = win
          break
        end
      end
    end

    if not target_win or not vim.api.nvim_win_is_valid(target_win) then
      vim.cmd("cc " .. idx)
      return
    end

    if close_qf then
      vim.cmd("cclose")
    end

    -- Switch focus to the origin split
    vim.api.nvim_set_current_win(target_win)

    -- Open the file in this exact split
    vim.cmd("edit " .. vim.fn.fnameescape(filepath))

    -- Move cursor to line & column and center
    local lnum = math.max(1, item.lnum or 1)
    local col = math.max(0, (item.col or 1) - 1)
    pcall(vim.api.nvim_win_set_cursor, target_win, { lnum, col })
    vim.cmd("normal! zz")
  end

  -- <CR> jumps into origin_win (keeps quickfix open for easy browsing)
  vim.keymap.set("n", "<CR>", function()
    jump_in_origin(false)
  end, { buffer = qf_buf, silent = true, noremap = true, desc = "Jump to task in origin split" })

  -- Double-click jumps into origin_win
  vim.keymap.set("n", "<2-LeftMouse>", function()
    jump_in_origin(false)
  end, { buffer = qf_buf, silent = true, noremap = true, desc = "Jump to task in origin split" })

  -- 'o' jumps and closes quickfix
  vim.keymap.set("n", "o", function()
    jump_in_origin(true)
  end, { buffer = qf_buf, silent = true, noremap = true, desc = "Jump to task and close Tasks split" })
end

--- Show in-tree Source-First TODO DAG
local function show_repo_tasks(script_path, origin_win, target_task_id)
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

  local target_idx = nil
  local qf = {}
  for idx, t in ipairs(tasks) do
    if target_task_id and t.id == target_task_id then
      target_idx = idx
    end
    local blocks_str = (#t.blocks > 0) and (" -> blocks [" .. table.concat(t.blocks, ", ") .. "]") or ""
    local deps_str = (#t.depends_on > 0) and (" (depends on: " .. table.concat(t.depends_on, ", ") .. ")") or ""
    local fullpath = root .. "/" .. t.file

    table.insert(qf, {
      filename = fullpath,
      lnum = t.line,
      text = string.format("[Tier %s] %s%s%s: %s", t.tier, t.id, deps_str, blocks_str, t.desc),
      type = t.tier == "A" and "E" or (t.tier == "B" and "W" or "I"),
    })
  end

  vim.fn.setqflist(qf, "r")
  vim.fn.setqflist({}, "a", { title = string.format("%s Source Tasks (%d tasks in DAG)", repo_name, #tasks) })
  vim.cmd("botright copen 12")
  local qf_win = vim.api.nvim_get_current_win()
  setup_qf_keymaps(origin_win)

  if target_idx then
    pcall(vim.api.nvim_win_set_cursor, qf_win, { target_idx, 0 })
    vim.cmd("normal! zz")
  end

  vim.api.nvim_echo({
    { string.format("● Loaded %d in-tree tasks for %s (<CR> to jump in split, o to jump & close, q to close)", #tasks, repo_name), "ModeMsg" }
  }, false, {})
end

--- Show global Taskwarrior tasks
local function show_taskwarrior(origin_win)
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
  setup_qf_keymaps(origin_win)
  vim.api.nvim_echo({
    { string.format("● Loaded %d Taskwarrior tasks (q to close)", #tasks), "ModeMsg" }
  }, false, {})
end

--- Parse the argument string for `:Tasks add`
--- Returns spec table or (nil, error_message)
local function parse_task_spec(args_str)
  local str = vim.trim(args_str or "")
  if str == "" then
    return nil, "Usage: :Tasks add [file] <TODO|FIXME>[: <description>]"
  end

  local target_file = nil
  local first_word, rest = str:match("^(%S+)%s+(.*)$")
  if first_word and rest and (first_word:find("[./]") or vim.fn.filereadable(first_word) == 1) then
    target_file = first_word
    str = rest
  end

  local kind, custom_id, custom_attrs, desc = str:match("^(%a+)%(([^|]+)|?([^)]*)%)%s*:%s*(.+)$")
  local tier = "B"
  local blocks = nil
  local depends_on = nil

  if kind then
    kind = kind:upper()
    custom_id = vim.trim(custom_id)
    if custom_attrs and custom_attrs ~= "" then
      local t = custom_attrs:match("tier:([ABC])")
      if t then tier = t end
      blocks = custom_attrs:match("blocks:%[[^%]]*%]")
      depends_on = custom_attrs:match("depends_on:%[[^%]]*%]")
    end
    return {
      file = target_file,
      kind = (kind == "FIXME") and "FIXME" or "TODO",
      id = custom_id,
      tier = tier,
      title = vim.trim(desc),
      description = vim.trim(desc),
      blocks = blocks,
      depends_on = depends_on,
    }
  end

  local k, d = str:match("^(%a+)%s*:%s*(.+)$")
  if not k then
    k, d = str:match("^(%a+)%s+(.+)$")
  end

  if not k or not d then
    return nil, "Invalid format. Expected: :Tasks add <TODO|FIXME>:<description>"
  end

  k = k:upper()
  if k ~= "TODO" and k ~= "FIXME" then
    return nil, string.format("Unknown task type '%s'. Expected TODO or FIXME", k)
  end

  d = vim.trim(d)
  if d == "" then
    return nil, "Task description cannot be empty"
  end

  local words = {}
  local stop_words = { a=true, an=true, the=true, ["to"]=true, ["for"]=true, ["in"]=true, of=true, on=true, at=true, with=true, ["and"]=true }
  for w in d:lower():gmatch("[a-z0-9]+") do
    if not stop_words[w] then
      table.insert(words, w)
    end
    if #words >= 4 then break end
  end
  if #words == 0 then
    table.insert(words, "task")
  end
  local auto_id = table.concat(words, "-")

  return {
    file = target_file,
    kind = k,
    id = auto_id,
    tier = "B",
    title = d,
    description = d,
  }
end

local function get_comment_leader(bufnr)
  local cs = vim.bo[bufnr].commentstring
  if cs and cs ~= "" and cs:find("%%s") then
    local leader = cs:gsub("%%s.*$", "")
    leader = vim.trim(leader)
    if leader ~= "" then
      return leader .. " "
    end
  end

  local name = vim.api.nvim_buf_get_name(bufnr)
  local ext = vim.fn.fnamemodify(name, ":e"):lower()
  if ext == "lua" then
    return "-- "
  elseif ext == "py" or ext == "sh" or ext == "bash" or ext == "yaml" or ext == "yml" or ext == "toml" then
    return "# "
  elseif ext == "c" or ext == "cpp" or ext == "h" or ext == "hpp" or ext == "rs" or ext == "go" or ext == "js" or ext == "ts" then
    return "// "
  end

  return "-- "
end

--- Semantically locate the best file and function line for a given task description
local function locate_target_file_and_line(repo_root, description)
  if not repo_root or repo_root == "" then
    return nil, 1
  end

  local git_cmd = string.format("git -C %s ls-files 2>/dev/null", vim.fn.shellescape(repo_root))
  local file_list_raw = vim.fn.system(git_cmd)
  if vim.v.shell_error ~= 0 or not file_list_raw or file_list_raw == "" then
    return nil, 1
  end

  local valid_exts = { lua=true, py=true, sh=true, bash=true, cpp=true, c=true, h=true, hpp=true, rs=true, go=true, js=true, ts=true, lean=true }
  local candidate_files = {}
  local scores = {}

  for line in file_list_raw:gmatch("[^\r\n]+") do
    local ext = vim.fn.fnamemodify(line, ":e"):lower()
    if valid_exts[ext] then
      table.insert(candidate_files, line)
      scores[line] = 0
    end
  end

  if #candidate_files == 0 then
    return nil, 1
  end

  local stop_words = {
    a=true, an=true, the=true, ["and"]=true, ["for"]=true, ["with"]=true,
    ["that"]=true, ["this"]=true, ["from"]=true, ["when"]=true, ["into"]=true,
    file=true, task=true, code=true, make=true, sure=true, need=true,
  }
  local keywords = {}
  for w in description:lower():gmatch("[a-z0-9_%-]+") do
    if #w > 2 and not stop_words[w] then
      table.insert(keywords, w)
    end
  end

  for _, f in ipairs(candidate_files) do
    local f_lower = f:lower()
    for _, kw in ipairs(keywords) do
      if f_lower:find(kw, 1, true) then
        scores[f] = scores[f] + 20
      end
    end
  end

  local rg_bin = vim.fn.expand("~/.local/bin/rg")
  if vim.fn.executable(rg_bin) ~= 1 then
    rg_bin = "rg"
  end

  for i = 1, math.min(#keywords, 6) do
    local kw = keywords[i]
    local rg_cmd = string.format("%s -l -i %s %s 2>/dev/null", rg_bin, vim.fn.shellescape(kw), vim.fn.shellescape(repo_root))
    local out = vim.fn.system(rg_cmd)
    if vim.v.shell_error == 0 and out and out ~= "" then
      for hit in out:gmatch("[^\r\n]+") do
        local rel = vim.fn.fnamemodify(hit, ":s?" .. repo_root .. "/??")
        if scores[rel] then
          scores[rel] = scores[rel] + 5
        end
      end
    end
  end

  local best_file = nil
  local max_score = -1
  for _, f in ipairs(candidate_files) do
    if scores[f] > max_score then
      max_score = scores[f]
      best_file = f
    end
  end

  if not best_file or max_score == 0 then
    best_file = candidate_files[1]
  end

  local fullpath = repo_root .. "/" .. best_file
  local target_line = 1

  if vim.fn.filereadable(fullpath) == 1 then
    local file_lines = vim.fn.readfile(fullpath)
    local best_func_line = nil
    local best_func_score = 0
    local cur_func_line = 1

    for lnum, line in ipairs(file_lines) do
      local is_def = line:match("^[ \t]*function%s+([%w_.:]+)")
        or line:match("^[ \t]*local%s+function%s+([%w_]+)")
        or line:match("^[ \t]*def%s+([%w_]+)")
        or line:match("^[ \t]*class%s+([%w_]+)")

      if is_def then
        cur_func_line = lnum
      end

      local line_lower = line:lower()
      local line_score = 0
      for _, kw in ipairs(keywords) do
        if line_lower:find(kw, 1, true) then
          line_score = line_score + 1
        end
      end

      if line_score > best_func_score then
        best_func_score = line_score
        best_func_line = cur_func_line
      end
    end

    if best_func_line and best_func_score > 0 then
      target_line = best_func_line
    end
  end

  return fullpath, target_line
end

local function resolve_target_file(spec_file, description, origin_win)
  if spec_file and spec_file ~= "" then
    local path = vim.fn.fnamemodify(spec_file, ":p")
    return path, 1
  end

  -- Determine active repository root
  local repo_root = nil
  local lint_script = find_lint_tasks()
  if lint_script then
    repo_root = vim.fs.root(lint_script, ".git") or vim.fn.fnamemodify(lint_script, ":p:h:h")
  end

  if not repo_root or repo_root == "" then
    if origin_win and vim.api.nvim_win_is_valid(origin_win) then
      local obuf = vim.api.nvim_win_get_buf(origin_win)
      local obuf_name = vim.api.nvim_buf_get_name(obuf)
      if obuf_name ~= "" and not obuf_name:match("^term://") then
        repo_root = vim.fs.root(obuf_name, ".git")
      end
    end
  end

  if not repo_root or repo_root == "" then
    repo_root = vim.fs.root(0, ".git") or vim.fn.getcwd()
  end

  local best_file, target_line = locate_target_file_and_line(repo_root, description)
  if best_file then
    return best_file, target_line
  end

  if origin_win and vim.api.nvim_win_is_valid(origin_win) then
    local obuf = vim.api.nvim_win_get_buf(origin_win)
    if vim.bo[obuf].buftype == "" and vim.api.nvim_buf_get_name(obuf) ~= "" then
      return vim.api.nvim_buf_get_name(obuf), 1
    end
  end

  return nil, 1
end

local function insert_task_into_buf(bufnr, origin_win, spec, target_line)
  local leader = get_comment_leader(bufnr)
  local line_count = vim.api.nvim_buf_line_count(bufnr)

  local row = target_line or 1
  if row > line_count then
    row = math.max(1, line_count)
  end

  -- Ensure unique ID within buffer
  local buf_text = table.concat(vim.api.nvim_buf_get_lines(bufnr, 0, -1, false), "\n")
  local base_id = spec.id
  local counter = 2
  while buf_text:find("%(" .. spec.id .. "%s*|") do
    spec.id = string.format("%s-%d", base_id, counter)
    counter = counter + 1
  end

  local current_line = (row <= line_count) and vim.api.nvim_buf_get_lines(bufnr, row - 1, row, false)[1] or ""
  local indent = current_line:match("^(%s*)") or ""

  local lines = {
    string.format("%s%s%s(%s | tier:%s): %s", indent, leader, spec.kind, spec.id, spec.tier, spec.title),
    string.format("%s%sDescription: %s", indent, leader, spec.description),
    string.format('%s%sCriterion: review: "%s"', indent, leader, spec.description),
    "",
  }

  local insert_idx = math.max(0, row - 1)
  vim.api.nvim_buf_set_lines(bufnr, insert_idx, insert_idx, false, lines)

  -- Save buffer so lint_tasks.py can immediately parse it
  vim.api.nvim_buf_call(bufnr, function()
    pcall(vim.cmd, "silent! write")
  end)

  local task_line = insert_idx + 1

  -- 1. Open that task in origin_win (the split from which the command was called)
  if origin_win and vim.api.nvim_win_is_valid(origin_win) then
    local orig_buf = vim.api.nvim_win_get_buf(origin_win)
    if vim.bo[orig_buf].buftype == "terminal" then
      vim.w[origin_win].terminal_origin = orig_buf
    end
    vim.api.nvim_win_set_buf(origin_win, bufnr)
    pcall(vim.api.nvim_win_set_cursor, origin_win, { task_line, #indent })
    vim.api.nvim_win_call(origin_win, function() vim.cmd("normal! zz") end)
  end

  local fname = vim.fn.fnamemodify(vim.api.nvim_buf_get_name(bufnr), ":~:.")
  vim.api.nvim_echo({
    { string.format("● Placed %s task '%s' into %s:%d", spec.kind, spec.id, fname, task_line), "ModeMsg" }
  }, false, {})

  -- 2. Open bottom split with task list, with cursor on the newly created task
  local lint_script = find_lint_tasks()
  if lint_script then
    show_repo_tasks(lint_script, origin_win, spec.id)
  end
end

--- Add a task to source file: :Tasks add [file] <TODO|FIXME>:<description>
function M.add_task(arg_str, origin_win)
  origin_win = origin_win or vim.api.nvim_get_current_win()
  local spec, err = parse_task_spec(arg_str)
  if not spec then
    vim.notify(err, vim.log.levels.ERROR)
    return
  end

  local target_file, target_line = resolve_target_file(spec.file, spec.description, origin_win)
  if target_file then
    local bufnr = vim.fn.bufnr(target_file)
    if bufnr == -1 or not vim.api.nvim_buf_is_loaded(bufnr) then
      bufnr = vim.fn.bufadd(target_file)
      vim.fn.bufload(bufnr)
    end
    insert_task_into_buf(bufnr, origin_win, spec, target_line)
  else
    vim.ui.input({ prompt = "Target source file for task: ", completion = "file" }, function(input)
      if input and vim.trim(input) ~= "" then
        local file = vim.fn.fnamemodify(vim.trim(input), ":p")
        local bufnr = vim.fn.bufadd(file)
        vim.fn.bufload(bufnr)
        insert_task_into_buf(bufnr, origin_win, spec, 1)
      else
        vim.notify("Cancelled: no source file specified", vim.log.levels.WARN)
      end
    end)
  end
end

--- Main entry point for :Tasks command
--- @param args? string "prover" | "syntra" | "syntra-nvim" | "tw" | "taskwarrior" | "add ..." | nil
function M.tasks(args)
  local origin_win = vim.api.nvim_get_current_win()
  args = args and vim.trim(args) or ""

  if vim.startswith(args, "add ") or args == "add" then
    local add_args = vim.trim(args:sub(4))
    M.add_task(add_args, origin_win)
    return
  end

  local target = args:lower()
  if target == "tw" or target == "taskwarrior" then
    show_taskwarrior(origin_win)
    return
  end

  local lint_script = find_lint_tasks(target ~= "" and target or nil)
  if lint_script then
    show_repo_tasks(lint_script, origin_win)
  elseif target == "prover" or target == "syntra" or target == "syntra-nvim" then
    vim.notify(string.format("tasks: Could not find %s's lint_tasks.py script", target), vim.log.levels.ERROR)
  else
    show_taskwarrior(origin_win)
  end
end

local function task_complete(arglead, cmdline)
  local parts = vim.split(vim.trim(cmdline), "%s+")
  if #parts == 2 and not cmdline:match("%s$") then
    local subcmds = { "add", "prover", "syntra", "syntra-nvim", "tw", "taskwarrior" }
    return vim.tbl_filter(function(s) return vim.startswith(s, arglead) end, subcmds)
  elseif (#parts == 2 and cmdline:match("%s$") and parts[2] == "add") or (#parts == 3 and parts[2] == "add") then
    local add_prefixes = { "TODO:", "FIXME:" }
    return vim.tbl_filter(function(s) return vim.startswith(s, arglead) end, add_prefixes)
  end
  local subcmds = { "add", "prover", "syntra", "syntra-nvim", "tw", "taskwarrior" }
  return vim.tbl_filter(function(s) return vim.startswith(s, arglead) end, subcmds)
end

-- Define user commands
vim.api.nvim_create_user_command("Tasks", function(ctx)
  M.tasks(ctx.args)
end, {
  nargs = "*",
  complete = task_complete,
  desc = "Navigate project TODO DAG, Taskwarrior, or add tasks",
})

vim.api.nvim_create_user_command("Task", function(ctx)
  M.tasks(ctx.args)
end, {
  nargs = "*",
  complete = task_complete,
  desc = "Navigate project TODO DAG, Taskwarrior, or add tasks",
})

vim.api.nvim_create_user_command("Taskwarrior", function()
  M.tasks("tw")
end, { desc = "View pending Taskwarrior tasks in bottom split" })

return M
