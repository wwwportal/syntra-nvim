local M = {}

local py = vim.fn.expand("~/myenv/bin/python")
local audio_path = "/tmp/syntra_voice.wav"

local function transcribe_audio()
    local script = string.format([[
import sys, os
sys.path.insert(0, "/home/yassine/myenv/lib/python3.12/site-packages")
from faster_whisper import WhisperModel
model = WhisperModel("base.en", device="cpu", compute_type="int8")
segments, info = model.transcribe("%s", language="en", beam_size=1, vad_filter=True)
text = "".join(s.text for s in segments).strip()
print(text)
]], audio_path)
    local out = vim.fn.system({ py, "-c", script })
    if vim.v.shell_error ~= 0 then
        return nil, out
    end
    return out:gsub("\n", ""):gsub("\r", "")
end

local stream_file = "/tmp/syntra_voice_stream.out"
local stream_pid = nil
local timer = nil
local last_line = 0

function M.start_transcription()
    -- Toggle continuous mode on/off via :v
    if stream_pid and vim.fn.jobwait({stream_pid}, 0)[1] == 0 then
        vim.fn.jobstop(stream_pid)
        stream_pid = nil
        if timer then timer:stop(); timer = nil end
        vim.notify(":listen continuous mode stopped", vim.log.levels.INFO)
        return
    end

    -- Kill any existing stream process
    vim.fn.system("pkill -f syntra_voice_stream.py 2>/dev/null || true")
    vim.fn.system("> /tmp/syntra_voice_stream.out")
    last_line = 0
    stream_pid = vim.fn.jobstart({ vim.fn.expand("~/myenv/bin/python"), "/tmp/syntra_voice_stream.py" })
    if stream_pid <= 0 then
        vim.notify("Failed to start voice stream", vim.log.levels.ERROR)
        stream_pid = nil
        return
    end
    vim.notify(":listen continuous listening started (run :listen again to stop)", vim.log.levels.INFO)
    timer = vim.loop.new_timer()
    timer:start(500, 500, vim.schedule_wrap(function()
        if not vim.api.nvim_buf_is_valid(0) then return end
        local lines = vim.fn.readfile(stream_file)
        if #lines <= last_line then return end
        local cur_buf = vim.api.nvim_get_current_buf()
        local cur_win = vim.api.nvim_get_current_win()
        local row, col = unpack(vim.api.nvim_win_get_cursor(cur_win))
        local inserted = false
        for i = last_line + 1, #lines do
            local text = lines[i]:gsub("\r", ""):gsub("\n", "")
            if text ~= "" then
                local line_text = vim.api.nvim_buf_get_lines(cur_buf, row - 1, row, false)[1] or ""
                local before = line_text:sub(1, col)
                local after = line_text:sub(col + 1)
                local was_mod = vim.bo[cur_buf].modifiable
                vim.bo[cur_buf].modifiable = true
                local sep = " "
                vim.api.nvim_buf_set_lines(cur_buf, row - 1, row, false, { before .. text .. sep .. after })
                vim.bo[cur_buf].modifiable = was_mod
                col = #before + #text + #sep
                inserted = true
            end
        end
        if inserted then
            vim.api.nvim_win_set_cursor(cur_win, { row, col })
        end
        last_line = #lines
    end))
end

function M.setup()
    vim.api.nvim_create_user_command("Listen", function()
        M.start_transcription()
    end, { desc = "Voice transcription (:listen)" })

    vim.keymap.set("n", "<leader>v", function()
        M.start_transcription()
    end, { desc = "Voice transcription" })
end

return M
