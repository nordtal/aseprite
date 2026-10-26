-- A tree whose nodes have no children reserves no expander column,
-- and a tree with a child does

ui_test(function()
  local flat, nested
  local dlg = Dialog{ title="Flat tree", tab=true }
  dlg:tree{ id="flat", hexpand=false, nodes={
    { id="a", text="alpha" },
    { id="b", text="beta" },
  } }
  dlg:samerow()
  dlg:canvas{ id="flat_rest", onpaint=function(ev) flat = ev.context.width end }
  dlg:newrow()
  -- The same rows, as the child is collapsed
  dlg:tree{ id="nested", hexpand=false, nodes={
    { id="c", text="alpha", children={ { id="d", text="d" } } },
    { id="e", text="beta" },
  } }
  dlg:samerow()
  dlg:canvas{ id="nested_rest", onpaint=function(ev) nested = ev.context.width end }
  dlg:show()

  local scale = app.uiScale
  local ticks = 0
  return function()
    ticks = ticks + 1
    if not flat or not nested then
      assert(ticks < 20, "the canvases got no room")
      return false
    end
    assert(flat - nested == 12 * scale,
           "the flat tree's row has " .. flat .. " px, the other " .. nested .. " px")
    dlg:close()
    return true
  end
end)
