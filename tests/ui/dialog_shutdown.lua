-- A dialog that an onclose opens while the app ends is closed too,
-- so its own onclose runs. Only that onclose prints PASS.

local first = Dialog{
  title="First",
  onclose=function()
    local second = Dialog{
      title="Second",
      onclose=function()
        io.stdout:write("PASS\n")
        io.stdout:flush()
      end }
    second:show{ wait=false }
  end }
first:show{ wait=false }

Timer{ interval=0.1, ontick=function() app.exit() end }:start()
