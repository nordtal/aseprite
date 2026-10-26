-- The bootstrap signs in through gh, finds the newest season
-- repository the account can write to, clones it and loads its plugin

if app.os.windows then return end

local function quote(s) return "'" .. s:gsub("'", "'\\''") .. "'" end

-- gh never sees a token of the environment, so the test runs with one
if not os.getenv("GH_TOKEN") or not os.getenv("GITHUB_TOKEN") then
  local script = debug.getinfo(1, "S").source:sub(2)
  assert(os.execute("GH_TOKEN=from-env GITHUB_TOKEN=from-env " .. quote(app.fs.appPath) ..
                    " -b --script " .. quote(script)))
  return
end

local source = app.fs.joinPath(app.fs.currentPath, "..", "data", "extensions",
                               "nordtal-bootstrap", "season.lua")
local tmp = app.fs.joinPath(app.fs.tempPath, "nordtal-bootstrap-test")
os.execute("rm -rf " .. quote(tmp))
app.fs.makeAllDirectories(tmp)

local function write(fn, text)
  app.fs.makeAllDirectories(app.fs.filePath(fn))
  local f = io.open(fn, "w")
  f:write(text)
  f:close()
end

local function read(fn)
  local f = io.open(fn)
  local text = f:read("a")
  f:close()
  return text
end

-- season.lua finds its tools next to itself, so the test uses a copy.
-- The copy asks a folder instead of the Adoptium API.
local ext = app.fs.joinPath(tmp, "nordtal-bootstrap")
local adoptium = app.fs.joinPath(tmp, "adoptium")
do
  local text, n = read(source):gsub("https://api%.adoptium%.net/v3/assets/latest/",
                                    "file://" .. adoptium .. "/")
  assert(n == 1)
  write(app.fs.joinPath(ext, "season.lua"), text)
end
local function loadSeason() return dofile(app.fs.joinPath(ext, "season.lua")) end

-- Without a bundled gh there is no bootstrap
assert(loadSeason().available == false)

-- A stub gh that answers like gh does, and logs its environment
local gh = app.fs.joinPath(ext, "tools", "gh", "bin", "gh")
write(gh, [[#!/bin/sh
echo "$GH_CONFIG_DIR|${GH_TOKEN-unset}|${GITHUB_TOKEN-unset}|$*" >> "$(dirname "$0")/calls"
case "$1 $2" in
  "auth status") [ -f "$GH_CONFIG_DIR/hosts.yml" ] ;;
  "auth login")
    echo >&2
    echo "! One-time code (ABCD-1234) copied to clipboard" >&2
    # The sign-in shows the code once, however often gh prints it
    echo "! One-time code (ABCD-1234) copied to clipboard" >&2
    echo "Open this URL to continue in your web browser: https://github.com/login/device" >&2
    sleep 1
    mkdir -p "$GH_CONFIG_DIR" && echo "github.com:" > "$GH_CONFIG_DIR/hosts.yml"
    # Without --insecure-storage gh keeps the token in the keychain
    case "$*" in *--insecure-storage*) echo "    oauth_token: from-stub-gh" >> "$GH_CONFIG_DIR/hosts.yml" ;; esac ;;
  "auth git-credential") cat > /dev/null; printf 'username=designer\npassword=from-stub-gh\n' ;;
  "api user") echo designer ;;
  "repo list") cat "$(dirname "$0")/repos.json" ;;
  *) exit 1 ;;
esac
]])
os.execute("chmod +x " .. quote(gh))
local season = loadSeason()
assert(season.available)
assert(season.gh == gh)
assert(season.git == "/usr/bin/git")
local configDir = app.fs.joinPath(app.fs.userConfigPath, "gh")
assert(season.configDir == configDir)
assert(season.env().GH_CONFIG_DIR == configDir)
os.execute("rm -rf " .. quote(configDir))

