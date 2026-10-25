-- Dialog{ tab=true } opens a workspace tab, and the app acts on the
-- sprite of its embedded editor

ui_test(function()
  assert(app.forkApiVersion >= 2)

  -- An editor needs a tab dialog
  assert(not pcall(function() Dialog("Window"):editor{ id="editor" } end))

  -- A wrong sprite is an error, and leaves the dialog without an editor
  local bad = Dialog{ tab=true }
  assert(not pcall(function() bad:editor{ id="editor", sprite=5 } end))
  bad:editor{ id="editor" }

  local closes, changes = 0, 0
  local dlg = Dialog{ title="Nordtal", tab=true, closable=false,
                      onclose=function() closes = closes + 1 end }
  dlg:label{ id="info", text="Workspace" }
  dlg:button{ id="ok", text="OK" }
  dlg:editor{ id="editor", onchange=function() changes = changes + 1 end }
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

  -- Closing in the tab closes its sprite, the tab stays, and onchange
  -- tells the script
  dlg:modify{ id="editor", sprite=other }
  assert(app.sprite == other)
  app.command.CloseFile()
  assert(dlg.data.editor == nil)
  assert(app.sprite == nil)
  assert(closes == 0)
  dlg:modify{ id="editor", sprite=spr }
  assert(app.sprite == spr)

  -- Another tab that embeds the sprite takes it, and onchange tells
  -- the first one
  local dlg2 = Dialog{ title="Other", tab=true }
  dlg2:editor{ id="editor", sprite=spr }
  dlg2:show()
  assert(dlg2.data.editor == spr)
  assert(dlg.data.editor == nil)
  assert(app.sprite == spr)
  dlg2:close()
  assert(app.sprite == spr)
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

  -- The events of a change the script didn't make arrive after it
  assert(changes == 0)
  return function()
    if changes < 2 then
      return false
    end
    assert(changes == 2)
    return true
  end
end)
