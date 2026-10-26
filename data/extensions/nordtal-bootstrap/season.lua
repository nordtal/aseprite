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
local find, gmatch, gsub, lower, match =
      string.find, string.gmatch, string.gsub, string.lower, string.match
local concat, insert = table.concat, table.insert
local max = math.max
local getinfo = debug.getinfo
local lines = io.lines
local getenv, remove, rename = os.getenv, os.remove, os.rename
local fs = app.fs
local joinPath, filePath, isFile, isDirectory = fs.joinPath, fs.filePath, fs.isFile, fs.isDirectory
local listFiles, makeAllDirectories, removeTree =
      fs.listFiles, fs.makeAllDirectories, fs.removeTree
local open = io.open
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

local function parseSha256(out)
  for _, line in ipairs(out) do
    local hex = match(gsub(line, " ", ""), "^(%x+)")
    if hex and #hex == 64 then
      return (lower(hex))
    end
  end
end

-- Downloads an archive and puts the one folder it holds at o.target.
-- o.download is the command that writes the archive to o.archive,
-- whose sha256 must be o.checksum. o.unpack(archive, folder) is the
-- command that unpacks it into the empty folder o.part, and
-- o.valid(folder) tells whether the folder it holds is what o.holds
-- names. o.what names the download in the reasons. done(problem) with
-- nil, or the lines that tell why it failed.
local function fetchFolder(s, o, done)
  local function fail(err)
    remove(o.archive)
    done(err)
  end
  s.run(o.download, function(code, out, err)
    if code ~= 0 then
      fail(err[1] and err or { o.what .. " could not be downloaded" })
      return
    end
    s.run(sha256(o.archive), function(code, out, err)
      if parseSha256(out) ~= o.checksum then
        fail({ "the download of " .. o.what .. " doesn't match its checksum" })
        return
      end
      -- The archive is unpacked into an empty folder, which a failed
      -- try leaves behind with whatever it unpacked. A target that
      -- fails the check is what a failed try left, too. A link or a
      -- file there stays, and tar would unpack through it, so it stops
      -- the try.
      for _, folder in ipairs({ o.part, o.target }) do
        if not removeTree(folder) then
          fail({ folder .. " could not be removed" })
          return
        end
      end
      makeAllDirectories(o.part)
      s.run(o.unpack(o.archive, o.part), function(code, out, err)
        remove(o.archive)
        local files = listFiles(o.part)
        local problem
        if code ~= 0 then
          problem = err[1] and err or { o.what .. " could not be unpacked" }
        elseif #files ~= 1 or not o.valid(joinPath(o.part, files[1])) then
          problem = { "the download of " .. o.what .. " holds no single " .. o.holds }
        elseif not rename(joinPath(o.part, files[1]), o.target) then
          problem = { o.what .. " could not be moved to " .. o.target }
        end
        if not removeTree(o.part) then
          problem = problem or { o.part .. " could not be removed" }
        end
        done(problem)
      end)
    end)
  end)
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
  makeAllDirectories(javaRoot)
  local url = adoptium .. version .. "/hotspot?image_type=jdk&vendor=eclipse" ..
              "&os=" .. (windows and "windows" or macos and "mac" or "linux") ..
              "&architecture=" .. (arm64 and "aarch64" or "x64")
  s.run({ curl, "-fsSL", url }, function(code, out, err)
    if code ~= 0 then
      done(nil, err)
      return
    end
    local ok, list = pcall(json.decode, concat(out, "\n"))
    local pkg = ok and list[1] and list[1].binary and list[1].binary.package
    local checksum = pkg and pkg.checksum and lower(tostring(pkg.checksum))
    if not checksum or not match(checksum, "^%x+$") or #checksum ~= 64 or not pkg.link then
      done(nil, { "the Adoptium API has no JDK " .. version .. " for this computer" })
      return
    end
    fetchFolder(s, {
      download={ curl, "-fsSL", "-o", archive, tostring(pkg.link) },
      archive=archive,
      checksum=checksum,
      unpack=function(a, folder) return { tar, "-xf", a, "-C", folder } end,
      part=target .. ".part",
      target=target,
      valid=function(folder) return (hasJava(javaHome(folder))) end,
      what="JDK " .. version,
      holds="JDK folder",
    }, function(problem)
      if problem then
        done(nil, problem)
        return
      end
      finish()
    end)
  end)
  return s
end

-- The designer's own releases. A tag is v<aseprite version>-designer.<n>,
-- and the release workflow writes it into the file release next to this
-- one. A build without that file, as every build but a release is,
-- never updates.
season.repo = season.org .. "/aseprite"
season.release = nil
do
  local ok, it = pcall(lines, joinPath(dir, "release"))
  if ok then
    for line in it do
      local tag = match(line, "^%s*(.-)%s*$")
      if tag ~= "" then season.release = tag end
      break
    end
  end
end

-- The release asset that runs on this computer
season.package = (macos and arm64 and "Nordtal-Designer-macOS-arm64.zip") or
                 (windows and "Nordtal-Designer-Windows-x64.zip") or nil

