-- Copyright (C) 2026  Nordtal
--
-- On start, Nordtal Designer signs the designer in to GitHub, offers
-- a newer release of itself, clones the newest season repository the
-- account can write to and loads the plugin in its designer/ folder.
-- The plugin knows the resource pack; this script doesn't.
--
-- The app trusts this file like season.lua, and it follows the same
-- rules: every global it uses is kept when it loads, no string
-- methods, no tail calls.

local app, Dialog, Process = app, Dialog, Process
local find, format, match = string.find, string.format, string.match
local concat, insert = table.concat, table.insert
local max = math.max
local pcall = pcall
local getenv = os.getenv
local fs = app.fs
local joinPath, isFile, isDirectory, listFiles, makeAllDirectories, chooseFolder =
      fs.joinPath, fs.isFile, fs.isDirectory, fs.listFiles, fs.makeAllDirectories, fs.chooseFolder
local clipboard = app.clipboard
local windows = app.os.windows
local preferences = app.preferences

local strings = {
  en = {
    title = "Nordtal Designer",
    signInText = "Sign in with a GitHub account that can write to a season repository of nordtal.",
    signIn = "Sign In with GitHub",
    codeText = "Enter this code on GitHub:",
    copyCode = "Copy Code",
    openGitHub = "Open GitHub",
    waiting = "Waiting for GitHub...",
    signInFailed = "Signing in failed:",
    tryAgain = "Try Again",
    noAccess = "The GitHub account %s can't write to a season repository of nordtal. Ask for write access, or sign in with another account.",
    signOut = "Sign Out",
    close = "Close",
    findFailed = "The season repositories could not be listed:",
    cloneText = "Clone nordtal/%s into this folder:",
    change = "Change...",
    chooseFolder = "Folder for the season repository",
    clone = "Clone",
    cloning = "Cloning nordtal/%s...",
    cloneFailed = "Cloning failed:",
    notAClone = "%s exists and isn't a clone. Choose another folder.",
    commandLineTools = "Git needs the Command Line Tools. Install them in the window macOS shows, then click Clone again.",
    pluginFailed = "The plugin of the season repository failed to load:",
    java = "Downloading Java for Gradle...",
    javaFailed = "Java for Gradle could not be set up:",
    changeFolder = "Nordtal Folder...",
    updateText = "Nordtal Designer %s is available. This is %s.",
    install = "Install",
    later = "Later",
    installing = "Installing Nordtal Designer %s...",
    installedOnQuit = "Nordtal Designer %s replaces this version when you quit the app.",
    installFailed = "Installing failed:",
  },
  de = {
    title = "Nordtal Designer",
    signInText = "Melde dich mit einem GitHub-Konto an, das in ein Saison-Repository von nordtal schreiben darf.",
    signIn = "Mit GitHub anmelden",
    codeText = "Gib diesen Code bei GitHub ein:",
    copyCode = "Code kopieren",
    openGitHub = "GitHub öffnen",
    waiting = "Warte auf GitHub...",
    signInFailed = "Die Anmeldung ist fehlgeschlagen:",
    tryAgain = "Erneut versuchen",
    noAccess = "Das GitHub-Konto %s darf in kein Saison-Repository von nordtal schreiben. Bitte um Schreibzugriff oder melde dich mit einem anderen Konto an.",
    signOut = "Abmelden",
    close = "Schließen",
    findFailed = "Die Saison-Repositorys konnten nicht gelistet werden:",
    cloneText = "nordtal/%s in diesen Ordner klonen:",
    change = "Ändern...",
    chooseFolder = "Ordner für das Saison-Repository",
    clone = "Klonen",
    cloning = "nordtal/%s wird geklont...",
    cloneFailed = "Das Klonen ist fehlgeschlagen:",
    notAClone = "%s gibt es schon, und es ist kein Klon. Wähle einen anderen Ordner.",
    commandLineTools = "Git braucht die Command Line Tools. Installiere sie in dem Fenster, das macOS zeigt, und klicke dann erneut auf Klonen.",
    pluginFailed = "Das Plugin des Saison-Repositorys konnte nicht geladen werden:",
    java = "Java für Gradle wird heruntergeladen...",
    javaFailed = "Java für Gradle konnte nicht eingerichtet werden:",
    changeFolder = "Nordtal-Ordner...",
    updateText = "Nordtal Designer %s ist verfügbar. Dies ist %s.",
    install = "Installieren",
    later = "Später",
    installing = "Nordtal Designer %s wird installiert...",
    installedOnQuit = "Nordtal Designer %s ersetzt diese Version, wenn du die App beendest.",
    installFailed = "Die Installation ist fehlgeschlagen:",
  },
}

