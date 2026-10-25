-- Dialog:tree{} holds nodes with a label, an icon and a badge

ui_test(function()
  assert(app.forkApiVersion >= 1)

  local changes = 0
  local dlg = Dialog("Tree")
  dlg:tree{
    id="files",
    nodes={
      { id="sprites", text="Sprites", icon="folder_icon_small", badge="2",
        children={
          { id="hero", text="hero.aseprite" },
          { id="slime", text="slime.aseprite", badge=1 },
        } },
      { id="readme", text="README" },
    },
    onchange=function(ev) changes = changes + 1 end,
  }
  dlg:show{ wait=false }

  -- Nothing is selected at first
  assert(dlg.data.files == nil)

  -- Selecting a hidden node works, and a programmatic change fires no event
  dlg.data = { files="slime" }
  assert(dlg.data.files == "slime")
  assert(changes == 0)

  -- A single node changes, the selection stays
  dlg:modifyNode{ id="files", node="slime", text="slime-2.aseprite", badge="3" }
  dlg:modifyNode{ id="files", node="sprites", badge=false }
  assert(dlg.data.files == "slime")

  -- New children replace the old ones; the selection goes with its node
  dlg:modifyNode{ id="files", node="sprites", children={ { id="bat", text="bat.aseprite" } } }
  assert(dlg.data.files == nil)
  dlg.data = { files="bat" }
  assert(dlg.data.files == "bat")

  -- Replacing every node keeps the selection when its ID survives
  dlg:modify{ id="files", nodes={
    { id="sprites", text="Sprites", children={ { id="bat", text="bat.aseprite" } } },
  } }
  assert(dlg.data.files == "bat")
  dlg:modify{ id="files", nodes={ { id="readme", text="README" } } }
  assert(dlg.data.files == nil)

  -- Unknown trees and nodes are errors
  assert(not pcall(function() dlg:modifyNode{ id="files", node="nothing" } end))
  assert(not pcall(function() dlg:modifyNode{ id="nothing", node="readme" } end))

  dlg:close()
end)
