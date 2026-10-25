-- The bootstrap signs in through gh, finds the newest season
-- repository the account can write to, clones it and loads its plugin

if app.os.windows then return end

local season = dofile(app.fs.joinPath(app.fs.currentPath, "..", "data", "extensions",
                                      "nordtal-bootstrap", "season.lua"))
local tmp = app.fs.joinPath(app.fs.tempPath, "nordtal-bootstrap-test")
os.execute("rm -rf '" .. tmp .. "'")
app.fs.makeAllDirectories(tmp)

local function write(fn, text)
  app.fs.makeAllDirectories(app.fs.filePath(fn))
  local f = io.open(fn, "w")
  f:write(text)
  f:close()
end

-- Without a bundled gh there is no bootstrap
assert(season.tools(tmp) == nil)

-- A stub gh that answers like gh does, and logs its environment
local gh = app.fs.joinPath(tmp, "tools", "gh", "bin", "gh")
write(gh, [[#!/bin/sh
echo "$GH_CONFIG_DIR|${GH_TOKEN-unset}|${GITHUB_TOKEN-unset}|$*" >> "$(dirname "$0")/calls"
case "$1 $2" in
  "auth status") [ -f "$GH_CONFIG_DIR/hosts.yml" ] ;;
  "auth login")
    echo >&2
    echo "! First copy your one-time code: ABCD-1234" >&2
    echo "Open this URL to continue in your web browser: https://github.com/login/device" >&2
    mkdir -p "$GH_CONFIG_DIR" && echo "github.com:" > "$GH_CONFIG_DIR/hosts.yml" ;;
  "auth logout") rm -f "$GH_CONFIG_DIR/hosts.yml" ;;
  "auth git-credential") cat > /dev/null; printf 'username=designer\npassword=from-stub-gh\n' ;;
  "api user") echo designer ;;
  "repo list") cat "$(dirname "$0")/repos.json" ;;
  *) exit 1 ;;
esac
]])
os.execute("chmod +x '" .. gh .. "'")
local tools = season.tools(tmp)
assert(tools.gh == gh)
assert(tools.git == "/usr/bin/git")
assert(tools.env.GH_CONFIG_DIR == app.fs.joinPath(app.fs.userConfigPath, "gh"))
tools.env.GH_CONFIG_DIR = app.fs.joinPath(tmp, "gh-config")

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

-- Signing in shows the device code, then gh has the account
do
  local signedIn
  season.signedIn(tools, function(ok) signedIn = ok end):wait()
  assert(signedIn == false)

  local code, url, ok
  season.signIn(tools, function(c, u) code, url = c, u end,
                function(o) ok = o end):wait()
  assert(code == "ABCD-1234")
  assert(url == "https://github.com/login/device")
  assert(ok)
  season.signedIn(tools, function(o) signedIn = o end):wait()
  assert(signedIn == true)

  local login
  season.login(tools, function(l) login = l end):wait()
  assert(login == "designer")
end

-- gh never gets a token of the environment, only its own config
do
  local f = io.open(app.fs.joinPath(tmp, "tools", "gh", "bin", "calls"))
  for line in f:lines() do
    assert(line:find(tools.env.GH_CONFIG_DIR .. "|unset|unset|", 1, true) == 1)
  end
  f:close()
end

-- The account's repositories decide which season is cloned
do
  write(app.fs.joinPath(tmp, "tools", "gh", "bin", "repos.json"), [[
[{"isArchived":false,"name":"season-2","viewerPermission":"WRITE"},
 {"isArchived":false,"name":"season-3","viewerPermission":"READ"}]
]])
  local repo
  season.find(tools, function(r) repo = r end):wait()
  assert(repo.name == "season-2")
  assert(season.url(repo) == "https://github.com/nordtal/season-2.git")

  write(app.fs.joinPath(tmp, "tools", "gh", "bin", "repos.json"),
        '[{"isArchived":false,"name":"season-2","viewerPermission":"READ"}]')
  local called = false
  season.find(tools, function(r) called = true; repo = r end):wait()
  assert(called and repo == nil)
