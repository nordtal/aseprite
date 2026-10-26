-- A side panel stays docked through the layout and mode commands, its
-- widgets keep working while it's docked, and the app ends with the
-- panel docked.

ui_test(function()
  -- The panel's id is one of the dialog's ids
  local taken = Dialog{ tab=true }
  taken:entry{ id="name" }
  assert(not pcall(function() taken:panel{ id="name" } end))
  local panelFirst = Dialog{ tab=true }
  panelFirst:panel{ id="side" }
  assert(not pcall(function() panelFirst:entry{ id="side" } end))

  -- The command asks nothing in a test
  app.preferences.advanced_mode.show_alert = false

  local plain = Dialog{ title="Plain", tab=true }
  plain:label{ text="No panel" }

  local spr = Sprite(16, 16)
  local changed
  local dlg = Dialog{ title="Session", tab=true }
  dlg:label{ text="Main" }
  dlg:panel{ id="side_panel_session", width=150 }
  dlg:entry{ id="name", text="properties" }
  dlg:newrow()
  dlg:editor{ id="editor", sprite=spr, onchange=function()
    -- A widget in the panel reads the dialog's data
    changed = { name=dlg.data.name, editor=dlg.data.editor }
  end }

  local other = Dialog{ title="Other", tab=true }
  other:editor{ id="editor", sprite=spr }

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
      assert(shown.x > none.x)
      plain:show()
    end,
    function()
      -- A tab that isn't active has no place in the window
      assert(dlg.bounds.isEmpty, "an inactive tab is at " .. tostring(dlg.bounds))
      dlg:show()
    end,
    function()
      assert(dlg.bounds == shown)
      app.command.ToggleWorkspaceLayout()
    end,
    function()
      -- The layout selector leaves the panel where it is
      assert(dlg.bounds.x == shown.x, "the tab starts at " .. dlg.bounds.x)
      app.command.ToggleWorkspaceLayout()
    end,
    function()
      assert(dlg.bounds == shown)
      app.command.AdvancedMode()
    end,
    function()
      -- Outside the normal mode the panel hides, like the colour bar
      assert(dlg.bounds.x < shown.x, "the tab starts at " .. dlg.bounds.x)
      app.command.AdvancedMode()
    end,
    function()
      assert(dlg.bounds.x < shown.x)
      app.command.AdvancedMode()
    end,
    function()
      assert(dlg.bounds == shown, "the panel came back at " .. tostring(dlg.bounds))
      -- Another tab takes the sprite, which the editor's onchange hears
      other:show()
    end,
    function()
      assert(changed, "onchange didn't run")
      assert(changed.name == "properties")
      assert(changed.editor == nil)
      dlg:show()
    end,
    function()
      -- The app ends while the panel is docked
      assert(dlg.bounds.x > none.x)
    end,
  }

  local step = 0
  return function()
    step = step + 1
    steps[step]()
    return step == #steps
  end
end)
