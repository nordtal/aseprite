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