-- The entries of the app's folder on Windows, as the package holds it
local ownEntries = { ["Nordtal Designer.exe"]=true, data=true }

-- true if the folder holds the app on Windows and nothing else. The
-- update replaces the whole folder, so it's never one the designer
-- keeps other files in, such as Downloads.
function season.ownFolder(folder)
  local files = listFiles(folder)
  if #files ~= 2 then return false end
  for _, name in ipairs(files) do
    if not ownEntries[name] then return false end
  end
  return (isFile(joinPath(folder, "Nordtal Designer.exe")) and
          isDirectory(joinPath(folder, "data")))
end

-- true if folder is the app: the bundle on macOS, the folder on Windows
local function isApp(folder)
  if windows then
    return (season.ownFolder(folder))
  end
  return (isFile(joinPath(folder, "Contents", "MacOS", "Nordtal Designer")))
end

-- The app an update replaces, or nil if there is none it may replace
if macos then
  season.target = match(fs.appPath, "^(.+%.app)/Contents/MacOS/[^/]+$")
elseif windows and season.ownFolder(filePath(fs.appPath)) then
  season.target = filePath(fs.appPath)
end
local packaged = windows and "Nordtal Designer" or "Nordtal Designer.app"

-- The numbers of the Aseprite version and the designer's number of a
-- tag, or nil
local function parseTag(tag)
  if type(tag) ~= "string" then return nil end
  local version, n = match(tag, "^v([%d%.]+)%-designer%.(%d+)$")
  if not version or find(version, "..", 1, true) or find(version, "^%.") or
     find(version, "%.$") then
    return nil
  end
  local numbers = {}
  for part in gmatch(version, "%d+") do insert(numbers, tonumber(part)) end
  return { numbers=numbers, n=tonumber(n) }
end

