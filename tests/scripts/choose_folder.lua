-- app.fs.chooseFolder() opens the native folder chooser, which needs
-- the UI: without it, there is no folder to return

assert(app.forkApiVersion >= 1)
assert(type(app.fs.chooseFolder) == "function")
assert(app.fs.chooseFolder() == nil)
assert(app.fs.chooseFolder{ title="Pick a folder", path=app.fs.tempPath } == nil)
