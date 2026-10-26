-- Dialog:splitter{} puts the side panel's widgets before it above a
-- splitter and the ones after it below. It starts at half the height,
-- or where the user dragged it before: cli/ui.sh keeps 25 % for the
-- panel side_panel_split_kept.

ui_test(function()
  assert(app.forkApiVersion >= 7)

  -- A splitter needs a side panel, once at most and not inside tabs
  assert(not pcall(function() Dialog{ tab=true }:splitter{} end))
  local twice = Dialog{ tab=true }
  twice:panel{}
  twice:splitter{}
  assert(not pcall(function() twice:splitter{} end))
  local tabs = Dialog{ tab=true }
  tabs:panel{}
  tabs:tab{ id="t", text="T" }
  assert(not pcall(function() tabs:splitter{} end))

  local heights = {}
  local function panel(id)
    local dlg = Dialog{ title=id, tab=true }
    dlg:label{ text="Main" }
    dlg:panel{ id=id, width=150 }
    dlg:canvas{ id="above", onpaint=function(ev) heights.above = ev.context.height end }
    dlg:splitter{}
    dlg:canvas{ id="below", onpaint=function(ev) heights.below = ev.context.height end }
    return dlg
  end

  -- The share of the panel's height that the part above has
  local function share()
    return heights.above / (heights.above + heights.below)
  end

  local dlg
  local steps = {
    function()
      dlg = panel("side_panel_split_test")
      dlg:show()
    end,
    function()
      assert(heights.above and heights.below, "a part got no room")
      assert(heights.above > 0 and math.abs(heights.above - heights.below) <= 1,
             heights.above .. " px above, " .. heights.below .. " px below")
      dlg:close()
      heights = {}
      dlg = panel("side_panel_split_kept")
      dlg:show()
    end,
    function()
      assert(heights.above and heights.below, "a part got no room")
      assert(math.abs(share() - 0.25) < 0.02, "the part above has " .. share())
      dlg:close()
    end,
  }

  local step = 0
  return function()
    step = step + 1
    steps[step]()
    return step == #steps
  end
end)
