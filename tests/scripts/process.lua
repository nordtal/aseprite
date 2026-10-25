-- Process starts a program from an argument list, without a shell

assert(app.forkApiVersion >= 1)

local function run(args)
  local out, err, exited = {}, {}, nil
  args.onstdout = function(line) table.insert(out, line) end
  args.onstderr = function(line) table.insert(err, line) end
  args.onexit = function(code) exited = code end
  local p = Process(args)
  assert(not p.isRunning)
  assert(p.exitCode == nil)
  p:start()
  local code = p:wait()
  assert(code == exited)
  assert(code == p.exitCode)
  assert(not p.isRunning)
  return out, err, code
end

-- Every argument reaches the program as it is, spaces and quotes included
do
  local out, err, code = run{ cmd={ "printf", "%s\\n", "a b", "'c'", "$HOME" } }
  assert(code == 0)
  assert(#out == 3)
  assert(out[1] == "a b")
  assert(out[2] == "'c'")
  assert(out[3] == "$HOME")
  assert(#err == 0)
end

-- Stdout and stderr arrive line by line, then the exit code
do
  local out, err, code = run{ cmd={ "sh", "-c", "echo one; echo two; echo bad >&2; printf last; exit 3" } }
  assert(code == 3)
  assert(#out == 3)
  assert(out[1] == "one")
  assert(out[2] == "two")
  assert(out[3] == "last")
  assert(#err == 1)
  assert(err[1] == "bad")
end

-- The working directory
do
  local dir = app.fs.joinPath(app.fs.tempPath, "process-cwd-test")
  app.fs.makeAllDirectories(dir)
  local out, err, code = run{ cmd={ "pwd" }, cwd=dir }
  assert(code == 0)
  assert(app.fs.fileName(out[1]) == "process-cwd-test")
end

-- A stopped process ends with the signal in its exit code
do
  local p = Process{ cmd={ "sleep", "30" } }
  p:start()
  assert(p.isRunning)
  p:stop()
  local code = p:wait()
  assert(not p.isRunning)
  assert(code == 128 + 15)
end

-- stop(true) kills a process that ignores the request to end
do
  local p = Process{ cmd={ "sh", "-c", "trap '' TERM; sleep 30" } }
  p:start()
  p:stop(true)
  assert(p:wait() == 128 + 9)
end

-- A process can only start once
do
  local p = Process{ cmd={ "true" } }
  p:start()
  assert(not pcall(function() p:start() end))
  assert(p:wait() == 0)
end

-- A program that doesn't exist fails to start
do
  local p = Process{ cmd={ "this-program-does-not-exist-anywhere" } }
  local ok, msg = pcall(function() p:start() end)
  assert(not ok)
  assert(msg:find("this-program-does-not-exist-anywhere", 1, true))
  assert(not p.isRunning)
end

-- A program in a missing working directory fails to start
do
  local p = Process{ cmd={ "pwd" }, cwd=app.fs.joinPath(app.fs.tempPath, "no-such-dir-for-process") }
  assert(not pcall(function() p:start() end))
end

-- The argument list must hold at least the program
assert(not pcall(function() Process{} end))
assert(not pcall(function() Process{ cmd={} } end))
assert(not pcall(function() Process{ cmd="ls -l" } end))
