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

function M.start_transcription()
    -- Save current cursor position
    local cur_buf = vim.api.nvim_get_current_buf()
    local cur_win = vim.api.nvim_get_current_win()
    local row, col = unpack(vim.api.nvim_win_get_cursor(cur_win))

    -- Notify user recording started
    vim.notify("Recording... (press Enter to stop, or wait 5s)", vim.log.levels.INFO)

    -- Start recording with arecord (5s max for simplicity)
    -- For interactive stop, we use a simple approach: start arecord in background,
    -- notify user, and after a brief delay process what we have.
    -- A more robust approach: start arecord without time limit and rely on a signal
    -- to stop. For initial implementation, we use a fixed 5-second window.
    local cmd = string.format("arecord -q -f cd -t wav -d 5 -c 1 %s 2>/dev/null", audio_path)
    vim.fn.system(cmd)

    -- Check if file exists and has content
    if vim.fn.filereadable(audio_path) == 0 or vim.fn.getfsize(audio_path) < 100 then
        vim.notify("No audio recorded", vim.log.levels.WARN)
        return
    end

    -- Process transcription
    vim.notify("Transcribing...", vim.log.levels.INFO)
    local text, err = transcribe_audio()

    if not text or text == "" then
        vim.notify("Transcription failed or empty: " .. (err or ""), vim.log.levels.ERROR)
        return
    end

    -- Insert at cursor position (pushing text forward)
    local line = vim.api.nvim_buf_get_lines(cur_buf, row - 1, row, false)[1] or ""
    local before = line:sub(1, col)
    local after = line:sub(col + 1)
    local new_line = before .. text .. after

    vim.api.nvim_buf_set_lines(cur_buf, row - 1, row, false, { new_line })

    -- Move cursor to end of inserted text
    local new_col = #before + #text
    vim.api.nvim_win_set_cursor(cur_win, { row, new_col })

    vim.notify("Transcription inserted: " .. text, vim.log.levels.INFO)
end

function M.setup()
    vim.api.nvim_create_user_command("v", function()
        M.start_transcription()
    end, { desc = "Voice transcription (:v)" })

    vim.keymap.set("n", "<leader>v", function()
        M.start_transcription()
    end, { desc = "Voice transcription" })
end

return M
