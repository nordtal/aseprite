-- An entry field pads its text by the 4-point edge of its frame plus
-- a gap of 2 points on each side

ui_test(function()
  local rest, ruler, w
  local dlg = Dialog{ title="Entry padding", tab=true }
  dlg:entry{ id="name", hexpand=false }
  dlg:samerow()
  dlg:canvas{ id="rest", hexpand=true, onpaint=function(ev)
    rest = ev.context.width
    w = ev.context:measureText("w").width
  end }
  dlg:newrow()
  dlg:canvas{ id="fixed", width=50, hexpand=false }
  dlg:samerow()
  dlg:canvas{ id="ruler", hexpand=true, onpaint=function(ev) ruler = ev.context.width end }
  dlg:show()

  local scale = app.uiScale
  local ticks = 0
  return function()
    ticks = ticks + 1
    if not rest or not ruler then
      assert(ticks < 20, "the canvases got no room")
      return false
    end
    -- Both rows share their width and spacing, so the entry is as
    -- wide as the fixed canvas plus what its row's canvas has less
    local entry = 50 * scale + ruler - rest
    -- Six "w" and the room of two carets, as Entry::onSizeHint asks
    local border = entry - 6 * w - 4 * scale
    assert(border == 12 * scale, "the entry's frame and padding take " .. border .. " px")
    dlg:close()
    return true
  end
end)
