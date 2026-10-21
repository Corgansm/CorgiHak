-- aa_recorder.lua  ( HrisitoSense / gamesense )
--
-- WHAT THIS DOES ( read before running ):
--   Records YOUR OWN anti-aim while you play, so the person who sent it can see why their shots miss you.
--   Nothing is uploaded or sent anywhere. It only writes a CSV file into your game folder when YOU press
--   "AA rec: save CSV". You decide whether to send that file.
--
--   It records, every tick you are alive:
--     * your command angles and the angles you actually ended up with ( pitch / yaw )
--     * your desync: body_yaw pose parameter ( +-60 ), lower body yaw target
--     * tickbase, simulation time, choked commands ( fake lag / double tap / defensive shifts )
--   Fake lag:
--     * every packet you send: how many commands were choked before it, and how far you moved since the
--       previous packet ( over 64 units breaks lag compensation )
--   Tick manipulation:
--     * every tickbase jump that is not +1 per command ( double tap recharge / shift, hide shots, defensive )
--     * skipped command numbers ( commands sent in a burst )
--     * the simulation time the server sends back for you, and when it goes backwards ( defensive )
--     * speed, flags, duck amount, weapon
--     * which of these features were on: double tap, hide shots, fake duck, slow walk
--   And as events:
--     * every change of your anti-aim menu values ( yaw / jitter / body yaw / fake lag ... ), including
--       changes other Lua scripts make every tick
--     * every bullet an enemy fires at you: who, hit or miss, which hitgroup, and how close the bullet
--       passed to your head
--
--   A panel on screen shows that it is recording and how many rows it holds.

local VERSION = "1.0"

------------------------------------------------------------------------------------------------------------------
-- menu
------------------------------------------------------------------------------------------------------------------
local TAB, CONTAINER = "LUA", "A"

local m = {
    enabled  = ui.new_checkbox(TAB, CONTAINER, "AA recorder"),
    panel    = ui.new_checkbox(TAB, CONTAINER, "AA rec: panel"),
    console  = ui.new_checkbox(TAB, CONTAINER, "AA rec: log shots at me to console"),
    max_rows = ui.new_slider(TAB, CONTAINER, "AA rec: rows kept ( x1000 )", 5, 200, 100, true, "k"),
}
ui.set(m.enabled, true)
ui.set(m.panel, true)
ui.set(m.console, true)

------------------------------------------------------------------------------------------------------------------
-- helpers
------------------------------------------------------------------------------------------------------------------
local function clock_str()
    local h, mi, s, ms = client.system_time()
    return string.format("%02d:%02d:%02d.%03d", h or 0, mi or 0, s or 0, ms or 0)
end

local function ti() return globals.tickinterval() end
local function to_ticks(t) return math.floor(t / ti() + 0.5) end

local function norm(a)
    a = a % 360
    if a > 180 then a = a - 360 end
    return a
end

local function r1(v)
    if type(v) ~= "number" then return "" end
    return string.format("%.1f", v)
end

local function csv_escape(s)
    s = tostring(s == nil and "" or s)
    if s:find('[,"\n]') then s = '"' .. s:gsub('"', '""') .. '"' end
    return s
end

-- reading a field that a given build may not have must not kill the script
local function field(obj, key)
    local ok, v = pcall(function() return obj[key] end)
    if ok then return v end
    return nil
end

local function prop(ent, name, idx)
    local ok, a, b, c = pcall(entity.get_prop, ent, name, idx)
    if ok then return a, b, c end
    return nil
end

local function bstr(v)
    if v == nil then return "" end
    return v and "1" or "0"
end

------------------------------------------------------------------------------------------------------------------
-- rows
------------------------------------------------------------------------------------------------------------------
local CSV_HEADER = table.concat({
    "kind",                    -- tick / setting / shot_at_me / hurt / round / note
    "clock", "tick", "command", "tickbase", "sim_ticks", "sim_minus_tickbase", "choked",
    "cmd_pitch", "cmd_yaw", "eye_pitch", "eye_yaw", "eye_yaw_minus_view", "body_yaw", "lby", "lby_minus_eye",
    "speed", "on_ground", "duck", "weapon",
    "tickbase_delta", "command_gap", "sent", "choke_cycle", "moved_since_send", "server_sim_delta",
    "dt", "hide_shots", "fake_duck", "slow_walk", "force_defensive",
    "name", "value", "extra"
}, ",")

