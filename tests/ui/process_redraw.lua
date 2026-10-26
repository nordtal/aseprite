-- A dialog that a Process callback changes shows the change at once,
-- not only at the next input or timer event

ui_test(function()
  local green = false
  local dlg = Dialog("Process redraw")
  dlg:canvas{ width=20, height=20, onpaint=function(ev)
    ev.context.color = green and Color(0, 255, 0) or Color(255, 0, 0)
    ev.context:fillRect(Rectangle(0, 0, ev.context.width, ev.context.height))
  end }
  dlg:show{ wait=false }
  -- In a corner, away from the dialogs the app shows at its start
  local b = dlg.bounds
  dlg.bounds = Rectangle(8, 40, b.width, b.height)

  -- true if the screen shows the canvas green. The screenshot passes
  -- through the display's color profile, which can turn pure green
  -- into (9, 251, 48). The bounds leave room for that and stay far
  -- from the red the canvas shows before the change.
  local function shown()
    app.command.Screenshot()
    local img = app.image
    local b = dlg.bounds
    local found = false
    for y = b.y, b.y + b.height - 1 do
      for x = b.x, b.x + b.width - 1 do
        local c = img:getPixel(x, y)
        if app.pixelColor.rgbaR(c) < 64 and app.pixelColor.rgbaG(c) > 192 and
           app.pixelColor.rgbaB(c) < 64 then
          found = true
        end
      end
    end
    app.command.CloseFile{ ui=false }
    return found
  end

  local exited = false
  local ticks = 0
  -- The ticks are a second apart, so nothing but the process wakes the
  -- app between them
  return function()
    ticks = ticks + 1
    if ticks == 1 then
      Process{ cmd={ "sh", "-c", "exit 0" },
               onexit=function()
                 exited = true
                 green = true
                 dlg:repaint()
               end }:start()
      return false
    end
    assert(exited, "the process didn't exit within a second")
    assert(shown(), "the change of the dialog isn't on the screen a second after the exit")
    dlg:close()
    return true
  end
end, 1)
