-- Copyright (C) 2026  Nordtal
--
-- The work of the bootstrap without its interface: it signs in
-- through gh, finds the season repository, clones it and loads its
-- plugin. Each step starts a Process, calls back from onexit and
-- returns the process, so a test can wait() for it.
--
-- The app trusts this file: its processes start without the access
-- prompt. So that no other script steers it, it takes no program
-- path from its caller, keeps every global it uses from the moment it
-- loads, calls no string method and makes no tail call ("return f()"
-- drops the caller from the stack, and the app refuses the trust to
-- a call it can't see the whole of; "return (f())" keeps it).

local app, Process, json = app, Process, json
local ipairs, pairs, pcall, setmetatable, tonumber, tostring, type =
      ipairs, pairs, pcall, setmetatable, tonumber, tostring, type
local find, gsub, lower, match = string.find, string.gsub, string.lower, string.match
local concat, insert = table.concat, table.insert
local getinfo = debug.getinfo
local remove = os.remove
local fs = app.fs
local joinPath, isFile, isDirectory = fs.joinPath, fs.isFile, fs.isDirectory
local windows = app.os.windows

local season = {}

season.org = "nordtal"
season.host = "github.com"
season.deviceUrl = "https://github.com/login/device"

local function quote_sh(s)
  return "'" .. gsub(s, "'", "'\\''") .. "'"
end

-- The tools next to this file: gh on every platform, MinGit on
-- Windows, the system git on macOS. gh keeps its config and its token
-- in the app's config directory and ignores the environment and the
-- keychain entry of an installed gh.
local dir = match(getinfo(1, "S").source, "^@(.*)[/\\][^/\\]*$")
local gh, git
if windows then
  gh = joinPath(dir, "tools", "gh", "bin", "gh.exe")
  git = joinPath(dir, "tools", "mingit", "cmd", "git.exe")
else
  gh = joinPath(dir, "tools", "gh", "bin", "gh")
  git = "/usr/bin/git"
end
local configDir = joinPath(fs.userConfigPath, "gh")
local env = {
  GH_CONFIG_DIR=configDir,
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

-- A build without the bundled gh has no bootstrap
season.available = isFile(gh)
season.gh, season.git, season.configDir = gh, git, configDir

-- A copy of the environment of the tools
function season.env()
  local copy = {}
  for k, v in pairs(env) do copy[k] = v end
  return copy
end

-- Runs a program with the tools' environment and calls back with the
-- exit code and the lines of stdout and stderr
local function run(cmd, done, onstderr, cwd)
  local out, err = {}, {}
  local p = Process{
    cmd=cmd,
    cwd=cwd,
    env=env,
    onstdout=function(line) insert(out, line) end,
    onstderr=function(line)
      insert(err, line)
      if onstderr then onstderr(line) end
    end,
    onexit=function(code) done(code, out, err) end }
  p:start()
  return p
end

-- done(true) if gh has an account for the host
function season.signedIn(done)
  return (run({ gh, "auth", "status", "--hostname", season.host },
              function(code) done(code == 0) end))
end

-- The one-time code in a line that gh auth login prints: "First copy
-- your one-time code: XXXX-XXXX" from older versions of gh, "One-time
-- code (XXXX-XXXX) copied to clipboard" from newer ones
function season.parseDeviceCode(line)
  if find(lower(line), "one-time code", 1, true) then
    return (match(line, "(%w%w%w%w%-%w%w%w%w)"))
  end
end

-- Signs in through the browser at season.deviceUrl. oncode(code)
-- shows the device code once, done(ok, err) follows when gh is done.
-- gh keeps the token in a file of its config directory, never in the
-- keychain.
function season.signIn(oncode, done)
  local shown = false
  return (run({ gh, "auth", "login", "--hostname", season.host,
                "--git-protocol", "https", "--web", "--skip-ssh-key",
                "--insecure-storage" },
              function(exitCode, out, err) done(exitCode == 0, err) end,
              function(line)
                local code = season.parseDeviceCode(line)
                if code and not shown then
                  shown = true
                  oncode(code)
                end
              end))
end

-- Forgets the account by removing gh's config of the hosts, which
-- holds the token. gh auth logout would also delete the keychain
-- entry of the account, which an installed gh may use.
function season.signOut(done)
  local fn = joinPath(configDir, "hosts.yml")
  done(not isFile(fn) or remove(fn) ~= nil)
end

-- done(login) with the account's name, or nil
function season.login(done)
  return (run({ gh, "api", "user", "--jq", ".login" },
              function(code, out) done(code == 0 and out[1] or nil) end))
end

local writers = { ADMIN=true, MAINTAIN=true, WRITE=true }

-- The newest repository season-<n> of the given ones that isn't
-- archived and that the account can write to, as { name, number }
function season.newestWritable(repos)
  local best
  for _, repo in ipairs(repos) do
    local n = match(tostring(repo.name), "^season%-(%d+)$")
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
function season.find(done)
  return (run({ gh, "repo", "list", season.org, "--limit", "1000",
                "--json", "name,viewerPermission,isArchived" },
              function(code, out, err)
                if code ~= 0 then
                  done(nil, err)
                  return
                end
                local ok, list = pcall(json.decode, concat(out, "\n"))
                if not ok then
                  done(nil, { tostring(list) })
                  return
                end
                local repos = {}
                for _, repo in ipairs(list) do
                  insert(repos, { name=repo.name,
                                  viewerPermission=repo.viewerPermission,
                                  isArchived=repo.isArchived })
                end
                done(season.newestWritable(repos))
              end))
end

function season.url(repo)
  return "https://" .. season.host .. "/" .. season.org .. "/" .. repo.name .. ".git"
end

-- The credential helper that lets git push through the bundled gh,
-- with its own config, from the designer and from a terminal alike.
-- git runs it with sh, which MinGit has too.
function season.credentialHelper()
  local function path(p)
    if windows then
      p = gsub(p, "\\", "/")
    end
    return p
  end
  return "!unset GH_TOKEN GITHUB_TOKEN; GH_TELEMETRY=0 GH_CONFIG_DIR=" ..
         quote_sh(path(configDir)) .. " " .. quote_sh(path(gh)) .. " auth git-credential"
end

-- Clones url into target and sets the clone up: git asks the bundled
-- gh for credentials instead of any other helper, and the season
-- repository's hooks run. The settings are in the clone before it
-- fetches. done(ok, err)
function season.clone(url, target, done)
  return (run({ git, "clone",
                "--config", "credential.helper=",
                "--config", "credential.helper=" .. season.credentialHelper(),
                "--config", "core.hooksPath=.githooks",
                url, target },
              function(code, out, err) done(code == 0, err) end))
end

function season.isClone(target)
  return (isDirectory(joinPath(target, ".git")))
end

-- Loads designer/init.lua from the clone like a script of an
-- extension, trusted like this file: it defines init(plugin) and
-- exit(plugin). Its plugin object is the bootstrap's with the
-- designer folder as path and with the bootstrap's context. Returns
-- the exit function, or nil and the reason.
function season.loadPlugin(clone, plugin, context)
  local designer = joinPath(clone, "designer")
  local fn = joinPath(designer, "init.lua")
  if not isFile(fn) then
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
        return function(_, ...) return (value(plugin, ...)) end
      end
      return value
    end })
  local ok, chunk = pcall(plugin.loadTrusted, plugin, fn)
  if not ok then
    return nil, chunk
  end
  init, exit = nil, nil
  local msg
  ok, msg = pcall(chunk)
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
