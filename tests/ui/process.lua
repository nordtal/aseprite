-- In the GUI the callbacks of a process run on their own

ui_test(function()
  local lines, code = {}, nil

  -- They run on the main Lua thread, even if a coroutine created the
  -- process and is gone
  coroutine.wrap(function()
    Process{ cmd={ "sh", "-c", "echo one; echo two" },
             onstdout=function(line) table.insert(lines, line) end,
             onexit=function(c) code = c end }:start()
  end)()
  collectgarbage()
  collectgarbage()

  -- The app ends without waiting for what a program left behind
  Process{ cmd={ "sh", "-c", "sleep 300 & echo left" } }:start()

  return function()
    if code == nil then
      return false
    end
    assert(code == 0)
    assert(#lines == 2)
    assert(lines[1] == "one")
    assert(lines[2] == "two")
    return true
  end
end)
