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

-- A line keeps the zero bytes in it
do
  local out, err, code = run{ cmd={ "printf", "a\\0b\\n" } }
  assert(code == 0)
  assert(#out == 1)
  assert(out[1] == "a\0b")
end

-- A program that leaves a process behind, which holds its output,
-- ends when it ends, and stop() still reaches what it left behind
do
  local started, left = os.time(), nil
  local p = Process{ cmd={ "sh", "-c", "sleep 300 & echo $!" },
                     onstdout=function(line) left = line end }
  p:start()
  assert(p:wait() == 0)
  assert(os.time() - started < 10)
  assert(left)
  p:stop(true)
  local out, err, code = run{ cmd={ "sh", "-c",
    "for i in $(seq 50) ; do kill -0 " .. left .. " 2>/dev/null || exit 0 ; sleep 0.1 ; done ; exit 1" } }
  assert(code == 0)
end

-- A program that leaves a process behind that keeps writing ends too,
-- with its own output first
do
  local started, first, left, exited = os.time(), nil, nil, nil
  local p = Process{ cmd={ "sh", "-c", "echo first; yes & echo $! >&2; sleep 0.5" },
                     onstdout=function(line) first = first or line end,
                     onstderr=function(line) left = line end,
                     onexit=function(code) exited = code end }
  p:start()
  assert(p:wait() == 0)
  assert(exited == 0)
  assert(os.time() - started < 10)
  assert(first == "first")
  assert(left)
  p:stop(true)
end

-- env sets variables for the program, and false removes one. The
-- rest of the app's environment reaches it as it is.
if app.forkApiVersion >= 3 then
  local home = os.getenv("HOME")
  local out, err, code = run{ cmd={ "sh", "-c", 'printf "%s|%s|%s\\n" "$NT_A" "${HOME-unset}" "$PATH"' },
                              env={ NT_A="a b=c", HOME=false } }
  assert(code == 0)
  assert(out[1] == "a b=c|unset|" .. os.getenv("PATH"))
  assert(os.getenv("HOME") == home)

  -- PATH from env is where the program is looked up
  local dir = app.fs.joinPath(app.fs.tempPath, "process-env-test")
  app.fs.makeAllDirectories(dir)
  local f = io.open(app.fs.joinPath(dir, "nt-env-probe"), "w")
  f:write("#!/bin/sh\necho found\n")
  f:close()
  os.execute("chmod +x '" .. app.fs.joinPath(dir, "nt-env-probe") .. "'")
  out, err, code = run{ cmd={ "nt-env-probe" }, env={ PATH=dir .. ":" .. os.getenv("PATH") } }
  assert(code == 0)
  assert(out[1] == "found")

  -- Only strings and false are values
  assert(not pcall(function() Process{ cmd={ "true" }, env={ A=1 } } end))
  assert(not pcall(function() Process{ cmd={ "true" }, env={ [1]="x" } } end))
  assert(not pcall(function() Process{ cmd={ "true" }, env={ ["A=B"]="x" } } end))
end
assert(app.forkApiVersion >= 3)

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