local rows, row_count = {}, 0
local head = 1 -- ring buffer start

local function push(kind, t)
    local line = table.concat({
        kind,
        csv_escape(t.clock or clock_str()), t.tick or globals.tickcount(), t.command or "",
        t.tickbase or "", t.sim_ticks or "", t.sim_minus_tickbase or "", t.choked or "",
        r1(t.cmd_pitch), r1(t.cmd_yaw), r1(t.eye_pitch), r1(t.eye_yaw), r1(t.eye_yaw_minus_view),
        r1(t.body_yaw), r1(t.lby), r1(t.lby_minus_eye),
        r1(t.speed), bstr(t.on_ground), r1(t.duck), csv_escape(t.weapon or ""),
        t.tickbase_delta or "", t.command_gap or "", bstr(t.sent), t.choke_cycle or "", r1(t.moved_since_send), t.server_sim_delta or "",
        bstr(t.dt), bstr(t.hide_shots), bstr(t.fake_duck), bstr(t.slow_walk), bstr(t.force_defensive),
        csv_escape(t.name or ""), csv_escape(t.value or ""), csv_escape(t.extra or "")
    }, ",")

    local cap = ui.get(m.max_rows) * 1000
    if row_count < cap then
        row_count = row_count + 1
        rows[row_count] = line
    else
        -- full: overwrite the oldest row
        rows[head] = line
        head = head % cap + 1
    end
end

