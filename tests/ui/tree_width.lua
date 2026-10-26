-- Dialog:tree{ width } gives the tree a width of its own, whatever its
-- nodes hold, and leaves the rest of its row to what shares it

ui_test(function()
  local painted
  local dlg = Dialog{ title="Tree width", tab=true }
  dlg:tree{ id="files", width=100, hexpand=false, nodes={
    { id="long", text=string.rep("a node with a long name ", 100) },
    { id="short", text="x" },
  } }
  dlg:samerow()
  dlg:canvas{ id="rest", onpaint=function(ev) painted = ev.context.width end }
  dlg:show()

  local scale = app.uiScale
  local before, ticks = nil, 0
  return function()
    ticks = ticks + 1
    if not painted then
      -- A canvas with no room isn't painted
      assert(ticks < 20, "the canvas got no room")
      return false
    end
    if not before then
      before = painted
      assert(before > 100 * scale, "the tree leaves " .. before .. " px")
      -- A new width takes room from the rest of the row
      dlg:modify{ id="files", width=300 }
      painted = nil
      dlg:repaint()
      return false
    end
    assert(before - painted == 200 * scale, before .. " px became " .. painted .. " px")
    assert(app.forkApiVersion >= 5)
    dlg:close()
    return true
  end
end)
