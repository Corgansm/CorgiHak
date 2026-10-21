// CorgiHak Lua engine.
// One LuaJIT state per script. The gamesense ( HrisitoSense ) API itself is written in Lua ( lua/runtime.lua,
// lua/libs.lua, embedded through lua_embedded.h ); this file only provides the native table "N" those files use.
#include "lua_engine.h"
#include "lua_embedded.h"

#include "../../context.h"
#include "../interfaces.h"
#include "../config.h"
#include "../menu rework/menu.h"
#include "../../utils/render.h"
#include "../../utils/math.h"
#include "../../sdk/hash/fnv1a.h"
#include "../../features/rage/autowall.h"
#include "../../features/rage/antiaim.h"
#include "../../features/misc/logger.h"
#include "../../features/misc/shots.h"
#include "../../features/visuals/visuals.h"
#include "../../dependencies/json/json.hpp"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <deque>
#include <array>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <iterator>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <shellapi.h>

extern "C" {
#include "../../dependencies/luajit/src/lua.h"
#include "../../dependencies/luajit/src/lauxlib.h"
#include "../../dependencies/luajit/src/lualib.h"
}

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_FAILURE_STRINGS
#include "../../dependencies/stb/stb_image.h"

// svg textures ( renderer.load_svg / images.load_svg )
#pragma warning( push )
#pragma warning( disable : 4996 4244 4305 4456 4701 4703 )
#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include "../../dependencies/nanosvg/nanosvg.h"
#include "../../dependencies/nanosvg/nanosvgrast.h"
#pragma warning( pop )

#ifndef MASK_SHOT
#define MASK_SHOT 0x4600400B
#endif

namespace {
	/* ------------------------------------------------------------------------------------------------ */
	/* scripts                                                                                           */
	/* ------------------------------------------------------------------------------------------------ */

	struct Script_t {
		std::string m_strName{};
		std::filesystem::path m_pathFile{};
		lua_State* L{};
		int m_iRuntimeRef{ LUA_NOREF };
		bool m_bTouchedAntiaim{};
	};

	std::vector< std::unique_ptr< Script_t > > g_vecScripts{};
	std::vector< std::string > g_vecFiles{};
	std::vector< std::string > g_vecLastFiles{};
	float g_flLastRefresh{};

	enum EQueuedAction {
		ACTION_LOAD,
		ACTION_UNLOAD,
		ACTION_RELOAD,
		ACTION_RELOAD_ALL,
		ACTION_UNLOAD_ALL,
		ACTION_REFRESH,
		ACTION_TOGGLE_AUTOLOAD,
		ACTION_OPEN_FOLDER
	};

	struct QueuedAction_t {
		EQueuedAction m_eAction{};
		std::string m_strName{};
	};

	std::deque< QueuedAction_t > g_dqActions{};
	std::deque< int > g_dqButtonPresses{};

	/* plist */
	struct PlayerOverride_t {
		bool m_bForceBodyYaw{};
		float m_flBodyYaw{};
		bool m_bCorrection{ true };
		int m_iPreferBody{};
		bool m_bWhitelist{};
	};
	std::array< PlayerOverride_t, 65 > g_arrPlayers{};

	/* local animation overrides ( gamesense applies these to the local model after pre_render ) */
	constexpr int POSE_COUNT{ 24 };
	constexpr int LAYER_COUNT{ 13 };
	constexpr float OVERRIDE_LIFETIME{ 0.25f };

	struct ValueOverride_t {
		float m_flValue{};
		float m_flTime{ -1.f };
	};
	std::array< ValueOverride_t, POSE_COUNT > g_arrPoseOverrides{};
	std::array< ValueOverride_t, LAYER_COUNT > g_arrLayerWeightOverrides{};
	std::array< ValueOverride_t, LAYER_COUNT > g_arrLayerCycleOverrides{};
	std::array< float, POSE_COUNT > g_arrPoseBackup{};
	CAnimationLayer g_arrLayerBackup[LAYER_COUNT]{};
	bool g_bAnimBackedUp{};

	bool g_bInPaint{};
	int g_iCallbackDepth{};
	bool g_bWasInGame{};
	int g_iSendPacketOverride{ -1 };
	// cmd.force_defensive: latched until the next sent packet ( defensive can only start on a send tick )
	int g_iForceDefensiveCmd{ -1000 };
	Vector g_vecCameraOrigin{};
	bool g_bHasCamera{};
	int g_iNextShotId{ 1 };

	// anti-aim mode the user had before a script took over the anti-aim references
	bool g_bAntiaimTakenOver{};
	int g_iPreviousAntiaimMode{};
	bool g_bPreviousAntiaimEnable{};

	std::filesystem::path LuaDirectory() {
		return Config::GetWorkingPath() / "lua";
	}

	std::filesystem::path GameDirectory() {
		char buffer[MAX_PATH]{};
		GetModuleFileNameA(nullptr, buffer, MAX_PATH);
		return std::filesystem::path(buffer).parent_path();
	}

	void PrintColored(int r, int g, int b, const std::string& text) {
		Interfaces::ConVar->ConsoleColorPrintf(Color(r, g, b), "%s", text.c_str());
	}

	// recent script errors, shown in the "Scripts" group ( newest last )
	std::deque< std::string > g_dqErrors{};
	bool g_bErrorsChanged{};

	void RememberError(const std::string& script, const std::string& where, const std::string& message) {
		// first line only: the traceback stays in the console
		std::string text{ message.substr(0, message.find('\n')) };
		text = script + " ( " + where + " ): " + text;

		// a callback failing every frame is one error, not a flood
		if (std::find(g_dqErrors.begin(), g_dqErrors.end(), text) != g_dqErrors.end())
			return;

		g_dqErrors.push_back(text);
		while (g_dqErrors.size() > 6)
			g_dqErrors.pop_front();
		g_bErrorsChanged = true;
	}

	void ReportError(const Script_t* script, const std::string& where, const std::string& message) {
		RememberError(script ? script->m_strName : std::string("engine"), where, message);
		PrintColored(255, 149, 46, "[ CorgiHak ] ");
		PrintColored(255, 90, 90, "lua " + (script ? script->m_strName : std::string("engine")) + " ( " + where + " ): " + message + "\n");
		Features::Logger.Log("lua error in " + (script ? script->m_strName : std::string("engine")) + ", see console", true);
	}

	/* ------------------------------------------------------------------------------------------------ */
	/* small helpers for natives ( natives never raise lua errors, so no C++ object is skipped by a   */
	/* longjmp )                                                                                        */
	/* ------------------------------------------------------------------------------------------------ */

	Script_t* ScriptOf(lua_State* L) {
		return static_cast<Script_t*>(lua_touserdata(L, lua_upvalueindex(1)));
	}

	float ArgFloat(lua_State* L, int i, float def = 0.f) {
		return lua_isnumber(L, i) ? static_cast<float>(lua_tonumber(L, i)) : def;
	}

	int ArgInt(lua_State* L, int i, int def = 0) {
		if (!lua_isnumber(L, i))
			return def;
		const auto value{ lua_tonumber(L, i) };
		return static_cast<int>(value >= 0.0 ? value + 0.5 : value - 0.5);
	}

	bool ArgBool(lua_State* L, int i) {
		return lua_toboolean(L, i) != 0;
	}

	std::string ArgString(lua_State* L, int i) {
		std::size_t length{};
		const char* text{ lua_tolstring(L, i, &length) };
		return text ? std::string(text, length) : std::string();
	}

	int ArgByte(lua_State* L, int i, int def = 255) {
		return std::clamp(ArgInt(L, i, def), 0, 255);
	}

	void SetField(lua_State* L, const char* key, double value) {
		lua_pushnumber(L, value);
		lua_setfield(L, -2, key);
	}

	void SetField(lua_State* L, const char* key, bool value) {
		lua_pushboolean(L, value);
		lua_setfield(L, -2, key);
	}

	void SetField(lua_State* L, const char* key, const char* value) {
		lua_pushstring(L, value ? value : "");
		lua_setfield(L, -2, key);
	}

	double GetFieldNumber(lua_State* L, int table, const char* key, double def) {
		lua_getfield(L, table, key);
		const double value{ lua_isnumber(L, -1) ? lua_tonumber(L, -1) : def };
		lua_pop(L, 1);
		return value;
	}

	// gamesense scripts write flags as booleans or as 0 / 1 ( lua treats the number 0 as true )
	bool GetFieldFlag(lua_State* L, int table, const char* key) {
		lua_getfield(L, table, key);
		bool value{};
		if (lua_type(L, -1) == LUA_TNUMBER)
			value = lua_tonumber(L, -1) != 0.0;
		else if (!lua_isnil(L, -1))
			value = lua_toboolean(L, -1) != 0;
		lua_pop(L, 1);
		return value;
	}

	bool GetFieldBool(lua_State* L, int table, const char* key, bool def) {
		lua_getfield(L, table, key);
		const bool value{ lua_isnil(L, -1) ? def : lua_toboolean(L, -1) != 0 };
		lua_pop(L, 1);
		return value;
	}