local function ordered_rows()
    local cap = ui.get(m.max_rows) * 1000
    if row_count < cap or head == 1 then return rows end
    local out = {}
    for i = head, row_count do out[#out + 1] = rows[i] end
    for i = 1, head - 1 do out[#out + 1] = rows[i] end
    return out
end

local last_saved = ""

local function save_csv()
    if not writefile then
        client.color_log(255, 90, 90, "[aa rec] writefile() is not available in this build")
        return
    end
    -- next free file name, never overwrite an earlier recording
    local n, name = 1, nil
    while true do
        name = "aa_recorder_" .. n .. ".csv"
        local exists = readfile and readfile(name)
        if not exists then break end
        n = n + 1
    end
    writefile(name, CSV_HEADER .. "\n" .. table.concat(ordered_rows(), "\n") .. "\n")
    last_saved = name
    client.color_log(150, 220, 120, "[aa rec] saved " .. row_count .. " rows to " .. name .. " ( game folder )")
end

local function clear_rows()
    rows, row_count, head = {}, 0, 1
    client.color_log(200, 200, 200, "[aa rec] cleared")
end

ui.new_button(TAB, CONTAINER, "AA rec: save CSV", save_csv)
ui.new_button(TAB, CONTAINER, "AA rec: clear", clear_rows)

------------------------------------------------------------------------------------------------------------------
-- anti-aim menu references ( whatever this build has; missing ones are skipped )
------------------------------------------------------------------------------------------------------------------
local REF_LIST = {
    { "AA", "Anti-aimbot angles", "Enabled" },
    { "AA", "Anti-aimbot angles", "Pitch" },
    { "AA", "Anti-aimbot angles", "Yaw base" },
    { "AA", "Anti-aimbot angles", "Yaw" },
    { "AA", "Anti-aimbot angles", "Yaw jitter" },
    { "AA", "Anti-aimbot angles", "Body yaw" },
    { "AA", "Anti-aimbot angles", "Freestanding body yaw" },
    { "AA", "Anti-aimbot angles", "Fake yaw limit" },
    { "AA", "Anti-aimbot angles", "Edge yaw" },
    { "AA", "Anti-aimbot angles", "Freestanding" },
    { "AA", "Anti-aimbot angles", "Roll" },
    { "AA", "Fake lag", "Enabled" },
    { "AA", "Fake lag", "Amount" },
    { "AA", "Fake lag", "Variance" },
    { "AA", "Fake lag", "Limit" },
    { "AA", "Other", "Slow motion" },
    { "AA", "Other", "Leg movement" },
    { "AA", "Other", "On shot anti-aim" },
    { "AA", "Other", "Fake peek" },
    { "RAGE", "Aimbot", "Double tap" },
    { "RAGE", "Aimbot", "Double tap mode" },
    { "RAGE", "Aimbot", "Double tap fake lag limit" },
    { "RAGE", "Other", "Duck peek assist" },
}

local refs = {}      -- { label, {item, item2, item3} }
local missing = {}

for _, r in ipairs(REF_LIST) do
    local ok, a, b, c = pcall(ui.reference, r[1], r[2], r[3])
    if ok and a then
        refs[#refs + 1] = { label = r[2] .. " / " .. r[3], items = { a, b, c } }
    else
        missing[#missing + 1] = r[3]
    end
end

local function item_string(item)
    local ok, a, b, c, d = pcall(ui.get, item)
    if not ok then return nil end
    if type(a) == "table" then return table.concat(a, "|") end
    if b ~= nil and type(a) == "number" and type(b) == "number" and c ~= nil and d ~= nil then
        return string.format("%d %d %d %d", a, b, c, d) -- colour picker
    end
    return tostring(a)
end

local function ref_value(entry)
    local parts = {}
    for i = 1, 3 do
        local it = entry.items[i]
        if it then
            local s = item_string(it)
            if s then parts[#parts + 1] = s end
        end
    end
    return table.concat(parts, " ; ")
end

local function ref_by(label)
    for _, e in ipairs(refs) do
        if e.label == label then return e end
    end
    return nil
end

-- a feature that has a checkbox plus a hotkey counts as active only when both are on
local function feature_active(label)
    local e = ref_by(label)
    if not e then return nil end
    local on = true
    for i = 1, 3 do
        local it = e.items[i]
        if it then
            local ok, v = pcall(ui.get, it)
            if ok and type(v) == "boolean" and not v then on = false end
        end
    end
    return on
end

local last_values = {}

local function snapshot_settings(force)
    for _, e in ipairs(refs) do
        local v = ref_value(e)
        if force or last_values[e.label] ~= v then
            push("setting", { name = e.label, value = v, extra = last_values[e.label] and ("was " .. last_values[e.label]) or "start" })
            last_values[e.label] = v
        end
    end
end

------------------------------------------------------------------------------------------------------------------
-- per command recording
------------------------------------------------------------------------------------------------------------------
local cmd_in = {}            -- setup_command data waiting for run_command
local stats = {
    ticks = 0, shots_at_me = 0, hits_on_me = 0, head_hits = 0, misses_near = {},
    last_body_yaw = nil, side_flips = 0, max_sim_jump = 0,
}
local last_sim_ticks = nil
local last_tickbase_rc = nil
local last_tickbase = nil

-- fake lag / tick manipulation state
local fl = {
    last_choked = nil, last_command = nil, send_origin = nil,
    cycles = {}, cycle_n = 0, cycle_sum = 0, max_cycle = 0, lc_breaks = 0,
    shifts = 0, biggest_shift = 0, last_shift = 0, gaps = 0,
    server_sim = nil, server_back = 0, server_biggest_back = 0,
}

local function weapon_name(lp)
    local w = entity.get_player_weapon(lp)
    if not w then return "" end
    return entity.get_classname(w) or ""
end

client.set_event_callback("setup_command", function(cmd)
    if not ui.get(m.enabled) then return end
    cmd_in = {
        command = field(cmd, "command_number"),
        pitch = field(cmd, "pitch"),
        yaw = field(cmd, "yaw"),
        choked = field(cmd, "chokedcommands"),
        force_defensive = field(cmd, "force_defensive"),
    }
end)

client.set_event_callback("run_command", function(e)
    if not ui.get(m.enabled) then return end
    local lp = entity.get_local_player()
    if not lp or not entity.is_alive(lp) then return end

    local tickbase = prop(lp, "m_nTickBase")
    local sim = prop(lp, "m_flSimulationTime")
    local sim_ticks = sim and to_ticks(sim) or nil
    local eye_pitch, eye_yaw = prop(lp, "m_angEyeAngles")
    local pose = prop(lp, "m_flPoseParameter", 11)
    local body_yaw = pose and (pose * 120 - 60) or nil
    local lby = prop(lp, "m_flLowerBodyYawTarget")
    local vx, vy = prop(lp, "m_vecVelocity")
    local speed = (vx and vy) and math.sqrt(vx * vx + vy * vy) or nil
    local flags = prop(lp, "m_fFlags")
    local duck = prop(lp, "m_flDuckAmount")
    local _, view_yaw = client.camera_angles()

    if body_yaw and stats.last_body_yaw and (body_yaw > 0) ~= (stats.last_body_yaw > 0) and math.abs(body_yaw) > 5 then
        stats.side_flips = stats.side_flips + 1
    end
    stats.last_body_yaw = body_yaw

    if sim_ticks and last_sim_ticks then
        stats.max_sim_jump = math.max(stats.max_sim_jump, math.abs(sim_ticks - last_sim_ticks))
    end
    last_sim_ticks = sim_ticks
    last_tickbase = tickbase

    stats.ticks = stats.ticks + 1

    -- tick manipulation: tickbase should move exactly +1 per executed command
    local command = cmd_in.command or e.command_number
    local tickbase_delta = (tickbase and last_tickbase_rc) and (tickbase - last_tickbase_rc) or nil
    last_tickbase_rc = tickbase
    local command_gap = (command and fl.last_command) and (command - fl.last_command) or nil
    fl.last_command = command

    if tickbase_delta and tickbase_delta ~= 1 then
        fl.shifts = fl.shifts + 1
        fl.last_shift = tickbase_delta
        if math.abs(tickbase_delta) > math.abs(fl.biggest_shift) then fl.biggest_shift = tickbase_delta end
        push("shift", {
            command = command, tickbase = tickbase, sim_ticks = sim_ticks,
            name = tickbase_delta > 1 and "tickbase jumped forward" or (tickbase_delta < 1 and "tickbase went back / held" or ""),
            value = tickbase_delta,
            extra = string.format("dt %s hs %s fd %s", bstr(feature_active("Aimbot / Double tap")), bstr(feature_active("Other / On shot anti-aim")), bstr(feature_active("Other / Duck peek assist"))),
        })
    end
    if command_gap and command_gap > 1 then fl.gaps = fl.gaps + 1 end

    -- fake lag: a command with 0 choked follows a sent packet; the choke count before it is the cycle length
    local choked = cmd_in.choked or e.chokedcommands
    local sent, cycle, moved = false, nil, nil
    if choked ~= nil and fl.last_choked ~= nil and choked <= fl.last_choked and choked == 0 then
        sent = true
        cycle = fl.last_choked + 1
        local ox, oy, oz = entity.get_origin(lp)
        if ox and fl.send_origin then
            local dx, dy, dz = ox - fl.send_origin[1], oy - fl.send_origin[2], oz - fl.send_origin[3]
            moved = math.sqrt(dx * dx + dy * dy + dz * dz)
            if moved > 64 then fl.lc_breaks = fl.lc_breaks + 1 end
        end
        if ox then fl.send_origin = { ox, oy, oz } end
        fl.cycle_n = fl.cycle_n + 1
        fl.cycle_sum = fl.cycle_sum + cycle
        fl.max_cycle = math.max(fl.max_cycle, cycle)
        fl.cycles[cycle] = (fl.cycles[cycle] or 0) + 1
        push("packet", {
            command = command, tickbase = tickbase, sim_ticks = sim_ticks, choked = fl.last_choked,
            choke_cycle = cycle, moved_since_send = moved, sent = true,
            speed = speed, on_ground = flags and (bit.band(flags, 1) == 1) or nil,
            extra = (moved and moved > 64) and "breaks lag compensation ( > 64u )" or "",
        })
    end
    fl.last_choked = choked

    push("tick", {
        command = cmd_in.command or e.command_number,
        tickbase = tickbase,
        sim_ticks = sim_ticks,
        sim_minus_tickbase = (sim_ticks and tickbase) and (sim_ticks - tickbase) or nil,
        choked = cmd_in.choked or e.chokedcommands,
        cmd_pitch = cmd_in.pitch, cmd_yaw = cmd_in.yaw,
        eye_pitch = eye_pitch, eye_yaw = eye_yaw,
        eye_yaw_minus_view = (eye_yaw and view_yaw) and norm(eye_yaw - view_yaw) or nil,
        body_yaw = body_yaw,
        lby = lby, lby_minus_eye = (lby and eye_yaw) and norm(lby - eye_yaw) or nil,
        speed = speed, on_ground = flags and (bit.band(flags, 1) == 1) or nil, duck = duck,
        weapon = weapon_name(lp),
        dt = feature_active("Aimbot / Double tap"),
        hide_shots = feature_active("Other / On shot anti-aim"),
        fake_duck = feature_active("Other / Duck peek assist"),
        slow_walk = feature_active("Other / Slow motion"),
        force_defensive = cmd_in.force_defensive,
        tickbase_delta = tickbase_delta, command_gap = command_gap, sent = sent, choke_cycle = cycle, moved_since_send = moved,
    })

    -- menu values other scripts change every tick show up as setting rows
    snapshot_settings(false)
end)

------------------------------------------------------------------------------------------------------------------
-- what the server sends back about me ( this is what enemies' cheats see )
------------------------------------------------------------------------------------------------------------------
client.set_event_callback("net_update_end", function()
    if not ui.get(m.enabled) then return end
    local lp = entity.get_local_player()
    if not lp or not entity.is_alive(lp) then return end
    local sim = prop(lp, "m_flSimulationTime")
    if not sim then return end
    local st = to_ticks(sim)
    if fl.server_sim and st ~= fl.server_sim then
        local d = st - fl.server_sim
        local row = {
            sim_ticks = st, tickbase = prop(lp, "m_nTickBase"), server_sim_delta = d,
            name = d < 0 and "server sim time went BACK ( defensive )" or (d > 1 and "server sim time jumped" or ""),
        }
        if d < 0 then
            fl.server_back = fl.server_back + 1
            fl.server_biggest_back = math.min(fl.server_biggest_back, d)
        end
        push("net", row)
    end
    fl.server_sim = st
end)

------------------------------------------------------------------------------------------------------------------
-- shots fired at me
------------------------------------------------------------------------------------------------------------------
local pending = {}   -- [attacker ent] = { clock, tick, impacts = {}, hurt = nil }

local function dist_point_to_segment(px, py, pz, ax, ay, az, bx, by, bz)
    local abx, aby, abz = bx - ax, by - ay, bz - az
    local apx, apy, apz = px - ax, py - ay, pz - az
    local len2 = abx * abx + aby * aby + abz * abz
    local t = 0
    if len2 > 0 then t = math.max(0, math.min(1, (apx * abx + apy * aby + apz * abz) / len2)) end
    local cx, cy, cz = ax + abx * t, ay + aby * t, az + abz * t
    local dx, dy, dz = px - cx, py - cy, pz - cz
    return math.sqrt(dx * dx + dy * dy + dz * dz)
end

local function eye_of(ent)
    local x, y, z = entity.get_origin(ent)
    if not x then return nil end
    local vz = prop(ent, "m_vecViewOffset[2]") or 64
    return x, y, z + vz
end

local function finish_shot(attacker)
    local p = pending[attacker]
    if not p then return end
    pending[attacker] = nil

    local lp = entity.get_local_player()
    local closest = nil
    if lp and p.eye and p.head then
        for _, imp in ipairs(p.impacts) do
            local d = dist_point_to_segment(p.head[1], p.head[2], p.head[3], p.eye[1], p.eye[2], p.eye[3], imp[1], imp[2], imp[3])
            if not closest or d < closest then closest = d end
        end
    end

    local result = p.hurt and ("hit " .. p.hurt.group .. " " .. p.hurt.dmg) or "miss"
    stats.shots_at_me = stats.shots_at_me + 1
    if p.hurt then
        stats.hits_on_me = stats.hits_on_me + 1
        if p.hurt.group == "head" then stats.head_hits = stats.head_hits + 1 end
    end

    push("shot_at_me", {
        clock = p.clock, tick = p.tick,
        tickbase = p.tickbase, sim_ticks = p.sim_ticks, choked = p.choked,
        eye_pitch = p.eye_pitch, eye_yaw = p.eye_yaw, body_yaw = p.body_yaw, lby = p.lby,
        speed = p.speed, duck = p.duck,
        name = p.name,
        value = result,
        extra = closest and string.format("bullet passed %.1f units from my head ( %d impacts )", closest, #p.impacts) or ("impacts " .. #p.impacts),
    })

    if ui.get(m.console) then
        local r, g, b = 255, 120, 120
        if not p.hurt then r, g, b = 120, 220, 120 end
        client.color_log(r, g, b, string.format("[aa rec] %s shot at me: %s | my body_yaw %s yaw %s pitch %s%s",
            p.name, result, r1(p.body_yaw), r1(p.eye_yaw), r1(p.eye_pitch),
            closest and string.format(" | %.1fu from head", closest) or ""))
    end
end

local HITGROUPS = { [0] = "generic", "head", "chest", "stomach", "left arm", "right arm", "left leg", "right leg", "neck", [10] = "gear" }

client.set_event_callback("weapon_fire", function(e)
    if not ui.get(m.enabled) then return end
    local lp = entity.get_local_player()
    if not lp or not entity.is_alive(lp) then return end
    local attacker = client.userid_to_entindex(e.userid)
    if attacker == 0 or attacker == lp or not entity.is_enemy(attacker) then return end

    if pending[attacker] then finish_shot(attacker) end

    local hx, hy, hz = entity.hitbox_position(lp, 0)
    local ex, ey, ez = eye_of(attacker)
    local eye_pitch, eye_yaw = prop(lp, "m_angEyeAngles")
    local pose = prop(lp, "m_flPoseParameter", 11)
    local vx, vy = prop(lp, "m_vecVelocity")
    local sim = prop(lp, "m_flSimulationTime")

    pending[attacker] = {
        clock = clock_str(), tick = globals.tickcount(),
        name = entity.get_player_name(attacker),
        head = hx and { hx, hy, hz } or nil,
        eye = ex and { ex, ey, ez } or nil,
        impacts = {},
        tickbase = prop(lp, "m_nTickBase"),
        sim_ticks = sim and to_ticks(sim) or nil,
        choked = globals.chokedcommands(),
        eye_pitch = eye_pitch, eye_yaw = eye_yaw,
        body_yaw = pose and (pose * 120 - 60) or nil,
        lby = prop(lp, "m_flLowerBodyYawTarget"),
        speed = (vx and vy) and math.sqrt(vx * vx + vy * vy) or nil,
        duck = prop(lp, "m_flDuckAmount"),
    }

    -- impacts and damage for this shot arrive within a few ticks
    client.delay_call(0.3, function() finish_shot(attacker) end)
end)

client.set_event_callback("bullet_impact", function(e)
    local shooter = client.userid_to_entindex(e.userid)
    local p = pending[shooter]
    if p then p.impacts[#p.impacts + 1] = { e.x, e.y, e.z } end
end)

client.set_event_callback("player_hurt", function(e)
    local lp = entity.get_local_player()
    if not lp then return end
    if client.userid_to_entindex(e.userid) ~= lp then return end
    local attacker = client.userid_to_entindex(e.attacker)
    local p = pending[attacker]
    local group = HITGROUPS[e.hitgroup] or tostring(e.hitgroup)
    if p then
        p.hurt = { group = group, dmg = e.dmg_health }
    else
        -- damage without a tracked shot ( nade, fire, knife )
        push("hurt", { name = attacker ~= 0 and entity.get_player_name(attacker) or "world", value = group .. " " .. tostring(e.dmg_health), extra = e.weapon })
    end
end)

client.set_event_callback("round_start", function()
    push("round", { extra = globals.mapname() or "" })
    snapshot_settings(true)
end)

------------------------------------------------------------------------------------------------------------------
-- panel
------------------------------------------------------------------------------------------------------------------
client.set_event_callback("paint", function()
    if not ui.get(m.panel) then return end

    local x, y = 20, 260
    local w = 300
    renderer.rectangle(x - 6, y - 6, w, 146, 15, 15, 20, 200)
    renderer.rectangle(x - 6, y - 6, w, 2, 150, 200, 255, 255)

    local on = ui.get(m.enabled)
    renderer.text(x, y, 255, 255, 255, 255, "b", 0, "AA recorder " .. VERSION)
    if on then
        renderer.text(x + 120, y, 255, 80, 80, 255, "b", 0, "● RECORDING")
    else
        renderer.text(x + 120, y, 160, 160, 160, 255, "b", 0, "paused")
    end
    y = y + 16
    renderer.text(x, y, 210, 210, 210, 255, nil, 0, string.format("rows %d   ticks %d   side flips %d", row_count, stats.ticks, stats.side_flips))
    y = y + 14
    renderer.text(x, y, 210, 210, 210, 255, nil, 0, string.format("shots at me %d   hit %d ( head %d )", stats.shots_at_me, stats.hits_on_me, stats.head_hits))
    y = y + 14
    renderer.text(x, y, 210, 210, 210, 255, nil, 0, string.format("body yaw %s   biggest sim jump %d ticks", r1(stats.last_body_yaw), stats.max_sim_jump))
    y = y + 14
    renderer.text(x, y, 210, 210, 210, 255, nil, 0, string.format("fake lag avg %.1f max %d   lc breaks %d",
        fl.cycle_n > 0 and fl.cycle_sum / fl.cycle_n or 0, fl.max_cycle, fl.lc_breaks))
    y = y + 14
    renderer.text(x, y, 210, 210, 210, 255, nil, 0, string.format("tick shifts %d ( last %d, biggest %d )   cmd bursts %d",
        fl.shifts, fl.last_shift, fl.biggest_shift, fl.gaps))
    y = y + 14
    renderer.text(x, y, 210, 210, 210, 255, nil, 0, string.format("server sim went back %d times ( biggest %d t )", fl.server_back, fl.server_biggest_back))
    y = y + 14
    renderer.text(x, y, 150, 150, 150, 255, nil, 0, "nothing is sent anywhere: \"AA rec: save CSV\" writes a file")
    y = y + 14
    if last_saved ~= "" then
        renderer.text(x, y, 150, 220, 120, 255, nil, 0, "last saved: " .. last_saved)
    end
end)

------------------------------------------------------------------------------------------------------------------
-- start
------------------------------------------------------------------------------------------------------------------
for _, name in ipairs({ "sv_maxusrcmdprocessticks", "cl_cmdrate", "cl_updaterate", "cl_interp", "cl_interp_ratio", "sv_maxunlag" }) do
    local cv = cvar[name]
    local v = cv and cv:get_string() or nil
    push("note", { name = name, value = v or "n/a" })
end

push("note", { extra = "aa_recorder " .. VERSION .. " started, map " .. (globals.mapname() or "menu") .. ", tick interval " .. string.format("%.5f", ti()) })
snapshot_settings(true)

client.color_log(150, 200, 255, "[aa rec] recording your anti-aim. Nothing is sent anywhere; press \"AA rec: save CSV\" to write aa_recorder_<n>.csv")
if #missing > 0 then
    client.color_log(200, 200, 120, "[aa rec] menu items not found in this build ( skipped ): " .. table.concat(missing, ", "))
end
