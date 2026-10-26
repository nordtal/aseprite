-- nordtal_bootstrap.lua removes folders of the app's config, so it
-- runs only when the config is a folder of its own inside the
-- temporary folder. Its guard runs here with configs it must refuse.

if app.os.windows then return end

local fs = app.fs
local f = io.open(fs.joinPath(fs.currentPath, "scripts", "nordtal_bootstrap.lua"))
local text = f:read("a")
f:close()
local guard = text:match("^(.-)\n%-%- %(the guard ends here%)\n")
assert(guard, "nordtal_bootstrap.lua has no guard")

-- Runs the guard with ASEPRITE_USER_FOLDER as user and the config in
-- config; true if it lets the test go on
local function passes(user, config)
  local env = setmetatable({
    os = { getenv=function(name) if name == "ASEPRITE_USER_FOLDER" then return user end end },
    app = { os=app.os,
            fs=setmetatable({ userConfigPath=config }, { __index=function(_, k) return fs[k] end }) },
  }, { __index=_G })
  local ok, err = pcall(load(guard, "=guard", "t", env))
  assert(ok or err:find("ASEPRITE_USER_FOLDER", 1, true), err)
  return ok
end

local temp = fs.joinPath(fs.tempPath, "nordtal-guard-user")
assert(passes(temp, temp))
assert(passes(temp, fs.joinPath(temp, "config")))
assert(not passes(nil, temp), "no ASEPRITE_USER_FOLDER")
assert(not passes("", temp), "an empty ASEPRITE_USER_FOLDER")
assert(not passes(temp, fs.joinPath(fs.currentPath, "config")), "a config outside the folder")
assert(not passes(temp .. "-other", temp .. "-other-config"), "a config next to the folder")
local outside = fs.joinPath(fs.currentPath, "_tmp_user")
assert(not passes(outside, outside), "a folder outside the temporary folder")
assert(not passes(fs.tempPath, fs.tempPath), "the temporary folder itself")
