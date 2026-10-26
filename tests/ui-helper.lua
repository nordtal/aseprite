-- Loaded before every script in ui/ by cli/ui.sh

-- In the GUI print() goes to the console window and the app doesn't
-- end with the script, so the result goes to stdout and the app ends
-- once its main loop runs. (dofile() would ask for read access, and
-- os.exit() isn't available.)
--
-- A test whose results arrive later returns a function. It's called
-- every interval seconds (0.1 s if not given) until it returns true,
-- for at most 20 s.
function ui_test(test, interval)
  interval = interval or 0.1
  local function finish(ok, msg)
    io.stdout:write(ok and "PASS\n" or ("FAIL: " .. tostring(msg) .. "\n"))
    io.stdout:flush()
    Timer{ interval=0.1, ontick=function() app.exit() end }:start()
  end

  local ok, check = xpcall(test, debug.traceback)
  if not ok or not check then
    return finish(ok, check)
  end

  local ticks = 0
  local timer
  timer = Timer{
    interval=interval,
    ontick=function()
      ticks = ticks + 1
      local ok, done = xpcall(check, debug.traceback)
      if not ok or done or ticks * interval >= 20 then
        timer:stop()
        finish(ok and done, ok and "timed out" or done)
      end
    end }
  timer:start()
end
