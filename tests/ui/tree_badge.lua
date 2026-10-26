-- A tree paints each badge at the right edge of what it shows, so a
-- tree with a width shows the badges of its longer nodes unscrolled,
-- and a tree without one paints them at the end of its nodes

ui_test(function()
  local scale = app.uiScale
  local face = app.theme.color.listitem_selected_face

  local function near(a, b) return math.abs(a - b) <= 4 end

  -- Bounds of the badge pixels a screenshot holds inside the dialog.
  -- The screenshot is converted to sRGB, which rounds the colors.
  local function badge(dlg)
    app.command.Screenshot()
    local img = app.image
    local b = dlg.bounds
    local x1, y1, x2, y2
    for y = b.y, b.y + b.height - 1 do
      for x = b.x, b.x + b.width - 1 do
        local c = img:getPixel(x, y)
        if near(app.pixelColor.rgbaR(c), face.red) and
           near(app.pixelColor.rgbaG(c), face.green) and
           near(app.pixelColor.rgbaB(c), face.blue) then
          x1 = math.min(x1 or x, x)
          y1 = math.min(y1 or y, y)
          x2 = math.max(x2 or x, x)
          y2 = math.max(y2 or y, y)
        end
      end
    end
    app.command.CloseFile{ ui=false }
    return x1 and Rectangle(x1, y1, x2 - x1 + 1, y2 - y1 + 1)
  end

  local function tree(width)
    local dlg = Dialog("Tree badge")
    dlg:tree{ id="files", width=width, nodes={
      { id="long", text=string.rep("a node with a long name ", width and 20 or 3), badge="7" },
    } }
    dlg:show{ wait=false }
    -- In a corner, away from the dialogs the app shows at its start
    local b = dlg.bounds
    dlg.bounds = Rectangle(8, 40, b.width, b.height)
    return dlg
  end

  local fixed, free
  local ticks = 0
  return function()
    ticks = ticks + 1
    -- The dialogs are painted before the screenshot
    if ticks == 1 then
      fixed = tree(100)
      return false
    end
    if ticks == 3 then
      local b = fixed.bounds
      local rc = badge(fixed)
      assert(rc, "no badge in a tree with a width")
      -- Right-aligned in the 100 px the tree shows, past its label
      assert(rc.x + rc.width <= b.x + b.width, "the badge ends at " .. rc.x + rc.width)
      assert(rc.x > b.x + 50 * scale, "the badge starts at " .. rc.x - b.x .. " px")
      fixed:close()
      free = tree(nil)
      return false
    end
    if ticks == 5 then
      local b = free.bounds
      local rc = badge(free)
      assert(rc, "no badge in a tree without a width")
      -- The dialog is as wide as the node, and the badge ends it
      assert(b.width > 200 * scale, "the dialog is " .. b.width .. " px wide")
      assert(rc.x > b.x + b.width - 50 * scale, "the badge starts at " .. rc.x - b.x .. " px")
      free:close()
      return true
    end
    return false
  end
end)