end

-- Signing out removes the account
do
  local signedIn
  season.signOut(tools, function() end):wait()
  season.signedIn(tools, function(ok) signedIn = ok end):wait()
  assert(signedIn == false)
end

-- The clone asks the bundled gh for credentials and runs the
-- repository's hooks
do
  local origin = app.fs.joinPath(tmp, "origin")
  assert(os.execute("git init -q '" .. origin .. "' && cd '" .. origin .. "'" ..
                    " && mkdir designer && echo 'loaded = (loaded or 0) + 1' > designer/init.lua" ..
                    " && git add . && git -c user.name=t -c user.email=t@t commit -qm init"))
  local dir = app.fs.joinPath(tmp, "Nordtal", "season-2")
  assert(not season.isClone(dir))
  local ok, err
  season.clone(tools, "file://" .. origin, dir, function(o, e) ok, err = o, e end):wait()
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
  assert(helpers[2] == "!GH_CONFIG_DIR='" .. tools.env.GH_CONFIG_DIR .. "' '" .. gh .. "' auth git-credential")

  -- git in the clone gets its credentials from the bundled gh, even
  -- from a terminal without the app's environment
  local p = io.popen("printf 'protocol=https\\nhost=github.com\\n\\n' | " ..
                     "GIT_TERMINAL_PROMPT=0 GH_CONFIG_DIR=/nowhere git -C '" .. dir .. "' credential fill")
  local filled = p:read("a")
  p:close()
  assert(filled:find("password=from-stub-gh", 1, true), filled)
  local f = io.open(app.fs.joinPath(tmp, "tools", "gh", "bin", "calls"))
  local last
  for line in f:lines() do last = line end
  f:close()
  assert(last == tools.env.GH_CONFIG_DIR .. "|unset|unset|auth git-credential get", last)

  -- The clone's plugin gets its folder, its own preferences and the context
  local plugin = { preferences={ cloneRoot="x" } }
  loaded = nil
  local exitPlugin, msg = season.loadPlugin(dir, plugin, { clone=dir })
  assert(exitPlugin, msg)
  assert(loaded == 1)
  assert(init == nil and exit == nil)

  write(app.fs.joinPath(dir, "designer", "init.lua"), [[
function init(plugin)
  seen = { path=plugin.path, clone=plugin.bootstrap.clone }
  plugin.preferences.n = 1
end
function exit(plugin) seen.exit = true end
]])
  exitPlugin = season.loadPlugin(dir, plugin, { clone=dir })
  assert(seen.path == app.fs.joinPath(dir, "designer"))
  assert(seen.clone == dir)
  assert(plugin.preferences.plugin.n == 1)
  assert(plugin.preferences.cloneRoot == "x")
  exitPlugin()
  assert(seen.exit)

  -- A clone without the plugin, or with a broken one, says why
  write(app.fs.joinPath(dir, "designer", "init.lua"), "error('broken')")
  local none
  none, msg = season.loadPlugin(dir, plugin, {})
  assert(none == nil and msg:find("broken"))
  os.remove(app.fs.joinPath(dir, "designer", "init.lua"))
  none, msg = season.loadPlugin(dir, plugin, {})
  assert(none == nil and msg:find("init.lua", 1, true))
end

-- The device code is found in what gh prints
do
  local code, url = season.parseDeviceCode("! First copy your one-time code: 1A2B-3C4D")
  assert(code == "1A2B-3C4D" and url == nil)
  -- Newer versions of gh copy the code themselves
  code, url = season.parseDeviceCode("! One-time code (E0C5-C9BA) copied to clipboard")
  assert(code == "E0C5-C9BA" and url == nil)
  code, url = season.parseDeviceCode("Open this URL to continue in your web browser: https://github.com/login/device")
  assert(code == nil and url == "https://github.com/login/device")
end

os.execute("rm -rf '" .. tmp .. "'")
