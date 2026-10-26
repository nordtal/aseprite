-- Copyright (C) 2026  Nordtal
--
-- The app loads text chunks only: no loader runs precompiled bytecode,
-- and a C module is looked for where the app looked when it started

-- string.dump() still makes bytecode, which nothing loads
local bytecode = string.dump(function() return 1 end)
assert(type(bytecode) == "string")

local function write(fn, text)
  local f = assert(io.open(fn, "wb"))
  f:write(text)
  f:close()
end

-- load()
assert(load("return 1")() == 1)
assert(load("return 1", "=text", "t")() == 1)
assert(load(bytecode) == nil)
for _, mode in ipairs{ "b", "bt", "tb" } do
  local ok, f = pcall(load, "return 1", "=text", mode)
  assert(not ok or f == nil, mode)
  ok, f = pcall(load, bytecode, "=bytecode", mode)
  assert(not ok or f == nil, mode)
end

-- loadfile() and dofile()
local dir = app.fs.joinPath(app.fs.currentPath, "_tmp_text_chunks")
app.fs.makeAllDirectories(dir)
local fn = app.fs.joinPath(dir, "bytecode.lua")
write(fn, bytecode)
assert(loadfile(fn) == nil)
local ok, f = pcall(loadfile, fn, "b")
assert(not ok or f == nil)
assert(not pcall(dofile, fn))

-- require() of a Lua module
local path = package.path
package.path = app.fs.joinPath(dir, "?.lua")
write(app.fs.joinPath(dir, "text.lua"), "return 'text'")
assert(require("text") == "text")
assert(not pcall(require, "bytecode"))

-- require() of a C module: package.cpath has no effect
local cpath = package.cpath
package.cpath = app.fs.joinPath(dir, "?.so")
local planted = app.fs.joinPath(dir, "cmod.so")
write(planted, "no library")
for _, name in ipairs{ "cmod", "cmod.sub" } do
  local ok, err = pcall(require, name)
  assert(not ok and not err:find(planted, 1, true), err)
end
package.cpath, package.path = cpath, path

-- A script file of bytecode doesn't run
if not app.os.windows then
  local quote = function(s) return "'" .. s:gsub("'", "'\\''") .. "'" end
  write(fn, string.dump(function() return 0 end))
  assert(not os.execute(quote(app.fs.appPath) .. " -b --script " .. quote(fn) .. " 2>/dev/null"))
end

for _, name in ipairs(app.fs.listFiles(dir)) do
  os.remove(app.fs.joinPath(dir, name))
end
app.fs.removeDirectory(dir)
