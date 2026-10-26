-- Dialog:panel{} gives a tab dialog a side panel. The main window
-- docks it at its far left, outside the colour bar, while the tab is
-- active, and hides it while another tab is active.
-- side_panel_width.lua checks the width that this script leaves.

ui_test(function()
  assert(app.forkApiVersion >= 6)

  -- A side panel needs a tab dialog, and a dialog has one at most
  assert(not pcall(function() Dialog("Window"):panel{} end))
  local twice = Dialog{ tab=true }
  twice:panel{}
  assert(not pcall(function() twice:panel{} end))
  assert(not pcall(function() Dialog{ tab=true }:panel{ side="top" } end))

  local plain = Dialog{ title="Plain", tab=true }
  plain:label{ text="No panel" }

  local dlg = Dialog{ title="Panel", tab=true }
  dlg:canvas{ id="main" }
  dlg:panel{ id="side_panel_test", width=120 }
  dlg:tree{ id="tree", nodes={ { id="a", text="A" } } }
  dlg:newrow()
  dlg:entry{ id="name", text="properties" }
  assert(dlg.data.name == "properties")

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
      -- The panel takes room at the left edge, outside the tab
      shown = dlg.bounds
      assert(shown.x >= none.x + 120 * scale, "the tab starts at " .. shown.x)
      assert(right(shown) == right(none))
      plain:show()
    end,
    function()
      -- Another tab hides it
      assert(plain.bounds == none, "the panel stays with another tab")
      dlg:show()
    end,
    function()
      -- It comes back with its width
      assert(dlg.bounds == shown, "the panel came back at another width")
      dlg:modify{ id="side_panel_test", width=220 }
    end,
    function()
      local rc = dlg.bounds
      assert(rc.x == shown.x + 100 * scale, shown.x .. " became " .. rc.x)
      shown = rc
      -- The far side is right of the tools
      dlg:modify{ id="side_panel_test", side="right" }
    end,
    function()
      local rc = dlg.bounds
      assert(rc.x == none.x, "the tab starts at " .. rc.x)
      assert(right(none) - right(rc) == shown.x - none.x)
      dlg:close()
    end,
    function()
      -- Closing the dialog removes its panel
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
