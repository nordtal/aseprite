-- The bootstrap opens a numbered file alone, without the question
-- whether to open its series as an animation, which would wait for a
-- click until the test is stopped

ui_test(function()
  local fs = app.fs
  local folder = fs.joinPath(fs.tempPath, "open-numbered")
  fs.makeAllDirectories(folder)
  for i = 1, 2 do
    local image = Image(4, 4)
    image:clear(Color{ r=i * 100, g=0, b=0 })
    image:saveAs(fs.joinPath(folder, "panel_" .. i .. ".png"))
  end

  local sprite = app.open(fs.joinPath(folder, "panel_1.png"))
  assert(sprite)
  assert(#sprite.frames == 1)
  sprite:close()
end)
