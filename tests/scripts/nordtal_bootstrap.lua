-- The bootstrap signs in through gh, finds the newest season
-- repository the account can write to, clones it and loads its plugin

if app.os.windows then return end

-- The test removes folders of the app's config, so it runs only with
-- ASEPRITE_USER_FOLDER set to a folder of its own inside the temporary
-- folder, as run-tests.sh sets it
do
  local fs = app.fs
  local function inside(path, folder, orEqual)
    path = fs.normalizePath(path):gsub("/+$", "")
    folder = fs.normalizePath(folder):gsub("/+$", "")
    return (orEqual and path == folder) or path:sub(1, #folder + 1) == folder .. "/"
  end
  local user = os.getenv("ASEPRITE_USER_FOLDER")
  if not user or user == "" then
    error("the test needs ASEPRITE_USER_FOLDER set to a new temporary folder")
  end
  if not inside(fs.userConfigPath, user, true) or not inside(fs.userConfigPath, fs.tempPath) then
    error("the test needs ASEPRITE_USER_FOLDER set to a new folder inside " .. fs.tempPath ..
          ", and the config in it, not " .. fs.userConfigPath)
  end
end
-- (the guard ends here)

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
  "api repos/nordtal/aseprite/releases/latest") cat "$(dirname "$0")/latest.json" ;;
  "release download")
    # Copies package.zip as the asset that --pattern names into --dir
    prev= ; pattern= ; dir=
    for a in "$@"; do
      [ "$prev" = --pattern ] && pattern=$a
      [ "$prev" = --dir ] && dir=$a
      prev=$a
    done
    cp "$(dirname "$0")/package.zip" "$dir/$pattern" ;;
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

  -- A link where the unpack or the JDK goes stops it, and nothing is
  -- unpacked or removed through the link
  local elsewhere = app.fs.joinPath(tmp, "elsewhere")
  write(app.fs.joinPath(elsewhere, "kept"), "x")
  answer(sum)
  for _, name in ipairs{ "25.part", "25" } do
    local link = app.fs.joinPath(javaDir, name)
    assert(os.execute("ln -s " .. quote(elsewhere) .. " " .. quote(link)))
    home, err = java(clone)
    assert(home == nil and table.concat(err, "\n"):find("could not be removed", 1, true),
           err and table.concat(err, "\n"))
    local files = app.fs.listFiles(elsewhere)
    assert(#files == 1 and files[1] == "kept", name)
    assert(os.execute("rm " .. quote(link)))
  end

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

-- A release build knows its tag from the file the release workflow
-- writes next to season.lua. A build without it never updates.
do
  assert(season.release == nil)
  write(app.fs.joinPath(ext, "release"), "v1.3.18.6-designer.2\n")
  assert(loadSeason().release == "v1.3.18.6-designer.2")
  os.remove(app.fs.joinPath(ext, "release"))
  if app.os.macos and app.os.arm64 then
    assert(season.package == "Nordtal-Designer-macOS-arm64.zip")
  elseif not app.os.windows then
    assert(season.package == nil)
  end
end

-- Tags compare by the Aseprite version, then by the designer's number
do
  local newer = season.newer
  assert(newer("v1.3.18.6-designer.3", "v1.3.18.6-designer.2"))
  assert(newer("v1.3.18.6-designer.10", "v1.3.18.6-designer.9"))
  assert(newer("v1.3.19-designer.1", "v1.3.18.6-designer.9"))
  assert(newer("v1.3.18.6-designer.1", "v1.3.18-designer.4"))
  assert(newer("v1.10-designer.1", "v1.9.9-designer.1"))
  assert(not newer("v1.3.18.6-designer.2", "v1.3.18.6-designer.2"))
  assert(not newer("v1.3.18-designer.4", "v1.3.18.0-designer.4"))
  assert(not newer("v1.3.18.6-designer.1", "v1.3.18.6-designer.2"))
  assert(not newer("v1.3.17-designer.9", "v1.3.18-designer.1"))
  for _, bad in ipairs{ "v1.3.19", "1.3.19-designer.1", "v1..3-designer.1", "v1.3.-designer.1",
                        "v1.3-designer.x", "v1.3-designer.1-dev", "", false } do
    assert(not newer(bad, "v1.3.18-designer.1"), tostring(bad))
    assert(not newer("v1.3.18-designer.1", bad), tostring(bad))
  end
end

-- The latest release is an update if its tag is newer than the app's
-- and it has the package for this computer with its sha256
do
  local sum = string.rep("ab", 32)
  local function release(fields)
    local r = { tag_name="v1.3.18.6-designer.3", draft=false, prerelease=false,
                assets={ { name="other.zip", digest="sha256:" .. string.rep("0", 64) },
                         { name="P.zip", digest="sha256:" .. sum:upper() } } }
    for k, v in pairs(fields or {}) do r[k] = v end
    return r
  end
  local current = "v1.3.18.6-designer.2"
  local update = season.updateFrom(release(), current, "P.zip")
  assert(update.tag == "v1.3.18.6-designer.3")
  assert(update.asset == "P.zip")
  assert(update.sha256 == sum)
  assert(season.updateFrom(release(), "v1.3.18.6-designer.3", "P.zip") == nil)
  assert(season.updateFrom(release(), "v1.3.19-designer.1", "P.zip") == nil)
  assert(season.updateFrom(release{ draft=true }, current, "P.zip") == nil)
  assert(season.updateFrom(release{ prerelease=true }, current, "P.zip") == nil)
  assert(season.updateFrom(release(), current, "Q.zip") == nil)
  assert(season.updateFrom(release(), current, nil) == nil)
  assert(season.updateFrom(release(), nil, "P.zip") == nil)
  assert(season.updateFrom(release{ assets={ { name="P.zip" } } }, current, "P.zip") == nil)
  assert(season.updateFrom(release{ assets={ { name="P.zip", digest="sha1:" .. sum } } },
                           current, "P.zip") == nil)
  assert(season.updateFrom(release{ assets=false }, current, "P.zip") == nil)
  assert(season.updateFrom("Not Found", current, "P.zip") == nil)

  -- gh asks the releases of the designer's repository
  local function latest(json)
    local fn = app.fs.joinPath(ext, "tools", "gh", "bin", "latest.json")
    if json then write(fn, json) else os.remove(fn) end
    local called, got, err = false, nil, nil
    local p = season.latestUpdate(function(u, e) called, got, err = true, u, e end)
    if p then p:wait() end
    assert(called)
    return got, err
  end
  local json = '{"tag_name":"v1.3.18.6-designer.3","draft":false,"prerelease":false,' ..
               '"assets":[{"name":"P.zip","digest":"sha256:' .. sum .. '"}]}'
  -- Without a tag of its own or a package for this computer it doesn't ask
  season.release, season.package = nil, "P.zip"
  assert(latest(json) == nil)
  season.release, season.package = current, nil
  assert(latest(json) == nil)
  season.package = "P.zip"
  update = latest(json)
  assert(update and update.tag == "v1.3.18.6-designer.3" and update.sha256 == sum)
  local last
  for line in io.lines(app.fs.joinPath(ext, "tools", "gh", "bin", "calls")) do last = line end
  assert(last:find("|api repos/nordtal/aseprite/releases/latest", 1, true), last)
  local none, err = latest(nil)
  assert(none == nil and err)
  assert(latest("not json") == nil)
  season.release, season.package = nil, nil
end

-- The new app takes the place of the old one, and a failed move leaves
-- the old one where it was
do
  local dir = app.fs.joinPath(tmp, "replace")
  local target = app.fs.joinPath(dir, "App")
  local staged = app.fs.joinPath(dir, "work", "App")
  local old = app.fs.joinPath(dir, "work", "old")
  write(app.fs.joinPath(target, "version"), "old")
  write(app.fs.joinPath(staged, "version"), "new")
  write(app.fs.joinPath(old, "left-over"), "x")
  assert(season.replace(staged, target, old))
  assert(read(app.fs.joinPath(target, "version")) == "new")
  assert(not app.fs.isDirectory(staged) and not app.fs.isDirectory(old))

  local ok, err = season.replace(staged, target, old)
  assert(not ok and err)
  assert(read(app.fs.joinPath(target, "version")) == "new")
  assert(not app.fs.isDirectory(old))
end

-- On Windows a helper moves the new folder into place once the app
-- quit and the old folder can be moved
do
  local text = season.windowsHelper("C:\\Apps\\Nordtal Designer",
                                    "C:\\Apps\\.nordtal-designer-update\\Nordtal Designer",
                                    "C:\\Apps\\.nordtal-designer-update\\old")
  assert(text:find('set "target=C:\\Apps\\Nordtal Designer"\r\n', 1, true), text)
  assert(text:find('set "staged=C:\\Apps\\.nordtal-designer-update\\Nordtal Designer"\r\n', 1, true))
  assert(text:find('set "old=C:\\Apps\\.nordtal-designer-update\\old"\r\n', 1, true))
  assert(text:find(':wait\r\nmove "%target%" "%old%" >nul 2>&1\r\nif errorlevel 1 (\r\n', 1, true))
  assert(text:find('goto wait', 1, true))
  assert(text:find('move "%staged%" "%target%"', 1, true))
  assert(text:find('if errorlevel 1 move "%old%" "%target%"', 1, true))
  assert(not text:find("^\n") and not text:find("[^\r]\n"), "every line ends with CRLF")
  local none, err = season.windowsHelper("C:\\100%\\Nordtal Designer", "C:\\a", "C:\\b")
  assert(none == nil and err:find("100%", 1, true))
end

-- On macOS the install downloads the package with gh, checks it
-- against the release's sha256, unpacks it next to the app, drops the
-- quarantine attribute and replaces the app with it
if app.os.macos then
  local apps = app.fs.joinPath(tmp, "Applications")
  local target = app.fs.joinPath(apps, "Nordtal Designer.app")
  local exe = app.fs.joinPath(target, "Contents", "MacOS", "Nordtal Designer")
  local work = app.fs.joinPath(apps, ".nordtal-designer-update")
  write(exe, "old")
  -- The package is a zip of a bundle by ditto, as the release workflow
  -- makes it, with the quarantine attribute a browser download gets
  local src = app.fs.joinPath(tmp, "package", "Nordtal Designer.app")
  write(app.fs.joinPath(src, "Contents", "MacOS", "Nordtal Designer"), "new")
  local zip = app.fs.joinPath(ext, "tools", "gh", "bin", "package.zip")
  assert(os.execute("xattr -w com.apple.quarantine '0081;0;test;' " .. quote(src) ..
                    " && ditto -c -k --keepParent " .. quote(src) .. " " .. quote(zip)))
  local p = io.popen("shasum -a 256 " .. quote(zip))
  local sum = p:read("a"):match("^(%x+)")
  p:close()
  local function install(update)
    local ok, err
    local steps = season.install(update, target, function(o, e) ok, err = o, e end)
    if steps then steps:wait() end
    return ok, err
  end
  local update = { tag="v1.3.18.6-designer.3", asset="Nordtal-Designer-macOS-arm64.zip" }

  -- A download that doesn't match leaves the app as it was
  update.sha256 = string.rep("0", 64)
  local ok, err = install(update)
  assert(not ok and table.concat(err, "\n"):find("checksum", 1, true))
  assert(read(exe) == "old")
  assert(not app.fs.isDirectory(work))

  update.sha256 = sum
  ok, err = install(update)
  assert(ok, err and table.concat(err, "\n"))
  assert(read(exe) == "new")
  local x = io.popen("xattr -r " .. quote(target))
  local attrs = x:read("a")
  x:close()
  assert(not attrs:find("com.apple.quarantine", 1, true), attrs)
  assert(not app.fs.isDirectory(work))
  local last
  for line in io.lines(app.fs.joinPath(ext, "tools", "gh", "bin", "calls")) do last = line end
  assert(last:find("|release download v1.3.18.6-designer.3 --repo nordtal/aseprite " ..
                   "--pattern Nordtal-Designer-macOS-arm64.zip --dir " .. work, 1, true), last)

  -- A package without the app inside leaves the app as it was
  write(app.fs.joinPath(tmp, "empty", "x"), "x")
  assert(os.execute("rm " .. quote(zip) .. " && ditto -c -k " ..
                    quote(app.fs.joinPath(tmp, "empty")) .. " " .. quote(zip)))
  p = io.popen("shasum -a 256 " .. quote(zip))
  update.sha256 = p:read("a"):match("^(%x+)")
  p:close()
  ok, err = install(update)
  assert(not ok and err)
  assert(read(exe) == "new")
  assert(not app.fs.isDirectory(work))
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
