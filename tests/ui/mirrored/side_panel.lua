-- In the mirrored layout the colour bar is at the right edge, and a
-- side panel goes to the far right, outside it

ui_test(function()
  local plain = Dialog{ title="Plain", tab=true }
  plain:label{ text="No panel" }

  local dlg = Dialog{ title="Panel", tab=true }
  dlg:label{ text="Main" }
  dlg:panel{ id="side_panel_mirrored", width=120 }
  dlg:label{ text="Panel" }

  local scale = app.uiScale
  local function right(rc) return rc.x + rc.width end
  local none, shown
  local steps = {
    function()
      plain:show()
    end,
    function()
      none = plain.bounds
      dlg:show()
    end,
    function()
      shown = dlg.bounds
      assert(shown.x == none.x, "the tab starts at " .. shown.x)
      assert(right(none) - right(shown) >= 120 * scale,
             "the tab ends at " .. right(shown))
      -- The far side is left of the tools
      dlg:modify{ id="side_panel_mirrored", side="right" }
    end,
    function()
      local rc = dlg.bounds
      assert(right(rc) == right(none))
      assert(rc.x - none.x == right(none) - right(shown))
      dlg:close()
    end,
    function()
      assert(plain.bounds == none, "the panel stays after its dialog closed")
      plain:close()
    end,
  }

  local step = 0
  return function()
    step = step + 1
    steps[step]()
    return step == #steps
  end
end)
