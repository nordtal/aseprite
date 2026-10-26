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
local lines = io.lines
local getenv, remove, rename = os.getenv, os.remove, os.rename
local fs = app.fs
local joinPath, isFile, isDirectory = fs.joinPath, fs.isFile, fs.isDirectory
local listFiles, makeAllDirectories, removeDirectory =
      fs.listFiles, fs.makeAllDirectories, fs.removeDirectory
local windows, macos, arm64 = app.os.windows, app.os.macos, app.os.arm64

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

-- Steps that run one process after the other. steps:wait() waits
-- for the last one, which a test needs.
local function steps()
  local s = {}
  function s.run(cmd, done, cwd)
    s.current = run(cmd, done, nil, cwd)
  end
  function s.wait()
    local p
    repeat
      p = s.current
      p:wait()
    until s.current == p
  end
  return s
end

local adoptium = "https://api.adoptium.net/v3/assets/latest/"
local javaRoot = joinPath(fs.userConfigPath, "java")

local function system32(name)
  return (joinPath(getenv("SystemRoot") or "C:\\Windows", "System32", name))
end
local curl = windows and system32("curl.exe") or "/usr/bin/curl"
local tar = windows and system32("tar.exe") or "/usr/bin/tar"

-- The version in the daemon JVM criteria that gradle updateDaemonJvm
-- writes into the clone, or nil and the reason
local function javaVersion(clone)
  local fn = joinPath(clone, "gradle", "gradle-daemon-jvm.properties")
  local ok, it = pcall(lines, fn)
  if not ok then
    return nil, "there is no " .. fn
  end
  for line in it do
    local version = match(line, "^toolchainVersion=(.*)$")
    if version then
      if match(version, "^%d+$") then
        return version
      end
      return nil, "toolchainVersion=" .. version .. " in " .. fn .. " is no number"
    end
  end
  return nil, "there is no toolchainVersion in " .. fn
end

-- The JDK home in the folder of an unpacked JDK
local function javaHome(dir)
  local mac = joinPath(dir, "Contents", "Home")
  if isDirectory(mac) then
    return mac
  end
  return dir
end

local function hasJava(home)
  return (isFile(joinPath(home, "bin", windows and "java.exe" or "java")))
end

-- The sha256 of a file, in lower case: cmd prints it, parse finds it
local function sha256(fn)
  if windows then
    return { system32("certutil.exe"), "-hashfile", fn, "SHA256" }
  end
  if macos then
    return { "/usr/bin/shasum", "-a", "256", fn }
  end
  return { "/usr/bin/sha256sum", fn }
end

-- Removes a folder and what it holds, without following its links
local function removeTree(dir)
  if windows then
    return { system32("cmd.exe"), "/d", "/c", "rmdir", "/s", "/q", dir }
  end
  return { "/bin/rm", "-rf", dir }
end

local function parseSha256(out)
  for _, line in ipairs(out) do
    local hex = match(gsub(line, " ", ""), "^(%x+)")
    if hex and #hex == 64 then
      return (lower(hex))
    end
  end
end

-- done(home, err) with the home of the Eclipse Temurin JDK that the
-- clone's daemon JVM criteria ask for. The first time it comes from
-- the Adoptium API, checked against the API's sha256, into a folder
-- of the app's config. The tools' environment gets it as JAVA_HOME,
-- so gradlew runs with it. Returns the steps, or nil if done was
-- called already.
function season.java(clone, done)
  local version, problem = javaVersion(clone)
  if not version then
    done(nil, { problem })
    return
  end
  local target = joinPath(javaRoot, version)
  local function finish()
    local home = javaHome(target)
    env.JAVA_HOME = home
    done(home)
  end
  if hasJava(javaHome(target)) then
    finish()
    return
  end

  local s = steps()
  local archive = target .. (windows and ".zip" or ".tar.gz")
  local part = target .. ".part"
  local function fail(err)
    remove(archive)
    done(nil, err)
  end
  makeAllDirectories(javaRoot)
  local url = adoptium .. version .. "/hotspot?image_type=jdk&vendor=eclipse" ..
              "&os=" .. (windows and "windows" or macos and "mac" or "linux") ..
              "&architecture=" .. (arm64 and "aarch64" or "x64")
  s.run({ curl, "-fsSL", url }, function(code, out, err)
    if code ~= 0 then
      fail(err)
      return
    end
    local ok, list = pcall(json.decode, concat(out, "\n"))
    local pkg = ok and list[1] and list[1].binary and list[1].binary.package
    local checksum = pkg and pkg.checksum and lower(tostring(pkg.checksum))
    if not checksum or not match(checksum, "^%x+$") or #checksum ~= 64 or not pkg.link then
      fail({ "the Adoptium API has no JDK " .. version .. " for this computer" })
      return
    end
    s.run({ curl, "-fsSL", "-o", archive, tostring(pkg.link) }, function(code, out, err)
      if code ~= 0 then
        fail(err)
        return
      end
      s.run(sha256(archive), function(code, out, err)
        if parseSha256(out) ~= checksum then
          fail({ "the download of JDK " .. version .. " doesn't match its checksum" })
          return
        end
        -- The archive is unpacked into an empty folder, which a failed
        -- try leaves behind with whatever it unpacked
        s.run(removeTree(part), function()
          makeAllDirectories(part)
          s.run({ tar, "-xf", archive, "-C", part }, function(code, out, err)
            remove(archive)
            local files = listFiles(part)
            local problem
            if code ~= 0 then
              problem = err[1] and err or { "JDK " .. version .. " could not be unpacked" }
            elseif #files ~= 1 or not hasJava(javaHome(joinPath(part, files[1]))) then
              problem = { "the download of JDK " .. version .. " holds no single JDK folder" }
            elseif not rename(joinPath(part, files[1]), target) then
              problem = { "JDK " .. version .. " could not be moved to " .. target }
            end
            if problem then
              s.run(removeTree(part), function() done(nil, problem) end)
              return
            end
            removeDirectory(part)
            finish()
          end)
        end)
      end)
    end)
  end)
  return s
end

function season.isClone(target)
  return (isDirectory(joinPath(target, ".git")))
end

-- Loads designer/init.lua from the clone like a script of an
-- extension, trusted like this file and in an environment of its own:
-- it defines init(plugin) and exit(plugin) there. Its plugin object
-- is the bootstrap's with the designer folder as path and with the
-- bootstrap's context. Returns the exit function, or nil and the
-- reason.
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
  local ok, chunk, env = pcall(plugin.loadTrusted, plugin, fn)
  if not ok then
    return nil, chunk
  end
  local msg
  ok, msg = pcall(chunk)
  if not ok then
    return nil, msg
  end
  local pluginInit, pluginExit = env.init, env.exit
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
