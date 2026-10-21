#pragma once
#include "../../sdk/entity.h"
#include "../../context.h"
#include "../../utils/math.h"
#include "../../core/config.h"
#include "../misc/misc.h"
#include "autowall.h"
#include <array>

// custom ( momentum builder ) player states, same order as the AA_STATE_IDS arrays
enum EAntiaimState : int {
	AA_STATE_DEFAULT = 0,
	AA_STATE_STAND,
	AA_STATE_MOVE,
	AA_STATE_SLOW,
	AA_STATE_DUCK,
	AA_STATE_AIR,
	AA_STATE_AIRDUCK,
	AA_STATE_FAKELAG,
	AA_STATE_DEFENSIVE,
	AA_STATE_COUNT
};

// gamesense "Anti-aimbot angles" values written by lua scripts ( anti-aim mode 2 )
struct LuaAntiaim_t {
	int m_iPitch{ 3 };          // Off, Default, Up, Down, Minimal, Random, Custom
	int m_iPitchValue{ 89 };
	int m_iYawBase{ 1 };        // Local view, At targets
	int m_iYaw{ 1 };            // Off, 180, Spin, Static, 180 Z, Crosshair
	int m_iYawValue{ };
	int m_iJitter{ };           // Off, Offset, Center, Random, Skitter
	int m_iJitterValue{ };
	int m_iBody{ 3 };           // Off, Opposite, Jitter, Static
	int m_iBodyValue{ };
	bool m_bFreestandBody{ };
	bool m_bEdge{ };
	bool m_bFreestanding{ };
	int m_iRoll{ };
	bool m_bFakeLag{ true };
	int m_iFakeLagAmount{ };    // Dynamic, Maximum, Fluctuate
};

class CAntiAim {
public:
	void Pitch( CUserCmd& cmd );
	void FakeLag( int cmdNum );
	void RunLocalModifications( CUserCmd& cmd, int tickbase );
	float BaseYaw( CUserCmd& cmd );
	bool Condition( CUserCmd& cmd, bool checkCmd = false );

	// classic or custom builder
	bool IsCustom( ) const;
	// angles driven by lua scripts ( gamesense references )
	bool IsLua( ) const;
	// yaw added to choked commands for the fake ( 120 = full side )
	float DesyncAmount( ) const;
	// whether choked commands should carry the desync offset this cycle
	bool DesyncActive( ) const;
	// short name of the detected movement state, for the crosshair indicators
	const char* StateName( ) const;

	int ManualSide{ };
	bool m_bFlickNow{ };
	bool Invert{ };

	// detected state ( before falling back to Default ) and the state whose settings are in use
	int m_iRawState{ AA_STATE_STAND };
	int m_iCustomState{ AA_STATE_DEFAULT };
	bool m_bOnUse{ };

	LuaAntiaim_t m_cLua{ };

private:
	bool ChokeCycleJitter{ };
	bool m_bAntiBackstab{ };
	int Freestanding( );
	int RawFreestanding( );
	void PickYaw( float& yaw );
	void AtTarget( float& yaw );
	//bool AutoDirection( float& yaw );

	bool m_bInvertFlick{ };
	int m_iFlickTimer{ };

	/* custom builder */
	bool DefensiveTick( ) const;
	bool OnUseAllowed( CUserCmd& cmd ) const;
	int DetectRawState( CUserCmd& cmd ) const;
	int ResolveState( int raw ) const;
	void AdvanceCustomCycle( CUserCmd& cmd );
	float CustomBaseYaw( CUserCmd& cmd );
	bool CustomPitch( CUserCmd& cmd );
	float DefensivePitchValue( int option, float fallback ) const;
	float ThreatPitch( ) const;

	template < typename T >
	T& StateVar( const std::array< std::uint32_t, AA_STATE_COUNT >& ids ) const {
		return Config::Get<T>( ids[ std::clamp( m_iCustomState, 0, AA_STATE_COUNT - 1 ) ] );
	}

	bool m_bCustomDesync{ true };
	float m_flCustomOffset{ };
	bool m_bJitterSide{ };
	bool m_bDelaySide{ };
	int m_iDelayCounter{ };
	int m_iDelayTarget{ 1 };
	int m_iWayIndex{ };
	int m_iSkitterIndex{ };
	int m_iBodyCounter{ };
	int m_iDefensiveSpin{ };
	int m_iDefensiveIndex{ };
	bool m_bDefensiveSide{ };

	/* lua ( gamesense ) mode */
	void AdvanceLuaCycle( CUserCmd& cmd );
	float LuaBaseYaw( CUserCmd& cmd );
	void LuaPitch( CUserCmd& cmd );

	bool m_bLuaDesync{ true };
	float m_flLuaDesync{ 120.f };
	float m_flLuaOffset{ };
	float m_flLuaSpin{ };
	bool m_bLuaJitterSide{ };
	bool m_bLuaFluctuate{ };
	int m_iLuaSkitter{ };
};

namespace Features { inline CAntiAim Antiaim; };
