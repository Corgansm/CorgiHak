#pragma once
// CorgiHak Lua engine: runs gamesense ( HrisitoSense ) scripts on LuaJIT.
// Every entry point here must be called from the game's main thread ( paint / createmove / frame stage ).
#include <string>
#include <vector>
#include <utility>

class CUserCmd;
class CMenuSubtab;
class CBasePlayer;
class Color;
struct Shot_t;
class Vector;

namespace Lua {
	/* setup */
	// creates the "Lua" subtab groups, call once from Menu::Register
	void RegisterMenu(CMenuSubtab* subtab);
	// unloads every script, call once when the cheat unloads ( after hooks are restored )
	void Shutdown();

	/* configs: script item values travel with the CorgiHak config */
	// json object of every loaded script's item values ( plus the ones the config had for unloaded scripts )
	std::string ExportConfig();
	// applies a config's script values now ( loaded scripts ) or when the script loads
	void ImportConfig(const std::string& text);

	// "a.lua, b.lua" ( scripts queued to load on inject ), "" when none
	std::string AutoloadSummary();

	/* hooks */
	// paint hook, before the menu: runs queued work, dispatches paint / paint_ui
	void OnPaint();
	// paint hook, after the menu: runs menu button presses and value callbacks
	void OnPaintPost();
	// createmove, before movement / fakelag
	void OnSetupCommand(CUserCmd& cmd);
	// createmove, after engine prediction ran the command
	void OnPredictCommand(CUserCmd& cmd);
	// createmove, last thing before the command is verified
	void OnRunCommand(CUserCmd& cmd);
	// frame stage notify ( post = after the original ran )
	void OnFrameStage(int stage, bool post);
	// override view, after the original: remembers the camera origin for client.camera_position
	void OnOverrideView(const Vector& origin);

	/* aimbot */
	void OnAimFire(Shot_t& shot, int target, int hitchance, int hitgroup, int damage, int backtrack, bool extrapolated, const Vector& point);
	void OnAimResult(Shot_t& shot, bool hit, const char* reason);

	/* visuals */
	void AppendEspFlags(CBasePlayer* player, std::vector< std::pair< std::string, Color > >& flags);

	// -1 = scripts leave the packet alone, 0 = choke, 1 = send ( setup_command allow_send_packet / no_choke )
	int SendPacketOverride();

	// a script set cmd.force_defensive; the compatibility layer also forces that request command to send
	// and keeps the existing 16-command latch until the defensive request is consumed
	bool ForceDefensive(int commandNumber);
	// the defensive tick was started ( or the packet went out ), clear the request
	void ConsumeDefensive();

	// true while at least one script is loaded
	bool Active();

	// true while a lua callback is running ( scripts may call hooked game functions directly )
	bool InCallback();

	/* player list ( plist.set ) -> ragebot */
	// resolver side forced by a script ( "Force body yaw" / "Correction active" ), -1 = no override
	int ResolverSideOverride(int index);
	// "Override prefer body aim": 1 = on, -1 = off, 0 = no override
	int PreferBodyOverride(int index);
	// "Add to whitelist"
	bool Whitelisted(int index);

	/* animation breakers: pose / layer writes scripts make on the local player */
	// call right before the local real matrix is built, then RestoreLocalAnimOverrides after it
	void ApplyLocalAnimOverrides(CBasePlayer* local);
	void RestoreLocalAnimOverrides(CBasePlayer* local);
}
