-- Copyright (C) 2019  Igara Studio S.A.
--
-- This file is released under the terms of the MIT license.
-- Read LICENSE.txt for more information.

local fs = app.fs
local sep = fs.pathSeparator

assert('' == fs.filePath('first.png'))
assert('path' == fs.filePath('path/second.png'))
if app.os.windows then
  assert('C:\\path' == fs.filePath('C:\\path\\third.png'))
end

assert('first.png' == fs.fileName('first.png'))
assert('second.png' == fs.fileName('path/second.png'))
if app.os.windows then
  assert('third.png' == fs.fileName('C:\\path\\third.png'))
end

assert('png' == fs.fileExtension('path/file.png'))

assert('first' == fs.fileTitle('first.png'))
assert('second' == fs.fileTitle('path/second.png'))
if app.os.windows then
  assert('third' == fs.fileTitle('C:\\path\\third.png'))
end

assert('first' == fs.filePathAndTitle('first.png'))
assert('path/second' == fs.filePathAndTitle('path/second.png'))
if app.os.windows then
  assert('C:\\path\\third' == fs.filePathAndTitle('C:\\path\\third.png'))
end

assert('hi/bye' == fs.joinPath('hi/', 'bye'))
assert('hi/bye' .. sep .. 'smth.png' == fs.joinPath('hi/', 'bye', 'smth.png'))

local pwd = fs.currentPath
assert(pwd ~= nil)
assert(fs.isDirectory(pwd))
assert(not fs.isFile(pwd))
assert(fs.isFile(fs.joinPath(pwd, 'run-tests.sh')))

do
  local runTestsFound = false
  local readmeFound = false
  local files = fs.listFiles(pwd)
  for i in pairs(files) do
    if files[i] == 'run-tests.sh' then
      runTestsFound = true
    elseif files[i] == 'README.md' then
      readmeFound = true
    end

    local fullFs = fs.joinPath(pwd, files[i])
    if fs.isFile(fullFs) then
      assert(fs.fileSize(fullFs) > 0)
    end
  end
  assert(runTestsFound)
  assert(readmeFound)
end

-- Create directories
do
  assert(fs.makeDirectory("_tmp"))
  assert(fs.isDirectory("_tmp"))

  assert(fs.makeAllDirectories("_tmp/a/b"))
  assert(fs.isDirectory("_tmp/a"))
  assert(fs.isDirectory("_tmp/a/b"))

  assert(fs.removeDirectory("_tmp/a/b"))
  assert(not fs.isDirectory("_tmp/a/b"))
  assert(fs.isDirectory("_tmp/a"))

  assert(not fs.removeDirectory("_tmp")) -- Should fail
  assert(fs.isDirectory("_tmp/a"))
  assert(fs.isDirectory("_tmp"))

  assert(fs.removeDirectory("_tmp/a"))
  assert(fs.removeDirectory("_tmp"))
  assert(not fs.isDirectory("_tmp/a"))
  assert(not fs.isDirectory("_tmp"))
end

-- Remove a folder with what it holds, without following its links
do
  assert(app.forkApiVersion >= 4)
  assert(fs.makeAllDirectories("_tmp_tree/a/b"))
  assert(fs.makeAllDirectories("_tmp_outside"))
  local function write(fn) local f = io.open(fn, "w") f:write("x") f:close() end
  write("_tmp_tree/a/b/file")
  write("_tmp_outside/kept")
  if not app.os.windows then
    assert(os.execute("ln -s ../_tmp_outside _tmp_tree/a/link"))
    assert(os.execute("ln -s ../_tmp_outside/kept _tmp_tree/filelink"))
  end
  assert(fs.removeTree("_tmp_tree"))
  assert(not fs.isDirectory("_tmp_tree"))
  assert(fs.isFile("_tmp_outside/kept"))
  -- What isn't there is removed already; a file is no folder
  assert(fs.removeTree("_tmp_tree"))
  assert(not fs.removeTree("_tmp_outside/kept"))
  assert(fs.isFile("_tmp_outside/kept"))
  if not app.os.windows then
    -- A path to a link that ends in a separator or a dot names the
    -- link, never the folder it points to
    assert(os.execute("ln -s _tmp_outside _tmp_link"))
    for _, path in ipairs{ "_tmp_link/", "_tmp_link//", "_tmp_link/." } do
      assert(not fs.removeTree(path), path)
      assert(fs.isFile("_tmp_outside/kept"), path)
    end
    assert(os.execute("rm _tmp_link"))
    -- A folder that can't be looked at isn't removed
    assert(fs.makeAllDirectories("_tmp_locked/sub"))
    assert(os.execute("chmod 000 _tmp_locked"))
    if not os.execute("ls _tmp_locked/sub >/dev/null 2>&1") then
      assert(not fs.removeTree("_tmp_locked/sub"))
    end
    assert(os.execute("chmod 755 _tmp_locked"))
    assert(fs.removeTree("_tmp_locked"))
  end
  os.remove("_tmp_outside/kept")
  assert(fs.removeDirectory("_tmp_outside"))
end
