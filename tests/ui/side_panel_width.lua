-- A side panel keeps the width that side_panel.lua gave it before the
-- app restarted, whatever width the script asks for at first

ui_test(function()
  local plain = Dialog{ title="Plain", tab=true }
  plain:label{ text="No panel" }

  -- A panel of another id has no width of its own yet
  local fresh = Dialog{ title="Fresh", tab=true }
  fresh:label{ text="Fresh" }
  fresh:panel{ id="side_panel_fresh", width=220 }
  fresh:label{ text="Panel" }

  local dlg = Dialog{ title="Panel", tab=true }
  dlg:label{ text="Main" }
  dlg:panel{ id="side_panel_test", width=120 }
  dlg:label{ text="Panel" }

  local none, wide
  local steps = {
    function()
      plain:show()
    end,
    function()
      none = plain.bounds
      fresh:show()
    end,
    function()
      wide = fresh.bounds
      assert(wide.x > none.x)
      dlg:show()
    end,
    function()
      local rc = dlg.bounds
      assert(rc.x == wide.x, "the panel is " .. (rc.x - none.x) .. " px wide")
      dlg:close()
      fresh:close()
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
