-- In the kiosk a script that doesn't come with the app opens and
-- creates no sprite and no tab dialog, and the commands that open,
-- create or close documents or tabs are disabled. The kiosk lasts for
-- the session.

ui_test(function()
  local fs = app.fs
  local png = fs.joinPath(fs.tempPath, "kiosk.png")
  Image(2, 2):saveAs(png)

  -- A sprite in a tab dialog and one in a tab of its own, from before
  local embedded = Sprite(4, 4)
  local other = Sprite(4, 4)
  local dlg = Dialog{ title="Kiosk", tab=true }
  dlg:editor{ id="editor", sprite=embedded }
  dlg:show{ wait=false }

  assert(app.kiosk == false)
  app.kiosk = true
  assert(app.kiosk == true)
  assert(not pcall(function() app.kiosk = false end))
  assert(app.kiosk == true)

  local refused = {
    function() return Sprite(4, 4) end,
    function() return Sprite{ width=4, height=4 } end,
    function() return Sprite(other) end,
    function() return Sprite{ fromFile=png } end,
    function() return app.open(png) end,
    function() return Dialog{ title="Another", tab=true } end,
  }
  for i, f in ipairs(refused) do
    local ok, err = pcall(f)
    assert(not ok, i)
    assert(tostring(err):find("kiosk", 1, true), err)
  end

  -- An image is still read from a file, and a dialog still opens
  assert(Image{ fromFile=png }.width == 2)
  Dialog{ title="Plain" }:close()

  local count = #app.sprites
  for _, id in ipairs{ "NewFile", "OpenFile", "ReopenClosedFile", "DuplicateSprite",
                       "DuplicateView", "NewSpriteFromSelection", "ImportSpriteSheet",
                       "CloseFile", "CloseAllFiles", "GotoNextTab", "GotoPreviousTab",
                       "Home" } do
    assert(app.command[id]() == false, id)
  end
  assert(app.command.SwitchColors() == true)
  assert(#app.sprites == count)

  -- The sprite the tab dialog lets go of stays open, in no tab
  dlg:modify{ id="editor", sprite=other }
  assert(#app.sprites == count)
  embedded:close()
  other:close()
  assert(#app.sprites == 0)
  dlg:close()
end)
