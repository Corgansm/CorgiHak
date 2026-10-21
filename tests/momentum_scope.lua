local path = assert(arg[1], "supply the script path")
local file = assert(io.open(path, "rb"))
local source = file:read("*a"):gsub("\r\n", "\n")
file:close()
local declaration = assert(source:match("(local defensive[^\n]*)\n    local command = {}"))
assert(not source:find("local defensive = {", 1, true), "initializer must not shadow the captured local")
local teleport = assert(source:match("teleport = (function%(%)\n.-\n        end)\n    }"))
-- Execute the real callback with minimal host mocks, and a deliberately absent global.
defensive = nil
antiaim = {lc_break = true}
ext = {}
get = function(value) return value end
entity_get_local_player = function() return 1 end
entity_is_alive = function() return true end
entity_get_players = function() return {2} end
entity_hitbox_position = function() return 1, 2, 3 end
client = {visible = function() return true end}
local make = assert(loadstring(declaration .. "\nlocal command = {teleport = " .. teleport
    .. "}\ndefensive = {ext_active = false}\nreturn command, defensive"))
local command, state = make()
command.teleport()
assert(ext.dt_block == true, "visible enemy must request the DT gate")
ext.dt_block = false
state.ext_active = true
command.teleport()
assert(ext.dt_block == false, "extended defensive must suppress teleport")
state.ext_active = false
antiaim.lc_break = false
command.teleport()
assert(ext.dt_block == false, "disabled teleport must do nothing")
print("Momentum defensive scope and teleport regression passed")
