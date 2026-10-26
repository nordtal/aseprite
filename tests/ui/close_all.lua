-- Close All survives a script that moves a sprite into its tab while
-- the command closes the others

ui_test(function()
  local first = Sprite(8, 8)
  local second = Sprite(8, 8)
  local dlg = Dialog{ title="Refill", tab=true }
  dlg:editor{ id="editor" }
  dlg:show()

  -- When the first sprite closes, the script takes the second one,
  -- whose own tab Close All hasn't closed yet
  local moved = false
  local function onsite()
    if not moved and app.sprite ~= first then
      moved = true
      dlg:modify{ id="editor", sprite=second }
    end
  end
  app.events:on("sitechange", onsite)
  app.command.CloseAllFiles()
  app.events:off(onsite)

  assert(moved)
  -- The second sprite is closed in the tab that took it
  assert(#app.sprites == 0)

end)
