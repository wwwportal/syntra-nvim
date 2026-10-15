local M = {}

local piper_bin = vim.fn.expand("~/myenv/bin/piper")
local model_path = vim.fn.expand("~/code/agent/vendor/voices/en_US-kristin-medium.onnx")
local text_path = "/tmp/lgcc_speech_text.txt"
local wav_path = "/tmp/lgcc_speech.wav"

function M.setup()
    vim.api.nvim_create_user_command("Lgcc", function(ctx)
        local args = ctx.args or ""
        local speak = false
        -- Strip -s / --s flag for voice output
        local cleaned = args:gsub("%-s", ""):gsub("%-%-s", "")
        -- If original contained -s, set speak
        if args:match("%-s") or args:match("%-%-s") then
            speak = true
        end

        -- If no file given, use current buffer
        local file_arg = cleaned:gsub("%s+", " "):gsub("^%s*", ""):gsub("%s*$", "")
        if file_arg == "" then
            local bufname = vim.api.nvim_buf_get_name(0)
            if bufname ~= "" then
                file_arg = vim.fn.fnameescape(bufname)
            else
                file_arg = vim.fn.fnameescape(vim.fn.expand("%"))
            end
        end

        local cmd = "lgcc " .. cleaned .. (cleaned ~= "" and " " or "") .. file_arg
        local out = vim.fn.system(cmd)

        -- Update quickfix if lgcc emitted diagnostics (standard behavior)
        -- For simplicity, we assume lgcc handles its own quickfix; we just vocalize.

        if speak then
            -- Extract textual content from output to speak
            local text = out:gsub("\r\n", "\n"):gsub("\r", "\n")
            -- If lgcc produced errors, speak them; otherwise say clean
            if text:match("error:") or text:match":" or text ~= "" then
                vim.fn.writefile({ text }, text_path)
                local piper_cmd = string.format(
                    "%s -m %s -i %s -f %s --sentence-silence 0.2 2>/dev/null",
                    piper_bin, model_path, text_path, wav_path
                )
                vim.fn.system(piper_cmd)
                if vim.fn.filereadable(wav_path) == 1 and vim.fn.getfsize(wav_path) > 0 then
                    vim.fn.system("aplay -q " .. wav_path .. " 2>/dev/null &")
                    vim.notify("Lgcc voice output playing", vim.log.levels.INFO)
                else
                    vim.notify("Lgcc voice synthesis failed", vim.log.levels.WARN)
                end
            else
                vim.notify("Lgcc: no errors found (clean)", vim.log.levels.INFO)
            end
        else
            -- Normal lgcc behavior (quickfix handled by lgcc itself, or notify)
            vim.notify("Lgcc completed", vim.log.levels.INFO)
        end
    end, { nargs = "*", complete = "file", desc = "Lgcc diagnostic engine; use -s for voice output" })
end

return M
