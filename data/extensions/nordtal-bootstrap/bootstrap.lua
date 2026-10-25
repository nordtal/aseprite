-- Copyright (C) 2026  Nordtal
--
-- On start, Nordtal Designer signs the designer in to GitHub, clones
-- the newest season repository the account can write to and loads
-- the plugin in its designer/ folder. The plugin knows the resource
-- pack; this script doesn't.

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
    changeFolder = "Nordtal Folder...",
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
    changeFolder = "Nordtal-Ordner...",
  },
}

local function tr(key, ...)
  local lang = strings[app.preferences.general.language] or strings.en
  return string.format(lang[key] or strings.en[key], ...)
end

local season, tools, plugin, exitPlugin
local dlg

local function home()
  return os.getenv("HOME") or os.getenv("USERPROFILE") or app.fs.userDocsPath
end

local function cloneRoot()
  return plugin.preferences.cloneRoot or app.fs.joinPath(home(), "Nordtal")
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
  for i = math.max(1, #lines - 4), #lines do
    d:newrow()
    d:label{ text=lines[i] }
  end
end

local function openUrl(url)
  local cmd = app.os.windows and { "explorer.exe", url } or { "/usr/bin/open", url }
  Process{ cmd=cmd }:start()
end

local start, signIn, findSeason, offerClone

local function loadPlugin(dir, repo)
  local msg
  exitPlugin, msg = season.loadPlugin(dir, plugin, {
    repository=season.org .. "/" .. repo.name,
    clone=dir,
    gh=tools.gh,
    git=tools.git,
    env=tools.env })
  if not exitPlugin and app.fs.isFile(app.fs.joinPath(dir, "designer", "init.lua")) then
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
  local dir = app.fs.joinPath(root, repo.name)
  if season.isClone(dir) then
    if dlg then dlg:close() end
    return loadPlugin(dir, repo)
  end
  show(function(d)
    d:label{ text=tr("cloneText", repo.name) }
    d:newrow()
    d:label{ id="path", text=dir }
    d:button{ text=tr("change"), onclick=function()
      local chosen = app.fs.chooseFolder{ title=tr("chooseFolder"), path=root }
      if chosen then
        plugin.preferences.cloneRoot = chosen
        offerClone(repo)
      end
    end }
    if problem then
      addLines(d, problem)
    end
    d:button{ id="clone", text=tr("clone"), focus=true, onclick=function()
      if app.fs.isDirectory(dir) and #app.fs.listFiles(dir) > 0 then
        return offerClone(repo, { tr("notAClone", dir) })
      end
      app.fs.makeAllDirectories(root)
      show(function(d2) d2:label{ text=tr("cloning", repo.name) } end)
      season.clone(tools, season.url(repo), dir, function(ok, err)
        if ok then
          if dlg then dlg:close() end
          return loadPlugin(dir, repo)
        end
        local text = table.concat(err, "\n")
        if text:find("xcrun", 1, true) or text:find("developer path", 1, true) then
          return offerClone(repo, { tr("commandLineTools") })
        end
        table.insert(err, 1, tr("cloneFailed"))
        offerClone(repo, err)
      end)
    end }
  end)
end

local function refuse(login)
  show(function(d)
    d:label{ text=tr("noAccess", login or "?") }
    d:button{ text=tr("signOut"), onclick=function()
      season.signOut(tools, function() signIn() end)
    end }
    d:button{ text=tr("close") }
  end)
end

findSeason = function()
  season.find(tools, function(repo, err)
    if repo then
      return offerClone(repo)
    end
    if err then
      return show(function(d)
        d:label{ text=tr("findFailed") }
        addLines(d, err)
        d:button{ text=tr("tryAgain"), onclick=function() findSeason() end }
        d:button{ text=tr("close") }
      end)
    end
    season.login(tools, refuse)
  end)
end

signIn = function()
  show(function(d)
    d:label{ text=tr("signInText") }
    d:button{ text=tr("signIn"), focus=true, onclick=function()
      local login, cancelled
      show(function(d2) d2:label{ id="status", text=tr("waiting") } end)
      login = season.signIn(tools,
        function(code, url)
          show(function(d2)
            d2:label{ text=tr("codeText") }
            d2:newrow()
            d2:label{ id="code", text=code }
            d2:button{ text=tr("copyCode"), onclick=function() app.clipboard.text = code end }
            d2:button{ text=tr("openGitHub"), focus=true, onclick=function() openUrl(url) end }
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
            return findSeason()
          end
          if cancelled then
            return signIn()
          end
          show(function(d2)
            d2:label{ text=tr("signInFailed") }
            addLines(d2, err)
            d2:button{ text=tr("tryAgain"), onclick=function() signIn() end }
            d2:button{ text=tr("close") }
          end)
        end)
    end }
    d:button{ text=tr("close") }
  end)
end

start = function()
  season.signedIn(tools, function(ok)
    if ok then
      findSeason()
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
  season = dofile(app.fs.joinPath(plugin.path, "season.lua"))
  tools = season.tools(plugin.path)
  -- A build without the bundled tools runs without the bootstrap
  if not tools then
    return
  end
  plugin:newCommand{
    id="NordtalFolder",
    title=tr("changeFolder"),
    group="file_app",
    onclick=function()
      local chosen = app.fs.chooseFolder{ title=tr("chooseFolder"), path=cloneRoot() }
      if chosen then
        plugin.preferences.cloneRoot = chosen
      end
    end }
  start()
end

function exit(p)
  if exitPlugin then
    exitPlugin()
  end
end