-- true if tag a is a newer release than tag b. A missing number of the
-- Aseprite version counts as 0.
function season.newer(a, b)
  local x, y = parseTag(a), parseTag(b)
  if not x or not y then return false end
  for i = 1, max(#x.numbers, #y.numbers) do
    local p, q = x.numbers[i] or 0, y.numbers[i] or 0
    if p ~= q then return p > q end
  end
  return x.n > y.n
end

-- The update to the latest release, as GitHub's API describes it, from
-- the app's tag current: { tag, asset, sha256 }, or nil if it isn't
-- newer or has no package with a sha256 for this computer
function season.updateFrom(latest, current, package)
  if type(latest) ~= "table" or latest.draft or latest.prerelease or not package or
     not season.newer(latest.tag_name, current) or type(latest.assets) ~= "table" then
    return nil
  end
  for _, asset in ipairs(latest.assets) do
    if type(asset) == "table" and asset.name == package then
      local sum = match(tostring(asset.digest), "^sha256:(%x+)$")
      if sum and #sum == 64 then
        return { tag=latest.tag_name, asset=package, sha256=lower(sum) }
      end
    end
  end
end

-- done(update, err) with the update to the latest release, or nil.
-- A build without a tag, a package or an app it may replace doesn't
-- ask and returns nil.
function season.latestUpdate(done)
  if not season.release or not season.package or not season.target then
    done(nil)
    return
  end
  return (run({ gh, "api", "repos/" .. season.repo .. "/releases/latest" },
              function(code, out, err)
                if code ~= 0 then
                  done(nil, err)
                  return
                end
                -- The release as the plain tables that updateFrom() takes
                local ok, latest = pcall(json.decode, concat(out, "\n"))
                if ok then
                  ok, latest = pcall(function()
                    local assets = {}
                    for _, asset in ipairs(latest.assets) do
                      insert(assets, { name=asset.name, digest=asset.digest })
                    end
                    return { tag_name=latest.tag_name, draft=latest.draft,
                             prerelease=latest.prerelease, assets=assets }
                  end)
                end
                done(ok and season.updateFrom(latest, season.release, season.package) or nil)
              end))
end

-- Moves the app at target to old and the one at staged in its place;
-- if that fails, the old app goes back. true, or nil and the reason.
function season.replace(staged, target, old)
  if not removeTree(old) then
    return nil, old .. " could not be removed"
  end
  if not isDirectory(staged) then
    return nil, "there is no " .. staged
  end
  if not rename(target, old) then
    return nil, target .. " could not be moved"
  end
  if not rename(staged, target) then
    if not rename(old, target) then
      return nil, staged .. " could not be moved to " .. target .. ", and the old app is in " .. old
    end
    return nil, staged .. " could not be moved to " .. target
  end
  removeTree(old)
  return true
end

-- The batch file that replaces the app folder target with staged on
-- Windows, as season.replace() does on macOS. It tries once a second
-- for a minute to move the folder, which works once the app has quit,
-- and leaves the old folder if it can't. Then it removes work, the
-- folder it's in, unless the app isn't at target, which a failed way
-- back leaves in old. Returns nil and the reason for a path that cmd
-- would expand.
function season.windowsHelper(target, staged, old, work)
  for _, path in ipairs({ target, staged, old, work }) do
    if find(path, "%", 1, true) then
      return nil, path .. " has a % in it, which the installer can't use"
    end
  end
  return (concat({
    "@echo off",
    "setlocal DisableDelayedExpansion",
    "chcp 65001 >nul",
    -- explorer.exe may start it in the folder it moves
    'cd /d "%SystemRoot%"',
    "rem Nordtal Designer puts its new version in place once it has quit",
    'set "target=' .. target .. '"',
    'set "staged=' .. staged .. '"',
    'set "old=' .. old .. '"',
    'set "work=' .. work .. '"',
    "set tries=0",
    ":wait",
    'move "%target%" "%old%" >nul 2>&1',
    "if not errorlevel 1 goto swap",
    "set /a tries+=1",
    "if %tries% geq 60 goto done",
    "ping -n 2 127.0.0.1 >nul",
    "goto wait",
    ":swap",
    'move "%staged%" "%target%" >nul 2>&1',
    'if errorlevel 1 move "%old%" "%target%" >nul 2>&1',
    ":done",
    'if not exist "%target%\\Nordtal Designer.exe" exit /b 1',
    -- (goto) ends the batch file before rmdir removes it
    '(goto) 2>nul & rmdir /s /q "%work%"',
    "" }, "\r\n"))
end

-- The update an install left ready for the app's exit
local pending

-- Installs the update in place of the app at target. The package
-- comes through gh, is checked against the release's sha256 and is
-- unpacked into a folder next to the app, so the new app moves into
-- place without a copy. On macOS it loses the quarantine attribute, as
-- the first install does. season.finishInstall() puts it in place as
-- the app exits. done(ok, err). Returns the steps, or nil if done was
-- called already.
function season.install(update, target, done)
  local work = joinPath(filePath(target), ".nordtal-designer-update")
  local staged = joinPath(work, packaged)
  local old = joinPath(work, "old")
  local archive = joinPath(work, update.asset)
  pending = nil
  if not removeTree(work) then
    done(false, { work .. " could not be removed" })
    return
  end
  makeAllDirectories(work)
  if not isDirectory(work) then
    done(false, { filePath(target) .. " can't be written to, so the update can't be put next to the app" })
    return
  end
  local function fail(err)
    removeTree(work)
    done(false, err)
  end
  local function ready(helper)
    pending = { target=target, staged=staged, old=old, work=work, helper=helper }
    done(true)
  end
  local s = steps()
  fetchFolder(s, {
    download={ gh, "release", "download", update.tag, "--repo", season.repo,
               "--pattern", update.asset, "--output", archive },
    archive=archive,
    checksum=update.sha256,
    unpack=function(a, folder)
      if windows then
        return { tar, "-xf", a, "-C", folder }
      end
      return { "/usr/bin/ditto", "-x", "-k", a, folder }
    end,
    part=joinPath(work, "part"),
    target=staged,
    valid=isApp,
    what=update.tag,
    holds="app",
  }, function(problem)
    if problem then
      fail(problem)
      return
    end
    if windows then
      local text, why = season.windowsHelper(target, staged, old, work)
      local helper = joinPath(work, "install.cmd")
      local f = text and open(helper, "wb")
      if not f then
        fail({ why or helper .. " could not be written" })
        return
      end
      f:write(text)
      f:close()
      ready(helper)
      return
    end
    s.run({ "/usr/bin/xattr", "-dr", "com.apple.quarantine", staged }, function(code, out, err)
      if code ~= 0 then
        fail(err[1] and err or { "the quarantine attribute could not be removed" })
        return
      end
      ready()
    end)
  end)
  return s
end

-- Puts the installed update in place as the app exits, so the app
-- never runs with the files of another version. On macOS the bundle
-- is swapped here; the folder the update came in goes only while the
-- app is at its place, since a failed way back leaves it in that
-- folder. On Windows the app's folder can't move while it runs, so
-- the helper waits for that, started through explorer.exe, which
-- hands it to the shell: it outlives the app, which ends every
-- process it started when it exits.
function season.finishInstall()
  local p = pending
  if not p then return end
  pending = nil
  if windows then
    run({ joinPath(getenv("SystemRoot") or "C:\\Windows", "explorer.exe"), p.helper },
        function() end):wait()
    return
  end
  season.replace(p.staged, p.target, p.old)
  if isApp(p.target) then
    removeTree(p.work)
  end
end

function season.isClone(target)
  return (isDirectory(joinPath(target, ".git")))
end

-- Loads designer/init.lua from the clone like a script of an
-- extension, trusted like this file and in an environment of its own:
-- it defines init(plugin) and exit(plugin) there, and require(name)
-- or plugin.require(name) loads designer/<name>.lua, dots for folders,
-- into that environment, once. Its plugin object
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
  -- plugin.require(name) loads a module of the designer folder into
  -- the plugin's environment, like require() there
  proxy.require = env.require
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