	std::wstring Widen(const std::string& text) {
		if (text.empty())
			return {};

		const int size{ MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0) };
		std::wstring out(size, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size);
		return out;
	}

	std::string Narrow(const std::wstring& text) {
		if (text.empty())
			return {};

		const int size{ WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr) };
		std::string out(size, '\0');
		WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
		return out;
	}

	bool ReadFileBytes(const std::filesystem::path& path, std::string& out) {
		std::ifstream file(path, std::ios::binary);
		if (!file.is_open())
			return false;

		std::ostringstream stream;
		stream << file.rdbuf();
		out = stream.str();
		return true;
	}

	bool WriteFileBytes(const std::filesystem::path& path, const std::string& data) {
		std::error_code error;
		if (path.has_parent_path())
			std::filesystem::create_directories(path.parent_path(), error);

		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file.is_open())
			return false;

		file.write(data.data(), static_cast<std::streamsize>(data.size()));
		return file.good();
	}

	std::filesystem::path ResolveGamePath(const std::string& path) {
		const std::filesystem::path requested{ Widen(path) };
		return requested.is_absolute() ? requested : GameDirectory() / requested;
	}

	/* ------------------------------------------------------------------------------------------------ */
	/* entities                                                                                          */
	/* ------------------------------------------------------------------------------------------------ */

	CBaseEntity* EntityAt(int index) {
		if (index <= 0 || !Interfaces::ClientEntityList)
			return nullptr;

		return static_cast<CBaseEntity*>(Interfaces::ClientEntityList->GetClientEntity(index));
	}

	CBaseEntity* ArgEntity(lua_State* L, int i) {
		return lua_isnumber(L, i) ? EntityAt(ArgInt(L, i)) : nullptr;
	}

	CBasePlayer* ArgPlayer(lua_State* L, int i) {
		auto entity{ ArgEntity(L, i) };
		return entity && entity->IsPlayer() ? static_cast<CBasePlayer*>(entity) : nullptr;
	}

	const char* NetworkName(CBaseEntity* entity) {
		if (!entity)
			return nullptr;

		auto networkable{ entity->GetClientNetworkable() };
		if (!networkable)
			return nullptr;

		auto clientClass{ networkable->GetClientClass() };
		return clientClass ? clientClass->szNetworkName : nullptr;
	}

	RecvTable_t* RecvTableOf(CBaseEntity* entity) {
		auto networkable{ entity ? entity->GetClientNetworkable() : nullptr };
		auto clientClass{ networkable ? networkable->GetClientClass() : nullptr };
		return clientClass ? clientClass->pRecvTable : nullptr;
	}

	int FindEntityByClass(const char* className) {
		if (!Interfaces::ClientEntityList)
			return 0;

		const int highest{ Interfaces::ClientEntityList->GetHighestEntityIndex() };
		for (int i{ 1 }; i <= highest; ++i) {
			const auto name{ NetworkName(EntityAt(i)) };
			if (name && std::strcmp(name, className) == 0)
				return i;
		}

		return 0;
	}

	CBasePlayer* LocalPlayer() {
		if (!Interfaces::Engine || !Interfaces::Engine->IsInGame())
			return nullptr;

		return static_cast<CBasePlayer*>(EntityAt(Interfaces::Engine->GetLocalPlayer()));
	}

	bool PlayerAlive(CBasePlayer* player) {
		return player && !player->IsDead();
	}

	bool IsEnemy(CBaseEntity* entity) {
		auto local{ LocalPlayer() };
		if (!entity || !local || entity == local)
			return false;

		return entity->m_iTeamNum() != local->m_iTeamNum();
	}

	int HandleToIndex(unsigned long handle) {
		if (handle == INVALID_EHANDLE_INDEX || handle == 0)
			return 0;

		return static_cast<int>(handle & 0xFFF);
	}

	bool HitboxPosition(CBasePlayer* player, int hitbox, Vector& out, float* radius = nullptr, Vector* otherEnd = nullptr) {
		if (!player || !player->GetModel())
			return false;

		auto hdr{ Interfaces::ModelInfo->GetStudioModel(player->GetModel()) };
		if (!hdr)
			return false;

		auto set{ hdr->GetHitboxSet(player->m_nHitboxSet()) };
		if (!set || hitbox < 0 || hitbox >= set->nHitboxes)
			return false;

		auto box{ set->GetHitbox(hitbox) };
		if (!box)
			return false;

		auto& bones{ player->m_CachedBoneData() };
		if (box->iBone < 0 || box->iBone >= bones.Count())
			return false;

		const auto& matrix{ bones.Base()[box->iBone] };
		out = Math::VectorTransform((box->vecBBMin + box->vecBBMax) * 0.5f, matrix);

		if (radius)
			*radius = box->flRadius;

		if (otherEnd) {
			const auto a{ Math::VectorTransform(box->vecBBMin, matrix) };
			const auto b{ Math::VectorTransform(box->vecBBMax, matrix) };
			out = a;
			*otherEnd = b;
		}

		return true;
	}

	CBasePlayer* CurrentThreat() {
		auto local{ LocalPlayer() };
		if (!local || local->IsDead())
			return nullptr;

		QAngle view{};
		Interfaces::Engine->GetViewAngles(view);
		const auto eye{ local->GetAbsOrigin() + local->m_vecViewOffset() };

		CBasePlayer* best{};
		float bestFov{ FLT_MAX };
		for (int i{ 1 }; i <= Interfaces::Globals->nMaxClients; ++i) {
			auto player{ static_cast<CBasePlayer*>(EntityAt(i)) };
			if (!player || player == local || player->Dormant() || player->IsDead() || !IsEnemy(player))
				continue;

			const auto fov{ Math::GetFov(view, Math::CalcAngle(eye, player->GetAbsOrigin() + player->m_vecViewOffset())) };
			if (fov < bestFov) {
				bestFov = fov;
				best = player;
			}
		}

		return best;
	}

	const char* WeaponGroupSuffix() {
		auto local{ LocalPlayer() };
		auto weapon{ local ? local->GetWeapon() : nullptr };
		auto data{ weapon ? weapon->GetCSWeaponData() : nullptr };
		if (!weapon || !data)
			return "Rifle";

		const auto index{ weapon->m_iItemDefinitionIndex() };
		switch (data->nWeaponType) {
		case WEAPONTYPE_PISTOL:
			return index == WEAPON_DEAGLE || index == WEAPON_REVOLVER ? "HeavyPistol" : "Pistol";
		case WEAPONTYPE_SUBMACHINEGUN:
			return "SMG";
		case WEAPONTYPE_SHOTGUN:
			return "Shotgun";
		case WEAPONTYPE_SNIPER:
			return index == WEAPON_AWP ? "AWP" : index == WEAPON_SSG08 ? "Scout" : "Auto";
		case WEAPONTYPE_MACHINEGUN:
			return "Machine";
		default:
			return "Rifle";
		}
	}

	/* ------------------------------------------------------------------------------------------------ */
	/* netvar access by name ( entity.get_prop / set_prop )                                              */
	/* ------------------------------------------------------------------------------------------------ */

	struct PropLookup_t {
		bool m_bFound{};
		int m_iOffset{};              // offset of element 0 ( relative to the entity or to the game rules object )
		ESendPropType m_eType{ DPT_INT };
		RecvProp_t* m_pProp{};        // the element prop ( for int sizes / flags )
		RecvTable_t* m_pArrayTable{}; // "000", "001" ... style arrays
		int m_iArrayStride{};         // DPT_ARRAY style arrays
		int m_iArrayCount{ 1 };
		bool m_bGameRules{};
		bool m_bVectorFallback{};     // "m_vecVelocity" resolved through "m_vecVelocity[0]"
	};

	std::unordered_map< std::uint64_t, PropLookup_t > g_mapPropCache{};

	bool IsArrayTable(RecvTable_t* table) {
		return table && table->nProps > 0 && table->pProps[0].szVarName && std::isdigit(static_cast<unsigned char>(table->pProps[0].szVarName[0]));
	}

	bool WalkTable(RecvTable_t* table, const char* name, int base, bool gameRules, PropLookup_t& out, int depth) {
		if (!table || depth > 24)
			return false;

		for (int i{}; i < table->nProps; ++i) {
			auto prop{ &table->pProps[i] };
			if (!prop || !prop->szVarName)
				continue;

			if (std::strcmp(prop->szVarName, name) == 0) {
				out.m_bFound = true;
				out.m_bGameRules = gameRules;
				out.m_iOffset = base + prop->iOffset;
				out.m_pProp = prop;
				out.m_eType = prop->iRecvType;

				if (prop->iRecvType == DPT_DATATABLE && IsArrayTable(prop->pDataTable)) {
					out.m_pArrayTable = prop->pDataTable;
					out.m_iArrayCount = prop->pDataTable->nProps;
					out.m_pProp = &prop->pDataTable->pProps[0];
					out.m_eType = out.m_pProp->iRecvType;
				}
				else if (prop->iRecvType == DPT_ARRAY && prop->pArrayProp) {
					out.m_pProp = prop->pArrayProp;
					out.m_eType = prop->pArrayProp->iRecvType;
					out.m_iArrayCount = std::max(prop->iElements, 1);
					out.m_iArrayStride = prop->iElementStride;
				}

				return true;
			}

			if (prop->iRecvType == DPT_DATATABLE && prop->pDataTable && !IsArrayTable(prop->pDataTable)) {
				// the game rules proxy forwards this table to the global game rules object
				const bool rules{ gameRules || std::strcmp(prop->szVarName, "cs_gamerules_data") == 0 };
				const int childBase{ rules && !gameRules ? 0 : base + prop->iOffset };

				if (WalkTable(prop->pDataTable, name, childBase, rules, out, depth + 1))
					return true;
			}
		}

		return false;
	}

	const PropLookup_t& LookupProp(CBaseEntity* entity, const std::string& name) {
		static PropLookup_t notFound{};

		auto table{ RecvTableOf(entity) };
		if (!table)
			return notFound;

		const std::uint64_t key{ (static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(table)) << 32) | FNV1A::Hash(name.c_str()) };
		const auto it{ g_mapPropCache.find(key) };
		if (it != g_mapPropCache.end())
			return it->second;

		PropLookup_t result{};
		if (!WalkTable(table, name.c_str(), 0, false, result, 0)) {
			// gamesense accepts "m_vecVelocity" for the networked "m_vecVelocity[0]" ( + [1], [2] )
			PropLookup_t first{};
			if (WalkTable(table, (name + "[0]").c_str(), 0, false, first, 0)) {
				result = first;
				result.m_bVectorFallback = true;
			}
		}

		return g_mapPropCache.emplace(key, result).first->second;
	}

	int IntPropSize(const RecvProp_t* prop) {
		static CStandartRecvProxies* proxies{ Interfaces::Client ? Interfaces::Client->GetStandardRecvProxies() : nullptr };
		if (!prop || !proxies)
			return 4;

		if (prop->oProxyFn == proxies->pInt32ToInt8)
			return 1;
		if (prop->oProxyFn == proxies->pInt32ToInt16)
			return 2;

		return 4;
	}

	std::uintptr_t PropAddress(CBaseEntity* entity, const PropLookup_t& lookup, int index) {
		std::uintptr_t base{ reinterpret_cast<std::uintptr_t>(entity) };
		if (lookup.m_bGameRules) {
			if (!Interfaces::GameRules)
				return 0;
			base = reinterpret_cast<std::uintptr_t>(Interfaces::GameRules);
		}

		index = std::clamp(index, 0, std::max(lookup.m_iArrayCount - 1, 0));

		if (lookup.m_pArrayTable)
			return base + lookup.m_iOffset + lookup.m_pArrayTable->pProps[index].iOffset;

		if (lookup.m_iArrayCount > 1) {
			int stride{ lookup.m_iArrayStride };
			if (stride <= 0)
				stride = lookup.m_eType == DPT_INT ? IntPropSize(lookup.m_pProp) : lookup.m_eType == DPT_VECTOR ? 12 : lookup.m_eType == DPT_INT64 ? 8 : 4;
			return base + lookup.m_iOffset + stride * index;
		}

		return base + lookup.m_iOffset;
	}

	int PushPropValue(lua_State* L, const PropLookup_t& lookup, std::uintptr_t address, const std::string& name) {
		if (lookup.m_bVectorFallback) {
			const auto values{ reinterpret_cast<const float*>(address) };
			lua_pushnumber(L, values[0]);
			lua_pushnumber(L, values[1]);
			lua_pushnumber(L, values[2]);
			return 3;
		}

		switch (lookup.m_eType) {
		case DPT_INT: {
			const int size{ IntPropSize(lookup.m_pProp) };
			const bool isUnsigned{ lookup.m_pProp && (lookup.m_pProp->iFlags & 1) };
			long long value{};
			if (size == 1)
				value = isUnsigned ? *reinterpret_cast<const std::uint8_t*>(address) : *reinterpret_cast<const std::int8_t*>(address);
			else if (size == 2)
				value = isUnsigned ? *reinterpret_cast<const std::uint16_t*>(address) : *reinterpret_cast<const std::int16_t*>(address);
			else
				value = isUnsigned ? *reinterpret_cast<const std::uint32_t*>(address) : *reinterpret_cast<const std::int32_t*>(address);

			// entity handles come back as entity indices
			if (name.size() > 3 && name.compare(0, 3, "m_h") == 0) {
				const int index{ HandleToIndex(static_cast<unsigned long>(value)) };
				if (!index)
					return 0;
				lua_pushinteger(L, index);
				return 1;
			}

			lua_pushnumber(L, static_cast<double>(value));
			return 1;
		}
		case DPT_FLOAT:
			lua_pushnumber(L, *reinterpret_cast<const float*>(address));
			return 1;
		case DPT_VECTOR:
		case DPT_VECTOR2D: {
			const auto values{ reinterpret_cast<const float*>(address) };
			lua_pushnumber(L, values[0]);
			lua_pushnumber(L, values[1]);
			lua_pushnumber(L, values[2]);
			return 3;
		}
		case DPT_STRING:
			lua_pushstring(L, reinterpret_cast<const char*>(address));
			return 1;
		case DPT_INT64:
			lua_pushnumber(L, static_cast<double>(*reinterpret_cast<const long long*>(address)));
			return 1;
		default:
			return 0;
		}
	}

	void WritePropValue(lua_State* L, const PropLookup_t& lookup, std::uintptr_t address, int first) {
		if (lookup.m_bVectorFallback || lookup.m_eType == DPT_VECTOR || lookup.m_eType == DPT_VECTOR2D) {
			auto values{ reinterpret_cast<float*>(address) };
			for (int i{}; i < 3; ++i) {
				if (lua_isnumber(L, first + i))
					values[i] = static_cast<float>(lua_tonumber(L, first + i));
			}
			return;
		}

		switch (lookup.m_eType) {
		case DPT_INT: {
			long long value{ lua_isboolean(L, first) ? (lua_toboolean(L, first) ? 1 : 0) : static_cast<long long>(lua_tonumber(L, first)) };
			const int size{ IntPropSize(lookup.m_pProp) };
			if (size == 1)
				*reinterpret_cast<std::uint8_t*>(address) = static_cast<std::uint8_t>(value);
			else if (size == 2)
				*reinterpret_cast<std::uint16_t*>(address) = static_cast<std::uint16_t>(value);
			else
				*reinterpret_cast<std::int32_t*>(address) = static_cast<std::int32_t>(value);
		} break;
		case DPT_FLOAT:
			*reinterpret_cast<float*>(address) = static_cast<float>(lua_tonumber(L, first));
			break;
		case DPT_INT64:
			*reinterpret_cast<long long*>(address) = static_cast<long long>(lua_tonumber(L, first));
			break;
		default:
			break;
		}
	}

	/* ------------------------------------------------------------------------------------------------ */
	/* textures                                                                                          */
	/* ------------------------------------------------------------------------------------------------ */

	int WhiteTexture() {
		static int texture{ -1 };
		if (texture == -1) {
			texture = Interfaces::Surface->CreateNewTextureID(true);
			const unsigned char pixel[4]{ 255, 255, 255, 255 };
			Interfaces::Surface->DrawSetTextureRGBA(texture, pixel, 1, 1);
		}

		return texture;
	}

	int UploadTexture(const unsigned char* rgba, int width, int height) {
		if (!rgba || width <= 0 || height <= 0)
			return -1;

		const int texture{ Interfaces::Surface->CreateNewTextureID(true) };
		Interfaces::Surface->DrawSetTextureRGBA(texture, rgba, width, height);
		return texture;
	}

	struct CachedTexture_t {
		int m_iId{ -1 };
		int m_iWidth{}, m_iHeight{};
	};
	// scripts often call load_svg / load_png every frame: one texture per unique image
	std::unordered_map< std::string, CachedTexture_t > g_mapTextures{};

	int PushCachedTexture(lua_State* L, const CachedTexture_t& texture) {
		if (texture.m_iId < 0)
			return 0;
		lua_pushinteger(L, texture.m_iId);
		lua_pushinteger(L, texture.m_iWidth);
		lua_pushinteger(L, texture.m_iHeight);
		return 3;
	}


	/* ------------------------------------------------------------------------------------------------ */
	/* game events                                                                                       */
	/* ------------------------------------------------------------------------------------------------ */

	// key layouts of the stock csgo events ( 'i' int, 'f' float, 's' string, 'b' bool, 'l' 64 bit )
	const std::unordered_map< std::string, std::vector< std::pair< const char*, char > > >& EventKeys() {
		static const std::unordered_map< std::string, std::vector< std::pair< const char*, char > > > keys{
			{ "player_hurt",{ { "userid", 'i' },{ "attacker", 'i' },{ "health", 'i' },{ "armor", 'i' },{ "weapon", 's' },{ "dmg_health", 'i' },{ "dmg_armor", 'i' },{ "hitgroup", 'i' } } },
			{ "player_death",{ { "userid", 'i' },{ "attacker", 'i' },{ "assister", 'i' },{ "assistedflash", 'b' },{ "weapon", 's' },{ "weapon_itemid", 's' },{ "weapon_fauxitemid", 's' },{ "weapon_originalowner_xuid", 's' },{ "headshot", 'b' },{ "dominated", 'i' },{ "revenge", 'i' },{ "wipe", 'i' },{ "penetrated", 'i' },{ "noreplay", 'b' },{ "noscope", 'b' },{ "thrusmoke", 'b' },{ "attackerblind", 'b' },{ "distance", 'f' } } },
			{ "weapon_fire",{ { "userid", 'i' },{ "weapon", 's' },{ "silenced", 'b' } } },
			{ "weapon_reload",{ { "userid", 'i' } } },
			{ "weapon_zoom",{ { "userid", 'i' } } },
			{ "weapon_fire_on_empty",{ { "userid", 'i' },{ "weapon", 's' } } },
			{ "bullet_impact",{ { "userid", 'i' },{ "x", 'f' },{ "y", 'f' },{ "z", 'f' } } },
			{ "round_start",{ { "timelimit", 'i' },{ "fraglimit", 'i' },{ "objective", 's' } } },
			{ "round_end",{ { "winner", 'i' },{ "reason", 'i' },{ "message", 's' },{ "legacy", 'i' },{ "player_count", 'i' },{ "nomusic", 'i' } } },
			{ "round_prestart",{} },
			{ "round_poststart",{} },
			{ "round_freeze_end",{} },
			{ "round_officially_ended",{} },
			{ "round_announce_match_start",{} },
			{ "begin_new_match",{} },
			{ "cs_win_panel_match",{} },
			{ "cs_game_disconnected",{} },
			{ "cs_pre_restart",{} },
			{ "announce_phase_end",{} },
			{ "client_disconnect",{} },
			{ "player_spawn",{ { "userid", 'i' },{ "teamnum", 'i' } } },
			{ "player_spawned",{ { "userid", 'i' },{ "inrestart", 'b' } } },
			{ "player_disconnect",{ { "userid", 'i' },{ "reason", 's' },{ "name", 's' },{ "networkid", 's' },{ "xuid", 'l' } } },
			{ "player_connect",{ { "name", 's' },{ "index", 'i' },{ "userid", 'i' },{ "networkid", 's' },{ "address", 's' },{ "xuid", 'l' } } },
			{ "player_connect_full",{ { "userid", 'i' },{ "index", 'i' } } },
			{ "player_team",{ { "userid", 'i' },{ "team", 'i' },{ "oldteam", 'i' },{ "disconnect", 'b' },{ "autoteam", 'b' },{ "silent", 'b' },{ "isbot", 'b' } } },
			{ "player_jump",{ { "userid", 'i' } } },
			{ "player_footstep",{ { "userid", 'i' } } },
			{ "player_falldamage",{ { "userid", 'i' },{ "damage", 'f' } } },
			{ "player_blind",{ { "userid", 'i' },{ "attacker", 'i' },{ "entityid", 'i' },{ "blind_duration", 'f' } } },
			{ "player_say",{ { "userid", 'i' },{ "text", 's' } } },
			{ "player_chat",{ { "teamonly", 'b' },{ "userid", 'i' },{ "text", 's' } } },
			{ "player_given_c4",{ { "userid", 'i' } } },
			{ "game_newmap",{ { "mapname", 's' } } },
			{ "item_purchase",{ { "userid", 'i' },{ "team", 'i' },{ "loadout", 'i' },{ "weapon", 's' } } },
			{ "item_pickup",{ { "userid", 'i' },{ "item", 's' },{ "silent", 'b' },{ "defindex", 'i' } } },
			{ "item_remove",{ { "userid", 'i' },{ "item", 's' },{ "defindex", 'i' } } },
			{ "item_equip",{ { "userid", 'i' },{ "item", 's' },{ "defindex", 'i' },{ "canzoom", 'b' },{ "hassilencer", 'b' },{ "issilenced", 'b' },{ "hastracers", 'b' },{ "weptype", 'i' },{ "ispainted", 'b' } } },
			{ "enter_buyzone",{ { "userid", 'i' },{ "canbuy", 'b' } } },
			{ "exit_buyzone",{ { "userid", 'i' },{ "canbuy", 'b' } } },
			{ "bomb_planted",{ { "userid", 'i' },{ "site", 'i' } } },
			{ "bomb_defused",{ { "userid", 'i' },{ "site", 'i' } } },
			{ "bomb_exploded",{ { "userid", 'i' },{ "site", 'i' } } },
			{ "bomb_beginplant",{ { "userid", 'i' },{ "site", 'i' } } },
			{ "bomb_abortplant",{ { "userid", 'i' },{ "site", 'i' } } },
			{ "bomb_begindefuse",{ { "userid", 'i' },{ "haskit", 'b' } } },
			{ "bomb_abortdefuse",{ { "userid", 'i' } } },
			{ "bomb_pickup",{ { "userid", 'i' } } },
			{ "bomb_dropped",{ { "userid", 'i' },{ "entindex", 'i' } } },
			{ "grenade_thrown",{ { "userid", 'i' },{ "weapon", 's' } } },
			{ "hegrenade_detonate",{ { "userid", 'i' },{ "entityid", 'i' },{ "x", 'f' },{ "y", 'f' },{ "z", 'f' } } },
			{ "flashbang_detonate",{ { "userid", 'i' },{ "entityid", 'i' },{ "x", 'f' },{ "y", 'f' },{ "z", 'f' } } },
			{ "smokegrenade_detonate",{ { "userid", 'i' },{ "entityid", 'i' },{ "x", 'f' },{ "y", 'f' },{ "z", 'f' } } },
			{ "smokegrenade_expired",{ { "userid", 'i' },{ "entityid", 'i' },{ "x", 'f' },{ "y", 'f' },{ "z", 'f' } } },
			{ "molotov_detonate",{ { "userid", 'i' },{ "x", 'f' },{ "y", 'f' },{ "z", 'f' } } },
			{ "decoy_started",{ { "userid", 'i' },{ "entityid", 'i' },{ "x", 'f' },{ "y", 'f' },{ "z", 'f' } } },
			{ "decoy_detonate",{ { "userid", 'i' },{ "entityid", 'i' },{ "x", 'f' },{ "y", 'f' },{ "z", 'f' } } },
			{ "inferno_startburn",{ { "entityid", 'i' },{ "x", 'f' },{ "y", 'f' },{ "z", 'f' } } },
			{ "inferno_expire",{ { "entityid", 'i' },{ "x", 'f' },{ "y", 'f' },{ "z", 'f' } } },
			{ "other_death",{ { "otherid", 'i' },{ "othertype", 's' },{ "attacker", 'i' },{ "weapon", 's' },{ "weapon_itemid", 's' },{ "weapon_fauxitemid", 's' },{ "weapon_originalowner_xuid", 's' },{ "headshot", 'b' },{ "penetrated", 'i' } } },
			{ "vote_cast",{ { "vote_option", 'i' },{ "team", 'i' },{ "entityid", 'i' } } },
			{ "vote_options",{ { "count", 'i' },{ "option1", 's' },{ "option2", 's' },{ "option3", 's' },{ "option4", 's' },{ "option5", 's' } } },
			{ "switch_team",{ { "numPlayers", 'i' },{ "numSpectators", 'i' },{ "avg_rank", 'i' },{ "numTSlotsFree", 'i' },{ "numCTSlotsFree", 'i' } } },
			{ "hostage_follows",{ { "userid", 'i' },{ "hostage", 'i' } } },
			{ "hostage_hurt",{ { "userid", 'i' },{ "hostage", 'i' } } },
			{ "hostage_killed",{ { "userid", 'i' },{ "hostage", 'i' } } },
			{ "hostage_rescued",{ { "userid", 'i' },{ "hostage", 'i' },{ "site", 'i' } } },
		};

		return keys;
	}

	void PushGameEvent(lua_State* L, IGameEvent* event) {
		lua_newtable(L);

		const auto& keys{ EventKeys() };
		const auto it{ keys.find(event->GetName()) };
		if (it == keys.end()) {
			// unknown layout: hand over the keys most custom events use
			for (const char* key : { "userid", "attacker", "entityid", "index" }) {
				if (!event->IsEmpty(key))
					SetField(L, key, static_cast<double>(event->GetInt(key)));
			}
			return;
		}

		for (const auto& [key, type] : it->second) {
			switch (type) {
			case 'i': SetField(L, key, static_cast<double>(event->GetInt(key))); break;
			case 'f': SetField(L, key, static_cast<double>(event->GetFloat(key))); break;
			case 'b': SetField(L, key, event->GetBool(key)); break;
			case 'l': SetField(L, key, std::to_string(event->GetUint64(key)).c_str()); break;
			default: SetField(L, key, event->GetString(key)); break;
			}
		}
	}

	void DispatchToAll(const char* name, const std::function< void(lua_State*) >& pushArgument);

	class CLuaEventListener : public IGameEventListener2 {
	public:
		void Listen(const std::string& name) {
			if (m_setEvents.count(name) || !Interfaces::GameEvent)
				return;

			if (Interfaces::GameEvent->AddListener(this, name.c_str(), false))
				m_setEvents.insert(name);
		}

		void Destroy() {
			if (!m_setEvents.empty() && Interfaces::GameEvent)
				Interfaces::GameEvent->RemoveListener(this);
			m_setEvents.clear();
		}

		void FireGameEvent(IGameEvent* event) override {
			if (!event || g_vecScripts.empty())
				return;

			DispatchToAll(event->GetName(), [event](lua_State* L) { PushGameEvent(L, event); });
		}

		int GetEventDebugID() override {
			return EVENT_DEBUG_ID_INIT;
		}

	private:
		std::unordered_set< std::string > m_setEvents{};
	};

	CLuaEventListener g_cEventListener{};

	/* ------------------------------------------------------------------------------------------------ */
	/* menu items made by scripts                                                                        */
	/* ------------------------------------------------------------------------------------------------ */

	enum ELuaItemKind {
		ITEM_CHECKBOX,
		ITEM_SLIDER,
		ITEM_COMBOBOX,
		ITEM_MULTISELECT,
		ITEM_HOTKEY,
		ITEM_BUTTON,
		ITEM_COLOR,
		ITEM_TEXTBOX,
		ITEM_LISTBOX,
		ITEM_LABEL
	};

	struct LuaItem_t {
		int m_iId{};
		Script_t* m_pScript{};
		ELuaItemKind m_eKind{};
		std::string m_strName{};   // display name ( sanitised )
		bool m_bVisible{ true };
		bool m_bInline{};          // color pickers / inline hotkeys share the row above
		LuaItem_t* m_pPrevious{};  // row these inline items attach to

		bool m_bValue{};
		int m_iValue{};
		int m_iMin{}, m_iMax{};
		Color m_colValue{ 255, 255, 255, 255 };
		keybind_t m_cKey{};
		std::string m_strText{};

		std::vector< std::string > m_vecOptions{};
		std::vector< ComboElement_t > m_vecComboElements{};
		std::unique_ptr< bool[] > m_pMulti{};
		std::vector< MultiElement_t > m_vecMultiElements{};

		// last values seen, for ui.set_callback on user edits
		bool m_bLastValue{};
		int m_iLastValue{};
		Color m_colLast{};
		std::string m_strLastText{};
		std::vector< bool > m_vecLastMulti{};
	};

	std::vector< std::unique_ptr< LuaItem_t > > g_vecItems{};
	int g_iNextItemId{ 1 };
	bool g_bItemsDirty{};
	bool g_bManagerDirty{ true };

	CMenuGroup* g_pManagerGroup{};
	CMenuGroup* g_pItemsGroup{};

	// state of the "Scripts" group
	int g_iSelectedFile{};
	std::vector< ComboElement_t > g_vecFileElements{};
	std::vector< std::string > g_vecFileLabels{};
	std::vector< std::string > g_vecManagerLabels{};

	// scripts loaded on inject ( Documents\Havoc\lua\autoload.txt, one name per line )
	std::vector< std::string > g_vecAutoload{};

	// script item values from the active CorgiHak config: script -> item key -> value
	// kept for scripts that are not loaded yet, applied the moment they load
	nlohmann::json g_jsonConfigItems = nlohmann::json::object();

	std::string SanitizeName(const std::string& raw) {
		std::string name{ raw };

		// gamesense hides everything after a newline ( used to make names unique )
		const auto newline{ name.find('\n') };
		if (newline != std::string::npos)
			name.erase(newline);

		// strip \aRRGGBBAA colour codes
		std::string out{};
		for (std::size_t i{}; i < name.size(); ++i) {
			if (name[i] == '\a') {
				std::size_t j{ i + 1 };
				while (j < name.size() && j < i + 9 && std::isxdigit(static_cast<unsigned char>(name[j])))
					++j;
				if (name.compare(i + 1, 7, "DEFAULT") == 0)
					j = i + 8;
				i = j - 1;
				continue;
			}

			// menu text goes through printf
			if (name[i] == '%')
				out += "%%";
			else if (static_cast<unsigned char>(name[i]) >= 32)
				out += name[i];
		}

		while (!out.empty() && out.back() == ' ')
			out.pop_back();

		return out;
	}

	LuaItem_t* FindItem(int id) {
		for (auto& item : g_vecItems) {
			if (item->m_iId == id)
				return item.get();
		}
		return nullptr;
	}

	void ResetFocus() {
		Menu::m_pFocusItem.m_pItem = nullptr;
		Menu::m_pFocusItem.m_flFocusAnim = 0.f;
		Menu::m_pFocusItem.m_bFadeOut = false;
	}

	void FreeItemArgs(std::vector< CMenuItem >& items) {
		for (auto& item : items) {
			if (!item.m_pArgs)
				continue;

			switch (item.m_eItemType) {
			case SLIDER_INT:
			case SLIDER_FLOAT: delete static_cast<SliderArgs_t*>(item.m_pArgs); break;
			case COMBO: delete static_cast<ComboArgs_t*>(item.m_pArgs); break;
			case MULTI_COMBO: delete static_cast<MultiComboArgs_t*>(item.m_pArgs); break;
			case COLOR_PICKER: delete static_cast<ColorPickerArgs_t*>(item.m_pArgs); break;
			case BUTTON: delete static_cast<ButtonArgs_t*>(item.m_pArgs); break;
			case TEXT_INPUT: delete static_cast<int*>(item.m_pArgs); break;
			default: break; // keybinds use m_pArgs as a "listening" flag, labels / checkboxes have none
			}

			item.m_pArgs = nullptr;
		}

		items.clear();
	}

	void AddButton(std::vector< CMenuItem >& items, const char* name, std::function< void() > callback, std::function< bool() > show = []() { return true; }) {
		auto& item{ items.emplace_back(name, nullptr, 2, show) };
		static_cast<ButtonArgs_t*>(item.m_pArgs)->m_fnCallback = std::move(callback);
	}

	void QueueAction(EQueuedAction action, const std::string& name = {}) {
		g_dqActions.push_back({ action, name });
	}

	std::string SelectedFile() {
		if (g_vecFiles.empty())
			return {};
		return g_vecFiles.at(std::clamp(g_iSelectedFile, 0, static_cast<int>(g_vecFiles.size()) - 1));
	}

	void RebuildManagerGroup() {
		if (!g_pManagerGroup)
			return;

		ResetFocus();
		auto& items{ g_pManagerGroup->Items() };
		FreeItemArgs(items);

		g_vecFileElements.clear();
		g_vecFileLabels.clear();
		g_vecFileLabels.reserve(g_vecFiles.size());
		for (const auto& file : g_vecFiles) {
			const bool autoload{ std::find(g_vecAutoload.begin(), g_vecAutoload.end(), file) != g_vecAutoload.end() };
			g_vecFileLabels.push_back(autoload ? file + "  [ auto ]" : file);
		}
		for (const auto& label : g_vecFileLabels)
			g_vecFileElements.emplace_back(label.c_str());
		if (g_vecFileElements.empty())
			g_vecFileElements.emplace_back("No scripts in Documents\\Havoc\\lua");

		g_iSelectedFile = std::clamp(g_iSelectedFile, 0, static_cast<int>(g_vecFileElements.size()) - 1);

		items.reserve(24 + g_vecScripts.size() + g_dqErrors.size() * 4);
		items.emplace_back("Script", &g_iSelectedFile, static_cast<int>(g_vecFileElements.size()), g_vecFileElements.data(), []() { return true; });

		AddButton(items, "Load", []() { QueueAction(ACTION_LOAD, SelectedFile()); });
		AddButton(items, "Unload", []() { QueueAction(ACTION_UNLOAD, SelectedFile()); });
		AddButton(items, "Reload", []() { QueueAction(ACTION_RELOAD, SelectedFile()); });
		AddButton(items, "Toggle auto-load", []() { QueueAction(ACTION_TOGGLE_AUTOLOAD, SelectedFile()); });
		AddButton(items, "Reload all", []() { QueueAction(ACTION_RELOAD_ALL); });
		AddButton(items, "Unload all", []() { QueueAction(ACTION_UNLOAD_ALL); });
		AddButton(items, "Refresh list", []() { QueueAction(ACTION_REFRESH); });
		AddButton(items, "Open scripts folder", []() { QueueAction(ACTION_OPEN_FOLDER); });

		g_vecManagerLabels.clear();
		g_vecManagerLabels.reserve(g_vecScripts.size() + 3);
		g_vecManagerLabels.push_back("Lua engine build " __DATE__ " " __TIME__);
		g_vecManagerLabels.push_back(std::to_string(g_vecFiles.size()) + " script file( s ) in:");
		g_vecManagerLabels.push_back(SanitizeName(Narrow(LuaDirectory().wstring())));
		g_vecManagerLabels.push_back(g_vecScripts.empty() ? std::string("No scripts loaded") : "Loaded scripts ( " + std::to_string(g_vecScripts.size()) + " )");
		for (const auto& script : g_vecScripts)
			g_vecManagerLabels.push_back("  " + SanitizeName(script->m_strName));

		// recent errors, wrapped to the group width
		if (!g_dqErrors.empty()) {
			g_vecManagerLabels.push_back("Errors ( " + std::to_string(g_dqErrors.size()) + " ):");
			for (const auto& error : g_dqErrors) {
				constexpr std::size_t width{ 46 };
				for (std::size_t offset{}; offset < error.size(); offset += width)
					g_vecManagerLabels.push_back((offset ? "    " : "  ") + SanitizeName(error.substr(offset, width)));
			}
		}

		for (const auto& label : g_vecManagerLabels)
			items.emplace_back(label.c_str(), true, []() { return true; });

		if (!g_dqErrors.empty())
			AddButton(items, "Clear errors", []() { g_dqErrors.clear(); g_bErrorsChanged = true; });

		g_bManagerDirty = false;
	}

	void RebuildItemsGroup() {
		if (!g_pItemsGroup)
			return;

		ResetFocus();
		auto& items{ g_pItemsGroup->Items() };
		FreeItemArgs(items);
		items.reserve(g_vecItems.size() * 2 + 8);

		Script_t* lastScript{};
		for (auto& owned : g_vecItems) {
			auto item{ owned.get() };

			// header row per script
			if (item->m_pScript != lastScript) {
				lastScript = item->m_pScript;
				items.emplace_back(lastScript->m_strName.c_str(), true, []() { return true; });
			}

			const auto show{ [item]() { return item->m_bVisible && (!item->m_bInline || !item->m_pPrevious || item->m_pPrevious->m_bVisible); } };
			const auto showOwn{ [item]() { return item->m_bVisible; } };

			switch (item->m_eKind) {
			case ITEM_CHECKBOX:
				items.emplace_back(item->m_strName.c_str(), &item->m_bValue, showOwn);
				break;
			case ITEM_SLIDER:
				items.emplace_back(item->m_strName.c_str(), &item->m_iValue, static_cast<float>(item->m_iMin), static_cast<float>(item->m_iMax), 2, showOwn);
				break;
			case ITEM_COMBOBOX:
			case ITEM_LISTBOX:
				items.emplace_back(item->m_strName.c_str(), &item->m_iValue, static_cast<int>(item->m_vecComboElements.size()), item->m_vecComboElements.data(), showOwn);
				break;
			case ITEM_MULTISELECT:
				items.emplace_back(item->m_strName.c_str(), static_cast<int>(item->m_vecMultiElements.size()), item->m_vecMultiElements.data(), showOwn);
				break;
			case ITEM_HOTKEY:
				if (!item->m_bInline)
					items.emplace_back(item->m_strName.c_str(), true, showOwn);
				items.emplace_back(&item->m_cKey, item->m_bInline ? std::function< bool() >(show) : std::function< bool() >(showOwn));
				break;
			case ITEM_BUTTON: {
				const int id{ item->m_iId };
				AddButton(items, item->m_strName.c_str(), [id]() { g_dqButtonPresses.push_back(id); }, showOwn);
			} break;
			case ITEM_COLOR:
				if (!item->m_bInline)
					items.emplace_back(item->m_strName.c_str(), true, showOwn);
				items.emplace_back(&item->m_colValue, true, item->m_bInline ? std::function< bool() >(show) : std::function< bool() >(showOwn));
				break;
			case ITEM_TEXTBOX:
				items.emplace_back(&item->m_strText, 2, showOwn);
				break;
			case ITEM_LABEL:
				items.emplace_back(item->m_strName.c_str(), true, showOwn);
				break;
			}
		}

		if (items.empty())
			items.emplace_back("Script menu items show up here", true, []() { return true; });

		g_bItemsDirty = false;
	}

	void RemoveScriptItems(Script_t* script) {
		const auto before{ g_vecItems.size() };
		g_vecItems.erase(std::remove_if(g_vecItems.begin(), g_vecItems.end(), [script](const std::unique_ptr< LuaItem_t >& item) { return item->m_pScript == script; }), g_vecItems.end());

		if (g_vecItems.size() != before) {
			// rebuild right away, the group still points at the removed items
			RebuildItemsGroup();
		}
	}

	void SnapshotItem(LuaItem_t* item) {
		item->m_bLastValue = item->m_bValue;
		item->m_iLastValue = item->m_iValue;
		item->m_colLast = item->m_colValue;
		item->m_strLastText = item->m_strText;
		item->m_vecLastMulti.assign(item->m_vecOptions.size(), false);
		for (std::size_t i{}; i < item->m_vecOptions.size() && item->m_pMulti; ++i)
			item->m_vecLastMulti[i] = item->m_pMulti[i];
	}

	bool ItemChanged(LuaItem_t* item) {
		switch (item->m_eKind) {
		case ITEM_CHECKBOX: return item->m_bValue != item->m_bLastValue;
		case ITEM_SLIDER:
		case ITEM_COMBOBOX:
		case ITEM_LISTBOX: return item->m_iValue != item->m_iLastValue;
		case ITEM_COLOR: return item->m_colValue != item->m_colLast;
		case ITEM_TEXTBOX: return item->m_strText != item->m_strLastText;
		case ITEM_MULTISELECT:
			for (std::size_t i{}; i < item->m_vecLastMulti.size() && item->m_pMulti; ++i) {
				if (item->m_pMulti[i] != item->m_vecLastMulti[i])
					return true;
			}
			return false;
		default: return false;
		}
	}

	void SetItemOptions(LuaItem_t* item, std::vector< std::string >&& options) {
		if (options.empty())
			options.emplace_back("-");

		std::vector< bool > previous{};
		if (item->m_eKind == ITEM_MULTISELECT && item->m_pMulti) {
			for (std::size_t i{}; i < item->m_vecOptions.size(); ++i)
				previous.push_back(item->m_pMulti[i]);
		}

		item->m_vecOptions = std::move(options);
		item->m_vecComboElements.clear();
		item->m_vecMultiElements.clear();

		for (std::size_t i{}; i < item->m_vecOptions.size(); ++i)
			item->m_vecOptions[i] = SanitizeName(item->m_vecOptions[i]).empty() ? std::string(" ") : item->m_vecOptions[i];

		if (item->m_eKind == ITEM_MULTISELECT) {
			item->m_pMulti = std::make_unique< bool[] >(item->m_vecOptions.size());
			for (std::size_t i{}; i < item->m_vecOptions.size(); ++i) {
				item->m_pMulti[i] = i < previous.size() ? previous[i] : false;
				item->m_vecMultiElements.emplace_back(&item->m_pMulti[i], item->m_vecOptions[i].c_str());
			}
		}
		else {
			for (const auto& option : item->m_vecOptions)
				item->m_vecComboElements.emplace_back(option.c_str());
			item->m_iValue = std::clamp(item->m_iValue, 0, static_cast<int>(item->m_vecOptions.size()) - 1);
		}

		SnapshotItem(item);
		g_bItemsDirty = true;
	}

	std::vector< std::string > ReadStringList(lua_State* L, int index) {
		std::vector< std::string > list{};
		if (!lua_istable(L, index))
			return list;

		const int count{ static_cast<int>(lua_objlen(L, index)) };
		for (int i{ 1 }; i <= count; ++i) {
			lua_rawgeti(L, index, i);
			if (lua_isstring(L, -1))
				list.push_back(ArgString(L, -1));
			lua_pop(L, 1);
		}

		return list;
	}

	// gamesense hotkey modes: 0 always on, 1 on hotkey, 2 toggle, 3 off hotkey
	int ToGamesenseMode(int mode) {
		switch (mode) {
		case EKeyMode::AlwaysOn: return 0;
		case EKeyMode::Hold: return 1;
		case EKeyMode::Toggle: return 2;
		default: return 3;
		}
	}

	int FromGamesenseMode(int mode) {
		switch (mode) {
		case 0: return EKeyMode::AlwaysOn;
		case 1: return EKeyMode::Hold;
		case 2: return EKeyMode::Toggle;
		default: return EKeyMode::Off;
		}
	}

	int ModeFromName(const std::string& name) {
		if (name == "Always on") return 0;
		if (name == "On hotkey") return 1;
		if (name == "Toggle") return 2;
		if (name == "Off hotkey") return 3;
		return -1;
	}

	/* ------------------------------------------------------------------------------------------------ */
	/* anti-aim references                                                                              */
	/* ------------------------------------------------------------------------------------------------ */

	void TakeOverAntiaim(Script_t* script) {
		if (script)
			script->m_bTouchedAntiaim = true;

		if (g_bAntiaimTakenOver)
			return;

		g_bAntiaimTakenOver = true;
		g_iPreviousAntiaimMode = Config::Get<int>(Vars.AntiaimMode);
		g_bPreviousAntiaimEnable = Config::Get<bool>(Vars.AntiaimEnable);
		Config::Get<int>(Vars.AntiaimMode) = 2;
		Config::Get<bool>(Vars.AntiaimEnable) = true;
		Features::Logger.Log("a script took over anti-aim ( mode set to Lua )", true);
	}

	void ReleaseAntiaimIfUnused() {
		if (!g_bAntiaimTakenOver)
			return;

		for (const auto& script : g_vecScripts) {
			if (script->m_bTouchedAntiaim)
				return;
		}

		g_bAntiaimTakenOver = false;
		if (Config::Get<int>(Vars.AntiaimMode) == 2) {
			Config::Get<int>(Vars.AntiaimMode) = g_iPreviousAntiaimMode;
			Config::Get<bool>(Vars.AntiaimEnable) = g_bPreviousAntiaimEnable;
		}
		Features::Antiaim.m_cLua = {};
	}

	/* ------------------------------------------------------------------------------------------------ */
	/* natives                                                                                           */
	/* ------------------------------------------------------------------------------------------------ */

	// console --------------------------------------------------------------------------------------------
	int N_log(lua_State* L) {
		PrintColored(ArgByte(L, 1), ArgByte(L, 2), ArgByte(L, 3), ArgString(L, 4));
		return 0;
	}

	int N_exec(lua_State* L) {
		const auto command{ ArgString(L, 1) };
		if (!command.empty())
			Interfaces::Engine->ClientCmdUnrestricted(command.c_str());
		return 0;
	}

	// time / globals -------------------------------------------------------------------------------------
	int N_realtime(lua_State* L) { lua_pushnumber(L, Interfaces::Globals->flRealTime); return 1; }
	int N_curtime(lua_State* L) { lua_pushnumber(L, Interfaces::Globals->flCurTime); return 1; }
	int N_frametime(lua_State* L) { lua_pushnumber(L, Interfaces::Globals->flFrameTime); return 1; }
	int N_absframetime(lua_State* L) { lua_pushnumber(L, Interfaces::Globals->flAbsFrameTime); return 1; }
	int N_maxplayers(lua_State* L) { lua_pushinteger(L, Interfaces::Globals->nMaxClients); return 1; }
	int N_tickcount(lua_State* L) { lua_pushinteger(L, Interfaces::Globals->iTickCount); return 1; }
	int N_tickinterval(lua_State* L) { lua_pushnumber(L, Interfaces::Globals->flIntervalPerTick); return 1; }
	int N_framecount(lua_State* L) { lua_pushinteger(L, Interfaces::Globals->iFrameCount); return 1; }

	int N_mapname(lua_State* L) {
		if (!Interfaces::Engine->IsInGame())
			return 0;

		const char* name{ Interfaces::Engine->GetLevelNameShort() };
		if (!name)
			return 0;
		lua_pushstring(L, name);
		return 1;
	}

	int N_lastoutgoing(lua_State* L) { lua_pushinteger(L, Interfaces::ClientState ? Interfaces::ClientState->iLastOutgoingCommand : 0); return 1; }
	int N_oldcommandack(lua_State* L) { lua_pushinteger(L, Interfaces::ClientState ? Interfaces::ClientState->iLastCommandAck : 0); return 1; }
	int N_commandack(lua_State* L) { lua_pushinteger(L, Interfaces::ClientState ? Interfaces::ClientState->iCommandAck : 0); return 1; }
	int N_choked(lua_State* L) { lua_pushinteger(L, Interfaces::ClientState ? Interfaces::ClientState->nChokedCommands : 0); return 1; }

	int N_timestamp(lua_State* L) {
		static LARGE_INTEGER frequency{};
		if (!frequency.QuadPart)
			QueryPerformanceFrequency(&frequency);

		LARGE_INTEGER counter{};
		QueryPerformanceCounter(&counter);
		lua_pushnumber(L, static_cast<double>(counter.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart));
		return 1;
	}

	int N_system_time(lua_State* L) {
		SYSTEMTIME time{};
		GetLocalTime(&time);
		lua_pushinteger(L, time.wHour);
		lua_pushinteger(L, time.wMinute);
		lua_pushinteger(L, time.wSecond);
		lua_pushinteger(L, time.wMilliseconds);
		return 4;
	}

	int N_unix_time(lua_State* L) { lua_pushnumber(L, static_cast<double>(std::time(nullptr))); return 1; }

	int N_latency(lua_State* L) {
		const auto info{ Interfaces::Engine->GetNetChannelInfo() };
		lua_pushnumber(L, info ? info->GetAvgLatency(FLOW_OUTGOING) : 0.f);
		return 1;
	}

	// screen / input -------------------------------------------------------------------------------------
	int N_screen_size(lua_State* L) {
		int width{}, height{};
		Interfaces::Engine->GetScreenSize(width, height);
		lua_pushinteger(L, width);
		lua_pushinteger(L, height);
		return 2;
	}

	int N_key_state(lua_State* L) {
		const int key{ ArgInt(L, 1) };
		lua_pushboolean(L, key > 0 && key < 256 && (GetAsyncKeyState(key) & 0x8000) != 0);
		return 1;
	}

	int N_mouse_pos(lua_State* L) {
		lua_pushinteger(L, static_cast<int>(Inputsys::MousePos.x));
		lua_pushinteger(L, static_cast<int>(Inputsys::MousePos.y));
		return 2;
	}

	int N_menu_open(lua_State* L) { lua_pushboolean(L, Menu::m_bOpened); return 1; }

	int N_menu_pos(lua_State* L) {
		lua_pushinteger(L, static_cast<int>(Menu::m_vecPos.x));
		lua_pushinteger(L, static_cast<int>(Menu::m_vecPos.y));
		return 2;
	}

	int N_menu_size(lua_State* L) {
		lua_pushinteger(L, static_cast<int>(Menu::m_vecSize.x));
		lua_pushinteger(L, static_cast<int>(Menu::m_vecSize.y));
		return 2;
	}

	int N_random_int(lua_State* L) {
		int low{ ArgInt(L, 1) }, high{ ArgInt(L, 2) };
		if (low > high)
			std::swap(low, high);
		lua_pushinteger(L, low == high ? low : low + static_cast<int>(std::rand() % (high - low + 1)));
		return 1;
	}

	int N_random_float(lua_State* L) {
		const float low{ ArgFloat(L, 1) }, high{ ArgFloat(L, 2) };
		lua_pushnumber(L, low + (high - low) * (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX)));
		return 1;
	}

	// entities -------------------------------------------------------------------------------------------
	int N_local_player(lua_State* L) {
		auto local{ LocalPlayer() };
		if (!local)
			return 0;
		lua_pushinteger(L, local->Index());
		return 1;
	}

	int N_get_all(lua_State* L) {
		const std::string className{ ArgString(L, 1) };
		lua_newtable(L);
		if (!Interfaces::Engine->IsInGame())
			return 1;

		int count{};
		const int highest{ Interfaces::ClientEntityList->GetHighestEntityIndex() };
		for (int i{ 1 }; i <= highest; ++i) {
			auto entity{ EntityAt(i) };
			if (!entity)
				continue;

			if (!className.empty()) {
				const auto name{ NetworkName(entity) };
				if (!name || className != name)
					continue;
			}

			lua_pushinteger(L, i);
			lua_rawseti(L, -2, ++count);
		}

		return 1;
	}

	int N_get_players(lua_State* L) {
		const bool enemiesOnly{ ArgBool(L, 1) };
		lua_newtable(L);
		if (!Interfaces::Engine->IsInGame())
			return 1;

		int count{};
		for (int i{ 1 }; i <= Interfaces::Globals->nMaxClients; ++i) {
			auto player{ static_cast<CBasePlayer*>(EntityAt(i)) };
			if (!player || player->Dormant() || player->IsDead())
				continue;

			if (enemiesOnly && !IsEnemy(player))
				continue;

			lua_pushinteger(L, i);
			lua_rawseti(L, -2, ++count);
		}

		return 1;
	}

	int N_classname(lua_State* L) {
		const auto name{ NetworkName(ArgEntity(L, 1)) };
		if (!name)
			return 0;
		lua_pushstring(L, name);
		return 1;
	}

	int N_is_enemy(lua_State* L) { lua_pushboolean(L, IsEnemy(ArgEntity(L, 1))); return 1; }

	int N_is_alive(lua_State* L) {
		auto entity{ ArgEntity(L, 1) };
		if (!entity) {
			lua_pushboolean(L, false);
			return 1;
		}

		if (entity->IsPlayer())
			lua_pushboolean(L, !static_cast<CBasePlayer*>(entity)->IsDead());
		else
			lua_pushboolean(L, entity->m_iHealth() > 0);
		return 1;
	}

	int N_is_dormant(lua_State* L) {
		auto entity{ ArgEntity(L, 1) };
		lua_pushboolean(L, !entity || entity->Dormant());
		return 1;
	}

	int N_player_name(lua_State* L) {
		const int index{ ArgInt(L, 1) };
		const auto info{ index > 0 ? Interfaces::Engine->GetPlayerInfo(index) : std::nullopt };
		lua_pushstring(L, info.has_value() ? info->szName : "unknown");
		return 1;
	}

	int N_player_weapon(lua_State* L) {
		auto player{ ArgPlayer(L, 1) };
		if (!player)
			return 0;

		const int index{ HandleToIndex(player->m_hActiveWeapon()) };
		if (!index)
			return 0;
		lua_pushinteger(L, index);
		return 1;
	}

	int N_hitbox_position(lua_State* L) {
		Vector position{};
		if (!HitboxPosition(ArgPlayer(L, 1), ArgInt(L, 2), position))
			return 0;

		lua_pushnumber(L, position.x);
		lua_pushnumber(L, position.y);
		lua_pushnumber(L, position.z);
		return 3;
	}

	int N_steam64(lua_State* L) {
		const int index{ ArgInt(L, 1) };
		const auto info{ index > 0 ? Interfaces::Engine->GetPlayerInfo(index) : std::nullopt };
		if (!info.has_value() || info->bFakePlayer)
			return 0;
		lua_pushnumber(L, static_cast<double>(info->ullXuid));
		return 1;
	}

	int N_bounding_box(lua_State* L) {
		auto player{ ArgPlayer(L, 1) };
		rect box{};
		if (!player || !Features::Visuals.PlayerESP.GetBBox(player, box)) {
			for (int i{}; i < 5; ++i)
				lua_pushnumber(L, 0);
			return 5;
		}

		const auto& entry{ Features::Visuals.PlayerESP.Entries.at(std::clamp(player->Index() - 1, 0, 63)) };
		lua_pushnumber(L, box.x);
		lua_pushnumber(L, box.y);
		lua_pushnumber(L, box.x + box.w);
		lua_pushnumber(L, box.y + box.h);
		lua_pushnumber(L, player->Dormant() ? std::clamp(entry.Alpha, 0.f, 1.f) : 1.f);
		return 5;
	}

	int N_origin(lua_State* L) {
		auto entity{ ArgEntity(L, 1) };
		if (!entity)
			return 0;

		const auto origin{ entity->IsPlayer() ? entity->m_vecOrigin() : entity->GetAbsOrigin() };
		lua_pushnumber(L, origin.x);
		lua_pushnumber(L, origin.y);
		lua_pushnumber(L, origin.z);
		return 3;
	}

	int N_esp_data(lua_State* L) {
		auto player{ ArgPlayer(L, 1) };
		if (!player)
			return 0;

		const auto& entry{ Features::Visuals.PlayerESP.Entries.at(std::clamp(player->Index() - 1, 0, 63)) };
		auto weapon{ player->GetWeapon() };
		lua_pushnumber(L, player->Dormant() ? std::clamp(entry.Alpha, 0.f, 1.f) : 1.f);
		lua_pushinteger(L, player->m_iHealth());
		lua_pushinteger(L, weapon ? weapon->m_iItemDefinitionIndex() : 0);
		return 3;
	}

	int N_get_prop(lua_State* L) {
		auto entity{ ArgEntity(L, 1) };
		const std::string name{ ArgString(L, 2) };
		if (!entity || name.empty())
			return 0;

		const auto& lookup{ LookupProp(entity, name) };
		if (!lookup.m_bFound)
			return 0;

		const auto address{ PropAddress(entity, lookup, ArgInt(L, 3, 0)) };
		if (!address)
			return 0;

		return PushPropValue(L, lookup, address, name);
	}

	int N_set_prop(lua_State* L) {
		auto entity{ ArgEntity(L, 1) };
		const std::string name{ ArgString(L, 2) };
		if (!entity || name.empty())
			return 0;

		const auto& lookup{ LookupProp(entity, name) };
		if (!lookup.m_bFound)
			return 0;

		const int index{ ArgInt(L, 3, 0) };
		const auto address{ PropAddress(entity, lookup, index) };
		if (address)
			WritePropValue(L, lookup, address, 4);

		// animation breakers: keep the pose so the local model build picks it up
		if (name == "m_flPoseParameter" && entity == LocalPlayer() && index >= 0 && index < POSE_COUNT && lua_isnumber(L, 4))
			g_arrPoseOverrides[index] = { ArgFloat(L, 4), Interfaces::Globals->flRealTime };
		return 0;
	}

	int N_game_rules(lua_State* L) {
		const int index{ FindEntityByClass("CCSGameRulesProxy") };
		if (!index)
			return 0;
		lua_pushinteger(L, index);
		return 1;
	}

	int N_player_resource(lua_State* L) {
		const int index{ FindEntityByClass("CCSPlayerResource") };
		if (!index)
			return 0;
		lua_pushinteger(L, index);
		return 1;
	}

	int N_userid_to_entindex(lua_State* L) {
		lua_pushinteger(L, Interfaces::Engine->GetPlayerForUserID(ArgInt(L, 1)));
		return 1;
	}

	int N_model_name(lua_State* L) {
		auto entity{ ArgEntity(L, 1) };
		auto model{ entity ? entity->GetModel() : nullptr };
		if (!model)
			return 0;
		lua_pushstring(L, Interfaces::ModelInfo->GetModelName(model));
		return 1;
	}

	int N_client_entity(lua_State* L) {
		auto entity{ ArgEntity(L, 1) };
		if (!entity)
			return 0;
		lua_pushlightuserdata(L, entity);
		return 1;
	}

	int N_anim_state(lua_State* L) {
		auto player{ ArgPlayer(L, 1) };
		auto state{ player ? player->m_pAnimState() : nullptr };
		if (!state)
			return 0;
		lua_pushlightuserdata(L, state);
		return 1;
	}

	int N_anim_layer(lua_State* L) {
		auto player{ ArgPlayer(L, 1) };
		const int layer{ ArgInt(L, 2) };
		if (!player || layer < 0 || layer >= 13 || !player->m_AnimationLayers())
			return 0;
		lua_pushlightuserdata(L, &player->m_AnimationLayers()[layer]);
		return 1;
	}

	// traces / camera ------------------------------------------------------------------------------------
	Vector ArgVector(lua_State* L, int i) {
		return Vector(ArgFloat(L, i), ArgFloat(L, i + 1), ArgFloat(L, i + 2));
	}

	int N_trace_line(lua_State* L) {
		auto skip{ ArgEntity(L, 1) };
		const auto from{ ArgVector(L, 2) }, to{ ArgVector(L, 5) };

		CTraceFilter filter{ skip };
		CGameTrace trace{};
		Interfaces::EngineTrace->TraceRay({ from, to }, MASK_SHOT, &filter, &trace);

		lua_pushnumber(L, trace.flFraction);
		lua_pushinteger(L, trace.pHitEntity ? trace.pHitEntity->Index() : -1);
		return 2;
	}

	int N_trace_bullet(lua_State* L) {
		auto shooter{ ArgPlayer(L, 1) };
		if (!shooter)
			shooter = LocalPlayer();

		auto weapon{ shooter ? shooter->GetWeapon() : nullptr };
		auto data{ weapon ? weapon->GetCSWeaponData() : nullptr };
		if (!shooter || !data) {
			lua_pushnil(L);
			lua_pushnumber(L, 0);
			return 2;
		}

		const auto from{ ArgVector(L, 2) }, to{ ArgVector(L, 5) };
		const auto result{ Features::Autowall.FireBullet(shooter, nullptr, data, weapon->m_iItemDefinitionIndex() == WEAPON_TASER, from, to, true) };

		if (result.target)
			lua_pushinteger(L, result.target->Index());
		else
			lua_pushnil(L);
		lua_pushnumber(L, std::max(result.dmg, 0.f));
		return 2;
	}

	int N_scale_damage(lua_State* L) {
		auto player{ ArgPlayer(L, 1) };
		float damage{ ArgFloat(L, 3) };
		if (player) {
			auto local{ LocalPlayer() };
			auto weapon{ local ? local->GetWeapon() : nullptr };
			auto data{ weapon ? weapon->GetCSWeaponData() : nullptr };
			Features::Autowall.ScaleDamage(player, damage, data ? data->flArmorRatio : 1.f, ArgInt(L, 2), data ? data->flHeadShotMultiplier : 4.f);
		}
		lua_pushnumber(L, damage);
		return 1;
	}

	Vector LocalEye() {
		auto local{ LocalPlayer() };
		if (!local)
			return {};
		return local->GetAbsOrigin() + local->m_vecViewOffset();
	}

	int N_visible(lua_State* L) {
		auto local{ LocalPlayer() };
		if (!local) {
			lua_pushboolean(L, false);
			return 1;
		}

		CTraceFilter filter{ local };
		CGameTrace trace{};
		Interfaces::EngineTrace->TraceRay({ LocalEye(), ArgVector(L, 1) }, MASK_SHOT, &filter, &trace);
		lua_pushboolean(L, trace.flFraction > 0.97f);
		return 1;
	}

	int N_eye_position(lua_State* L) {
		if (!LocalPlayer())
			return 0;

		const auto eye{ ctx.m_pLocal && ctx.m_bInCreatemove ? ctx.m_vecEyePos : LocalEye() };
		lua_pushnumber(L, eye.x);
		lua_pushnumber(L, eye.y);
		lua_pushnumber(L, eye.z);
		return 3;
	}

	int N_camera_angles(lua_State* L) {
		QAngle angles{};
		Interfaces::Engine->GetViewAngles(angles);
		lua_pushnumber(L, angles.x);
		lua_pushnumber(L, angles.y);
		lua_pushnumber(L, angles.z);
		return 3;
	}

	int N_camera_position(lua_State* L) {
		const auto position{ g_bHasCamera ? g_vecCameraOrigin : LocalEye() };
		lua_pushnumber(L, position.x);
		lua_pushnumber(L, position.y);
		lua_pushnumber(L, position.z);
		return 3;
	}

	// body yaw a resolver side stands for: eye yaw minus that side's foot ( abs ) yaw, same sign convention
	// a script uses for the local player ( eye - abs ), so both ends of a resolver test compare directly
	float SideBodyYaw(const LagRecord_t& record, int side) {
		side = std::clamp(side, 0, RESOLVER_SIDE_COUNT - 1);
		const auto& data{ record.m_cAnimData.m_arrSides.at(side) };
		if (!data.m_bFilled)
			return 0.f;
		return std::remainderf(record.m_angEyeAngles.y - data.m_cAnimState.flAbsYaw, 360.f);
	}

	// client.resolver_info( ent ) -> { side, body_yaw, pattern, tick, multi_matrix } for the newest record ( nil without one )
	int N_resolver_info(lua_State* L) {
		const int index{ ArgInt(L, 1, 0) };
		if (index < 1 || index > 64) {
			lua_pushnil(L);
			return 1;
		}

		auto& entry{ Features::AnimSys.m_arrEntries.at(index - 1) };
		if (!entry.m_pPlayer || entry.m_pRecords.empty() || !entry.m_pRecords.back()) {
			lua_pushnil(L);
			return 1;
		}

		const auto& record{ *entry.m_pRecords.back() };
		const int side{ Features::Ragebot.ResolveRecordSide(entry, record, index) };

		lua_newtable(L);
		SetField(L, "side", static_cast<double>(side));
		SetField(L, "body_yaw", static_cast<double>(record.m_bMultiMatrix ? SideBodyYaw(record, side) : 0.f));
		SetField(L, "pattern", CResolverBrain::PatternName(Features::Ragebot.m_cBrain.PatternFor(index, record)));
		SetField(L, "tick", static_cast<double>(TIME_TO_TICKS(record.m_cAnimData.m_flSimulationTime)));
		SetField(L, "eye_yaw", static_cast<double>(record.m_angEyeAngles.y));
		SetField(L, "multi_matrix", record.m_bMultiMatrix);
		return 1;
	}

	// client.rage_debug( ent ): what the ragebot and the defensive detector think about this player right now
	int N_rage_debug(lua_State* L) {
		const int index{ ArgInt(L, 1, 0) };
		if (index < 1 || index > 64) {
			lua_pushnil(L);
			return 1;
		}

		const int tick{ Interfaces::Globals->iTickCount };
		const auto& debug{ Features::Ragebot.Debug(index) };
		const auto& brain{ Features::Ragebot.m_cBrain.Get(index) };
		auto& entry{ Features::AnimSys.m_arrEntries.at(index - 1) };

		lua_newtable(L);

		// ragebot's last pass on this player
		SetField(L, "status", debug.m_strStatus.c_str());
		SetField(L, "status_age", static_cast<double>(debug.m_iTick >= 0 ? tick - debug.m_iTick : -1));
		SetField(L, "target", debug.m_bTarget);
		SetField(L, "fired", debug.m_bFired);
		SetField(L, "record_age", static_cast<double>(debug.m_iRecordAge));
		SetField(L, "validity", static_cast<double>(debug.m_iValidity));
		SetField(L, "extrapolated", debug.m_bExtrapolated);
		SetField(L, "extrap_ticks", static_cast<double>(debug.m_iExtrapTicks));
		SetField(L, "spread", static_cast<double>(debug.m_iSpread));
		SetField(L, "damage", static_cast<double>(debug.m_iDamage));

		// defensive detector
		SetField(L, "defensive", brain.m_bDefensive);
		SetField(L, "defensive_user", brain.m_bDefensiveUser);
		SetField(L, "in_shift", brain.m_bInShift);
		SetField(L, "below_max", brain.m_bBelowMax);
		SetField(L, "hold_fire", Features::Ragebot.m_cBrain.ShouldHoldFire(index));
		SetField(L, "defensive_left", static_cast<double>(brain.m_iDefensiveUntil >= 0 ? brain.m_iDefensiveUntil - tick : -1));
		SetField(L, "defensive_shift", static_cast<double>(brain.m_iDefensiveShift));
		SetField(L, "doubletap_shift", brain.m_bDoubletapShift);
		SetField(L, "lc_broken", brain.m_bLagCompBroken || entry.m_bBrokeLC);
		SetField(L, "flicker", brain.m_bFlicker);
		SetField(L, "suspect_flick", brain.m_bSuspectFlick);
		SetField(L, "miss_streak", static_cast<double>(brain.m_iMissStreak));
		SetField(L, "drops", static_cast<double>(brain.m_iDbgShifts));
		SetField(L, "drops_defensive", static_cast<double>(brain.m_iDbgDefensive));
		SetField(L, "drops_pushed", static_cast<double>(brain.m_iDbgPushed));
		SetField(L, "last_drop", static_cast<double>(brain.m_iDbgLastShift));
		SetField(L, "max_sim_tick", static_cast<double>(TIME_TO_TICKS(brain.m_flMaxSimTime)));
		SetField(L, "last_sim_tick", static_cast<double>(TIME_TO_TICKS(brain.m_flLastSimTime)));

		// lag records
		int valid{}, defensiveRecords{}, flickRecords{};
		for (const auto& record : entry.m_pRecords) {
			if (!record)
				continue;
			if (record->Validity())
				++valid;
			if (Features::Ragebot.m_cBrain.IsDefensiveRecord(index, *record))
				++defensiveRecords;
			if (Features::Ragebot.m_cBrain.IsFlickRecord(index, *record))
				++flickRecords;
		}
		SetField(L, "records", static_cast<double>(entry.m_pRecords.size()));
		SetField(L, "records_valid", static_cast<double>(valid));
		SetField(L, "records_defensive", static_cast<double>(defensiveRecords));
		SetField(L, "records_flick", static_cast<double>(flickRecords));
		if (!entry.m_pRecords.empty() && entry.m_pRecords.back()) {
			auto& newest{ *entry.m_pRecords.back() };
			SetField(L, "newest_sim_tick", static_cast<double>(TIME_TO_TICKS(newest.m_cAnimData.m_flSimulationTime)));
			SetField(L, "newest_age", static_cast<double>(tick - newest.m_iReceiveTick));
			SetField(L, "newest_valid", static_cast<double>(newest.Validity()));
		}
		return 1;
	}

	int N_current_threat(lua_State* L) {
		auto threat{ CurrentThreat() };
		if (!threat)
			return 0;
		lua_pushinteger(L, threat->Index());
		return 1;
	}

	// rendering ( only valid inside paint / paint_ui ) -----------------------------------------------------
	std::unordered_map< std::string, HFont > g_mapFonts{};

	int N_font(lua_State* L) {
		const std::string name{ ArgString(L, 1) };
		const int tall{ ArgInt(L, 2, 12) }, weight{ ArgInt(L, 3, 400) }, flags{ ArgInt(L, 4, 0) };
		const std::string key{ name + "|" + std::to_string(tall) + "|" + std::to_string(weight) + "|" + std::to_string(flags) };

		auto it{ g_mapFonts.find(key) };
		if (it == g_mapFonts.end()) {
			const HFont font{ Interfaces::Surface->FontCreate() };
			Interfaces::Surface->SetFontGlyphSet(font, name.c_str(), tall, weight, 0, 0, flags);
			it = g_mapFonts.emplace(key, font).first;
		}

		lua_pushnumber(L, static_cast<double>(it->second));
		return 1;
	}

	int N_text(lua_State* L) {
		if (!g_bInPaint)
			return 0;

		const auto font{ static_cast<HFont>(lua_tonumber(L, 1)) };
		const auto text{ Widen(ArgString(L, 8)) };
		if (text.empty())
			return 0;

		Interfaces::Surface->DrawSetTextFont(font);
		Interfaces::Surface->DrawSetTextColor(ArgByte(L, 4), ArgByte(L, 5), ArgByte(L, 6), ArgByte(L, 7));
		Interfaces::Surface->DrawSetTextPos(ArgInt(L, 2), ArgInt(L, 3));
		Interfaces::Surface->DrawPrintText(text.c_str(), static_cast<int>(text.size()));
		return 0;
	}

	int N_text_size(lua_State* L) {
		const auto font{ static_cast<HFont>(lua_tonumber(L, 1)) };
		const auto text{ Widen(ArgString(L, 2)) };
		int width{}, height{};
		if (!text.empty())
			Interfaces::Surface->GetTextSize(font, text.c_str(), width, height);
		else
			Interfaces::Surface->GetTextSize(font, L"A", width, height), width = 0;

		lua_pushinteger(L, width);
		lua_pushinteger(L, height);
		return 2;
	}

	int N_rect(lua_State* L) {
		if (!g_bInPaint)
			return 0;

		const int x{ ArgInt(L, 1) }, y{ ArgInt(L, 2) }, w{ ArgInt(L, 3) }, h{ ArgInt(L, 4) };
		Interfaces::Surface->DrawSetColor(ArgByte(L, 5), ArgByte(L, 6), ArgByte(L, 7), ArgByte(L, 8));
		Interfaces::Surface->DrawFilledRect(x, y, x + w, y + h);
		return 0;
	}

	int N_line(lua_State* L) {
		if (!g_bInPaint)
			return 0;

		Interfaces::Surface->DrawSetColor(ArgByte(L, 5), ArgByte(L, 6), ArgByte(L, 7), ArgByte(L, 8));
		Interfaces::Surface->DrawLine(ArgInt(L, 1), ArgInt(L, 2), ArgInt(L, 3), ArgInt(L, 4));
		return 0;
	}

	int N_gradient(lua_State* L) {
		if (!g_bInPaint)
			return 0;

		const int x{ ArgInt(L, 1) }, y{ ArgInt(L, 2) }, w{ ArgInt(L, 3) }, h{ ArgInt(L, 4) };
		const int r1{ ArgByte(L, 5) }, g1{ ArgByte(L, 6) }, b1{ ArgByte(L, 7) }, a1{ ArgByte(L, 8) };
		const int r2{ ArgByte(L, 9) }, g2{ ArgByte(L, 10) }, b2{ ArgByte(L, 11) }, a2{ ArgByte(L, 12) };
		const bool horizontal{ ArgBool(L, 13) };

		// first colour fades out across the rect, second fades in
		Interfaces::Surface->DrawSetColor(r1, g1, b1, 255);
		Interfaces::Surface->DrawFilledRectFade(x, y, x + w, y + h, a1, 0, horizontal);
		Interfaces::Surface->DrawSetColor(r2, g2, b2, 255);
		Interfaces::Surface->DrawFilledRectFade(x, y, x + w, y + h, 0, a2, horizontal);
		return 0;
	}

	int N_poly(lua_State* L) {
		if (!g_bInPaint || !lua_istable(L, 1))
			return 0;

		const int count{ static_cast<int>(lua_objlen(L, 1)) / 2 };
		if (count < 3 || count > 512)
			return 0;

		Vertex_t vertices[512];
		for (int i{}; i < count; ++i) {
			lua_rawgeti(L, 1, i * 2 + 1);
			lua_rawgeti(L, 1, i * 2 + 2);
			vertices[i].Init(Vector2D(static_cast<float>(lua_tonumber(L, -2)), static_cast<float>(lua_tonumber(L, -1))));
			lua_pop(L, 2);
		}

		// the surface culls counter clockwise polygons: put every polygon in clockwise ( screen space ) order
		float area{};
		for (int i{}; i < count; ++i) {
			const auto& a{ vertices[i].vecPosition };
			const auto& b{ vertices[(i + 1) % count].vecPosition };
			area += a.x * b.y - b.x * a.y;
		}
		if (area < 0.f)
			std::reverse(vertices, vertices + count);

		Interfaces::Surface->DrawSetColor(ArgByte(L, 2), ArgByte(L, 3), ArgByte(L, 4), ArgByte(L, 5));
		Interfaces::Surface->DrawSetTexture(WhiteTexture());
		Interfaces::Surface->DrawTexturedPolygon(count, vertices);
		return 0;
	}

	int N_w2s(lua_State* L) {
		Vector2D screen{};
		if (!Math::WorldToScreen(ArgVector(L, 1), screen))
			return 0;
		lua_pushnumber(L, screen.x);
		lua_pushnumber(L, screen.y);
		return 2;
	}

	int N_texture_rgba(lua_State* L) {
		const auto data{ ArgString(L, 1) };
		const int width{ ArgInt(L, 2) }, height{ ArgInt(L, 3) };
		if (width <= 0 || height <= 0 || data.size() < static_cast<std::size_t>(width) * height * 4)
			return 0;

		lua_pushinteger(L, UploadTexture(reinterpret_cast<const unsigned char*>(data.data()), width, height));
		return 1;
	}

	int N_texture_image(lua_State* L) {
		const auto data{ ArgString(L, 1) };
		if (data.empty())
			return 0;

		const std::string key{ "img|" + data };
		if (const auto it{ g_mapTextures.find(key) }; it != g_mapTextures.end())
			return PushCachedTexture(L, it->second);

		CachedTexture_t texture{};
		int width{}, height{}, channels{};
		if (unsigned char* pixels{ stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(data.data()), static_cast<int>(data.size()), &width, &height, &channels, 4) }) {
			texture = { UploadTexture(pixels, width, height), width, height };
			stbi_image_free(pixels);
		}

		g_mapTextures.emplace(key, texture);
		return PushCachedTexture(L, texture);
	}

	int N_texture_svg(lua_State* L) {
		auto data{ ArgString(L, 1) };
		const int wantW{ ArgInt(L, 2) }, wantH{ ArgInt(L, 3) };
		if (data.empty())
			return 0;

		const std::string key{ "svg|" + std::to_string(wantW) + "|" + std::to_string(wantH) + "|" + data };
		if (const auto it{ g_mapTextures.find(key) }; it != g_mapTextures.end())
			return PushCachedTexture(L, it->second);

		CachedTexture_t texture{};
		data.push_back('\0'); // nsvgParse edits the buffer in place
		if (auto image{ nsvgParse(data.data(), "px", 96.f) }) {
			if (image->width > 0.f && image->height > 0.f) {
				float scale{ 1.f };
				if (wantW > 0)
					scale = wantW / image->width;
				else if (wantH > 0)
					scale = wantH / image->height;

				const int width{ std::max(1, wantW > 0 ? wantW : static_cast<int>(std::ceil(image->width * scale))) };
				const int height{ std::max(1, wantH > 0 ? wantH : static_cast<int>(std::ceil(image->height * scale))) };

				if (width <= 2048 && height <= 2048) {
					std::vector< unsigned char > pixels(static_cast<std::size_t>(width) * height * 4);
					if (auto rasterizer{ nsvgCreateRasterizer() }) {
						// non uniform target size: rasterize at the width scale, the texture stretches to the rest
						nsvgRasterize(rasterizer, image, 0.f, 0.f, scale, pixels.data(), width, height, width * 4);
						nsvgDeleteRasterizer(rasterizer);
						texture = { UploadTexture(pixels.data(), width, height), width, height };
					}
				}
			}
			nsvgDelete(image);
		}

		g_mapTextures.emplace(key, texture);
		return PushCachedTexture(L, texture);
	}

	int N_draw_texture(lua_State* L) {
		if (!g_bInPaint || !lua_isnumber(L, 1))
			return 0;

		const int x{ ArgInt(L, 2) }, y{ ArgInt(L, 3) }, w{ ArgInt(L, 4) }, h{ ArgInt(L, 5) };
		Interfaces::Surface->DrawSetColor(ArgByte(L, 6), ArgByte(L, 7), ArgByte(L, 8), ArgByte(L, 9));
		Interfaces::Surface->DrawSetTexture(ArgInt(L, 1));
		MEM::CallVFunc<void>(Interfaces::Surface, 41, x, y, x + w, y + h); // DrawTexturedRect
		return 0;
	}

	// debug overlay ----------------------------------------------------------------------------------------
	int N_debug_text(lua_State* L) {
		const auto text{ ArgString(L, 10) };
		Interfaces::DebugOverlay->AddTextOverlayRGB(ArgVector(L, 1), ArgInt(L, 4), ArgFloat(L, 5),
			ArgByte(L, 6), ArgByte(L, 7), ArgByte(L, 8), ArgByte(L, 9), "%s", text.c_str());
		return 0;
	}

	int N_draw_hitboxes(lua_State* L) {
		auto player{ ArgPlayer(L, 1) };
		if (!player)
			return 0;

		const float duration{ ArgFloat(L, 2) };
		const int which{ ArgInt(L, 3, -1) };
		const int r{ ArgByte(L, 4) }, g{ ArgByte(L, 5) }, b{ ArgByte(L, 6) }, a{ ArgByte(L, 7) };

		for (int hitbox{ which < 0 ? 0 : which }; hitbox <= (which < 0 ? 18 : which); ++hitbox) {
			Vector start{}, end{};
			float radius{};
			if (!HitboxPosition(player, hitbox, start, &radius, &end))
				continue;

			Interfaces::DebugOverlay->AddLineOverlayAlpha(start, end, r, g, b, a, true, duration);
			if (radius > 0.f) {
				Interfaces::DebugOverlay->AddSphereOverlay(start, radius, 6, 6, r, g, b, a / 4, duration);
				Interfaces::DebugOverlay->AddSphereOverlay(end, radius, 6, 6, r, g, b, a / 4, duration);
			}
		}
		return 0;
	}

	// cvars --------------------------------------------------------------------------------------------------
	int N_cvar_find(lua_State* L) {
		const auto name{ ArgString(L, 1) };
		if (name.empty())
			return 0;

		if (auto variable{ Interfaces::ConVar->FindVar(name.c_str()) }) {
			lua_pushlightuserdata(L, variable);
			lua_pushboolean(L, false);
			return 2;
		}

		if (auto command{ Interfaces::ConVar->FindCommand(name.c_str()) }) {
			lua_pushlightuserdata(L, command);
			lua_pushboolean(L, true);
			return 2;
		}

		return 0;
	}

	CConVar* ArgConVar(lua_State* L, int i) {
		return lua_islightuserdata(L, i) ? static_cast<CConVar*>(lua_touserdata(L, i)) : nullptr;
	}

	// replicated cvars ( sv_maxunlag, sv_gravity ... ) belong to the server: a script changing them locally only
	// makes the cheat disagree with the server ( e.g. backtracking records the server already threw away )
	CConVar* WritableConVar(lua_State* L, int i) {
		auto variable{ ArgConVar(L, i) };
		if (!variable)
			return nullptr;

		constexpr int FCVAR_REPLICATED_FLAG{ 1 << 13 };
		const int flags{ variable->pParent ? variable->pParent->nFlags : variable->nFlags };
		if (flags & FCVAR_REPLICATED_FLAG) {
			static std::unordered_set< std::string > warned{};
			const std::string name{ variable->szName ? variable->szName : "?" };
			if (warned.insert(name).second)
				PrintColored(255, 200, 90, "[ CorgiHak ] lua: blocked a script from changing server cvar " + name + "\n");
			return nullptr;
		}

		return variable;
	}

	int N_cvar_get(lua_State* L) {
		auto variable{ ArgConVar(L, 1) };
		if (!variable)
			return 0;

		lua_pushstring(L, variable->GetString() ? variable->GetString() : "");
		lua_pushnumber(L, variable->GetFloat());
		lua_pushinteger(L, variable->GetInt());
		return 3;
	}

	int N_cvar_set_string(lua_State* L) {
		if (auto variable{ WritableConVar(L, 1) })
			variable->SetValue(ArgString(L, 2).c_str());
		return 0;
	}

	int N_cvar_set_float(lua_State* L) {
		if (auto variable{ WritableConVar(L, 1) })
			variable->SetValue(ArgFloat(L, 2));
		return 0;
	}

	int N_cvar_set_int(lua_State* L) {
		if (auto variable{ WritableConVar(L, 1) })
			variable->SetValue(ArgInt(L, 2));
		return 0;
	}

	// raw writes skip change callbacks, values are stored xored with the parent pointer
	int N_cvar_set_raw_float(lua_State* L) {
		auto variable{ WritableConVar(L, 1) };
		if (!variable || !variable->pParent)
			return 0;

		const float value{ ArgFloat(L, 2) };
		auto parent{ variable->pParent };
		*reinterpret_cast<std::uint32_t*>(&parent->flValue) = *reinterpret_cast<const std::uint32_t*>(&value) ^ reinterpret_cast<std::uint32_t>(variable);
		return 0;
	}

	int N_cvar_set_raw_int(lua_State* L) {
		auto variable{ WritableConVar(L, 1) };
		if (!variable || !variable->pParent)
			return 0;

		variable->pParent->iValue = ArgInt(L, 2) ^ reinterpret_cast<int>(variable);
		return 0;
	}

	// cheat config ( gamesense menu references ) ---------------------------------------------------------------
	VariableObject_t* FindVariable(const std::string& name) {
		const auto index{ Config::GetVariableIndex(FNV1A::Hash(name.c_str())) };
		if (index == C_INVALID_VARIABLE || index >= Config::vecVariables.size())
			return nullptr;
		return &Config::vecVariables.at(index);
	}

	int N_cfg_get(lua_State* L) {
		auto variable{ FindVariable(ArgString(L, 1)) };
		if (!variable)
			return 0;

		if (variable->uTypeHash == FNV1A::HashConst("bool")) {
			lua_pushboolean(L, variable->Get<bool>());
			return 1;
		}
		if (variable->uTypeHash == FNV1A::HashConst("int")) {
			lua_pushinteger(L, variable->Get<int>());
			return 1;
		}
		if (variable->uTypeHash == FNV1A::HashConst("float")) {
			lua_pushnumber(L, variable->Get<float>());
			return 1;
		}
		if (variable->uTypeHash == FNV1A::HashConst("Color")) {
			const auto& color{ variable->Get<Color>() };
			lua_pushinteger(L, color.Get<COLOR_R>());
			lua_pushinteger(L, color.Get<COLOR_G>());
			lua_pushinteger(L, color.Get<COLOR_B>());
			lua_pushinteger(L, color.Get<COLOR_A>());
			return 4;
		}
		if (variable->uTypeHash == FNV1A::HashConst("keybind_t")) {
			const auto& key{ variable->Get<keybind_t>() };
			lua_pushboolean(L, key.enabled);
			lua_pushinteger(L, ToGamesenseMode(key.mode));
			lua_pushinteger(L, key.key);
			return 3;
		}

		return 0;
	}

	int N_cfg_set(lua_State* L) {
		auto variable{ FindVariable(ArgString(L, 1)) };
		if (!variable)
			return 0;

		if (variable->uTypeHash == FNV1A::HashConst("bool"))
			variable->Get<bool>() = ArgBool(L, 2);
		else if (variable->uTypeHash == FNV1A::HashConst("int"))
			variable->Get<int>() = ArgInt(L, 2, variable->Get<int>());
		else if (variable->uTypeHash == FNV1A::HashConst("float"))
			variable->Get<float>() = ArgFloat(L, 2, variable->Get<float>());
		else if (variable->uTypeHash == FNV1A::HashConst("Color"))
			variable->Get<Color>() = Color(ArgByte(L, 2), ArgByte(L, 3), ArgByte(L, 4), ArgByte(L, 5));
		else if (variable->uTypeHash == FNV1A::HashConst("keybind_t") && lua_isnumber(L, 3))
			variable->Get<keybind_t>().mode = FromGamesenseMode(ArgInt(L, 3));

		return 0;
	}

	int N_weapon_group(lua_State* L) {
		lua_pushstring(L, WeaponGroupSuffix());
		return 1;
	}

	int N_gs_aa(lua_State* L) {
		const auto field{ ArgString(L, 1) };
		const int value{ ArgInt(L, 2) };
		auto& aa{ Features::Antiaim.m_cLua };

		if (field == "pitch") aa.m_iPitch = value;
		else if (field == "pitch_value") aa.m_iPitchValue = value;
		else if (field == "yaw_base") aa.m_iYawBase = value;
		else if (field == "yaw") aa.m_iYaw = value;
		else if (field == "yaw_value") aa.m_iYawValue = value;
		else if (field == "yaw_jitter") aa.m_iJitter = value;
		else if (field == "yaw_jitter_value") aa.m_iJitterValue = value;
		else if (field == "body_yaw") aa.m_iBody = value;
		else if (field == "body_yaw_value") aa.m_iBodyValue = value;
		else if (field == "fs_body_yaw") aa.m_bFreestandBody = value != 0;
		else if (field == "edge_yaw") aa.m_bEdge = value != 0;
		else if (field == "freestanding") aa.m_bFreestanding = value != 0;
		else if (field == "roll") aa.m_iRoll = value;
		else if (field == "fakelag") aa.m_bFakeLag = value != 0;
		else if (field == "fakelag_amount") aa.m_iFakeLagAmount = value;
		else
			return 0;

		TakeOverAntiaim(ScriptOf(L));
		return 0;
	}

	int N_aa_info(lua_State* L) {
		lua_newtable(L);
		auto local{ LocalPlayer() };
		auto state{ local ? local->m_pAnimState() : nullptr };

		const bool doubletap{ Config::Get<bool>(Vars.ExploitsDoubletap) && Config::Get<keybind_t>(Vars.ExploitsDoubletapKey).enabled };
		SetField(L, "double_tap", doubletap && ctx.m_iTicksAllowed > 0);
		SetField(L, "shift", static_cast<double>(ctx.m_iTicksAllowed));
		SetField(L, "max_desync", 58.0);

		if (state) {
			const float bodyYaw{ std::remainderf(state->flEyeYaw - state->flAbsYaw, 360.f) };
			SetField(L, "abs_yaw", static_cast<double>(state->flAbsYaw));
			SetField(L, "goal_feet_yaw", static_cast<double>(state->flAbsYaw));
			SetField(L, "body_yaw", static_cast<double>(std::clamp(bodyYaw, -60.f, 60.f)));
			SetField(L, "desync", static_cast<double>(std::min(std::abs(bodyYaw), 58.f)));
			SetField(L, "overlap", static_cast<double>(1.f - std::min(std::abs(bodyYaw), 58.f) / 58.f));
		}

		SetField(L, "lby_updating", false);
		SetField(L, "lby_next_update", 0.0);
		return 1;
	}

	// menu ---------------------------------------------------------------------------------------------------
	int N_ui_add(lua_State* L) {
		auto script{ ScriptOf(L) };
		const auto kind{ ArgString(L, 1) };

		auto item{ std::make_unique< LuaItem_t >() };
		item->m_iId = g_iNextItemId++;
		item->m_pScript = script;
		item->m_strName = SanitizeName(ArgString(L, 2));

		// arguments after ( kind, name, tab, container )
		constexpr int A{ 5 };

		if (kind == "checkbox")
			item->m_eKind = ITEM_CHECKBOX;
		else if (kind == "slider") {
			item->m_eKind = ITEM_SLIDER;
			item->m_iMin = ArgInt(L, A, 0);
			item->m_iMax = ArgInt(L, A + 1, 100);
			if (item->m_iMax <= item->m_iMin)
				item->m_iMax = item->m_iMin + 1;
			item->m_iValue = std::clamp(ArgInt(L, A + 2, item->m_iMin), item->m_iMin, item->m_iMax);
		}
		else if (kind == "combobox" || kind == "listbox") {
			item->m_eKind = kind == "combobox" ? ITEM_COMBOBOX : ITEM_LISTBOX;
			SetItemOptions(item.get(), ReadStringList(L, A));
		}
		else if (kind == "multiselect") {
			item->m_eKind = ITEM_MULTISELECT;
			SetItemOptions(item.get(), ReadStringList(L, A));
		}
		else if (kind == "hotkey") {
			item->m_eKind = ITEM_HOTKEY;
			item->m_bInline = ArgBool(L, A);
			item->m_cKey.key = ArgInt(L, A + 1, 0);
			item->m_cKey.mode = EKeyMode::Hold;
		}
		else if (kind == "button")
			item->m_eKind = ITEM_BUTTON;
		else if (kind == "color") {
			item->m_eKind = ITEM_COLOR;
			item->m_bInline = true;
			item->m_colValue = Color(ArgByte(L, A), ArgByte(L, A + 1), ArgByte(L, A + 2), ArgByte(L, A + 3));
		}
		else if (kind == "textbox")
			item->m_eKind = ITEM_TEXTBOX;
		else if (kind == "label")
			item->m_eKind = ITEM_LABEL;
		else
			return 0;

		// inline items attach to the script's previous row
		for (auto it{ g_vecItems.rbegin() }; it != g_vecItems.rend(); ++it) {
			if ((*it)->m_pScript == script) {
				item->m_pPrevious = it->get();
				break;
			}
		}
		if (!item->m_pPrevious)
			item->m_bInline = false;

		SnapshotItem(item.get());
		const int id{ item->m_iId };
		g_vecItems.push_back(std::move(item));
		g_bItemsDirty = true;

		lua_pushinteger(L, id);
		return 1;
	}

	int N_ui_get(lua_State* L) {
		auto item{ FindItem(ArgInt(L, 1)) };
		if (!item)
			return 0;

		switch (item->m_eKind) {
		case ITEM_CHECKBOX:
			lua_pushboolean(L, item->m_bValue);
			return 1;
		case ITEM_SLIDER:
		case ITEM_LISTBOX:
			lua_pushinteger(L, item->m_iValue);
			return 1;
		case ITEM_COMBOBOX:
			lua_pushstring(L, item->m_vecOptions.empty() ? "" : item->m_vecOptions.at(std::clamp(item->m_iValue, 0, static_cast<int>(item->m_vecOptions.size()) - 1)).c_str());
			return 1;
		case ITEM_MULTISELECT: {
			lua_newtable(L);
			int count{};
			for (std::size_t i{}; i < item->m_vecOptions.size() && item->m_pMulti; ++i) {
				if (item->m_pMulti[i]) {
					lua_pushstring(L, item->m_vecOptions[i].c_str());
					lua_rawseti(L, -2, ++count);
				}
			}
			return 1;
		}
		case ITEM_HOTKEY:
			lua_pushboolean(L, item->m_cKey.enabled);
			lua_pushinteger(L, ToGamesenseMode(item->m_cKey.mode));
			lua_pushinteger(L, item->m_cKey.key);
			return 3;
		case ITEM_COLOR:
			lua_pushinteger(L, item->m_colValue.Get<COLOR_R>());
			lua_pushinteger(L, item->m_colValue.Get<COLOR_G>());
			lua_pushinteger(L, item->m_colValue.Get<COLOR_B>());
			lua_pushinteger(L, item->m_colValue.Get<COLOR_A>());
			return 4;
		case ITEM_TEXTBOX:
			lua_pushstring(L, item->m_strText.c_str());
			return 1;
		case ITEM_LABEL:
			lua_pushstring(L, item->m_strName.c_str());
			return 1;
		default:
			return 0;
		}
	}

	int N_ui_set(lua_State* L) {
		auto item{ FindItem(ArgInt(L, 1)) };
		if (!item)
			return 0;

		switch (item->m_eKind) {
		case ITEM_CHECKBOX:
			if (!lua_isnoneornil(L, 2))
				item->m_bValue = ArgBool(L, 2);
			break;
		case ITEM_SLIDER:
			if (lua_isnumber(L, 2))
				item->m_iValue = std::clamp(ArgInt(L, 2), item->m_iMin, item->m_iMax);
			break;
		case ITEM_LISTBOX:
			if (lua_isnumber(L, 2))
				item->m_iValue = std::clamp(ArgInt(L, 2), 0, std::max(static_cast<int>(item->m_vecOptions.size()) - 1, 0));
			break;
		case ITEM_COMBOBOX:
			if (lua_type(L, 2) == LUA_TSTRING) {
				const auto value{ ArgString(L, 2) };
				for (std::size_t i{}; i < item->m_vecOptions.size(); ++i) {
					if (item->m_vecOptions[i] == value) {
						item->m_iValue = static_cast<int>(i);
						break;
					}
				}
			}
			else if (lua_isnumber(L, 2))
				item->m_iValue = std::clamp(ArgInt(L, 2), 0, std::max(static_cast<int>(item->m_vecOptions.size()) - 1, 0));
			break;
		case ITEM_MULTISELECT: {
			if (!item->m_pMulti)
				break;

			std::vector< std::string > selected{ ReadStringList(L, 2) };
			if (lua_type(L, 2) == LUA_TSTRING) {
				for (int i{ 2 }; i <= lua_gettop(L); ++i)
					selected.push_back(ArgString(L, i));
			}
			for (std::size_t i{}; i < item->m_vecOptions.size(); ++i)
				item->m_pMulti[i] = std::find(selected.begin(), selected.end(), item->m_vecOptions[i]) != selected.end();
		} break;
		case ITEM_HOTKEY: {
			if (lua_type(L, 2) == LUA_TSTRING) {
				const int mode{ ModeFromName(ArgString(L, 2)) };
				if (mode >= 0)
					item->m_cKey.mode = FromGamesenseMode(mode);
			}
			if (lua_isnumber(L, 3))
				item->m_cKey.key = ArgInt(L, 3);
		} break;
		case ITEM_COLOR:
			item->m_colValue = Color(ArgByte(L, 2), ArgByte(L, 3), ArgByte(L, 4), ArgByte(L, 5));
			break;
		case ITEM_TEXTBOX:
			if (!lua_isnoneornil(L, 2))
				item->m_strText = ArgString(L, 2);
			break;
		case ITEM_LABEL:
			if (lua_isstring(L, 2)) {
				const auto name{ SanitizeName(ArgString(L, 2)) };
				if (name != item->m_strName) {
					item->m_strName = name;
					g_bItemsDirty = true; // the menu row holds a pointer to the old text
				}
			}
			break;
		default:
			break;
		}

		// script made the change itself, the runtime already fires its callbacks
		SnapshotItem(item);
		return 0;
	}

	int N_ui_visible(lua_State* L) {
		if (auto item{ FindItem(ArgInt(L, 1)) })
			item->m_bVisible = ArgBool(L, 2);
		return 0;
	}

	int N_ui_items(lua_State* L) {
		auto item{ FindItem(ArgInt(L, 1)) };
		if (item && (item->m_eKind == ITEM_COMBOBOX || item->m_eKind == ITEM_LISTBOX || item->m_eKind == ITEM_MULTISELECT))
			SetItemOptions(item, ReadStringList(L, 2));
		return 0;
	}

	// player list ---------------------------------------------------------------------------------------------
	int N_plist_set(lua_State* L) {
		const int index{ ArgInt(L, 1) };
		if (index <= 0 || index > 64)
			return 0;

		const auto field{ ArgString(L, 2) };
		auto& player{ g_arrPlayers[index] };

		if (field == "Force body yaw")
			player.m_bForceBodyYaw = ArgBool(L, 3);
		else if (field == "Force body yaw value")
			player.m_flBodyYaw = std::clamp(ArgFloat(L, 3), -60.f, 60.f);
		else if (field == "Correction active")
			player.m_bCorrection = ArgBool(L, 3);
		else if (field == "Add to whitelist")
			player.m_bWhitelist = ArgBool(L, 3);
		else if (field == "Override prefer body aim") {
			const auto value{ ArgString(L, 3) };
			player.m_iPreferBody = value == "On" || value == "Force" ? 1 : value == "Off" ? -1 : 0;
		}

		return 0;
	}

	// events -------------------------------------------------------------------------------------------------
	int N_listen(lua_State* L) {
		const auto name{ ArgString(L, 1) };
		if (!name.empty())
			g_cEventListener.Listen(name);
		return 0;
	}

	// files / clipboard ------------------------------------------------------------------------------------
	int N_file_read(lua_State* L) {
		std::string data{};
		if (!ReadFileBytes(ResolveGamePath(ArgString(L, 1)), data))
			return 0;
		lua_pushlstring(L, data.data(), data.size());
		return 1;
	}

	int N_file_write(lua_State* L) {
		lua_pushboolean(L, WriteFileBytes(ResolveGamePath(ArgString(L, 1)), ArgString(L, 2)));
		return 1;
	}

	int N_db_read(lua_State* L) {
		std::string data{};
		if (!ReadFileBytes(LuaDirectory() / "database.json", data))
			return 0;
		lua_pushlstring(L, data.data(), data.size());
		return 1;
	}

	int N_db_write(lua_State* L) {
		WriteFileBytes(LuaDirectory() / "database.json", ArgString(L, 1));
		return 0;
	}

	int N_clipboard_get(lua_State* L) {
		std::string text{};
		if (OpenClipboard(nullptr)) {
			if (auto handle{ GetClipboardData(CF_UNICODETEXT) }) {
				if (auto data{ static_cast<const wchar_t*>(GlobalLock(handle)) }) {
					text = Narrow(data);
					GlobalUnlock(handle);
				}
			}
			CloseClipboard();
		}

		lua_pushlstring(L, text.data(), text.size());
		return 1;
	}

	int N_clipboard_set(lua_State* L) {
		const auto text{ Widen(ArgString(L, 1)) };
		if (!OpenClipboard(nullptr))
			return 0;

		EmptyClipboard();
		const auto bytes{ (text.size() + 1) * sizeof(wchar_t) };
		if (auto handle{ GlobalAlloc(GMEM_MOVEABLE, bytes) }) {
			if (auto data{ GlobalLock(handle) }) {
				std::memcpy(data, text.c_str(), bytes);
				GlobalUnlock(handle);
				if (!SetClipboardData(CF_UNICODETEXT, handle))
					GlobalFree(handle);
			}
			else
				GlobalFree(handle);
		}

		CloseClipboard();
		return 0;
	}

	// memory -------------------------------------------------------------------------------------------------
	int N_find_signature(lua_State* L) {
		const auto module{ ArgString(L, 1) }, pattern{ ArgString(L, 2) };
		if (module.empty() || pattern.empty())
			return 0;

		std::uintptr_t address{};
		try {
			address = MEM::FindPattern(module, pattern);

			// the cheat detours some game functions ( minhook writes a 5 byte jmp over the prologue ):
			// look again with the first 5 bytes as wildcards and accept it only if a jmp sits there
			if (!address) {
				std::istringstream tokens{ pattern };
				std::vector< std::string > bytes{ std::istream_iterator< std::string >(tokens), std::istream_iterator< std::string >() };
				if (bytes.size() >= 10) {
					std::string relaxed{ "? ? ? ? ?" };
					for (std::size_t i{ 5 }; i < bytes.size(); ++i)
						relaxed += " " + bytes[i];

					const auto candidate{ MEM::FindPattern(module, relaxed) };
					if (candidate && *reinterpret_cast<const std::uint8_t*>(candidate) == 0xE9)
						address = candidate;
				}
			}
		}
		catch (...) {
			address = 0;
		}

		if (!address)
			return 0;
		lua_pushlightuserdata(L, reinterpret_cast<void*>(address));
		return 1;
	}

	int N_create_interface(lua_State* L) {
		const auto module{ ArgString(L, 1) }, name{ ArgString(L, 2) };
		auto base{ MEM::GetModuleBaseHandle(module) };
		if (!base)
			return 0;

		auto create{ reinterpret_cast<void* (__cdecl*)(const char*, int*)>(MEM::GetExportAddress(base, "CreateInterface")) };
		if (!create)
			return 0;

		auto instance{ create(name.c_str(), nullptr) };
		if (!instance)
			return 0;
		lua_pushlightuserdata(L, instance);
		return 1;
	}

	// materials ----------------------------------------------------------------------------------------------
	IMaterial* ArgMaterial(lua_State* L, int i) {
		return lua_islightuserdata(L, i) ? static_cast<IMaterial*>(lua_touserdata(L, i)) : nullptr;
	}

	int N_mat_find(lua_State* L) {
		const auto path{ ArgString(L, 1) };
		auto material{ Interfaces::MaterialSystem->FindMaterial(path.c_str(), nullptr, false) };
		if (!material || material->IsErrorMaterial())
			return 0;
		lua_pushlightuserdata(L, material);
		return 1;
	}

	int N_mat_find_all(lua_State* L) {
		const auto partial{ ArgString(L, 1) };
		lua_newtable(L);

		int count{};
		for (auto handle{ Interfaces::MaterialSystem->FirstMaterial() }; handle != Interfaces::MaterialSystem->InvalidMaterial(); handle = Interfaces::MaterialSystem->NextMaterial(handle)) {
			auto material{ Interfaces::MaterialSystem->GetMaterial(handle) };
			if (!material || material->IsErrorMaterial())
				continue;

			const char* name{ material->GetName() };
			if (!name || std::strstr(name, partial.c_str()) == nullptr)
				continue;

			lua_pushlightuserdata(L, material);
			lua_rawseti(L, -2, ++count);
		}

		return 1;
	}

	int N_mat_name(lua_State* L) {
		auto material{ ArgMaterial(L, 1) };
		if (!material)
			return 0;
		lua_pushstring(L, material->GetName());
		return 1;
	}

	int N_mat_color(lua_State* L) {
		if (auto material{ ArgMaterial(L, 1) })
			material->ColorModulate(ArgFloat(L, 2, 1.f), ArgFloat(L, 3, 1.f), ArgFloat(L, 4, 1.f));
		return 0;
	}

	int N_mat_alpha(lua_State* L) {
		if (auto material{ ArgMaterial(L, 1) })
			material->AlphaModulate(ArgFloat(L, 2, 1.f));
		return 0;
	}

	int N_mat_flag(lua_State* L) {
		if (auto material{ ArgMaterial(L, 1) })
			material->SetMaterialVarFlag(static_cast<EMaterialVarFlags>(1 << std::clamp(ArgInt(L, 2), 0, 31)), ArgBool(L, 3));
		return 0;
	}

	int N_mat_get_flag(lua_State* L) {
		auto material{ ArgMaterial(L, 1) };
		lua_pushboolean(L, material && material->GetMaterialVarFlag(static_cast<EMaterialVarFlags>(1 << std::clamp(ArgInt(L, 2), 0, 31))));
		return 1;
	}

	int N_mat_reload(lua_State* L) {
		if (auto material{ ArgMaterial(L, 1) })
			material->Refresh();
		return 0;
	}

	int N_mat_set_param(lua_State* L) {
		auto material{ ArgMaterial(L, 1) };
		if (!material)
			return 0;

		const auto name{ ArgString(L, 2) };
		bool found{};
		auto variable{ material->FindVar(name.c_str(), &found, false) };
		if (!variable || !found)
			return 0;

		if (lua_isnumber(L, 3))
			variable->SetFloat(ArgFloat(L, 3));
		else if (lua_isboolean(L, 3))
			variable->SetInt(ArgBool(L, 3) ? 1 : 0);
		else if (lua_isstring(L, 3))
			variable->SetString(ArgString(L, 3).c_str());
		return 0;
	}

	int N_mat_get_param(lua_State* L) {
		return 0;
	}

	int N_tex_find(lua_State* L) {
		const auto path{ ArgString(L, 1) };
		auto texture{ Interfaces::MaterialSystem->FindTexture(path.c_str(), nullptr, false) };
		if (!texture)
			return 0;
		lua_pushlightuserdata(L, texture);
		return 1;
	}

	// misc ---------------------------------------------------------------------------------------------------
	const char* WeaponTypeName(int type) {
		switch (type) {
		case WEAPONTYPE_KNIFE: return "knife";
		case WEAPONTYPE_PISTOL: return "pistol";
		case WEAPONTYPE_SUBMACHINEGUN: return "smg";
		case WEAPONTYPE_RIFLE: return "rifle";
		case WEAPONTYPE_SHOTGUN: return "shotgun";
		case WEAPONTYPE_SNIPER: return "sniperrifle";
		case WEAPONTYPE_MACHINEGUN: return "machinegun";
		case WEAPONTYPE_C4: return "c4";
		case WEAPONTYPE_TASER: return "taser";
		case WEAPONTYPE_GRENADE: return "grenade";
		case WEAPONTYPE_HEALTHSHOT: return "healthshot";
		default: return "unknown";
		}
	}

	int N_weapon_info(lua_State* L) {
		const int index{ ArgInt(L, 1) };
		if (index <= 0 || index > 0xFFFF || !Interfaces::WeaponSystem)
			return 0;

		auto data{ Interfaces::WeaponSystem->GetWeaponData(static_cast<short>(index)) };
		if (!data || !data->szWeaponName)
			return 0;

		std::string console{ data->szWeaponName };
		std::string pretty{ console.rfind("weapon_", 0) == 0 ? console.substr(7) : console };
		std::transform(pretty.begin(), pretty.end(), pretty.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

		lua_newtable(L);
		SetField(L, "idx", static_cast<double>(index));
		SetField(L, "console_name", console.c_str());
		SetField(L, "name", pretty.c_str());
		SetField(L, "type", WeaponTypeName(data->nWeaponType));
		SetField(L, "weapon_type_int", static_cast<double>(data->nWeaponType));
		SetField(L, "max_player_speed", static_cast<double>(data->flMaxSpeed));
		SetField(L, "max_player_speed_alt", static_cast<double>(data->flMaxSpeedAlt));
		SetField(L, "damage", static_cast<double>(data->iDamage));
		SetField(L, "armor_ratio", static_cast<double>(data->flArmorRatio));
		SetField(L, "penetration", static_cast<double>(data->flPenetration));
		SetField(L, "range", static_cast<double>(data->flRange));
		SetField(L, "range_modifier", static_cast<double>(data->flRangeModifier));
		SetField(L, "headshot_multiplier", static_cast<double>(data->flHeadShotMultiplier));
		SetField(L, "bullets", static_cast<double>(data->iBullets));
		SetField(L, "cycletime", static_cast<double>(data->flCycleTime));
		SetField(L, "cycletime_alt", static_cast<double>(data->flCycleTimeAlt));
		SetField(L, "is_full_auto", data->bFullAuto);
		SetField(L, "is_melee_weapon", data->bIsMeleeWeapon);
		SetField(L, "has_silencer", data->bHasSilencer);
		SetField(L, "primary_clip_size", static_cast<double>(data->iMaxClip1));
		SetField(L, "primary_reserve_ammo_max", static_cast<double>(data->iPrimaryMaxReserveAmmo));
		SetField(L, "price", static_cast<double>(data->iWeaponPrice));
		SetField(L, "kill_award", static_cast<double>(data->iKillAward));
		SetField(L, "weight", static_cast<double>(data->flWeaponWeight));
		SetField(L, "spread", static_cast<double>(data->flSpread[0]));
		SetField(L, "inaccuracy_crouch", static_cast<double>(data->flInaccuracyCrouch[0]));
		SetField(L, "inaccuracy_stand", static_cast<double>(data->flInaccuracyStand[0]));
		SetField(L, "inaccuracy_jump", static_cast<double>(data->flInaccuracyJump[0]));
		SetField(L, "inaccuracy_land", static_cast<double>(data->flInaccuracyLand[0]));
		SetField(L, "inaccuracy_move", static_cast<double>(data->flInaccuracyMove[0]));
		SetField(L, "inaccuracy_fire", static_cast<double>(data->flInaccuracyFire[0]));
		return 1;
	}

	int N_clan_tag(lua_State* L) {
		static std::string tag{};
		tag = ArgString(L, 1);
		if (Displacement::Sigs.ClanTag)
			reinterpret_cast<int(__fastcall*)(const char*, const char*)>(Displacement::Sigs.ClanTag)(tag.c_str(), tag.c_str());
		return 0;
	}

	int N_steam_name(lua_State* L) {
		if (auto local{ LocalPlayer() }) {
			const auto info{ Interfaces::Engine->GetPlayerInfo(local->Index()) };
			if (info.has_value()) {
				lua_pushstring(L, info->szName);
				return 1;
			}
		}

		auto name{ Interfaces::ConVar->FindVar("name") };
		lua_pushstring(L, name && name->GetString() ? name->GetString() : "unknown");
		return 1;
	}

	int N_steam_id64(lua_State* L) {
		std::uint64_t id{};
		if (Interfaces::SteamUser) {
			// ISteamUser::GetSteamID returns the CSteamID through a hidden pointer
			MEM::CallVFunc<std::uint64_t*>(Interfaces::SteamUser, 2, &id);
		}

		if (!id) {
			if (auto local{ LocalPlayer() }) {
				const auto info{ Interfaces::Engine->GetPlayerInfo(local->Index()) };
				if (info.has_value())
					id = info->ullXuid;
			}
		}

		lua_pushstring(L, std::to_string(id).c_str());
		return 1;
	}

	int N_username(lua_State* L) {
		char buffer[256]{};
		DWORD size{ sizeof(buffer) };
		if (!GetUserNameA(buffer, &size))
			strcpy_s(buffer, "user");
		lua_pushstring(L, buffer);
		return 1;
	}

	int N_reload_scripts(lua_State* L) {
		QueueAction(ACTION_RELOAD_ALL);
		return 0;
	}

	int N_script_name(lua_State* L) {
		auto script{ ScriptOf(L) };
		lua_pushstring(L, script ? script->m_strName.c_str() : "");
		return 1;
	}

	const luaL_Reg g_arrNatives[]{
		{ "log", N_log },{ "exec", N_exec },
		{ "realtime", N_realtime },{ "curtime", N_curtime },{ "frametime", N_frametime },{ "absframetime", N_absframetime },
		{ "maxplayers", N_maxplayers },{ "tickcount", N_tickcount },{ "tickinterval", N_tickinterval },{ "framecount", N_framecount },
		{ "mapname", N_mapname },{ "lastoutgoing", N_lastoutgoing },{ "oldcommandack", N_oldcommandack },{ "commandack", N_commandack },
		{ "choked", N_choked },{ "timestamp", N_timestamp },{ "system_time", N_system_time },{ "unix_time", N_unix_time },{ "latency", N_latency },
		{ "screen_size", N_screen_size },{ "key_state", N_key_state },{ "mouse_pos", N_mouse_pos },{ "menu_open", N_menu_open },
		{ "menu_pos", N_menu_pos },{ "menu_size", N_menu_size },{ "random_int", N_random_int },{ "random_float", N_random_float },
		{ "local_player", N_local_player },{ "get_all", N_get_all },{ "get_players", N_get_players },{ "classname", N_classname },
		{ "is_enemy", N_is_enemy },{ "is_alive", N_is_alive },{ "is_dormant", N_is_dormant },{ "player_name", N_player_name },
		{ "player_weapon", N_player_weapon },{ "hitbox_position", N_hitbox_position },{ "steam64", N_steam64 },
		{ "bounding_box", N_bounding_box },{ "origin", N_origin },{ "esp_data", N_esp_data },{ "get_prop", N_get_prop },
		{ "set_prop", N_set_prop },{ "game_rules", N_game_rules },{ "player_resource", N_player_resource },
		{ "userid_to_entindex", N_userid_to_entindex },{ "model_name", N_model_name },{ "client_entity", N_client_entity },
		{ "anim_state", N_anim_state },{ "anim_layer", N_anim_layer },
		{ "trace_line", N_trace_line },{ "trace_bullet", N_trace_bullet },{ "scale_damage", N_scale_damage },{ "visible", N_visible },
		{ "eye_position", N_eye_position },{ "camera_angles", N_camera_angles },{ "camera_position", N_camera_position },
		{ "current_threat", N_current_threat },{ "resolver_info", N_resolver_info },{ "rage_debug", N_rage_debug },
		{ "font", N_font },{ "text", N_text },{ "text_size", N_text_size },{ "rect", N_rect },{ "line", N_line },
		{ "gradient", N_gradient },{ "poly", N_poly },{ "w2s", N_w2s },{ "texture_rgba", N_texture_rgba },
		{ "texture_image", N_texture_image },{ "texture_svg", N_texture_svg },{ "draw_texture", N_draw_texture },
		{ "debug_text", N_debug_text },{ "draw_hitboxes", N_draw_hitboxes },
		{ "cvar_find", N_cvar_find },{ "cvar_get", N_cvar_get },{ "cvar_set_string", N_cvar_set_string },
		{ "cvar_set_float", N_cvar_set_float },{ "cvar_set_int", N_cvar_set_int },{ "cvar_set_raw_float", N_cvar_set_raw_float },
		{ "cvar_set_raw_int", N_cvar_set_raw_int },
		{ "cfg_get", N_cfg_get },{ "cfg_set", N_cfg_set },{ "weapon_group", N_weapon_group },{ "gs_aa", N_gs_aa },{ "aa_info", N_aa_info },
		{ "ui_add", N_ui_add },{ "ui_get", N_ui_get },{ "ui_set", N_ui_set },{ "ui_visible", N_ui_visible },{ "ui_items", N_ui_items },
		{ "listen", N_listen },{ "plist_set", N_plist_set },
		{ "file_read", N_file_read },{ "file_write", N_file_write },{ "db_read", N_db_read },{ "db_write", N_db_write },
		{ "clipboard_get", N_clipboard_get },{ "clipboard_set", N_clipboard_set },
		{ "find_signature", N_find_signature },{ "create_interface", N_create_interface },
		{ "mat_find", N_mat_find },{ "mat_find_all", N_mat_find_all },{ "mat_name", N_mat_name },{ "mat_color", N_mat_color },
		{ "mat_alpha", N_mat_alpha },{ "mat_flag", N_mat_flag },{ "mat_get_flag", N_mat_get_flag },{ "mat_reload", N_mat_reload },
		{ "mat_set_param", N_mat_set_param },{ "mat_get_param", N_mat_get_param },{ "tex_find", N_tex_find },
		{ "weapon_info", N_weapon_info },{ "clan_tag", N_clan_tag },{ "steam_name", N_steam_name },{ "steam_id64", N_steam_id64 },
		{ "username", N_username },{ "reload_scripts", N_reload_scripts },{ "script_name", N_script_name },
		{ nullptr, nullptr }
	};

	/* ------------------------------------------------------------------------------------------------ */
	/* calling into scripts                                                                              */
	/* ------------------------------------------------------------------------------------------------ */

	int Traceback(lua_State* L) {
		const char* message{ lua_tostring(L, 1) };
		luaL_traceback(L, L, message ? message : "( error object is not a string )", 1);
		return 1;
	}

	// pushes R[ name ] and runs it with the arguments pushArguments leaves on the stack
	bool CallRuntime(Script_t* script, const char* function, const std::function< int(lua_State*) >& pushArguments, int results = 0) {
		auto L{ script->L };
		if (!L || script->m_iRuntimeRef == LUA_NOREF)
			return false;

		const int top{ lua_gettop(L) };
		lua_pushcfunction(L, Traceback);
		const int handler{ lua_gettop(L) };

		lua_rawgeti(L, LUA_REGISTRYINDEX, script->m_iRuntimeRef);
		lua_getfield(L, -1, function);
		lua_remove(L, -2);
		if (!lua_isfunction(L, -1)) {
			lua_settop(L, top);
			return false;
		}

		const int arguments{ pushArguments ? pushArguments(L) : 0 };
		++g_iCallbackDepth;
		const int status{ lua_pcall(L, arguments, results, handler) };
		--g_iCallbackDepth;
		if (status != 0) {
			ReportError(script, function, lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown error");
			lua_settop(L, top);
			return false;
		}

		lua_remove(L, handler);
		if (!results)
			lua_settop(L, top);
		return true;
	}

	void DispatchTo(Script_t* script, const char* name, const std::function< void(lua_State*) >& pushArgument) {
		CallRuntime(script, "dispatch", [&](lua_State* L) {
			lua_pushstring(L, name);
			if (pushArgument)
				pushArgument(L);
			else
				lua_pushnil(L);
			return 2;
			});
	}

	void DispatchToAll(const char* name, const std::function< void(lua_State*) >& pushArgument) {
		// a callback may queue an unload, but nothing is removed until the queue runs in paint
		for (std::size_t i{}; i < g_vecScripts.size(); ++i)
			DispatchTo(g_vecScripts[i].get(), name, pushArgument);
	}

	/* ------------------------------------------------------------------------------------------------ */
	/* loading                                                                                           */
	/* ------------------------------------------------------------------------------------------------ */

	/* auto-load list */

	std::filesystem::path AutoloadPath() {
		return LuaDirectory() / "autoload.txt";
	}

	void ReadAutoload() {
		g_vecAutoload.clear();

		std::string text{};
		if (!ReadFileBytes(AutoloadPath(), text))
			return;

		std::istringstream stream{ text };
		std::string line{};
		while (std::getline(stream, line)) {
			// trim whitespace / carriage returns, allow "name.lua" as well as "name"
			const auto first{ line.find_first_not_of(" \t\r\n") };
			if (first == std::string::npos)
				continue;
			line = line.substr(first, line.find_last_not_of(" \t\r\n") - first + 1);
			if (line.size() > 4 && line.compare(line.size() - 4, 4, ".lua") == 0)
				line.erase(line.size() - 4);
			if (!line.empty() && line[0] != '#' && std::find(g_vecAutoload.begin(), g_vecAutoload.end(), line) == g_vecAutoload.end())
				g_vecAutoload.push_back(line);
		}
	}

	void WriteAutoload() {
		std::string text{ "# scripts CorgiHak loads on inject, one per line\n" };
		for (const auto& name : g_vecAutoload)
			text += name + "\n";
		WriteFileBytes(AutoloadPath(), text);
	}

	void ToggleAutoload(const std::string& name) {
		if (name.empty())
			return;

		const auto it{ std::find(g_vecAutoload.begin(), g_vecAutoload.end(), name) };
		const bool enabled{ it == g_vecAutoload.end() };
		if (enabled)
			g_vecAutoload.push_back(name);
		else
			g_vecAutoload.erase(it);

		WriteAutoload();
		g_bManagerDirty = true;
		Features::Logger.Log(name + ".lua " + (enabled ? "will load on inject" : "no longer loads on inject"), true);
	}

	/* script item values in configs */

	// registration order + kind + display name: stable for a given script version, unique even when
	// gamesense style names hide everything after a newline
	std::string ConfigKey(const LuaItem_t* item, int ordinal) {
		return std::to_string(ordinal) + "|" + std::to_string(static_cast<int>(item->m_eKind)) + "|" + item->m_strName;
	}

	nlohmann::json ItemConfigValue(const LuaItem_t* item) {
		switch (item->m_eKind) {
		case ITEM_CHECKBOX: return item->m_bValue;
		case ITEM_SLIDER:
		case ITEM_COMBOBOX:
		case ITEM_LISTBOX: return item->m_iValue;
		case ITEM_TEXTBOX: return item->m_strText;
		case ITEM_COLOR: {
			auto color{ item->m_colValue };
			return nlohmann::json::array({ color.Get< COLOR_R >(), color.Get< COLOR_G >(), color.Get< COLOR_B >(), color.Get< COLOR_A >() });
		}
		case ITEM_HOTKEY: return nlohmann::json::array({ item->m_cKey.key, item->m_cKey.mode });
		case ITEM_MULTISELECT: {
			auto values{ nlohmann::json::array() };
			for (std::size_t i{}; i < item->m_vecOptions.size() && item->m_pMulti; ++i)
				values.push_back(item->m_pMulti[i]);
			return values;
		}
		default: return nullptr;
		}
	}

	void ApplyConfigValue(LuaItem_t* item, const nlohmann::json& value) {
		switch (item->m_eKind) {
		case ITEM_CHECKBOX:
			if (value.is_boolean())
				item->m_bValue = value.get< bool >();
			break;
		case ITEM_SLIDER:
			if (value.is_number())
				item->m_iValue = std::clamp(value.get< int >(), std::min(item->m_iMin, item->m_iMax), std::max(item->m_iMin, item->m_iMax));
			break;
		case ITEM_COMBOBOX:
		case ITEM_LISTBOX:
			if (value.is_number() && !item->m_vecOptions.empty())
				item->m_iValue = std::clamp(value.get< int >(), 0, static_cast<int>(item->m_vecOptions.size()) - 1);
			break;
		case ITEM_TEXTBOX:
			if (value.is_string())
				item->m_strText = value.get< std::string >();
			break;
		case ITEM_COLOR:
			if (value.is_array() && value.size() == 4)
				item->m_colValue = Color(value[0].get< int >(), value[1].get< int >(), value[2].get< int >(), value[3].get< int >());
			break;
		case ITEM_HOTKEY:
			if (value.is_array() && value.size() == 2) {
				item->m_cKey.key = value[0].get< int >();
				item->m_cKey.mode = value[1].get< int >();
			}
			break;
		case ITEM_MULTISELECT:
			if (value.is_array() && item->m_pMulti) {
				for (std::size_t i{}; i < item->m_vecOptions.size() && i < value.size(); ++i) {
					if (value[i].is_boolean())
						item->m_pMulti[i] = value[i].get< bool >();
				}
			}
			break;
		default:
			break;
		}
	}

	nlohmann::json ExportScriptItems(const Script_t* script) {
		auto out{ nlohmann::json::object() };
		int ordinal{};
		for (const auto& owned : g_vecItems) {
			const auto item{ owned.get() };
			if (item->m_pScript != script)
				continue;

			auto value{ ItemConfigValue(item) };
			if (!value.is_null())
				out[ConfigKey(item, ordinal)] = std::move(value);
			++ordinal;
		}
		return out;
	}

	// changed values reach the script through the normal ui callbacks ( FireChangedItems )
	void ApplyConfigItems(Script_t* script) {
		const auto found{ g_jsonConfigItems.find(script->m_strName) };
		if (found == g_jsonConfigItems.end() || !found->is_object())
			return;

		int ordinal{};
		for (const auto& owned : g_vecItems) {
			const auto item{ owned.get() };
			if (item->m_pScript != script)
				continue;

			const auto value{ found->find(ConfigKey(item, ordinal)) };
			if (value != found->end()) {
				try {
					ApplyConfigValue(item, *value);
				}
				catch (const nlohmann::detail::exception&) {
					// a hand edited / foreign value: keep the script's own
				}
			}
			++ordinal;
		}
	}

	void RefreshFiles() {
		g_vecFiles.clear();

		std::error_code error;
		const auto directory{ LuaDirectory() };
		std::filesystem::create_directories(directory, error);

		for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
			if (!entry.is_regular_file() || entry.path().extension() != ".lua")
				continue;
			g_vecFiles.push_back(Narrow(entry.path().stem().wstring()));
		}

		std::sort(g_vecFiles.begin(), g_vecFiles.end());
		if (g_vecFiles != g_vecLastFiles) {
			g_vecLastFiles = g_vecFiles;
			g_bManagerDirty = true;
		}
	}

	Script_t* FindScript(const std::string& name) {
		for (auto& script : g_vecScripts) {
			if (script->m_strName == name)
				return script.get();
		}
		return nullptr;
	}

	bool RunChunk(Script_t* script, const char* data, std::size_t size, const char* chunkName, int arguments, int results) {
		auto L{ script->L };

		// stack: [ args... ] -> load chunk below the arguments
		if (luaL_loadbuffer(L, data, size, chunkName) != 0) {
			ReportError(script, "load", lua_tostring(L, -1) ? lua_tostring(L, -1) : "could not load");
			lua_pop(L, 1 + arguments);
			return false;
		}
		lua_insert(L, -(arguments + 1));

		lua_pushcfunction(L, Traceback);
		lua_insert(L, -(arguments + 2));
		const int handler{ lua_gettop(L) - arguments - 1 };

		++g_iCallbackDepth;
		const int status{ lua_pcall(L, arguments, results, handler) };
		--g_iCallbackDepth;
		if (status != 0) {
			ReportError(script, "run", lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown error");
			lua_settop(L, handler - 1);
			return false;
		}

		lua_remove(L, handler);
		return true;
	}

	void CloseScript(Script_t* script, bool runShutdown) {
		if (script->L) {
			if (runShutdown)
				CallRuntime(script, "shutdown", nullptr);

			lua_close(script->L);
			script->L = nullptr;
		}

		RemoveScriptItems(script);
	}

	void UnloadScript(const std::string& name) {
		auto it{ std::find_if(g_vecScripts.begin(), g_vecScripts.end(), [&](const std::unique_ptr< Script_t >& script) { return script->m_strName == name; }) };
		if (it == g_vecScripts.end())
			return;

		CloseScript(it->get(), true);
		g_vecScripts.erase(it);
		g_arrPlayers = {};
		ReleaseAntiaimIfUnused();
		g_bManagerDirty = true;
		Features::Logger.Log("unloaded " + name + ".lua", true);
	}

	void LoadScript(const std::string& name) {
		if (name.empty())
			return;

		if (FindScript(name))
			UnloadScript(name);

		auto owned{ std::make_unique< Script_t >() };
		auto script{ owned.get() };
		script->m_strName = name;
		script->m_pathFile = LuaDirectory() / Widen(name + ".lua");

		std::string source{};
		if (!ReadFileBytes(script->m_pathFile, source)) {
			ReportError(script, "load", "could not read " + name + ".lua");
			return;
		}

		// strip a utf-8 byte order mark
		if (source.size() >= 3 && static_cast<unsigned char>(source[0]) == 0xEF && static_cast<unsigned char>(source[1]) == 0xBB && static_cast<unsigned char>(source[2]) == 0xBF)
			source.erase(0, 3);

		script->L = luaL_newstate();
		if (!script->L) {
			ReportError(script, "load", "out of memory");
			return;
		}

		auto L{ script->L };
		luaL_openlibs(L);

		// require( ) looks in the scripts folder ( and its gamesense / lib sub folders )
		{
			const auto directory{ Narrow(LuaDirectory().wstring()) };
			const std::string path{ directory + "\\?.lua;" + directory + "\\?\\init.lua;" + directory + "\\lib\\?.lua;" + directory + "\\gamesense\\?.lua" };
			lua_getglobal(L, "package");
			lua_pushstring(L, path.c_str());
			lua_setfield(L, -2, "path");
			lua_pop(L, 1);
		}

		// native table, every function knows its script through the upvalue
		lua_newtable(L);
		lua_pushlightuserdata(L, script);
		luaL_setfuncs(L, g_arrNatives, 1);
		const int natives{ lua_gettop(L) };

		// runtime: returns the R table
		lua_pushvalue(L, natives);
		lua_pushstring(L, name.c_str());
		if (!RunChunk(script, reinterpret_cast<const char*>(LuaEmbedded::Runtime), sizeof(LuaEmbedded::Runtime), "=corgi_runtime", 2, 1)) {
			CloseScript(script, false);
			return;
		}
		script->m_iRuntimeRef = luaL_ref(L, LUA_REGISTRYINDEX);

		// library modules
		lua_pushvalue(L, natives);
		if (!RunChunk(script, reinterpret_cast<const char*>(LuaEmbedded::Libraries), sizeof(LuaEmbedded::Libraries), "=corgi_libraries", 1, 0)) {
			CloseScript(script, false);
			return;
		}
		lua_settop(L, 0);

		// the script can already register items / callbacks, so it has to be in the list before it runs
		g_vecScripts.push_back(std::move(owned));
		g_bManagerDirty = true;

		const std::string chunk{ "@" + name + ".lua" };
		if (!RunChunk(script, source.data(), source.size(), chunk.c_str(), 0, 0)) {
			UnloadScript(name);
			return;
		}

		// values from the active config win over the script's own saved ones
		ApplyConfigItems(script);

		Features::Logger.Log("loaded " + name + ".lua", true);
	}

	void ProcessActions() {
		while (!g_dqActions.empty()) {
			const auto action{ g_dqActions.front() };
			g_dqActions.pop_front();

			switch (action.m_eAction) {
			case ACTION_LOAD:
				LoadScript(action.m_strName);
				break;
			case ACTION_UNLOAD:
				UnloadScript(action.m_strName);
				break;
			case ACTION_RELOAD:
				UnloadScript(action.m_strName);
				LoadScript(action.m_strName);
				break;
			case ACTION_RELOAD_ALL: {
				std::vector< std::string > names{};
				for (const auto& script : g_vecScripts)
					names.push_back(script->m_strName);
				for (const auto& name : names)
					UnloadScript(name);
				for (const auto& name : names)
					LoadScript(name);
			} break;
			case ACTION_UNLOAD_ALL:
				while (!g_vecScripts.empty())
					UnloadScript(g_vecScripts.back()->m_strName);
				break;
			case ACTION_REFRESH:
				RefreshFiles();
				break;
			case ACTION_TOGGLE_AUTOLOAD:
				ToggleAutoload(action.m_strName);
				break;
			case ACTION_OPEN_FOLDER: {
				std::error_code error;
				std::filesystem::create_directories(LuaDirectory(), error);
				ShellExecuteW(nullptr, L"open", LuaDirectory().wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
			} break;
			}
		}
	}

	/* ------------------------------------------------------------------------------------------------ */
	/* usercmd tables                                                                                    */
	/* ------------------------------------------------------------------------------------------------ */

	const std::pair< const char*, int > g_arrButtons[]{
		{ "in_attack", IN_ATTACK },{ "in_jump", IN_JUMP },{ "in_duck", IN_DUCK },{ "in_forward", IN_FORWARD },
		{ "in_back", IN_BACK },{ "in_use", IN_USE },{ "in_cancel", IN_CANCEL },{ "in_left", IN_LEFT },
		{ "in_right", IN_RIGHT },{ "in_moveleft", IN_MOVELEFT },{ "in_moveright", IN_MOVERIGHT },{ "in_attack2", IN_ATTACK2 },
		{ "in_run", IN_RUN },{ "in_reload", IN_RELOAD },{ "in_alt1", IN_LEFT_ALT },{ "in_alt2", IN_RIGHT_ALT },
		{ "in_score", IN_SCORE },{ "in_speed", IN_SPEED },{ "in_walk", IN_WALK },{ "in_zoom", IN_ZOOM },
		{ "in_weapon1", IN_FIRST_WEAPON },{ "in_weapon2", IN_SECOND_WEAPON },{ "in_bullrush", IN_BULLRUSH },
		{ "in_grenade1", IN_FIRST_GRENADE },{ "in_grenade2", IN_SECOND_GRENADE },{ "in_attack3", IN_MIDDLE_ATTACK },
	};

	void PushCommand(lua_State* L, const CUserCmd& cmd) {
		lua_newtable(L);
		SetField(L, "command_number", static_cast<double>(cmd.iCommandNumber));
		SetField(L, "tick_count", static_cast<double>(cmd.iTickCount));
		SetField(L, "pitch", static_cast<double>(cmd.viewAngles.x));
		SetField(L, "yaw", static_cast<double>(cmd.viewAngles.y));
		SetField(L, "roll", static_cast<double>(cmd.viewAngles.z));
		SetField(L, "move_yaw", static_cast<double>(ctx.m_angOriginalViewangles.y));
		SetField(L, "forwardmove", static_cast<double>(cmd.flForwardMove));
		SetField(L, "sidemove", static_cast<double>(cmd.flSideMove));
		SetField(L, "upmove", static_cast<double>(cmd.flUpMove));
		SetField(L, "buttons", static_cast<double>(cmd.iButtons));
		SetField(L, "weaponselect", static_cast<double>(cmd.iWeaponSelect));
		SetField(L, "weaponsubtype", static_cast<double>(cmd.iWeaponSubType));
		SetField(L, "mousedx", static_cast<double>(cmd.sMouseDeltaX));
		SetField(L, "mousedy", static_cast<double>(cmd.sMouseDeltaY));
		SetField(L, "chokedcommands", static_cast<double>(Interfaces::ClientState ? Interfaces::ClientState->nChokedCommands : 0));

		// CorgiHak-native exploit state. Gamesense scripts normally have to infer this
		// from tickbase; exposing it here makes compatibility deterministic.
		const bool doubletapEnabled{
			Config::Get<bool>(Vars.ExploitsDoubletap)
			&& Config::Get<keybind_t>(Vars.ExploitsDoubletapKey).enabled
		};
		const int defensiveAge{ cmd.iCommandNumber - g_iForceDefensiveCmd };
		SetField(L, "exploit_shift", static_cast<double>(ctx.m_iTicksAllowed));
		SetField(L, "double_tap_enabled", doubletapEnabled);
		SetField(L, "double_tap_active", doubletapEnabled && ctx.m_iTicksAllowed > 0);
		SetField(L, "force_defensive_pending", defensiveAge >= 0 && defensiveAge <= 16);
		SetField(L, "force_defensive_age", static_cast<double>(defensiveAge >= 0 && defensiveAge <= 16 ? defensiveAge : -1));

		SetField(L, "allow_send_packet", true);
		SetField(L, "send_packet", true);
		SetField(L, "no_choke", false);
		SetField(L, "quick_stop", false);
		SetField(L, "force_defensive", false);
		SetField(L, "discharge_pending", false);

		for (const auto& [name, bit] : g_arrButtons)
			SetField(L, name, (cmd.iButtons & bit) != 0);
	}

	void ReadCommand(lua_State* L, int table, CUserCmd& cmd) {
		cmd.viewAngles.x = static_cast<float>(GetFieldNumber(L, table, "pitch", cmd.viewAngles.x));
		cmd.viewAngles.y = static_cast<float>(GetFieldNumber(L, table, "yaw", cmd.viewAngles.y));
		cmd.viewAngles.z = static_cast<float>(GetFieldNumber(L, table, "roll", cmd.viewAngles.z));
		cmd.flForwardMove = static_cast<float>(GetFieldNumber(L, table, "forwardmove", cmd.flForwardMove));
		cmd.flSideMove = static_cast<float>(GetFieldNumber(L, table, "sidemove", cmd.flSideMove));
		cmd.flUpMove = static_cast<float>(GetFieldNumber(L, table, "upmove", cmd.flUpMove));
		cmd.iWeaponSelect = static_cast<int>(GetFieldNumber(L, table, "weaponselect", cmd.iWeaponSelect));
		cmd.iWeaponSubType = static_cast<int>(GetFieldNumber(L, table, "weaponsubtype", cmd.iWeaponSubType));
		cmd.sMouseDeltaX = static_cast<short>(GetFieldNumber(L, table, "mousedx", cmd.sMouseDeltaX));
		cmd.sMouseDeltaY = static_cast<short>(GetFieldNumber(L, table, "mousedy", cmd.sMouseDeltaY));

		// whole button mask first, then individual in_* flags the script flipped
		const int originalButtons{ cmd.iButtons };
		int buttons{ static_cast<int>(GetFieldNumber(L, table, "buttons", originalButtons)) };
		for (const auto& [name, bit] : g_arrButtons) {
			const bool before{ (originalButtons & bit) != 0 };
			const bool after{ GetFieldBool(L, table, name, before) };
			if (after != before)
				buttons = after ? (buttons | bit) : (buttons & ~bit);
		}
		cmd.iButtons = buttons;

		const bool forceDefensive{ GetFieldFlag(L, table, "force_defensive") };
		const bool noChoke{ GetFieldFlag(L, table, "no_choke") };
		const bool allowSend{
			GetFieldBool(L, table, "allow_send_packet", true)
			&& GetFieldBool(L, table, "send_packet", true)
		};

		if (forceDefensive) {
			// Defensive is consumed by the exploit on a sent command. Keep the existing
			// 16-command latch, but make the request deterministic for Gamesense scripts:
			// the command that requests defensive is also forced to send.
			g_iForceDefensiveCmd = cmd.iCommandNumber;
			g_iSendPacketOverride = 1;
		}
		else if (noChoke)
			g_iSendPacketOverride = 1;
		else if (!allowSend)
			g_iSendPacketOverride = 0;
	}

	/* ------------------------------------------------------------------------------------------------ */
	/* per frame                                                                                         */
	/* ------------------------------------------------------------------------------------------------ */

	void UpdateItems() {
		for (auto& item : g_vecItems) {
			if (item->m_eKind == ITEM_HOTKEY) {
				// keys only count while the game has focus and no text box is being typed in
				if (item->m_cKey.key > 0 || item->m_cKey.mode == EKeyMode::AlwaysOn)
					item->m_cKey.Resolve();
				else
					item->m_cKey.enabled = item->m_cKey.mode == EKeyMode::Off;
			}
		}
	}

	void FireChangedItems() {
		for (std::size_t i{}; i < g_vecItems.size(); ++i) {
			auto item{ g_vecItems[i].get() };
			if (!ItemChanged(item))
				continue;

			SnapshotItem(item);
			const int id{ item->m_iId };
			CallRuntime(item->m_pScript, "ui_event", [id](lua_State* L) { lua_pushinteger(L, id); return 1; });
		}
	}
}

/* ---------------------------------------------------------------------------------------------------- */
/* public                                                                                                */
/* ---------------------------------------------------------------------------------------------------- */

void Lua::RegisterMenu(CMenuSubtab* subtab) {
	// add both groups first: the second AddGroup can reallocate the vector and move the first group
	subtab->AddGroup("Scripts", 1.f);
	subtab->AddGroup("Script items", 1.f);
	g_pManagerGroup = &subtab->m_vecGroups.at(subtab->m_vecGroups.size() - 2);
	g_pItemsGroup = &subtab->m_vecGroups.back();

	RefreshFiles();
	ReadAutoload();
	RebuildManagerGroup();
	RebuildItemsGroup();

	// auto-load: queued, so they load on the game thread in the first paint
	for (const auto& name : g_vecAutoload) {
		if (std::find(g_vecFiles.begin(), g_vecFiles.end(), name) != g_vecFiles.end())
			QueueAction(ACTION_LOAD, name);
	}
}

std::string Lua::AutoloadSummary() {
	std::string out{};
	for (const auto& name : g_vecAutoload) {
		if (std::find(g_vecFiles.begin(), g_vecFiles.end(), name) == g_vecFiles.end())
			continue;
		if (!out.empty())
			out += ", ";
		out += name + ".lua";
	}
	return out;
}

std::string Lua::ExportConfig() {
	// scripts that are not loaded keep whatever the config had for them
	auto out{ g_jsonConfigItems.is_object() ? g_jsonConfigItems : nlohmann::json::object() };
	for (const auto& script : g_vecScripts) {
		if (script->L)
			out[script->m_strName] = ExportScriptItems(script.get());
	}
	g_jsonConfigItems = out;
	return out.dump();
}

void Lua::ImportConfig(const std::string& text) {
	auto parsed{ nlohmann::json::parse(text, nullptr, false) };
	if (parsed.is_discarded() || !parsed.is_object())
		parsed = nlohmann::json::object();

	g_jsonConfigItems = std::move(parsed);
	for (const auto& script : g_vecScripts) {
		if (script->L)
			ApplyConfigItems(script.get());
	}
}

void Lua::Shutdown() {
	g_dqActions.clear();
	g_dqButtonPresses.clear();

	while (!g_vecScripts.empty()) {
		auto script{ g_vecScripts.back().get() };
		CloseScript(script, true);
		g_vecScripts.pop_back();
	}

	ReleaseAntiaimIfUnused();
	g_cEventListener.Destroy();

	if (g_pItemsGroup)
		FreeItemArgs(g_pItemsGroup->Items());
	if (g_pManagerGroup)
		FreeItemArgs(g_pManagerGroup->Items());
}

void Lua::OnPaint() {
	ProcessActions();

	// pick up scripts dropped into the folder while the menu is open
	if (Menu::m_bOpened && std::abs(Interfaces::Globals->flRealTime - g_flLastRefresh) > 2.f) {
		g_flLastRefresh = Interfaces::Globals->flRealTime;
		RefreshFiles();
	}

	if (g_bErrorsChanged) {
		g_bErrorsChanged = false;
		g_bManagerDirty = true;
	}

	if (g_bManagerDirty)
		RebuildManagerGroup();
	if (g_bItemsDirty)
		RebuildItemsGroup();

	if (g_vecScripts.empty())
		return;

	UpdateItems();

	const bool inGame{ Interfaces::Engine->IsInGame() };
	if (inGame && !g_bWasInGame) {
		g_mapPropCache.clear();
		DispatchToAll("level_init", nullptr);
	}
	g_bWasInGame = inGame;

	for (std::size_t i{}; i < g_vecScripts.size(); ++i)
		CallRuntime(g_vecScripts[i].get(), "tick", nullptr);

	g_bInPaint = true;
	if (inGame && LocalPlayer())
		DispatchToAll("paint", nullptr);
	DispatchToAll("paint_ui", nullptr);
	g_bInPaint = false;
}

void Lua::OnPaintPost() {
	while (!g_dqButtonPresses.empty()) {
		const int id{ g_dqButtonPresses.front() };
		g_dqButtonPresses.pop_front();

		if (auto item{ FindItem(id) })
			CallRuntime(item->m_pScript, "ui_event", [id](lua_State* L) { lua_pushinteger(L, id); return 1; });
	}

	if (!g_vecScripts.empty())
		FireChangedItems();

	// script loads / unloads requested from menu buttons this frame
	if (!g_dqActions.empty())
		ProcessActions();
}

void Lua::OnSetupCommand(CUserCmd& cmd) {
	g_iSendPacketOverride = -1;
	if (g_vecScripts.empty())
		return;

	for (std::size_t i{}; i < g_vecScripts.size(); ++i) {
		auto script{ g_vecScripts[i].get() };
		auto L{ script->L };
		if (!L)
			continue;

		// keep the table reachable after dispatch so the edits can be read back
		lua_settop(L, 0);
		PushCommand(L, cmd);
		const int table{ lua_gettop(L) };

		DispatchTo(script, "setup_command", [table](lua_State* state) { lua_pushvalue(state, table); });

		if (lua_istable(L, table))
			ReadCommand(L, table, cmd);
		lua_settop(L, 0);
	}

	ctx.m_angOriginalViewangles = cmd.viewAngles;
}

void Lua::OnPredictCommand(CUserCmd& cmd) {
	if (g_vecScripts.empty())
		return;

	DispatchToAll("predict_command", [&cmd](lua_State* L) {
		lua_newtable(L);
		SetField(L, "command_number", static_cast<double>(cmd.iCommandNumber));
		SetField(L, "tick_count", static_cast<double>(cmd.iTickCount));
		SetField(L, "chokedcommands", static_cast<double>(Interfaces::ClientState ? Interfaces::ClientState->nChokedCommands : 0));
		SetField(L, "exploit_shift", static_cast<double>(ctx.m_iTicksAllowed));
		const int defensiveAge{ cmd.iCommandNumber - g_iForceDefensiveCmd };
		SetField(L, "force_defensive_pending", defensiveAge >= 0 && defensiveAge <= 16);
		SetField(L, "force_defensive_age", static_cast<double>(defensiveAge >= 0 && defensiveAge <= 16 ? defensiveAge : -1));
		});
}

void Lua::OnRunCommand(CUserCmd& cmd) {
	if (g_vecScripts.empty())
		return;

	DispatchToAll("run_command", [&cmd](lua_State* L) {
		lua_newtable(L);
		SetField(L, "command_number", static_cast<double>(cmd.iCommandNumber));
		SetField(L, "tick_count", static_cast<double>(cmd.iTickCount));
		SetField(L, "chokedcommands", static_cast<double>(Interfaces::ClientState ? Interfaces::ClientState->nChokedCommands : 0));
		SetField(L, "exploit_shift", static_cast<double>(ctx.m_iTicksAllowed));
		const int defensiveAge{ cmd.iCommandNumber - g_iForceDefensiveCmd };
		SetField(L, "force_defensive_pending", defensiveAge >= 0 && defensiveAge <= 16);
		SetField(L, "force_defensive_age", static_cast<double>(defensiveAge >= 0 && defensiveAge <= 16 ? defensiveAge : -1));
		});
}

void Lua::OnFrameStage(int stage, bool post) {
	if (g_vecScripts.empty() || !Interfaces::Engine->IsInGame())
		return;

	// FRAME_NET_UPDATE_START = 1, FRAME_NET_UPDATE_END = 4, FRAME_RENDER_START = 5, FRAME_RENDER_END = 6
	if (!post) {
		if (stage == 1)
			DispatchToAll("net_update_start", nullptr);
		else if (stage == 5) {
			// anything pre_render writes into the local poses / layers is kept for the local model build
			auto local{ LocalPlayer() };
			const bool track{ local && !local->IsDead() && local->m_AnimationLayers() };

			std::array< float, POSE_COUNT > poses{};
			CAnimationLayer layers[LAYER_COUNT]{};
			if (track) {
				std::memcpy(poses.data(), local->m_flPoseParameter().data(), sizeof(float) * POSE_COUNT);
				std::memcpy(layers, local->m_AnimationLayers(), sizeof(layers));
			}

			DispatchToAll("pre_render", nullptr);

			if (track) {
				const float now{ Interfaces::Globals->flRealTime };
				for (int i{}; i < POSE_COUNT; ++i) {
					if (local->m_flPoseParameter()[i] != poses[i])
						g_arrPoseOverrides[i] = { local->m_flPoseParameter()[i], now };
				}
				for (int i{}; i < LAYER_COUNT; ++i) {
					if (local->m_AnimationLayers()[i].flWeight != layers[i].flWeight)
						g_arrLayerWeightOverrides[i] = { local->m_AnimationLayers()[i].flWeight, now };
					if (local->m_AnimationLayers()[i].flCycle != layers[i].flCycle)
						g_arrLayerCycleOverrides[i] = { local->m_AnimationLayers()[i].flCycle, now };
				}
			}
		}
	}
	else {
		if (stage == 4)
			DispatchToAll("net_update_end", nullptr);
		else if (stage == 6)
			DispatchToAll("post_render", nullptr);
	}
}

void Lua::OnOverrideView(const Vector& origin) {
	g_vecCameraOrigin = origin;
	g_bHasCamera = true;
}

void Lua::OnAimFire(Shot_t& shot, int target, int hitchance, int hitgroup, int damage, int backtrack, bool extrapolated, const Vector& point) {
	shot.m_iLuaId = g_iNextShotId++;
	shot.m_iLuaTick = Interfaces::Globals->iTickCount;
	shot.m_iLuaHitchance = hitchance;

	if (g_vecScripts.empty())
		return;

	const int id{ shot.m_iLuaId }, tick{ shot.m_iLuaTick };
	DispatchToAll("aim_fire", [&](lua_State* L) {
		lua_newtable(L);
		SetField(L, "id", static_cast<double>(id));
		SetField(L, "target", static_cast<double>(target));
		SetField(L, "hit_chance", static_cast<double>(hitchance));
		SetField(L, "hitgroup", static_cast<double>(hitgroup));
		SetField(L, "damage", static_cast<double>(damage));
		SetField(L, "backtrack", static_cast<double>(backtrack));
		SetField(L, "boosted", false);
		SetField(L, "high_priority", false);
		SetField(L, "interpolated", false);
		SetField(L, "extrapolated", extrapolated);
		SetField(L, "teleported", false);
		SetField(L, "tick", static_cast<double>(tick));
		// CorgiHak extras: the exact record and the resolver's choice for it
		if (shot.m_pRecord) {
			SetField(L, "record_tick", static_cast<double>(TIME_TO_TICKS(shot.m_pRecord->m_cAnimData.m_flSimulationTime)));
			SetField(L, "resolver_side", static_cast<double>(shot.m_iResolverSide));
			SetField(L, "resolver_body_yaw", static_cast<double>(shot.m_pRecord->m_bMultiMatrix ? SideBodyYaw(*shot.m_pRecord, shot.m_iResolverSide) : 0.f));
		}
		SetField(L, "x", static_cast<double>(point.x));
		SetField(L, "y", static_cast<double>(point.y));
		SetField(L, "z", static_cast<double>(point.z));
		});
}

void Lua::OnAimResult(Shot_t& shot, bool hit, const char* reason) {
	if (shot.m_bLuaReported || !shot.m_iLuaId)
		return;
	shot.m_bLuaReported = true;

	if (g_vecScripts.empty())
		return;

	const int target{ shot.m_pPlayer ? shot.m_pPlayer->Index() : 0 };
	DispatchToAll(hit ? "aim_hit" : "aim_miss", [&](lua_State* L) {
		lua_newtable(L);
		SetField(L, "id", static_cast<double>(shot.m_iLuaId));
		SetField(L, "target", static_cast<double>(target));
		SetField(L, "hit_chance", static_cast<double>(shot.m_iLuaHitchance));
		SetField(L, "hitgroup", static_cast<double>(hit ? shot.m_iServerHitgroup : shot.m_iHitgroup));
		SetField(L, "tick", static_cast<double>(shot.m_iLuaTick));
		if (hit)
			SetField(L, "damage", static_cast<double>(shot.m_iServerDamage));
		else
			SetField(L, "reason", reason ? reason : "?");
		});
}

void Lua::AppendEspFlags(CBasePlayer* player, std::vector< std::pair< std::string, Color > >& flags) {
	if (g_vecScripts.empty() || !player)
		return;

	const int index{ player->Index() };
	for (std::size_t i{}; i < g_vecScripts.size(); ++i) {
		auto script{ g_vecScripts[i].get() };
		if (!CallRuntime(script, "esp_flags", [index](lua_State* L) { lua_pushinteger(L, index); return 1; }, 1))
			continue;

		auto L{ script->L };
		if (lua_istable(L, -1)) {
			const int count{ static_cast<int>(lua_objlen(L, -1)) };
			for (int j{ 1 }; j + 3 <= count; j += 4) {
				lua_rawgeti(L, -1, j);
				lua_rawgeti(L, -2, j + 1);
				lua_rawgeti(L, -3, j + 2);
				lua_rawgeti(L, -4, j + 3);
				const std::string text{ ArgString(L, -4) };
				const Color color(ArgByte(L, -3), ArgByte(L, -2), ArgByte(L, -1));
				lua_pop(L, 4);
				flags.emplace_back(SanitizeName(text), color);
			}
		}
		lua_pop(L, 1);
	}
}

int Lua::SendPacketOverride() {
	return g_iSendPacketOverride;
}

bool Lua::ForceDefensive(int commandNumber) {
	if (g_vecScripts.empty())
		return false;

	const int age{ commandNumber - g_iForceDefensiveCmd };
	return age >= 0 && age <= 16;
}

void Lua::ConsumeDefensive() {
	g_iForceDefensiveCmd = -1000;
}

bool Lua::Active() {
	return !g_vecScripts.empty();
}

int Lua::ResolverSideOverride(int index) {
	if (index <= 0 || index > 64 || g_vecScripts.empty())
		return -1;

	const auto& player{ g_arrPlayers[index] };
	if (player.m_bForceBodyYaw) {
		// side 1 animates with eye yaw +120 ( body yaw positive ), side 2 with -120
		const float value{ player.m_flBodyYaw };
		if (value >= 30.f)
			return RESOLVER_SIDE_FIRST;
		if (value <= -30.f)
			return RESOLVER_SIDE_SECOND;
		if (value >= 8.f)
			return RESOLVER_SIDE_LOW_FIRST;
		if (value <= -8.f)
			return RESOLVER_SIDE_LOW_SECOND;
		return RESOLVER_SIDE_CENTER;
	}

	if (!player.m_bCorrection)
		return RESOLVER_SIDE_CENTER;

	return -1;
}

int Lua::PreferBodyOverride(int index) {
	if (index <= 0 || index > 64 || g_vecScripts.empty())
		return 0;
	return g_arrPlayers[index].m_iPreferBody;
}

bool Lua::Whitelisted(int index) {
	if (index <= 0 || index > 64 || g_vecScripts.empty())
		return false;
	return g_arrPlayers[index].m_bWhitelist;
}

void Lua::ApplyLocalAnimOverrides(CBasePlayer* local) {
	g_bAnimBackedUp = false;
	if (g_vecScripts.empty() || !local || !local->m_AnimationLayers())
		return;

	const float now{ Interfaces::Globals->flRealTime };
	const auto live{ [now](const ValueOverride_t& value) { return value.m_flTime >= 0.f && now - value.m_flTime <= OVERRIDE_LIFETIME && now >= value.m_flTime; } };

	bool any{};
	for (const auto& value : g_arrPoseOverrides)
		any |= live(value);
	for (int i{}; i < LAYER_COUNT; ++i)
		any |= live(g_arrLayerWeightOverrides[i]) || live(g_arrLayerCycleOverrides[i]);
	if (!any)
		return;

	std::memcpy(g_arrPoseBackup.data(), local->m_flPoseParameter().data(), sizeof(float) * POSE_COUNT);
	std::memcpy(g_arrLayerBackup, local->m_AnimationLayers(), sizeof(g_arrLayerBackup));
	g_bAnimBackedUp = true;

	for (int i{}; i < POSE_COUNT; ++i) {
		if (live(g_arrPoseOverrides[i]))
			local->m_flPoseParameter()[i] = g_arrPoseOverrides[i].m_flValue;
	}

	for (int i{}; i < LAYER_COUNT; ++i) {
		if (live(g_arrLayerWeightOverrides[i]))
			local->m_AnimationLayers()[i].flWeight = g_arrLayerWeightOverrides[i].m_flValue;
		if (live(g_arrLayerCycleOverrides[i]))
			local->m_AnimationLayers()[i].flCycle = g_arrLayerCycleOverrides[i].m_flValue;
	}
}

void Lua::RestoreLocalAnimOverrides(CBasePlayer* local) {
	if (!g_bAnimBackedUp || !local || !local->m_AnimationLayers())
		return;

	std::memcpy(local->m_flPoseParameter().data(), g_arrPoseBackup.data(), sizeof(float) * POSE_COUNT);
	std::memcpy(local->m_AnimationLayers(), g_arrLayerBackup, sizeof(g_arrLayerBackup));
	g_bAnimBackedUp = false;
}

bool Lua::InCallback() {
	return g_iCallbackDepth > 0;
}
