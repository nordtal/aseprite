-- Dialog{ tab=true } opens a workspace tab, and the app acts on the
-- sprite of its embedded editor

ui_test(function()
  assert(app.forkApiVersion >= 2)

  -- An editor needs a tab dialog
  assert(not pcall(function() Dialog("Window"):editor{ id="editor" } end))

  local closes = 0
  local dlg = Dialog{ title="Nordtal", tab=true, closable=false,
                      onclose=function() closes = closes + 1 end }
  dlg:label{ id="info", text="Workspace" }
  dlg:button{ id="ok", text="OK" }
  dlg:editor{ id="editor" }
  assert(dlg.data.editor == nil)

  -- Before the tab is shown, the editor remembers its sprite
  local spr = Sprite(16, 16)
  assert(app.sprite == spr)
  dlg:modify{ id="editor", sprite=spr }
  assert(dlg.data.editor == spr)
  dlg:show()

  -- The tab is active, and the app works on the embedded sprite
  assert(app.sprite == spr)
  assert(app.editor.sprite == spr)
  local layers = #spr.layers
  app.command.NewLayer()
  assert(#spr.layers == layers + 1)
  assert(app.layer == spr.layers[layers + 1])
  app.command.Undo()
  assert(#spr.layers == layers)

  -- Another sprite opens in a tab of its own, and show() returns to the tab
  local other = Sprite(8, 8)
  assert(app.sprite == other)
  dlg:show()
  assert(app.sprite == spr)

  -- A new sprite in the editor sends the old one back to a tab of its own
  dlg:modify{ id="editor", sprite=other }
  assert(dlg.data.editor == other)
  assert(app.sprite == other)
  app.sprite = spr
  assert(app.sprite == spr)
  assert(app.editor.sprite == spr)
  dlg:show()
  assert(app.sprite == other)

  -- An empty editor leaves no active sprite
  dlg:modify{ id="editor", sprite=false }
  assert(dlg.data.editor == nil)
  assert(app.sprite == nil)

  -- Closing in the tab closes its sprite, the tab stays
  dlg:modify{ id="editor", sprite=other }
  assert(app.sprite == other)
  app.command.CloseFile()
  assert(dlg.data.editor == nil)
  assert(app.sprite == nil)
  assert(closes == 0)
  dlg:modify{ id="editor", sprite=spr }
  assert(app.sprite == spr)

  -- close() ends even a tab that can't be closed, and its sprite
  -- goes back to a tab of its own
  dlg:close()
  assert(closes == 1)
  assert(app.sprite == spr)
  assert(app.editor.sprite == spr)

  -- The tab opens again with its sprite, and the app ends with the
  -- tab open
  dlg:show()
  assert(app.sprite == spr)
  assert(dlg.data.editor == spr)
end)
