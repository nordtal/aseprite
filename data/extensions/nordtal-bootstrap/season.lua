-- Copyright (C) 2026  Nordtal
--
-- The work of the bootstrap without its interface: it signs in
-- through gh, finds the season repository, clones it and loads its
-- plugin. Each step starts a Process, calls back from onexit and
-- returns the process, so a test can wait() for it.

local season = {}

season.org = "nordtal"
season.host = "github.com"

local function is_windows()
  return app.os.windows
end

local function quote_sh(s)
  return "'" .. s:gsub("'", "'\\''") .. "'"
end

-- The tools next to this file: gh on every platform, MinGit on
-- Windows, the system git on macOS. gh keeps its config in the app's
-- config directory and ignores the environment of an installed gh.
function season.tools(dir)
  local gh, git
  if is_windows() then
    gh = app.fs.joinPath(dir, "tools", "gh", "bin", "gh.exe")
    git = app.fs.joinPath(dir, "tools", "mingit", "cmd", "git.exe")
  else
    gh = app.fs.joinPath(dir, "tools", "gh", "bin", "gh")
    git = "/usr/bin/git"
  end
  if not app.fs.isFile(gh) then
    return nil
  end
  return {
    gh=gh,
    git=git,
    env={
      GH_CONFIG_DIR=app.fs.joinPath(app.fs.userConfigPath, "gh"),
      GH_TOKEN=false,
      GITHUB_TOKEN=false,
      GH_ENTERPRISE_TOKEN=false,
      GITHUB_ENTERPRISE_TOKEN=false,
      GH_HOST=false,
      GH_REPO=false,
      GH_FORCE_TTY=false,
      GH_NO_UPDATE_NOTIFIER="1",
      GH_TELEMETRY="0",
    }
  }
end

-- Runs a program with the tools' environment and calls back with the
-- exit code and the lines of stdout and stderr
local function run(tools, cmd, done, onstderr, cwd)
  local out, err = {}, {}
  local p = Process{
    cmd=cmd,
    cwd=cwd,
    env=tools.env,
    onstdout=function(line) table.insert(out, line) end,
    onstderr=function(line)
      table.insert(err, line)
      if onstderr then onstderr(line) end
    end,
    onexit=function(code) done(code, out, err) end }
  p:start()
  return p
end

-- done(true) if gh has an account for the host
function season.signedIn(tools, done)
  return run(tools, { tools.gh, "auth", "status", "--hostname", season.host },
             function(code) done(code == 0) end)
end

-- The one-time code and the URL in a line that gh auth login prints:
-- "First copy your one-time code: XXXX-XXXX" from older versions of
-- gh, "One-time code (XXXX-XXXX) copied to clipboard" from newer ones
function season.parseDeviceCode(line)
  local code
  if line:lower():find("one-time code", 1, true) then
    code = line:match("(%w%w%w%w%-%w%w%w%w)")
  end
  local url = line:match("(https://%S+)")
  return code, url
end

-- Signs in through the browser. oncode(code, url) shows the device
-- code, done(ok, err) follows when gh is done.
function season.signIn(tools, oncode, done)
  local code
  local url = "https://" .. season.host .. "/login/device"
  return run(tools,
             { tools.gh, "auth", "login", "--hostname", season.host,
               "--git-protocol", "https", "--web", "--skip-ssh-key" },
             function(exitCode, out, err) done(exitCode == 0, err) end,
             function(line)
               local c, u = season.parseDeviceCode(line)
               if c then code = c end
               if u then url = u end
               if code and (c or u) then oncode(code, url) end
             end)
end

function season.signOut(tools, done)
  return run(tools, { tools.gh, "auth", "logout", "--hostname", season.host },
             function(code) done(code == 0) end)
end

-- done(login) with the account's name, or nil
function season.login(tools, done)
  return run(tools, { tools.gh, "api", "user", "--jq", ".login" },
             function(code, out) done(code == 0 and out[1] or nil) end)