local function tr(key, ...)
  local lang = strings[preferences.general.language] or strings.en
  return (format(lang[key] or strings.en[key], ...))
end

local season, plugin, exitPlugin
local dlg

local function home()
  return (getenv("HOME") or getenv("USERPROFILE") or fs.userDocsPath)
end

local function cloneRoot()
  return plugin.preferences.cloneRoot or joinPath(home(), "Nordtal")
end

-- One dialog at a time, replaced by each step
local function show(build, onclose)
  if dlg then
    dlg:close()
  end
  dlg = Dialog{ title=tr("title"), onclose=onclose }
  build(dlg)
  dlg:show{ wait=false }
end

-- A label for each of the last lines of what a tool printed
local function addLines(d, lines)
  for i = max(1, #lines - 4), #lines do
    d:newrow()
    d:label{ text=lines[i] }
  end
end

local function openUrl(url)
  local cmd = windows and { "explorer.exe", url } or { "/usr/bin/open", url }
  Process{ cmd=cmd }:start()
end

local start, signIn, findSeason, offerClone

-- The version in a release tag
local function version(tag)
  return (match(tag, "^v(.*)$") or tag)
end

-- Offers the latest release if it's newer, then done() goes on with
-- the season. A failed check goes on without a word.
local function offerUpdate(done)
  season.latestUpdate(function(update)
    if not update then
      done()
      return
    end
    -- Closing a dialog goes on, but not while the next one replaces it
    local busy = false
    local function goOn()
      if not busy then done() end
    end
    local function install()
      busy = true
      show(function(d) d:label{ text=tr("installing", version(update.tag)) } end)
      season.install(update, season.target, function(ok, err)
        busy = false
        show(function(d)
          if ok then
            d:label{ text=tr("installedOnQuit", version(update.tag)) }
          else
            d:label{ text=tr("installFailed") }
            addLines(d, err or {})
            d:button{ text=tr("tryAgain"), onclick=install }
          end
          d:button{ text=tr("close") }
        end, goOn)
      end)
    end
    show(function(d)
      d:label{ text=tr("updateText", version(update.tag), version(season.release)) }
      d:button{ text=tr("install"), focus=true, onclick=install }
      d:button{ text=tr("later") }
    end, goOn)
  end)
end

local loadPlugin

-- The JDK the season's Gradle runs on, then the plugin
local function setUp(dir, repo)
  local steps = season.java(dir, function(home, err)
    if home then
      if dlg then dlg:close() end
      loadPlugin(dir, repo)
      return
    end
    show(function(d)
      d:label{ text=tr("javaFailed") }
      addLines(d, err or {})
      d:button{ text=tr("tryAgain"), onclick=function() setUp(dir, repo) end }
      d:button{ text=tr("close") }
    end)
  end)
  if steps then
    show(function(d) d:label{ text=tr("java") } end)
  end
end

loadPlugin = function(dir, repo)
  local msg
  exitPlugin, msg = season.loadPlugin(dir, plugin, {
    repository=season.org .. "/" .. repo.name,
    clone=dir,
    gh=season.gh,
    git=season.git,
    env=season.env() })
  if not exitPlugin and isFile(joinPath(dir, "designer", "init.lua")) then
    show(function(d)
      d:label{ text=tr("pluginFailed") }
      addLines(d, { msg })
      d:button{ text=tr("close") }
    end)
  end
end

-- problem is a list of lines that tell why the last try failed
offerClone = function(repo, problem)
  local root = cloneRoot()
  local dir = joinPath(root, repo.name)
  if season.isClone(dir) then
    if dlg then dlg:close() end
    setUp(dir, repo)
    return
  end
  show(function(d)
    d:label{ text=tr("cloneText", repo.name) }
    d:newrow()
    d:label{ id="path", text=dir }
    d:button{ text=tr("change"), onclick=function()
      local chosen = chooseFolder{ title=tr("chooseFolder"), path=root }
      if chosen then
        plugin.preferences.cloneRoot = chosen
        offerClone(repo)
      end
    end }
    if problem then
      addLines(d, problem)
    end
    d:button{ id="clone", text=tr("clone"), focus=true, onclick=function()
      if isDirectory(dir) and #listFiles(dir) > 0 then
        offerClone(repo, { tr("notAClone", dir) })
        return
      end
      makeAllDirectories(root)
      show(function(d2) d2:label{ text=tr("cloning", repo.name) } end)
      season.clone(season.url(repo), dir, function(ok, err)
        if ok then
          setUp(dir, repo)
          return
        end
        local text = concat(err, "\n")
        if find(text, "xcrun", 1, true) or find(text, "developer path", 1, true) then
          offerClone(repo, { tr("commandLineTools") })
          return
        end
        insert(err, 1, tr("cloneFailed"))
        offerClone(repo, err)
      end)
    end }
  end)
end

local function refuse(login)
  show(function(d)
    d:label{ text=tr("noAccess", login or "?") }
    d:button{ text=tr("signOut"), onclick=function()
      season.signOut(function() signIn() end)
    end }
    d:button{ text=tr("close") }
  end)
end

findSeason = function()
  season.find(function(repo, err)
    if repo then
      offerClone(repo)
      return
    end
    if err then
      show(function(d)
        d:label{ text=tr("findFailed") }
        addLines(d, err)
        d:button{ text=tr("tryAgain"), onclick=function() findSeason() end }
        d:button{ text=tr("close") }
      end)
      return
    end
    season.login(refuse)
  end)
end

signIn = function()
  show(function(d)
    d:label{ text=tr("signInText") }
    d:button{ text=tr("signIn"), focus=true, onclick=function()
      local login, cancelled
      show(function(d2) d2:label{ id="status", text=tr("waiting") } end)
      login = season.signIn(
        function(code)
          show(function(d2)
            d2:label{ text=tr("codeText") }
            d2:newrow()
            d2:label{ id="code", text=code }
            d2:button{ text=tr("copyCode"), onclick=function() clipboard.text = code end }
            d2:button{ text=tr("openGitHub"), focus=true,
                       onclick=function() openUrl(season.deviceUrl) end }
            d2:newrow()
            d2:label{ text=tr("waiting") }
            d2:button{ text=tr("close") }
          end,
          -- Closing the dialog, however, ends the sign-in
          function()
            if login.isRunning then
              cancelled = true
              login:stop()
            end
          end)
        end,
        function(ok, err)
          if ok then
            offerUpdate(findSeason)
          elseif cancelled then
            signIn()
          else
            show(function(d2)
              d2:label{ text=tr("signInFailed") }
              addLines(d2, err)
              d2:button{ text=tr("tryAgain"), onclick=function() signIn() end }
              d2:button{ text=tr("close") }
            end)
          end
        end)
    end }
    d:button{ text=tr("close") }
  end)
end

start = function()
  season.signedIn(function(ok)
    if ok then
      offerUpdate(findSeason)
    else
      signIn()
    end
  end)
end

function init(p)
  plugin = p
  if not app.isUIAvailable then
    return
  end
  season = plugin:loadTrusted(joinPath(plugin.path, "season.lua"))()
  -- A build without the bundled tools runs without the bootstrap
  if not season.available then
    return
  end
  plugin:newCommand{
    id="NordtalFolder",
    title=tr("changeFolder"),
    group="file_app",
    onclick=function()
      local chosen = chooseFolder{ title=tr("chooseFolder"), path=cloneRoot() }
      if chosen then
        plugin.preferences.cloneRoot = chosen
      end
    end }
  start()
end

-- The app calls exit() as it quits, and also when the extension is
-- disabled; scripts learn of no other quit. The update goes first, so
-- an error of the season's plugin can't lose it.
function exit(p)
  if season then
    season.finishInstall()
  end
  if exitPlugin then
    pcall(exitPlugin)
  end
end