-- The newest season the account can write to, archived ones left out
do
  local repos = {
    { name="season-1", viewerPermission="ADMIN", isArchived=true },
    { name="season-2", viewerPermission="WRITE", isArchived=false },
    { name="season-3", viewerPermission="READ", isArchived=false },
    { name="season-10-art", viewerPermission="ADMIN", isArchived=false },
    { name="workspace", viewerPermission="ADMIN", isArchived=false },
  }
  local repo = season.newestWritable(repos)
  assert(repo.name == "season-2" and repo.number == 2)
  repos[3].viewerPermission = "MAINTAIN"
  assert(season.newestWritable(repos).name == "season-3")
  table.insert(repos, { name="season-12", viewerPermission="WRITE", isArchived=false })
  assert(season.newestWritable(repos).name == "season-12")
  assert(season.newestWritable({ { name="season-2", viewerPermission="TRIAGE" } }) == nil)
end

-- Signing in shows the device code once, while gh keeps waiting for
-- the browser, then gh has the account and its token in its own config
do
  local signedIn
  season.signedIn(function(ok) signedIn = ok end):wait()
  assert(signedIn == false)

  local codes, runningAtCode, ok, p = {}, nil, nil, nil
  p = season.signIn(function(c)
                      table.insert(codes, c)
                      runningAtCode = p.isRunning
                    end,
                    function(o) ok = o end)
  p:wait()
  assert(#codes == 1 and codes[1] == "ABCD-1234")
  assert(runningAtCode == true)
  assert(ok)
  assert(season.deviceUrl == "https://github.com/login/device")
  assert(read(app.fs.joinPath(configDir, "hosts.yml")):find("oauth_token: from-stub-gh", 1, true))
  season.signedIn(function(o) signedIn = o end):wait()
  assert(signedIn == true)

  local login
  season.login(function(l) login = l end):wait()
  assert(login == "designer")
end

-- gh never gets a token of the environment, only its own config
do
  assert(os.getenv("GH_TOKEN") == "from-env")
  local f = io.open(app.fs.joinPath(ext, "tools", "gh", "bin", "calls"))
  for line in f:lines() do
    assert(line:find(configDir .. "|unset|unset|", 1, true) == 1, line)
  end
  f:close()
end

-- The account's repositories decide which season is cloned
do
  write(app.fs.joinPath(ext, "tools", "gh", "bin", "repos.json"), [[
[{"isArchived":false,"name":"season-2","viewerPermission":"WRITE"},
 {"isArchived":false,"name":"season-3","viewerPermission":"READ"}]
]])
  local repo
  season.find(function(r) repo = r end):wait()
  assert(repo.name == "season-2")
  assert(season.url(repo) == "https://github.com/nordtal/season-2.git")

  write(app.fs.joinPath(ext, "tools", "gh", "bin", "repos.json"),
        '[{"isArchived":false,"name":"season-2","viewerPermission":"READ"}]')
  local called = false
  season.find(function(r) called = true; repo = r end):wait()
  assert(called and repo == nil)
end

-- Signing out removes the account
do
  local signedIn
  local out
  season.signOut(function(ok) out = ok end)
  assert(out == true)
  -- gh auth logout would delete a keychain entry an installed gh uses
  for line in io.lines(app.fs.joinPath(ext, "tools", "gh", "bin", "calls")) do
    assert(not line:find("|auth logout", 1, true), line)
  end
  season.signedIn(function(ok) signedIn = ok end):wait()
  assert(signedIn == false)
end

-- The clone asks the bundled gh for credentials and runs the
-- repository's hooks
do
  local origin = app.fs.joinPath(tmp, "origin")
  assert(os.execute("git init -q '" .. origin .. "' && cd '" .. origin .. "'" ..
                    " && mkdir designer && echo 'probe.loaded = (probe.loaded or 0) + 1' > designer/init.lua" ..
                    " && git add . && git -c user.name=t -c user.email=t@t commit -qm init"))
  local dir = app.fs.joinPath(tmp, "Nordtal", "season-2")
  assert(not season.isClone(dir))
  local ok, err
  season.clone("file://" .. origin, dir, function(o, e) ok, err = o, e end):wait()
  assert(ok, err and table.concat(err, "\n"))
  assert(season.isClone(dir))

  local function config(key)
    local p = io.popen("git -C '" .. dir .. "' config --local --get-all " .. key)
    local values = {}
    for line in p:lines() do table.insert(values, line) end
    p:close()
    return values
  end
  assert(config("core.hooksPath")[1] == ".githooks")
  local helpers = config("credential.helper")
  assert(#helpers == 2)
  assert(helpers[1] == "")
  assert(helpers[2] == "!unset GH_TOKEN GITHUB_TOKEN; GH_TELEMETRY=0 GH_CONFIG_DIR=" ..
                      quote(configDir) .. " " .. quote(gh) .. " auth git-credential")

  -- git in the clone gets its credentials from the bundled gh, even
  -- from a terminal without the app's environment and with tokens in it
  local p = io.popen("printf 'protocol=https\\nhost=github.com\\n\\n' | " ..
                     "GIT_TERMINAL_PROMPT=0 GH_CONFIG_DIR=/nowhere GH_TOKEN=t GITHUB_TOKEN=t " ..
                     "git -C '" .. dir .. "' credential fill")
  local filled = p:read("a")
  p:close()
  assert(filled:find("password=from-stub-gh", 1, true), filled)
  local f = io.open(app.fs.joinPath(ext, "tools", "gh", "bin", "calls"))
  local last
  for line in f:lines() do last = line end
  f:close()
  assert(last == configDir .. "|unset|unset|auth git-credential get", last)

  -- The clone's plugin gets its folder, its own preferences and the
  -- context. The plugin object of the app loads it trusted, in an
  -- environment of its own; here the stand-in loads it as any file, in
  -- an environment that also holds the probe table of this test.
  local probe = {}
  local plugin = { preferences={ cloneRoot="x" },
                   loadTrusted=function(_, fn)
                     local env = setmetatable({ probe=probe, require=probe }, { __index=_G })
                     return assert(loadfile(fn, "t", env)), env
                   end }
  local exitPlugin, msg = season.loadPlugin(dir, plugin, { clone=dir })
  assert(exitPlugin, msg)
  assert(probe.loaded == 1)

  write(app.fs.joinPath(dir, "designer", "init.lua"), [[
function init(plugin)
  probe.path, probe.clone, probe.require = plugin.path, plugin.bootstrap.clone, plugin.require
  plugin.preferences.n = 1
end
function exit(plugin) probe.exit = true end
]])
  exitPlugin = season.loadPlugin(dir, plugin, { clone=dir })
  assert(probe.path == app.fs.joinPath(dir, "designer"))
  assert(probe.clone == dir)
  assert(probe.require == probe, "plugin.require is the require() of its environment")
  assert(plugin.preferences.plugin.n == 1)
  assert(plugin.preferences.cloneRoot == "x")
  assert(init == nil and exit == nil)
  exitPlugin()
  assert(probe.exit)

  -- A clone without the plugin, or with a broken one, says why
  write(app.fs.joinPath(dir, "designer", "init.lua"), "error('broken')")
  local none
  none, msg = season.loadPlugin(dir, plugin, {})
  assert(none == nil and msg:find("broken"))
  os.remove(app.fs.joinPath(dir, "designer", "init.lua"))
  none, msg = season.loadPlugin(dir, plugin, {})
  assert(none == nil and msg:find("init.lua", 1, true))
end

-- The JDK for gradlew is the one the clone's daemon JVM criteria name,
-- downloaded once into the app's config and checked against the
-- checksum the API gives
do
  local clone = app.fs.joinPath(tmp, "java-clone")
  local javaDir = app.fs.joinPath(app.fs.userConfigPath, "java")
  os.execute("rm -rf " .. quote(javaDir))
  local function java(dir)
    local home, err
    local steps = season.java(dir, function(h, e) home, err = h, e end)
    if steps then steps:wait() end
    return home, err
  end

  -- Without the criteria there is no JDK
  local home, err = java(clone)
  assert(home == nil and table.concat(err, "\n"):find("gradle-daemon-jvm.properties", 1, true))

  -- A version that is no number isn't asked for
  write(app.fs.joinPath(clone, "gradle", "gradle-daemon-jvm.properties"),
        "toolchainVendor=ADOPTIUM\ntoolchainVersion=25;x\n")
  home, err = java(clone)
  assert(home == nil and err, "a version that is no number")

  write(app.fs.joinPath(clone, "gradle", "gradle-daemon-jvm.properties"),
        "#This file is generated by :updateDaemonJvm\ntoolchainVendor=ADOPTIUM\ntoolchainVersion=25\n")
  local top = app.fs.joinPath(adoptium, "pkg", "jdk-25.0.5+8")
  local bin = app.os.macos and app.fs.joinPath(top, "Contents", "Home", "bin")
                            or app.fs.joinPath(top, "bin")
  write(app.fs.joinPath(bin, "java"), "#!/bin/sh\n")
  local archive = app.fs.joinPath(adoptium, "jdk.tar.gz")
  assert(os.execute("tar -czf " .. quote(archive) .. " -C " .. quote(app.fs.joinPath(adoptium, "pkg")) ..
                    " jdk-25.0.5+8"))
  local p = io.popen("shasum -a 256 " .. quote(archive))
  local sum = p:read("a"):match("^(%x+)")
  p:close()
  local function answer(checksum, link)
    write(app.fs.joinPath(adoptium, "25", "hotspot"),
          '[{"binary":{"package":{"name":"jdk.tar.gz","link":"file://' .. (link or archive) ..
          '","checksum":"' .. checksum .. '"}},"release_name":"jdk-25.0.5+8"}]')
  end
  local function sha(fn)
    local p = io.popen("shasum -a 256 " .. quote(fn))
    local sum = p:read("a"):match("^(%x+)")
    p:close()
    return sum
  end

  -- A download that doesn't match the checksum is thrown away
  answer(string.rep("0", 64))
  home, err = java(clone)
  assert(home == nil and table.concat(err, "\n"):find("checksum", 1, true), err and table.concat(err, "\n"))
  assert(not app.fs.isDirectory(app.fs.joinPath(javaDir, "25")))

  -- An archive without one JDK folder says so and leaves no unpacked
  -- files behind
  local twice = app.fs.joinPath(adoptium, "twice.tar.gz")
  write(app.fs.joinPath(adoptium, "pkg", "other", "x"), "x")
  assert(os.execute("tar -czf " .. quote(twice) .. " -C " .. quote(app.fs.joinPath(adoptium, "pkg")) ..
                    " jdk-25.0.5+8 other"))
  answer(sha(twice), twice)
  home, err = java(clone)
  assert(home == nil and table.concat(err, "\n"):find("JDK 25", 1, true), err and table.concat(err, "\n"))
  assert(not app.fs.isDirectory(app.fs.joinPath(javaDir, "25.part")))

  -- Files that an earlier unpack left behind don't stop the next one,
  -- nor does a JDK folder without java
  app.fs.makeAllDirectories(app.fs.joinPath(javaDir, "25.part", "left-over"))
  app.fs.makeAllDirectories(app.fs.joinPath(javaDir, "25", "stale"))
  answer(sum)
  home, err = java(clone)
  assert(home, err and table.concat(err, "\n"))
  local expected = app.fs.joinPath(javaDir, "25")
  if app.os.macos then expected = app.fs.joinPath(expected, "Contents", "Home") end
  assert(home == expected, home)
  assert(app.fs.isFile(app.fs.joinPath(home, "bin", "java")))
  assert(season.env().JAVA_HOME == home)
  for _, name in ipairs(app.fs.listFiles(javaDir)) do
    assert(name == "25", "left behind: " .. name)
  end

  -- The next start uses it without asking the API
  os.remove(app.fs.joinPath(adoptium, "25", "hotspot"))
  home, err = java(clone)
  assert(home == expected, err and table.concat(err, "\n"))
  os.execute("rm -rf " .. quote(javaDir))
end

-- The device code is found in what gh prints
do
  assert(season.parseDeviceCode("! First copy your one-time code: 1A2B-3C4D") == "1A2B-3C4D")
  -- Newer versions of gh copy the code themselves
  assert(season.parseDeviceCode("! One-time code (E0C5-C9BA) copied to clipboard") == "E0C5-C9BA")
  assert(season.parseDeviceCode("Open this URL to continue in your web browser: https://github.com/login/device") == nil)
end

-- The trusted files make no tail call to a Lua function, which would
-- drop their caller from the stack and lose the trust
for _, name in ipairs{ "season.lua", "bootstrap.lua" } do
  local n = 0
  for line in io.lines(app.fs.joinPath(app.fs.filePath(source), name)) do
    n = n + 1
    local callee = line:match("^[^-]*return%s+([%a_][%w_.:]*)%s*[%(%{\"]")
    assert(not callee or callee == "function", name .. ":" .. n .. ": " .. line)
  end
end

os.execute("rm -rf " .. quote(tmp))