end

local writers = { ADMIN=true, MAINTAIN=true, WRITE=true }

-- The newest repository season-<n> of the given ones that isn't
-- archived and that the account can write to, as { name, number }
function season.newestWritable(repos)
  local best
  for _, repo in ipairs(repos) do
    local n = tostring(repo.name):match("^season%-(%d+)$")
    if n and writers[repo.viewerPermission] and not repo.isArchived then
      n = tonumber(n)
      if not best or n > best.number then
        best = { name=repo.name, number=n }
      end
    end
  end
  return best
end

-- done(repo, err) with the newest season repository the account can
-- write to, or nil
function season.find(tools, done)
  return run(tools,
             { tools.gh, "repo", "list", season.org, "--limit", "1000",
               "--json", "name,viewerPermission,isArchived" },
             function(code, out, err)
               if code ~= 0 then
                 return done(nil, err)
               end
               local ok, list = pcall(json.decode, table.concat(out, "\n"))
               if not ok then
                 return done(nil, { tostring(list) })
               end
               local repos = {}
               for _, repo in ipairs(list) do
                 table.insert(repos, { name=repo.name,
                                       viewerPermission=repo.viewerPermission,
                                       isArchived=repo.isArchived })
               end
               done(season.newestWritable(repos))
             end)
end

function season.url(repo)
  return "https://" .. season.host .. "/" .. season.org .. "/" .. repo.name .. ".git"
end

-- The credential helper that lets git push through the bundled gh,
-- with its own config, from the designer and from a terminal alike
function season.credentialHelper(tools)
  local function path(p)
    return is_windows() and p:gsub("\\", "/") or p
  end
  return "!GH_CONFIG_DIR=" .. quote_sh(path(tools.env.GH_CONFIG_DIR)) .. " " ..
         quote_sh(path(tools.gh)) .. " auth git-credential"
end

-- Clones url into dir and sets the clone up: git asks the bundled gh
-- for credentials instead of any other helper, and the season
-- repository's hooks run. The settings are in the clone before it
-- fetches. done(ok, err)
function season.clone(tools, url, dir, done)
  local helper = season.credentialHelper(tools)
  return run(tools,
             { tools.git, "clone",
               "--config", "credential.helper=",
               "--config", "credential.helper=" .. helper,
               "--config", "core.hooksPath=.githooks",
               url, dir },
             function(code, out, err) done(code == 0, err) end)
end

function season.isClone(dir)
  return app.fs.isDirectory(app.fs.joinPath(dir, ".git"))
end

-- Loads designer/init.lua from the clone like a script of an
-- extension: it defines init(plugin) and exit(plugin). Its plugin
-- object is the bootstrap's with the designer folder as path and
-- with the bootstrap's context. Returns the exit function, or nil
-- and the reason.
function season.loadPlugin(dir, plugin, context)
  local designer = app.fs.joinPath(dir, "designer")
  local fn = app.fs.joinPath(designer, "init.lua")
  if not app.fs.isFile(fn) then
    return nil, "there is no " .. fn
  end
  -- The plugin's preferences are its own, inside the bootstrap's
  plugin.preferences.plugin = plugin.preferences.plugin or {}
  local proxy = setmetatable({ path=designer,
                               preferences=plugin.preferences.plugin,
                               bootstrap=context }, {
    __index=function(_, key)
      local value = plugin[key]
      if type(value) == "function" then
        return function(_, ...) return value(plugin, ...) end
      end
      return value
    end })
  init, exit = nil, nil
  local ok, msg = pcall(dofile, fn)
  local pluginInit, pluginExit = init, exit
  init, exit = nil, nil
  if not ok then
    return nil, msg
  end
  if type(pluginInit) == "function" then
    ok, msg = pcall(pluginInit, proxy)
    if not ok then
      return nil, msg
    end
  end
  return function()
    if type(pluginExit) == "function" then
      pluginExit(proxy)
    end
  end
end

return season
