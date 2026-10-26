-- No script can reach into the functions or the objects of another
-- one: the debug functions that read or change locals, upvalues,
-- hooks, metatables or the registry are unsupported, and the
-- metatables of the app's objects can't be read

for _, name in ipairs{ "getlocal", "setlocal", "getupvalue", "setupvalue",
                       "upvaluejoin", "sethook", "getregistry",
                       "getmetatable", "setmetatable",
                       "getuservalue", "setuservalue" } do
  local ok, msg = pcall(debug[name], 1, 1, 1)
  assert(not ok)
  assert(tostring(msg):find("unsupported function", 1, true), name .. ": " .. tostring(msg))
end
assert(debug.traceback("x"):find("x", 1, true))
assert(debug.getinfo(1, "S").source:find("security.lua", 1, true))

local r = Rectangle(1, 2, 3, 4)
assert(getmetatable(r) == false)
assert(getmetatable(app) == false)

-- The objects don't look their methods and properties up through
-- the global functions, which any script can replace
local origGetmetatable = getmetatable
getmetatable = function() return { width=function() return 99 end, __getters={} } end
assert(r.width == 3)
assert(app.fs.joinPath("a", "b") ~= nil)
getmetatable = origGetmetatable

-- Nor are the metatables of strings and files: a string's methods are
-- the string library's, and one a script adds still works
assert(getmetatable("") == false)
assert(getmetatable(io.stdout) == false)
local origUpper = string.upper
string.upper = function() return "x" end
assert(("a"):upper() == "A")
string.upper = origUpper
function string.twice(s) return s .. s end
assert(("a"):twice() == "aa")
string.twice = nil

-- debug.getinfo() hands out no function of the stack
local function f() return debug.getinfo(2, "fS") end
local info = f()
assert(info.func == nil and info.source:find("security.lua", 1, true))
assert(debug.getinfo(print, "f").func == print)

-- The error for a field an object lacks has the traceback, then the
-- name of the field
local ok, msg = pcall(function() return r.nope end)
assert(not ok and msg:find("^[^\n]*stack traceback") and
       msg:find("Field 'nope' does not exist", 1, true), msg)
ok, msg = pcall(function() r.nope = 1 end)
assert(not ok and msg:find("^[^\n]*stack traceback") and
       msg:find("Cannot set field 'nope'", 1, true), msg)
