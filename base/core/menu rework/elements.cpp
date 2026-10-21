#include "menu.h"
#include "../../features/visuals/visuals.h"
#include "../../features/misc/logger.h"
#include "../../features/misc/skinchanger/kitparser.h"
#include "abuse.h"
#include "../lua/lua_engine.h"

int selectedConfig{ };
std::string typedConfig{ };
int weapGroup{ };
int aaStateSel{ };

// anti-aim menu visibility helpers
static bool AAClassic( ) {
	return Config::Get<int>( Vars.AntiaimMode ) == 0;
}

static bool AACustom( ) {
	return Config::Get<int>( Vars.AntiaimMode ) == 1;
}

static bool AALua( ) {
	return Config::Get<int>( Vars.AntiaimMode ) == 2;
}

static bool AAStatePage( int state ) {
	return AACustom( ) && aaStateSel == state;
}

static bool AAStateOn( int state ) {
	static const auto enableIds{ AA_STATE_IDS( AACEnable ) };
	return AAStatePage( state ) && ( state == 0 || Config::Get<bool>( enableIds[ state ] ) );
}

void __cdecl LoadConfig( ) {
	const auto fig{ Config::vecFileNames.empty( ) ? "" : Config::vecFileNames.at( selectedConfig ) };
	Config::Load( fig );
	Features::Logger.Log( _( "Loaded config " ) + fig, true );
}

void __cdecl SaveConfig( ) {
	const auto fig{ Config::vecFileNames.empty( ) ? "" : Config::vecFileNames.at( selectedConfig ) };
	Config::Save( fig );
	Features::Logger.Log( _( "Saved config " ) + fig, true );
}

void __cdecl RemoveConfig( ) {
	const auto fig{ Config::vecFileNames.empty( ) ? "" : Config::vecFileNames.at( selectedConfig ) };
	Config::Remove( selectedConfig );
	Features::Logger.Log( _( "Removed config " ) + fig, true );
	Config::m_bDueRefresh = true;
}

void __cdecl CreateConfig( ) {
	Config::Save( typedConfig );
	Features::Logger.Log( _( "Created config " ) + typedConfig, true );
	Config::m_bDueRefresh = true;
}

void __cdecl RefreshConfig( ) {
	Config::m_bDueRefresh = true;
}

void __cdecl ResetMenuSize( ) {
	Menu::m_vecSize = { MENU_DEFAULT_WIDTH, MENU_DEFAULT_HEIGHT };
	Features::Logger.Log( _( "Reset menu size." ), true );
}

void __cdecl Unload( ) {
	ctx.m_bUnload = true;
}

void Menu::Register( ) {
	// labels shown next to the vector icons in the sidebar ( MenuDraw::TabIcon, same order )
	m_cTabs[ 0 ].m_szName = "Rage";
	m_cTabs[ 1 ].m_szName = "Players";
	m_cTabs[ 2 ].m_szName = "Visuals";
	m_cTabs[ 3 ].m_szName = "Misc";
	m_cTabs[ 4 ].m_szName = "Config";

	auto aimbot{ &m_cTabs[ 0 ].m_vecSubtabs.emplace_back( "Aimbot", 2 ) };
	{
		auto generalGroup{ aimbot->AddGroup( "General", 1.f ) };
		{
			generalGroup->Register( _( "Enable" ), &Config::Get<bool>( Vars.RagebotEnable ) );
			generalGroup->Register( _( "Resolver" ), &Config::Get<bool>( Vars.RagebotResolver ) );
			generalGroup->Register( _( "Lag-compensation" ), &Config::Get<bool>( Vars.RagebotLagcompensation ) );
			generalGroup->Register( _( "Backtrack" ), &Config::Get<bool>( Vars.RagebotBacktrack ) );
			generalGroup->Register( _( "Extrapolate lag-comp breakers" ), &Config::Get<bool>( Vars.RagebotExtrapolateBreakers ), SHOW( Config::Get<bool>( Vars.RagebotLagcompensation ) ) );
			generalGroup->Register( _( "Extrapolation max ticks" ), &Config::Get<int>( Vars.RagebotExtrapMaxTicks ), 4, 32, SHOW( Config::Get<bool>( Vars.RagebotLagcompensation ) && Config::Get<bool>( Vars.RagebotExtrapolateBreakers ) ) );
			generalGroup->Register( _( "Extrapolation max spread" ), &Config::Get<int>( Vars.RagebotExtrapMaxSpread ), 5, 64, SHOW( Config::Get<bool>( Vars.RagebotLagcompensation ) && Config::Get<bool>( Vars.RagebotExtrapolateBreakers ) ) );
			generalGroup->Register( _( "Debug overlay" ), &Config::Get<bool>( Vars.RagebotDebugOverlay ) );
			generalGroup->Register( _( "Head safety" ), &Config::Get<bool>( Vars.RagebotHeadSafety ) );
			generalGroup->Register( _( "Record safety margin ( ticks )" ), &Config::Get<int>( Vars.RagebotRecordMargin ), 0, 7, SHOW( Config::Get<bool>( Vars.RagebotLagcompensation ) ) );

			// defensive anti-aim detection: raise these if players get flagged that don't use it
			generalGroup->Register( _( "Defensive detection" ), &Config::Get<bool>( Vars.RagebotDefDetect ) );
			generalGroup->Register( _( "Defensive min shift ( ticks )" ), &Config::Get<int>( Vars.RagebotDefMinShift ), 1, 14, SHOW( Config::Get<bool>( Vars.RagebotDefDetect ) ) );
			generalGroup->Register( _( "Defensive max shift ( ticks )" ), &Config::Get<int>( Vars.RagebotDefMaxShift ), 8, 32, SHOW( Config::Get<bool>( Vars.RagebotDefDetect ) ) );
			generalGroup->Register( _( "Shifts to confirm" ), &Config::Get<int>( Vars.RagebotDefConfirmShifts ), 1, 10, SHOW( Config::Get<bool>( Vars.RagebotDefDetect ) ) );
			generalGroup->Register( _( "Confirm window ( seconds )" ), &Config::Get<int>( Vars.RagebotDefConfirmTime ), 1, 10, SHOW( Config::Get<bool>( Vars.RagebotDefDetect ) ) );
			generalGroup->Register( _( "Only shifts with defensive angles" ), &Config::Get<bool>( Vars.RagebotDefNeedAngles ), SHOW( Config::Get<bool>( Vars.RagebotDefDetect ) ) );
			generalGroup->Register( _( "Defensive angles confirm instantly" ), &Config::Get<bool>( Vars.RagebotDefAngleConfirm ), SHOW( Config::Get<bool>( Vars.RagebotDefDetect ) ) );
			generalGroup->Register( _( "Forward push confirms instantly" ), &Config::Get<bool>( Vars.RagebotDefPushConfirm ), SHOW( Config::Get<bool>( Vars.RagebotDefDetect ) ) );
			generalGroup->Register( _( "Defensive pitch jump" ), &Config::Get<int>( Vars.RagebotDefPitchJump ), 10, 180, SHOW( Config::Get<bool>( Vars.RagebotDefDetect ) ) );
			generalGroup->Register( _( "Defensive yaw jump" ), &Config::Get<int>( Vars.RagebotDefYawJump ), 30, 180, SHOW( Config::Get<bool>( Vars.RagebotDefDetect ) ) );
			generalGroup->Register( _( "Own network tolerance ( % , 100 = off )" ), &Config::Get<int>( Vars.RagebotDefNetTolerance ), 0, 100, SHOW( Config::Get<bool>( Vars.RagebotDefDetect ) ) );
			generalGroup->Register( _( "Filter same-tick shifts" ), &Config::Get<bool>( Vars.RagebotDefFilterSimultaneous ), SHOW( Config::Get<bool>( Vars.RagebotDefDetect ) ) );
			generalGroup->Register( _( "Defensive debug ( esp )" ), &Config::Get<bool>( Vars.RagebotDefDebug ), SHOW( Config::Get<bool>( Vars.RagebotDefDetect ) ) );

			// fake flick filter: never shoot a head that was only there for one record
			generalGroup->Register( _( "Hold fire on new yaw ( anti flick )" ), &Config::Get<bool>( Vars.RagebotFlickHoldNew ) );
			generalGroup->Register( _( "Flick yaw jump" ), &Config::Get<int>( Vars.RagebotFlickJump ), 20, 180 );
			generalGroup->Register( _( "Flick return tolerance" ), &Config::Get<int>( Vars.RagebotFlickAgree ), 5, 90 );
			generalGroup->Register( _( "Weapon group" ), &weapGroup, 9, weaponGroups );

			RAGEINTSLIDER( generalGroup, _( "FOV" ), RagebotFOV, weapGroup, 1, 180 );
			RAGECHECKBOX( generalGroup, _( "Auto fire" ), RagebotAutoFire, weapGroup );
			RAGECHECKBOX( generalGroup, _( "Auto scope" ), RagebotAutoScope, weapGroup );
			RAGECHECKBOX( generalGroup, _( "Silent aim" ), RagebotSilentAim, weapGroup );
			RAGEINTSLIDER( generalGroup, _( "Hitchance" ), RagebotHitchance, weapGroup, 0, 100 );
			RAGEINTSLIDER( generalGroup, _( "Noscope hitchance" ), RagebotNoscopeHitchance, weapGroup, 0, 100 );
			RAGECHECKBOX( generalGroup, _( "Ensure accuracy" ), RagebotHitchanceThorough, weapGroup );
			RAGEINTSLIDER( generalGroup, _( "Minimum damage" ), RagebotMinimumDamage, weapGroup, 0, 110 );
			RAGECHECKBOX( generalGroup, _( "Autowall" ), RagebotAutowall, weapGroup );

			RAGEINTSLIDER_SHOWCONDITION( generalGroup, _( "Penetration damage" ), RagebotPenetrationDamage, weapGroup, 0, 110, RagebotAutowall );

			RAGECHECKBOX( generalGroup, _( "Scale damage" ), RagebotScaleDamage, weapGroup );
			generalGroup->Register( _( "Damage override" ), &Config::Get<bool>( Vars.RagebotDamageOverride ) );
			generalGroup->Register( &Config::Get<keybind_t>( Vars.RagebotDamageOverrideKey ) );

			RAGEINTSLIDER( generalGroup, _( "Override damage" ), RagebotOverrideDamage, weapGroup, 0, 110 );
			RAGECHECKBOX( generalGroup, _( "Autostop" ), RagebotAutoStop, weapGroup );
			RAGECHECKBOX( generalGroup, _( "Move between shots" ), RagebotBetweenShots, weapGroup );
			//RAGECHECKBOX( generalGroup, _( "Stop in air" ), RagebotAutostopInAir, weapGroup );
			generalGroup->Register( _( "Zeusbot" ), &Config::Get<bool>( Vars.RagebotZeusbot ) );
			generalGroup->Register( _( "Knifebot" ), &Config::Get<bool>( Vars.RagebotKnifebot ) );
		}

		auto selectionGroup{ aimbot->AddGroup( "Selection", 0.6f ) };
		{
			selectionGroup->Register( _( "Target selection" ), &Config::Get<int>( Vars.RagebotTargetSelection ), 4, targetSelection );

			RAGEMULTICOMBO( selectionGroup, _( "Hitboxes" ), 9, hitboxes, weapGroup );
			RAGEMULTICOMBO( selectionGroup, _( "Multipoints" ), 9, multipoints, weapGroup );

			RAGECHECKBOX( selectionGroup, _( "Static pointscale" ), RagebotStaticPointscale, weapGroup );

			RAGEINTSLIDER_SHOWCONDITION( selectionGroup, _( "Head scale" ), RagebotHeadScale, weapGroup, 0, 100, RagebotStaticPointscale );
			RAGEINTSLIDER_SHOWCONDITION( selectionGroup, _( "Body scale" ), RagebotBodyScale, weapGroup, 0, 100, RagebotStaticPointscale );

			RAGECHECKBOX( selectionGroup, _( "Ignore limbs when moving" ), RagebotIgnoreLimbs, weapGroup );

			RAGECHECKBOX( selectionGroup, _( "Force baim after missed shots" ), RagebotForceBaimAfterX, weapGroup );
			RAGEINTSLIDER_SHOWCONDITION( selectionGroup, _( "Shot count" ), RagebotForceBaimAfterXINT, weapGroup, 1, 20, RagebotForceBaimAfterX );

			RAGEMULTICOMBO( selectionGroup, "Prefer baim", 3, preferBaim, weapGroup );
			RAGEMULTICOMBO( selectionGroup, "Prefer safe point", 4, preferSafe, weapGroup );

			selectionGroup->Register( _( "Force baim" ) );
			selectionGroup->Register( &Config::Get<keybind_t>( Vars.RagebotForceBaimKey ) );

			selectionGroup->Register( _( "Force safe point" ) );
			selectionGroup->Register( &Config::Get<keybind_t>( Vars.RagebotForceSafePointKey ) );

			selectionGroup->Register( _( "Force yaw safety" ) );
			selectionGroup->Register( &Config::Get<keybind_t>( Vars.RagebotForceYawSafetyKey ) );
		}

		auto exploitsGroup{ aimbot->AddGroup( "Exploits", 0.4f ) };
		{
			exploitsGroup->Register( _( "Doubletap" ), &Config::Get<bool>( Vars.ExploitsDoubletap ) );
			exploitsGroup->Register( &Config::Get<keybind_t>( Vars.ExploitsDoubletapKey ) );

			exploitsGroup->Register( _( "Lag peek" ), &Config::Get<bool>( Vars.ExploitsDoubletapDefensive ), SHOW( Config::Get<bool>( Vars.ExploitsDoubletap ) ) );
			exploitsGroup->Register( _( "Delay teleport" ), &Config::Get<bool>( Vars.ExploitsDoubletapExtended ), SHOW( Config::Get<bool>( Vars.ExploitsDoubletap ) && Config::Get<bool>( Vars.ExploitsDoubletapDefensive ) ) );
			exploitsGroup->Register( _( "Break animations" ), &Config::Get<bool>( Vars.ExploitsDefensiveBreakAnimations ), SHOW( Config::Get<bool>( Vars.ExploitsDoubletap ) && Config::Get<bool>( Vars.ExploitsDoubletapDefensive ) ) );
			exploitsGroup->Register( _( "Continuous in air" ), &Config::Get<bool>( Vars.ExploitsDefensiveInAir ), SHOW( Config::Get<bool>( Vars.ExploitsDoubletap ) && Config::Get<bool>( Vars.ExploitsDoubletapDefensive ) ) );

			//exploitsGroup->Register( _( "No fakelag on safety" ), &Config::Get<bool>( Vars.AntiaimSafeNoFakelag ), SHOW( Config::Get<bool>( Vars.ExploitsDoubletap ) && Config::Get<bool>( Vars.ExploitsDoubletapDefensive ) ) );
			exploitsGroup->Register( _( "Safe pitch" ), &Config::Get<int>( Vars.AntiaimSafePitch ), 5, safePitch, SHOW( Config::Get<bool>( Vars.ExploitsDoubletap ) && Config::Get<bool>( Vars.ExploitsDoubletapDefensive ) ) );
			exploitsGroup->Register( _( "Safe yaw randomisation" ), &Config::Get<int>( Vars.AntiaimSafeYawRandomisation ), 0, 100, SHOW( Config::Get<bool>( Vars.ExploitsDoubletap ) && Config::Get<bool>( Vars.ExploitsDoubletapDefensive ) ) );

			exploitsGroup->Register( _( "Hideshots" ), &Config::Get<bool>( Vars.ExploitsHideshots ) );
			//exploitsGroup->Register( _( "Break CBB" ), &Config::Get<bool>( Vars.ExploitsBreakCBB ) );
			exploitsGroup->Register( &Config::Get<keybind_t>( Vars.ExploitsHideshotsKey ) );
		}
	}

	auto antiaim{ &m_cTabs[ 0 ].m_vecSubtabs.emplace_back( "Anti-aim", 2 ) };
	{
		static ComboElement_t aaModes[ 3 ]{ _( "Classic" ), _( "Custom" ), _( "Lua" ) };
		static ComboElement_t aaStates[ 9 ]{ _( "Default" ), _( "Standing" ), _( "Moving" ), _( "Slow walk" ), _( "Ducking" ), _( "In air" ), _( "Air duck" ), _( "Fake lag" ), _( "Defensive" ) };
		static ComboElement_t aaPitch[ 4 ]{ _( "Disabled" ), _( "Down" ), _( "Up" ), _( "Random" ) };
		static ComboElement_t aaYawModes[ 7 ]{ _( "Default" ), _( "3 way" ), _( "Extrapolated" ), _( "Extrapolated skitter" ), _( "Synced body" ), _( "Synced random" ), _( "Delayed jitter" ) };
		static ComboElement_t aaBody[ 4 ]{ _( "Static" ), _( "Jitter" ), _( "Opposite" ), _( "Off" ) };
		static ComboElement_t aaBodySide[ 2 ]{ _( "Left" ), _( "Right" ) };
		static ComboElement_t aaDefYaw[ 10 ]{ _( "None" ), _( "Forward" ), _( "Spin" ), _( "Sideways" ), _( "Jitter 90" ), _( "Opposite" ), _( "Opposite jitter" ), _( "3-way" ), _( "5-way" ), _( "Random" ) };
		static ComboElement_t aaDefPitch[ 7 ]{ _( "None" ), _( "Zero" ), _( "Up" ), _( "Down" ), _( "Random" ), _( "Semi-up" ), _( "Semi-down" ) };
		static ComboElement_t aaHeightPitch[ 7 ]{ _( "Defensive pitch" ), _( "Zero" ), _( "Up" ), _( "Down" ), _( "Random" ), _( "Semi-up" ), _( "Semi-down" ) };

		const auto generalGroup{ antiaim->AddGroup( _( "General" ), 1.f ) };
		{
			/* shared */
			generalGroup->Register( _( "Enable" ), &Config::Get<bool>( Vars.AntiaimEnable ) );
			generalGroup->Register( _( "Anti-aim mode" ), &Config::Get<int>( Vars.AntiaimMode ), 3, aaModes );
			generalGroup->Register( _( "Angles come from loaded Lua scripts" ), true, SHOW( AALua( ) ) );

			/* classic */
			generalGroup->Register( _( "Pitch" ), &Config::Get<int>( Vars.AntiaimPitch ), 5, pitch, SHOW( AAClassic( ) ) );
			generalGroup->Register( _( "Yaw" ), &Config::Get<int>( Vars.AntiaimYaw ), 4, yaw, SHOW( AAClassic( ) ) );
			generalGroup->Register( _( "Yaw add" ), &Config::Get<int>( Vars.AntiaimYawAdd ), 5, yawAdd, SHOW( AAClassic( ) ) );
			generalGroup->Register( _( "Yaw range" ), &Config::Get<int>( Vars.AntiaimYawRange ), 2, 180, SHOW( AAClassic( ) && Config::Get<int>( Vars.AntiaimYawAdd ) && Config::Get<int>( Vars.AntiaimYawAdd ) != 3 ) );
			generalGroup->Register( _( "Yaw speed" ), &Config::Get<int>( Vars.AntiaimYawSpeed ), 1, 60, SHOW( AAClassic( ) && ( Config::Get<int>( Vars.AntiaimYawAdd ) == 2 || Config::Get<int>( Vars.AntiaimYawAdd ) == 3 ) ) );

			generalGroup->Register( _( "Flick head" ), &Config::Get<bool>( Vars.AntiaimFlickHead ), SHOW( AAClassic( ) ) );
			generalGroup->Register( _( "Invert flick side" ), true, SHOW( AAClassic( ) && Config::Get<bool>( Vars.AntiaimFlickHead ) ) );
			generalGroup->Register( &Config::Get<keybind_t>( Vars.AntiaimFlickInvert ), SHOW( AAClassic( ) && Config::Get<bool>( Vars.AntiaimFlickHead ) ) );
			generalGroup->Register( _( "Constant flick invert" ), &Config::Get<bool>( Vars.AntiaimConstantInvertFlick ), SHOW( AAClassic( ) && Config::Get<bool>( Vars.AntiaimFlickHead ) ) );
			generalGroup->Register( _( "Flick amount" ), &Config::Get<int>( Vars.AntiaimFlickAdd ), 0, 180, SHOW( AAClassic( ) && Config::Get<bool>( Vars.AntiaimFlickHead ) ) );
			generalGroup->Register( _( "Flick speed" ), &Config::Get<int>( Vars.AntiaimFlickSpeed ), 2, 40, SHOW( AAClassic( ) && Config::Get<bool>( Vars.AntiaimFlickHead ) ) );

			generalGroup->Register( _( "At targets" ), &Config::Get<int>( Vars.AntiaimAtTargets ), 3, atTargets );

			generalGroup->Register( _( "Desync" ), &Config::Get<bool>( Vars.AntiaimDesync ), SHOW( AAClassic( ) ) );
			generalGroup->Register( _( "Avoid overlap" ), &Config::Get<bool>( Vars.AntiaimTrickLBY ), SHOW( AAClassic( ) ) );
			generalGroup->Register( _( "Constant desync invert" ), &Config::Get<bool>( Vars.AntiaimConstantInvert ), SHOW( AAClassic( ) && Config::Get<bool>( Vars.AntiaimDesync ) && !Config::Get<bool>( Vars.AntiaimTrickLBY ) ) );
			generalGroup->Register( _( "Flip desync" ), true, SHOW( AAClassic( ) && Config::Get<bool>( Vars.AntiaimDesync ) && !Config::Get<bool>( Vars.AntiaimConstantInvert ) ) );
			generalGroup->Register( &Config::Get<keybind_t>( Vars.AntiaimInvert ), SHOW( AAClassic( ) && Config::Get<bool>( Vars.AntiaimDesync ) && !Config::Get<bool>( Vars.AntiaimConstantInvert ) ) );

			generalGroup->Register( _( "Yaw add" ), &Config::Get<int>( Vars.AntiaimNormalYawAdd ), -180, 180, SHOW( AAClassic( ) ) );
			generalGroup->Register( _( "Inverted yaw add" ), &Config::Get<int>( Vars.AntiaimInvertedYawAdd ), -180, 180, SHOW( AAClassic( ) ) );

			generalGroup->Register( _( "Freestanding type" ), &Config::Get<int>( Vars.AntiaimFreestanding ), 4, freestandingType, SHOW( AAClassic( ) ) );
			generalGroup->Register( _( "Freestanding key" ), true, SHOW( AAClassic( ) && Config::Get<int>( Vars.AntiaimFreestanding ) ) );
			generalGroup->Register( &Config::Get<keybind_t>( Vars.AntiaimFreestandingKey ), SHOW( AAClassic( ) && Config::Get<int>( Vars.AntiaimFreestanding ) ) );

			/* custom ( momentum builder ) */
			generalGroup->Register( _( "Freestanding" ), true, SHOW( AACustom( ) ) );
			generalGroup->Register( &Config::Get<keybind_t>( Vars.AntiaimFreestandingKey ), SHOW( AACustom( ) ) );

			generalGroup->Register( _( "Player state" ), &aaStateSel, 9, aaStates, SHOW( AACustom( ) ) );

			for ( int i{ }; i < 9; ++i ) {
				static const auto enableIds{ AA_STATE_IDS( AACEnable ) };
				static const auto pitchIds{ AA_STATE_IDS( AACPitch ) };
				static const auto yawModeIds{ AA_STATE_IDS( AACYawMode ) };
				static const auto centerIds{ AA_STATE_IDS( AACYawCenter ) };
				static const auto leftIds{ AA_STATE_IDS( AACYawLeft ) };
				static const auto rightIds{ AA_STATE_IDS( AACYawRight ) };
				static const auto way1Ids{ AA_STATE_IDS( AACWay1 ) };
				static const auto way2Ids{ AA_STATE_IDS( AACWay2 ) };
				static const auto way3Ids{ AA_STATE_IDS( AACWay3 ) };
				static const auto minLeftIds{ AA_STATE_IDS( AACRandMinLeft ) };
				static const auto maxLeftIds{ AA_STATE_IDS( AACRandMaxLeft ) };
				static const auto minRightIds{ AA_STATE_IDS( AACRandMinRight ) };
				static const auto maxRightIds{ AA_STATE_IDS( AACRandMaxRight ) };
				static const auto delayIds{ AA_STATE_IDS( AACJitterDelay ) };
				static const auto randomIds{ AA_STATE_IDS( AACJitterRandom ) };
				static const auto bodyIds{ AA_STATE_IDS( AACBody ) };
				static const auto sideIds{ AA_STATE_IDS( AACBodySide ) };
				static const auto speedIds{ AA_STATE_IDS( AACBodySpeed ) };
				static const auto noFsIds{ AA_STATE_IDS( AACNoFreestand ) };

				const auto page{ [ i ]( ) { return AAStatePage( i ); } };
				const auto on{ [ i ]( ) { return AAStateOn( i ); } };
				const auto yawMode{ [ i ]( int mode ) { return AAStateOn( i ) && Config::Get<int>( yawModeIds[ i ] ) == mode; } };

				if ( i != 0 )
					generalGroup->Register( _( "Enable state" ), &Config::Get<bool>( enableIds[ i ] ), page );

				generalGroup->Register( _( "Pitch" ), &Config::Get<int>( pitchIds[ i ] ), 4, aaPitch, on );
				generalGroup->Register( _( "Yaw system" ), &Config::Get<int>( yawModeIds[ i ] ), 7, aaYawModes, on );

				generalGroup->Register( _( "Jitter degree" ), &Config::Get<int>( centerIds[ i ] ), -180, 180, [ yawMode ]( ) { return yawMode( 0 ); } );

				generalGroup->Register( _( "1st angle" ), &Config::Get<int>( way1Ids[ i ] ), -180, 180, [ yawMode ]( ) { return yawMode( 1 ); } );
				generalGroup->Register( _( "2nd angle" ), &Config::Get<int>( way2Ids[ i ] ), -180, 180, [ yawMode ]( ) { return yawMode( 1 ); } );
				generalGroup->Register( _( "3rd angle" ), &Config::Get<int>( way3Ids[ i ] ), -180, 180, [ yawMode ]( ) { return yawMode( 1 ); } );

				const auto leftRight{ [ yawMode ]( ) { return yawMode( 2 ) || yawMode( 3 ) || yawMode( 4 ) || yawMode( 6 ); } };
				generalGroup->Register( _( "Yaw left" ), &Config::Get<int>( leftIds[ i ] ), -180, 180, leftRight );
				generalGroup->Register( _( "Yaw right" ), &Config::Get<int>( rightIds[ i ] ), -180, 180, leftRight );

				generalGroup->Register( _( "Left min" ), &Config::Get<int>( minLeftIds[ i ] ), -180, 180, [ yawMode ]( ) { return yawMode( 5 ); } );
				generalGroup->Register( _( "Left max" ), &Config::Get<int>( maxLeftIds[ i ] ), -180, 180, [ yawMode ]( ) { return yawMode( 5 ); } );
				generalGroup->Register( _( "Right min" ), &Config::Get<int>( minRightIds[ i ] ), -180, 180, [ yawMode ]( ) { return yawMode( 5 ); } );
				generalGroup->Register( _( "Right max" ), &Config::Get<int>( maxRightIds[ i ] ), -180, 180, [ yawMode ]( ) { return yawMode( 5 ); } );

				generalGroup->Register( _( "Jitter delay" ), &Config::Get<int>( delayIds[ i ] ), 1, 8, [ yawMode ]( ) { return yawMode( 6 ); } );
				generalGroup->Register( _( "Jitter delay random" ), &Config::Get<int>( randomIds[ i ] ), 0, 4, [ yawMode ]( ) { return yawMode( 6 ); } );

				generalGroup->Register( _( "Body yaw" ), &Config::Get<int>( bodyIds[ i ] ), 4, aaBody, on );
				generalGroup->Register( _( "Body side" ), &Config::Get<int>( sideIds[ i ] ), 2, aaBodySide, [ i ]( ) { return AAStateOn( i ) && Config::Get<int>( bodyIds[ i ] ) == 0; } );
				generalGroup->Register( _( "Body flip speed ( 0 = random )" ), &Config::Get<int>( speedIds[ i ] ), 0, 8, [ i ]( ) { return AAStateOn( i ) && Config::Get<int>( bodyIds[ i ] ) == 1 && Config::Get<int>( yawModeIds[ i ] ) != 6; } );

				generalGroup->Register( _( "Disable freestanding" ), &Config::Get<bool>( noFsIds[ i ] ), on );
			}

			/* shared */
			generalGroup->Register( _( "Anti backstab" ), &Config::Get<bool>( Vars.AntiaimAntiBackStab ) );

			generalGroup->Register( _( "Manual direction" ), &Config::Get<bool>( Vars.AntiAimManualDir ) );
			generalGroup->Register( _( "Manual direction indicator" ), &Config::Get<bool>( Vars.AntiAimManualDirInd ) );
			generalGroup->Register( &Config::Get<Color>( Vars.AntiaimManualCol ) );
			generalGroup->Register( _( "Left" ) );
			generalGroup->Register( &Config::Get<keybind_t>( Vars.AntiaimLeft ) );
			Config::Get<keybind_t>( Vars.AntiaimLeft ).mode = EKeyMode::Toggle;
			generalGroup->Register( _( "Right" ) );
			generalGroup->Register( &Config::Get<keybind_t>( Vars.AntiaimRight ) );
			Config::Get<keybind_t>( Vars.AntiaimRight ).mode = EKeyMode::Toggle;
		}

		const auto fakelagGroup{ antiaim->AddGroup( _( "Fake-lag & extras" ), 1.f ) };
		{
			fakelagGroup->Register( _( "Limit" ), &Config::Get<int>( Vars.AntiaimFakeLagLimit ), 0, 15 );
			fakelagGroup->Register( _( "Randomization" ), &Config::Get<int>( Vars.AntiaimFakeLagVariance ), 0, 100 );
			fakelagGroup->Register( _( "Choke peek" ), &Config::Get<bool>( Vars.AntiaimFakeLagInPeek ) );
			fakelagGroup->Register( _( "Break lagcompensation" ), &Config::Get<bool>( Vars.AntiaimFakeLagBreakLC ) );

			/* custom: defensive window angles + extras */
			fakelagGroup->Register( _( "Defensive yaw" ), &Config::Get<int>( Vars.AACDefYaw ), 10, aaDefYaw, SHOW( AACustom( ) ) );
			fakelagGroup->Register( _( "Defensive pitch" ), &Config::Get<int>( Vars.AACDefPitch ), 7, aaDefPitch, SHOW( AACustom( ) ) );
			fakelagGroup->Register( _( "Height-based pitch" ), &Config::Get<bool>( Vars.AACDefHeightPitch ), SHOW( AACustom( ) ) );
			fakelagGroup->Register( _( "Pitch: threat above" ), &Config::Get<int>( Vars.AACDefPitchAbove ), 7, aaHeightPitch, SHOW( AACustom( ) && Config::Get<bool>( Vars.AACDefHeightPitch ) ) );
			fakelagGroup->Register( _( "Pitch: threat below" ), &Config::Get<int>( Vars.AACDefPitchBelow ), 7, aaHeightPitch, SHOW( AACustom( ) && Config::Get<bool>( Vars.AACDefHeightPitch ) ) );
			fakelagGroup->Register( _( "Height threshold" ), &Config::Get<int>( Vars.AACDefHeightThreshold ), 1, 45, SHOW( AACustom( ) && Config::Get<bool>( Vars.AACDefHeightPitch ) ) );

			fakelagGroup->Register( _( "Anti-aim on use" ), &Config::Get<bool>( Vars.AACOnUse ), SHOW( AACustom( ) ) );
			fakelagGroup->Register( _( "Safe head ( knife / zeus air duck )" ), &Config::Get<bool>( Vars.AACSafeHead ), SHOW( AACustom( ) ) );
		}
	}

	/* player */
	m_cTabs[ 1 ].m_vecSubtabs.emplace_back( _( "Enemy" ), 2 );
	m_cTabs[ 1 ].m_vecSubtabs.emplace_back( _( "Team" ), 2 );
	m_cTabs[ 1 ].m_vecSubtabs.emplace_back( _( "Local" ), 2 );
	{
		for ( int i{ }; i < 3; ++i ) {
			auto subTab{ &m_cTabs[ 1 ].m_vecSubtabs.at( i ) };
			auto ESPGroup{ subTab->AddGroup( "ESP", 1.f ) };
			{
				PlayerCheckbox( ESPGroup, _( "Enabled" ), VisEnable, i );
				PlayerCheckbox( ESPGroup, _( "Name" ), VisName, i );
				PlayerColorPicker( ESPGroup, VisNameCol, i );
				PlayerCheckbox( ESPGroup, _( "Box" ), VisBox, i );
				PlayerColorPicker( ESPGroup, VisBoxCol, i );
				PlayerCheckbox( ESPGroup, _( "Health" ), VisHealth, i );
				PlayerCheckbox( ESPGroup, _( "Color override" ), VisHealthOverride, i );
				PlayerColorPicker( ESPGroup, VisHealthCol, i );
				PlayerCheckbox( ESPGroup, _( "Ammo bar" ), VisAmmo, i );
				PlayerColorPicker( ESPGroup, VisAmmoCol, i );
				PlayerCheckbox( ESPGroup, _( "Out of FOV" ), VisOOF, i );
				PlayerColorPicker( ESPGroup, VisOOFCol, i );
				PlayerCheckbox( ESPGroup, _( "Skeleton" ), VisSkeleton, i );
				PlayerColorPicker( ESPGroup, VisSkeletonCol, i );
				PlayerCheckbox( ESPGroup, _( "Glow" ), VisGlow, i );
				PlayerColorPicker( ESPGroup, VisGlowCol, i );

				PlayerMultiCombo( ESPGroup, _( "Weapon" ), weapons, i, 2 );
				ESPGroup->Register( _( "Weapon color" ) );
				PlayerColorPicker( ESPGroup, VisWeapCol, i );

				if ( i == 2 )
					ESPGroup->Register( _( "Flags" ), 6, flagsLocal );
				else {
					PlayerMultiCombo( ESPGroup, _( "Flags" ), flags, i, 8 );
				}
			}

			auto chamGroup{ subTab->AddGroup( "Chams", 1.f ) };
			{
				PlayerCheckbox( chamGroup, _( "Visible" ), ChamVis, i );
				PlayerColorPicker( chamGroup, ChamVisCol, i );
				PlayerCombo( chamGroup, _( "Visible material" ), ChamMatVisible, i, materials, 5 );

				PlayerCheckbox( chamGroup, _( "Hidden" ), ChamHid, i );
				PlayerColorPicker( chamGroup, ChamHidCol, i );
				PlayerCombo( chamGroup, _( "Hidden material" ), ChamMatInvisible, i, materials, 5 );

				PlayerCheckbox( chamGroup, _( "Overlay" ), ChamDouble, i );
				PlayerColorPicker( chamGroup, ChamDoubleCol, i );
				PlayerCheckbox( chamGroup, _( "Overlay ignore Z" ), ChamDoubleZ, i );

				PlayerCombo( chamGroup, _( "Double layer material" ), ChamDoubleMat, i, materials, 5 );

				if ( i == 0 ) {
					chamGroup->Register( _( "Backtrack chams" ), &Config::Get<bool>( Vars.ChamBacktrack ) );
					chamGroup->Register( &Config::Get<Color>( Vars.ChamBacktrackCol ) );
					chamGroup->Register( _( "Backtrack chams material" ), &Config::Get<int>( Vars.ChamBacktrackMat ), 5, materials );

					chamGroup->Register( _( "Hitbox visualisation" ), &Config::Get<bool>( Vars.MiscHitMatrix ) );
					chamGroup->Register( &Config::Get<Color>( Vars.MiscHitMatrixCol ) );

					{
						chamGroup->Register( _( "Hitbox visualisation type" ), &Config::Get<int>( Vars.MiscShotVisualizationType ), 2, hitboxType, SHOW( Config::Get<bool>( Vars.MiscHitMatrix ) ) );
						chamGroup->Register( _( "Hitbox visualisation hidden" ), &Config::Get<bool>( Vars.MiscHitMatrixXQZ ), SHOW( Config::Get<bool>( Vars.MiscHitMatrix ) ) );
						chamGroup->Register( _( "Onshot chams material" ), &Config::Get<int>( Vars.MiscHitMatrixMat ), 5, materials, SHOW( Config::Get<bool>( Vars.MiscHitMatrix ) ) );

						chamGroup->Register( _( "Hitbox visualisation time" ), &Config::Get<float>( Vars.MiscHitMatrixTime ), 0.5f, 10.f, SHOW( Config::Get<bool>( Vars.MiscHitMatrix ) ) );
					}

				}

				if ( i == 2 ) {
					chamGroup->Register( _( "Desync chams" ), &Config::Get<bool>( Vars.ChamDesync ) );
					chamGroup->Register( &Config::Get<Color>( Vars.ChamDesyncCol ) );
					chamGroup->Register( _( "Desync chams material" ), &Config::Get<int>( Vars.ChamDesyncMat ), 5, materials );
					chamGroup->Register( _( "Desync chams pulse" ), &Config::Get<bool>( Vars.ChamDesyncPulse ), SHOW( Config::Get<bool>( Vars.ChamDesync ) ) );

				}

				PlayerIntSlider( chamGroup, _( "Glow strength" ), ChamGlowStrength, i, 0.f, 100.f );

				if ( i == 2 )
					chamGroup->Register( _( "Transparency in scope" ), &Config::Get<int>( Vars.ChamLocalTransparencyInScope ), 0, 100 );
			}
		}
	}

	const auto visualsSubtab{ &m_cTabs[ 2 ].m_vecSubtabs.emplace_back( _( "Visuals" ), 2 ) };
	{
		const auto ESPGroup{ visualsSubtab->AddGroup( _( "General" ), 1.f ) };
		{
			ESPGroup->Register( _( "Removals" ), 6, removals );

			ESPGroup->Register( _( "Thirdperson" ), &Config::Get<bool>( Vars.VisThirdPerson ) );
			ESPGroup->Register( &Config::Get<keybind_t>( Vars.VisThirdPersonKey ) );
			ESPGroup->Register( _( "Thirdperson distance" ), &Config::Get<int>( Vars.VisThirdPersonDistance ), 30, 250 );

			ESPGroup->Register( _( "Second Scope Zoom" ), &Config::Get<int>( Vars.SecondZoomAmt ), 0.f, 100.f, SHOW( Config::Get<bool>( Vars.RemovalZoom ) ) );

			ESPGroup->Register( _( "Penetration crosshair" ), &Config::Get<bool>( Vars.VisPenetrationCrosshair ) );

			ESPGroup->Register( _( "Crosshair indicators" ), &Config::Get<bool>( Vars.VisIndicators ) );
			ESPGroup->Register( &Config::Get<Color>( Vars.VisIndicatorsCol ), false );
			ESPGroup->Register( _( "Min damage indicator" ), &Config::Get<bool>( Vars.VisMinDmgIndicator ) );

			ESPGroup->Register( _( "Custom scope" ), &Config::Get<bool>( Vars.VisCustomScope ) );
			ESPGroup->Register( &Config::Get<Color>( Vars.VisCustomScopeCol ) );
			ESPGroup->Register( _( "Scope gap" ), &Config::Get<int>( Vars.VisCustomScopeGap ), 0, 100, SHOW( Config::Get<bool>( Vars.VisCustomScope ) ) );
			ESPGroup->Register( _( "Scope size" ), &Config::Get<int>( Vars.VisCustomScopeSize ), 5, 400, SHOW( Config::Get<bool>( Vars.VisCustomScope ) ) );

			ESPGroup->Register( _( "Grenade prediction" ), &Config::Get<bool>( Vars.VisGrenadePrediction ) );
			ESPGroup->Register( &Config::Get<Color>( Vars.VisGrenadePredictionCol ) );

			ESPGroup->Register( _( "Grenade warning" ), &Config::Get<bool>( Vars.VisGrenadeWarning ) );

			ESPGroup->Register( _( "Dropped weapons" ), &Config::Get<bool>( Vars.VisDroppedWeapon ) );
			ESPGroup->Register( &Config::Get<Color>( Vars.VisDroppedWeaponCol ) );
			ESPGroup->Register( _( "Team grenades" ), &Config::Get<bool>( Vars.VisGrenadesTeam ) );
			ESPGroup->Register( &Config::Get<Color>( Vars.VisGrenadesTeamCol ) );
			ESPGroup->Register( _( "Enemy grenades" ), &Config::Get<bool>( Vars.VisGrenadesEnemy ) );
			ESPGroup->Register( &Config::Get<Color>( Vars.VisGrenadesEnemyCol ) );
			ESPGroup->Register( _( "Planted Bomb" ), &Config::Get<bool>( Vars.VisBomb ) );
			ESPGroup->Register( _( "Local client bullet impacts" ), &Config::Get<bool>( Vars.VisClientBulletImpacts ) );
			ESPGroup->Register( &Config::Get<Color>( Vars.VisClientBulletImpactsCol ) );
			ESPGroup->Register( _( "Local server bullet impacts" ), &Config::Get<bool>( Vars.VisLocalBulletImpacts ) );
			ESPGroup->Register( &Config::Get<Color>( Vars.VisLocalBulletImpactsCol ) );
			ESPGroup->Register( _( "Enemy bullet impacts" ), &Config::Get<bool>( Vars.VisServerBulletImpacts ) );
			ESPGroup->Register( &Config::Get<Color>( Vars.VisServerBulletImpactsCol ) );

			ESPGroup->Register( _( "Local bullet tracers" ), &Config::Get<bool>( Vars.VisLocalBulletTracers ) );
			ESPGroup->Register( &Config::Get<Color>( Vars.VisLocalBulletTracersCol ) );

			ESPGroup->Register( _( "Enemy bullet tracers" ), &Config::Get<bool>( Vars.VisOtherBulletTracers ) );
			ESPGroup->Register( &Config::Get<Color>( Vars.VisOtherBulletTracersCol ) );

			ESPGroup->Register( _( "Bullet tracers type" ), &Config::Get<int>( Vars.VisBulletTracersType ), 2, tracerType );
		}

		const auto modificationsGroup{ visualsSubtab->AddGroup( _( "Modifications" ), 1.f ) };
		{
			modificationsGroup->Register( _( "Player interpolation" ), &Config::Get<int>( Vars.VisPlayerInterpolation ), 0, 16 );

			modificationsGroup->Register( _( "Full bright" ), &Config::Get<bool>( Vars.WorldFullbright ) );
			modificationsGroup->Register( _( "World Modulation" ), &Config::Get<bool>( Vars.WorldModulation ) );
			modificationsGroup->Register( &Config::Get<Color>( Vars.WorldModulationCol ), false );

			modificationsGroup->Register( _( "Skybox Color Modulation" ), &Config::Get<bool>( Vars.VisWorldSkyboxMod ) );
			modificationsGroup->Register( &Config::Get<Color>( Vars.WorldSkyboxCol ), false );

			modificationsGroup->Register( _( "Prop Color Modulation" ), &Config::Get<bool>( Vars.VisWorldPropMod ) );
			modificationsGroup->Register( &Config::Get<Color>( Vars.VisWorldPropCol ) );

			modificationsGroup->Register( _( "Ambient lighting" ), &Config::Get<bool>( Vars.WorldAmbientLighting ) );
			modificationsGroup->Register( &Config::Get<Color>( Vars.WorldAmbientLightingCol ), false );

			modificationsGroup->Register( _( "Bloom" ), &Config::Get<bool>( Vars.VisWorldBloom ) );

			modificationsGroup->Register( _( "Scale" ), &Config::Get<int>( Vars.VisWorldBloomScale ), 0.f, 750.f, SHOW( Config::Get<bool>( Vars.VisWorldBloom ) ) );
			modificationsGroup->Register( _( "Ambience" ), &Config::Get<int>( Vars.VisWorldBloomAmbience ), 0.f, 200.f, SHOW( Config::Get<bool>( Vars.VisWorldBloom ) ) );
			modificationsGroup->Register( _( "Exposure" ), &Config::Get<int>( Vars.VisWorldBloomExposure ), 0.f, 200.f, SHOW( Config::Get<bool>( Vars.VisWorldBloom ) ) );

			modificationsGroup->Register( _( "Weather" ), &Config::Get<int>( Vars.VisWorldWeather ), 6, weatherType );
			modificationsGroup->Register( _( "Fog" ), &Config::Get<bool>( Vars.VisWorldFog ) );
			modificationsGroup->Register( &Config::Get<Color>( Vars.VisWorldFogCol ) );

			modificationsGroup->Register( _( "Fog Distance" ), &Config::Get<int>( Vars.VisWorldFogDistance ), 0.f, 3000.f, SHOW( Config::Get<bool>( Vars.VisWorldFog ) ) );
			modificationsGroup->Register( _( "Fog Density" ), &Config::Get<int>( Vars.VisWorldFogDensity ), 0.f, 100.f, SHOW( Config::Get<bool>( Vars.VisWorldFog ) ) );
			modificationsGroup->Register( _( "Fog HDR" ), &Config::Get<int>( Vars.VisWorldFogHDR ), 0.f, 100.f, SHOW( Config::Get<bool>( Vars.VisWorldFog ) ) );

			modificationsGroup->Register( _( "Skybox changer" ), &Config::Get<int>( Vars.VisWorldSkybox ), 22, skies );

			modificationsGroup->Register( &Config::Get<std::string>( Vars.VisWorldSkyboxCustom ), SHOW( Config::Get<int>( Vars.VisWorldSkybox ) == 21 ) );

			modificationsGroup->Register( _( "Player model changer T" ), &Config::Get<int>( Vars.MiscPlayerModelT ), 13, modelsForChange );

			modificationsGroup->Register( _( "Player model changer CT" ), &Config::Get<int>( Vars.MiscPlayerModelCT ), 13, modelsForChange );
		}
	}

	const auto viewModelSubtab{ &m_cTabs[ 2 ].m_vecSubtabs.emplace_back( _( "Viewmodel" ), 2 ) };
	{
		const auto skinChangerGroup{ viewModelSubtab->AddGroup( _( "Skinchanger" ), 1.f ) };
		{
			skinChangerGroup->Register( _( "Coming soon!" ) );

			/*static std::vector<ComboElement_t> arrayOfData( k_knife_names.size( ) );

			std::transform( k_knife_names.begin( ), k_knife_names.end( ), arrayOfData.begin( ),
				[ ]( const WeaponName_t& v ) -> ComboElement_t {
					return ComboElement_t( v.name );
				} );

			if ( k_knife_names.size( ) )
				skinChangerGroup->Register( "Knife changer", &Config::Get<int>( Vars.SkinKnifeChanger ), k_knife_names.size( ), arrayOfData.data( ) );*/
		}

		const auto chamsGroup{ viewModelSubtab->AddGroup( _( "Chams" ), 1.f ) };
		{
			chamsGroup->Register( _( "Hand chams" ), &Config::Get<bool>( Vars.ChamHand ) );
			chamsGroup->Register( &Config::Get<Color>( Vars.ChamHandCol ) );
			chamsGroup->Register( _( "Hand chams material" ), &Config::Get<int>( Vars.ChamHandMat ), 5, materials );
			chamsGroup->Register( _( "Hand overlay" ), &Config::Get<bool>( Vars.ChamHandOverlay ) );

			chamsGroup->Register( &Config::Get<Color>( Vars.ChamHandOverlayCol ) );
			chamsGroup->Register( _( "Hand overlay material" ), &Config::Get<int>( Vars.ChamHandOverlayMat ), 5, materials );

			chamsGroup->Register( _( "Hand glow strength" ), &Config::Get<int>( Vars.ChamHandGlow ), 0.f, 100.f );

			chamsGroup->Register( _( "Weapon chams" ), &Config::Get<bool>( Vars.ChamWeapon ) );
			chamsGroup->Register( &Config::Get<Color>( Vars.ChamWeaponCol ) );
			chamsGroup->Register( _( "Weapon chams material" ), &Config::Get<int>( Vars.ChamWeaponMat ), 5, materials );

			chamsGroup->Register( _( "Weapon overlay" ), &Config::Get<bool>( Vars.ChamWeaponOverlay ) );
			chamsGroup->Register( &Config::Get<Color>( Vars.ChamWeaponOverlayCol ) );
			chamsGroup->Register( _( "Weapon overlay material" ), &Config::Get<int>( Vars.ChamWeaponOverlayMat ), 5, materials );

			chamsGroup->Register( _( "Weapon glow strength" ), &Config::Get<int>( Vars.ChamWeaponGlow ), 0.f, 100.f );
		}
	}

	auto miscSubTab{ &m_cTabs[ 3 ].m_vecSubtabs.emplace_back( "Misc", 2 ) };
	{
		auto generalGroup{ miscSubTab->AddGroup( "General", 1.f ) };
		{
			/* propaganda section */
			generalGroup->Register( _( "Watermark" ), &Config::Get<bool>( Vars.MiscWatermark ) );
			generalGroup->Register( _( "Accent color" ) );
			//generalGroup->Register( _( "Accent Color" ), Menu::AccentCol, false );
			generalGroup->Register( _( "Clantag" ), &Config::Get<bool>( Vars.MiscClantag ) );
			static ComboElement_t clantagModes[ 2 ]{ _( "Static" ), _( "Animated" ) };
			generalGroup->Register( _( "Clantag style" ), &Config::Get<int>( Vars.MiscClantagMode ), 2, clantagModes, SHOW( Config::Get<bool>( Vars.MiscClantag ) ) );

			generalGroup->Register( _( "Keybinds list" ), &Config::Get<bool>( Vars.MiscKeybindList ) );
			generalGroup->Register( _( "Spectator list" ), &Config::Get<bool>( Vars.MiscSpectatorList ) );
			generalGroup->Register( _( "Aspect ratio" ), &Config::Get<bool>( Vars.MiscAspectRatio ) );

			generalGroup->Register( _( "Ratio" ), &Config::Get<float>( Vars.MiscAspectRatioAmt ), 0.02f, 5.f, SHOW( Config::Get<bool>( Vars.MiscAspectRatio ) ) );

			generalGroup->Register( _( "Field of view" ), &Config::Get<int>( Vars.MiscFOV ), 0, 70 );

			generalGroup->Register( _( "Only show record anims" ), &Config::Get<bool>( Vars.VisRecordAnims ) );
			generalGroup->Register( _( "Local animation sway" ), &Config::Get<int>( Vars.MiscLocalAnimSway ), 0, 40 );

			generalGroup->Register( _( "Preserve killfeed" ), &Config::Get<bool>( Vars.MiscPreserveKillfeed ) );
			generalGroup->Register( _( "Force crosshair" ), &Config::Get<bool>( Vars.MiscForceCrosshair ) );
			generalGroup->Register( _( "Fake ping" ), &Config::Get<bool>( Vars.MiscFakePing ) );
			generalGroup->Register( _( "Weapon volume" ), &Config::Get<int>( Vars.MiscWeaponVolume ), 0, 100 );

			static MultiElement_t hitMarkers[ 2 ]{
				{ &Config::Get<bool>( Vars.MiscScreenHitmarker ), "Screen" },
				{ &Config::Get<bool>( Vars.MiscWorldHitmarker ), "World" }
			};

			generalGroup->Register( _( "Hit marker" ), 2, hitMarkers );

			{
				generalGroup->Register( _( "Screen hit marker size" ), &Config::Get<int>( Vars.MiscScreenHitmarkerSize ), 1.f, 20.f, SHOW( Config::Get<bool>( Vars.MiscScreenHitmarker ) ) );
				generalGroup->Register( _( "Screen hit marker gap" ), &Config::Get<int>( Vars.MiscScreenHitmarkerGap ), 0.f, 20.f, SHOW( Config::Get<bool>( Vars.MiscScreenHitmarker ) ) );
			}

			{
				generalGroup->Register( _( "World hit marker size" ), &Config::Get<int>( Vars.MiscWorldHitmarkerSize ), 1, 20, SHOW( Config::Get<bool>( Vars.MiscWorldHitmarker ) ) );
				generalGroup->Register( _( "World hit marker gap" ), &Config::Get<int>( Vars.MiscWorldHitmarkerGap ), 0, 20, SHOW( Config::Get<bool>( Vars.MiscWorldHitmarker ) ) );
			}

			generalGroup->Register( _( "Damage marker" ), &Config::Get<bool>( Vars.MiscDamageMarker ) );
			generalGroup->Register( &Config::Get<Color>( Vars.MiscDamageMarkerCol ) );

			// indices match SoundPack::Path ( 2 stays custom so old configs keep their sound )
			static ComboElement_t hitsounds[ SoundPack::Count ]{ _( "None" ), _( "Metallic" ), _( "Custom" ), _( "Metal impact" ), _( "Bell" ), _( "Click" ) };
			generalGroup->Register( _( "Hitsound" ), &Config::Get<int>( Vars.MiscHitSound ), SoundPack::Count, hitsounds );
			generalGroup->Register( _( "Kill sound" ), &Config::Get<int>( Vars.MiscKillSound ), SoundPack::Count, hitsounds );

			generalGroup->Register( _( "Custom sound" ), true, SHOW( Config::Get<int>( Vars.MiscHitSound ) == 2 || Config::Get<int>( Vars.MiscKillSound ) == 2 ) );
			generalGroup->Register( &Config::Get<std::string>( Vars.MiscCustomHitSound ), SHOW( Config::Get<int>( Vars.MiscHitSound ) == 2 || Config::Get<int>( Vars.MiscKillSound ) == 2 ) );

			generalGroup->Register( _( "Kill effect" ), &Config::Get<bool>( Vars.VisKillEffect ) );
			generalGroup->Register( &Config::Get<Color>( Vars.VisKillEffectCol ) );
			generalGroup->Register( _( "Kill screen flash" ), &Config::Get<bool>( Vars.VisKillFlash ), SHOW( Config::Get<bool>( Vars.VisKillEffect ) ) );

			generalGroup->Register( _( "Auto buy" ), &Config::Get<bool>( Vars.MiscBuyBot ) );

			{
				static MultiElement_t buyother[ 7 ]{
					{ &Config::Get<bool>( Vars.MiscBuyBotOtherArmor ), _( "Armor" ) },
					{ &Config::Get<bool>( Vars.MiscBuyBotOtherTaser ), _( "Taser" ) },
					{ &Config::Get<bool>( Vars.MiscBuyBotOtherKit ), _( "Kit" ) },
					{ &Config::Get<bool>( Vars.MiscBuyBotOtherSmoke ), _( "Smoke" ) },
					{ &Config::Get<bool>( Vars.MiscBuyBotOtherNade ), _( "HE Grenade" ) },
					{ &Config::Get<bool>( Vars.MiscBuyBotOtherFlashbang ), _( "Flashbang" ) },
					{ &Config::Get<bool>( Vars.MiscBuyBotOtherMolotov ), _( "Molotov" ) }
				};

				static ComboElement_t primary[ 6 ]{ _( "None" ), _( "Autosniper" ), _( "SSG-08" ), _( "awp" ), _( "negev" ), _( "ak-47/m4" ) };
				static ComboElement_t secondary[ 5 ]{ _( "None" ), _( "Deagle/r8" ), _( "Dualies" ), _( "USP/Glock" ), _( "Tec9/Fiveseven" ) };

				generalGroup->Register( "Primary weapons", &Config::Get<int>( Vars.MiscBuyBotPrimary ), 6, primary, SHOW( Config::Get<bool>( Vars.MiscBuyBot ) ) );
				generalGroup->Register( "Secondary weapons", &Config::Get<int>( Vars.MiscBuyBotSecondary ), 5, secondary, SHOW( Config::Get<bool>( Vars.MiscBuyBot ) ) );
				generalGroup->Register( _( "Other" ), 7, buyother, SHOW( Config::Get<bool>( Vars.MiscBuyBot ) ) );
			}

			generalGroup->Register( _( "Reset menu size" ), ResetMenuSize );
			generalGroup->Register( _( "Unload" ), Unload );
		}

		auto movementGroup{ miscSubTab->AddGroup( "Movement", 0.6f ) };
		{
			movementGroup->Register( _( "Bunnyhop" ), &Config::Get<bool>( Vars.MiscBunnyhop ) );
			movementGroup->Register( _( "Autostrafer" ), &Config::Get<bool>( Vars.MiscAutostrafe ) );

			movementGroup->Register( _( "Autostrafer speed" ), &Config::Get<int>( Vars.MiscAutostrafeSpeed ), 0.f, 100.f, SHOW( Config::Get<bool>( Vars.MiscAutostrafe ) ) );

			movementGroup->Register( _( "Infinite Stamina" ), &Config::Get<bool>( Vars.MiscInfiniteStamina ) );
			movementGroup->Register( _( "Quick stop" ), &Config::Get<bool>( Vars.MiscQuickStop ) );
			movementGroup->Register( _( "Slide walk" ), &Config::Get<bool>( Vars.MiscSlideWalk ) );
			movementGroup->Register( _( "Slow walk" ), &Config::Get<bool>( Vars.MiscSlowWalk ) );
			movementGroup->Register( &Config::Get<keybind_t>( Vars.MiscSlowWalkKey ) );

			movementGroup->Register( _( "Fake duck" ), &Config::Get<bool>( Vars.MiscFakeDuck ), SHOW( Config::Get<bool>( Vars.MiscInfiniteStamina ) ) );
			movementGroup->Register( &Config::Get<keybind_t>( Vars.MiscFakeDuckKey ), SHOW( Config::Get<bool>( Vars.MiscInfiniteStamina ) ) );

			movementGroup->Register( _( "Auto retreat" ), &Config::Get<bool>( Vars.MiscAutoPeek ) );
			movementGroup->Register( &Config::Get<keybind_t>( Vars.MiscAutoPeekKey ) );
			movementGroup->Register( _( "Auto retreat on release" ), &Config::Get<bool>( Vars.MiscAutoPeekOnRelease ), SHOW( Config::Get<bool>( Vars.MiscAutoPeek ) ) );
			movementGroup->Register( _( "Auto retreat color" ) );
			movementGroup->Register( &Config::Get<Color>( Vars.MiscAutoPeekCol ), true );
		}

		auto betaGroup{ miscSubTab->AddGroup( "Beta testing", 0.4f ) };
		{
			betaGroup->Register( _( "Disable yaw extrapolation" ), &Config::Get<bool>( Vars.DBGLC1 ) );
			//betaGroup->Register( _( "Extrapolate players" ), &Config::Get<bool>( Vars.DBGExtrap ) );
			//betaGroup->Register( _( "Valve simtime fix" ), &Config::Get<bool>( Vars.DBGNoPingReducer ) );
			//betaGroup->Register( _( "Record options" ), 2, recordOptions );
			//betaGroup->Register( _( "Smoother gameplay" ), &Config::Get<bool>( Vars.DBGNoLatch ) );
		}
	}

	auto configSubtab{ &m_cTabs[ 4 ].m_vecSubtabs.emplace_back( "Configs", 1 ) };
	{
		auto configGroup{ configSubtab->AddGroup( "Configurations", 1.f ) };

		configGroup->Register( &selectedConfig );
		configGroup->Register( &typedConfig );
		configGroup->Register( "Refresh list", RefreshConfig );
		configGroup->Register( "Create", CreateConfig );
		configGroup->Register( "Remove", RemoveConfig );

		configGroup->Register( "Load", LoadConfig );
		configGroup->Register( "Save", SaveConfig );
	}

	auto luaSubtab{ &m_cTabs[ 4 ].m_vecSubtabs.emplace_back( "Lua", 2 ) };
	Lua::RegisterMenu( luaSubtab );
}

// L func
void Menu::LerpToCol( Color& col, Color to ) {
	col = Color(
		( to[ 0 ] > col[ 0 ] ? uint8_t( std::ceil( std::min( to[ 0 ], uint8_t( ( to[ 0 ] - col[ 0 ] ) * ANIMATION_SPEED + col[ 0 ] ) ) ) ) : uint8_t( std::floor( std::max( to[ 0 ], uint8_t( ( to[ 0 ] - col[ 0 ] ) * ANIMATION_SPEED + col[ 0 ] ) ) ) ) ),
		( to[ 1 ] > col[ 1 ] ? uint8_t( std::ceil( std::min( to[ 1 ], uint8_t( ( to[ 1 ] - col[ 1 ] ) * ANIMATION_SPEED + col[ 1 ] ) ) ) ) : uint8_t( std::floor( std::max( to[ 1 ], uint8_t( ( to[ 1 ] - col[ 1 ] ) * ANIMATION_SPEED + col[ 1 ] ) ) ) ) ),
		( to[ 2 ] > col[ 2 ] ? uint8_t( std::ceil( std::min( to[ 2 ], uint8_t( ( to[ 2 ] - col[ 2 ] ) * ANIMATION_SPEED + col[ 2 ] ) ) ) ) : uint8_t( std::floor( std::max( to[ 2 ], uint8_t( ( to[ 2 ] - col[ 2 ] ) * ANIMATION_SPEED + col[ 2 ] ) ) ) ) ),
		( to[ 3 ] > col[ 3 ] ? uint8_t( std::ceil( std::min( to[ 3 ], uint8_t( ( to[ 3 ] - col[ 3 ] ) * ANIMATION_SPEED + col[ 3 ] ) ) ) ) : uint8_t( std::floor( std::max( to[ 3 ], uint8_t( ( to[ 3 ] - col[ 3 ] ) * ANIMATION_SPEED + col[ 3 ] ) ) ) ) )
	);
}

/* ------------------------------------------------------------------------------------------------ */
/* settings search ( header box, ctrl + f )                                                          */
/* ------------------------------------------------------------------------------------------------ */

namespace {
	struct SearchHit_t {
		int m_iTab{ };
		CMenuSubtab* m_pSubtab{ };
		CMenuItem* m_pItem{ };
		std::string m_strName{ };
		std::string m_strPath{ };
		int m_iScore{ };
	};

	constexpr int SEARCH_MAX_HITS{ 8 };
	constexpr float SEARCH_ROW{ 22.f };
	constexpr float SEARCH_PANEL_WIDTH{ 320.f };

	std::string g_strQuery{ };
	std::vector< SearchHit_t > g_vecHits{ };
	int g_iSearchHover{ -1 };
	float g_flBoxFocus{ };          // box outline animation
	Anim::Fade_t g_cPanelFade{ };   // results panel

	Vector2D g_vecBoxPos{ }, g_vecBoxSize{ 180.f, 22.f };
	Vector2D g_vecPanelPos{ }, g_vecPanelSize{ };

	std::string Lower( std::string text ) {
		for ( auto& c : text )
			c = static_cast< char >( std::tolower( static_cast< unsigned char >( c ) ) );
		return text;
	}

	// every visible, named control in every tab whose name contains the query; prefix and word matches first
	void CollectHits( ) {
		g_vecHits.clear( );
		if ( g_strQuery.empty( ) )
			return;

		const auto needle{ Lower( g_strQuery ) };
		for ( int tab{ }; tab < 5; ++tab ) {
			auto& menuTab{ Menu::m_cTabs[ tab ] };
			for ( auto& subtab : menuTab.m_vecSubtabs ) {
				for ( auto& group : subtab.m_vecGroups ) {
					for ( auto& item : group.Items( ) ) {
						if ( !item.m_szName || !item.m_szName[ 0 ] )
							continue;
						if ( item.m_eItemType == COLOR_PICKER || item.m_eItemType == KEY_BIND || item.m_eItemType == LIST_BOX )
							continue;
						if ( item.m_fnShow && !item.m_fnShow( ) )
							continue;

						const auto name{ Lower( item.m_szName ) };
						const auto at{ name.find( needle ) };
						if ( at == std::string::npos )
							continue;

						SearchHit_t hit{ };
						hit.m_iTab = tab;
						hit.m_pSubtab = &subtab;
						hit.m_pItem = &item;
						hit.m_strName = item.m_szName;
						hit.m_strPath = std::string( menuTab.m_szName ? menuTab.m_szName : "" ) + "  >  " + ( subtab.m_szName ? subtab.m_szName : "" )
							+ "  >  " + ( group.Name( ) ? group.Name( ) : "" );
						hit.m_iScore = at == 0 ? 0 : name[ at - 1 ] == ' ' ? 1 : 2;
						g_vecHits.push_back( std::move( hit ) );
					}
				}
			}
		}

		std::stable_sort( g_vecHits.begin( ), g_vecHits.end( ), [ ]( const SearchHit_t& a, const SearchHit_t& b ) {
			if ( a.m_iScore != b.m_iScore )
				return a.m_iScore < b.m_iScore;
			return a.m_strName.size( ) < b.m_strName.size( );
		} );

		if ( g_vecHits.size( ) > SEARCH_MAX_HITS )
			g_vecHits.resize( SEARCH_MAX_HITS );
	}

	void JumpTo( const SearchHit_t& hit ) {
		Menu::m_iSelectedTab = static_cast< uint8_t >( hit.m_iTab );
		Menu::m_cTabs[ hit.m_iTab ].m_pSelectedSubtab = hit.m_pSubtab;
		Menu::m_pHighlightItem = hit.m_pItem;
		Menu::m_flHighlightTime = Interfaces::Globals->flRealTime;
		Menu::m_bHighlightScroll = true;

		Menu::m_bSearchActive = false;
		g_strQuery.clear( );
		g_vecHits.clear( );
	}

	void TypeInto( std::string& text ) {
		const bool shift{ Inputsys::down( VK_SHIFT ) };

		for ( int key{ 'A' }; key <= 'Z'; ++key ) {
			if ( Inputsys::pressed( key ) )
				text += static_cast< char >( shift ? key : key + 32 );
		}
		for ( int key{ '0' }; key <= '9'; ++key ) {
			if ( Inputsys::pressed( key ) )
				text += static_cast< char >( key );
		}
		for ( int key{ VK_NUMPAD0 }; key <= VK_NUMPAD9; ++key ) {
			if ( Inputsys::pressed( key ) )
				text += static_cast< char >( '0' + key - VK_NUMPAD0 );
		}
		if ( Inputsys::pressed( VK_SPACE ) )
			text += ' ';
		if ( Inputsys::pressed( VK_OEM_MINUS ) )
			text += shift ? '_' : '-';
		if ( Inputsys::pressed( VK_BACK ) && !text.empty( ) )
			text.pop_back( );

		if ( text.size( ) > 32 )
			text.resize( 32 );
	}

	// box + results, drawn last so they sit above the page
	void DrawSearch( ) {
		const auto& pos{ g_vecBoxPos };
		const auto& size{ g_vecBoxSize };
		const bool boxHovered{ Inputsys::hovered( pos, size ) };

		g_flBoxFocus = Anim::Approach( g_flBoxFocus, Menu::m_bSearchActive ? 1.f : boxHovered ? 0.5f : 0.f, 16.f );

		MenuDraw::Glow( pos, size, g_flBoxFocus * 0.8f, ACCENT );
		Render::FilledRoundedBox( pos - Vector2D( 1, 1 ), size + Vector2D( 2, 2 ), 5, 5, Menu::m_bSearchActive ? ACCENT.Set<COLOR_A>( 170 ) : OUTLINE_DARK );
		Render::FilledRoundedBox( pos, size, 5, 5, boxHovered || Menu::m_bSearchActive ? MENU_CONTROL_HOVER : MENU_CONTROL );

		// magnifier
		const Vector2D glass{ pos.x + 12.f, pos.y + size.y / 2.f - 1.f };
		const auto iconCol{ Menu::m_bSearchActive ? ACCENT : MENU_TEXT_DIM };
		MenuDraw::Arc( glass, 4.f, iconCol, 1.5f );
		MenuDraw::ThickLine( glass + Vector2D( 3.f, 3.f ), glass + Vector2D( 6.f, 6.f ), iconCol, 1.5f );

		const Vector2D textPos{ pos.x + 24.f, pos.y + size.y / 2.f - 7.f };
		if ( g_strQuery.empty( ) )
			Render::Text( Fonts::Menu, textPos, MENU_TEXT_DIM, FONT_LEFT, Menu::m_bSearchActive ? "type to search..." : "search  ( ctrl + f )" );
		else {
			Render::Text( Fonts::Menu, textPos, MENU_TEXT, FONT_LEFT, g_strQuery.c_str( ) );

			// blinking caret
			if ( Menu::m_bSearchActive && std::fmod( Interfaces::Globals->flRealTime, 1.f ) < 0.55f ) {
				const float caretX{ textPos.x + Render::GetTextSize( g_strQuery, Fonts::Menu ).x + 1.f };
				Render::FilledRectangle( Vector2D( caretX, pos.y + 5.f ), Vector2D( 1.f, size.y - 10.f ), ACCENT );
			}
		}

		g_cPanelFade.Update( Menu::m_bSearchActive && !g_strQuery.empty( ), Anim::PopupTime );
		if ( !g_cPanelFade.Visible( ) )
			return;

		const float alpha{ g_cPanelFade.Alpha( ) };
		const auto a{ [ alpha ]( Color c ) { return c.Set<COLOR_A>( c.Get<COLOR_A>( ) * alpha ); } };
		const Vector2D panelPos{ g_vecPanelPos - Vector2D( 0.f, g_cPanelFade.Offset( 6.f ) ) };

		Render::FilledRoundedBox( panelPos - Vector2D( 3, 3 ), g_vecPanelSize + Vector2D( 6, 6 ), 5, 7, a( Color( 0, 0, 0, 70 ) ) );
		Render::FilledRoundedBox( panelPos - Vector2D( 1, 1 ), g_vecPanelSize + Vector2D( 2, 2 ), 5, 5, a( OUTLINE_DARK ) );
		Render::FilledRoundedBox( panelPos, g_vecPanelSize, 5, 5, a( MENU_POPUP ) );

		if ( g_vecHits.empty( ) ) {
			Render::Text( Fonts::Menu, panelPos + Vector2D( 10.f, 5.f ), a( MENU_TEXT_DIM ), FONT_LEFT, "no matching setting" );
			return;
		}

		for ( int i{ }; i < static_cast< int >( g_vecHits.size( ) ); ++i ) {
			const auto& hit{ g_vecHits[ i ] };
			const Vector2D rowPos{ panelPos.x + 4.f, panelPos.y + 4.f + SEARCH_ROW * i };
			const bool hovered{ i == g_iSearchHover };

			if ( hovered || ( g_iSearchHover < 0 && i == 0 ) ) {
				Render::FilledRoundedBox( rowPos, Vector2D( g_vecPanelSize.x - 8.f, SEARCH_ROW - 2.f ), 5, 4, a( MENU_CONTROL ) );
				Render::FilledRectangle( rowPos + Vector2D( 0.f, 5.f ), Vector2D( 2.f, SEARCH_ROW - 12.f ), a( ACCENT ) );
			}

			Render::Text( Fonts::Menu, rowPos + Vector2D( 8.f, 3.f ), a( hovered ? MENU_TEXT : SELECTED_ELEMENT ), FONT_LEFT, hit.m_strName.c_str( ) );
			Render::Text( Fonts::Logs, rowPos + Vector2D( g_vecPanelSize.x - 14.f, 5.f ), a( MENU_TEXT_DIM ), FONT_RIGHT, hit.m_strPath.c_str( ) );
		}
	}
}

void Menu::RenderSearch( bool& blockClicks ) {
	blockClicks = false;

	g_vecBoxPos = Vector2D( m_vecPos.x + m_vecSize.x - MARGIN - g_vecBoxSize.x, m_vecPos.y + ( HEADER_SIZE - g_vecBoxSize.y ) / 2.f );

	const bool boxHovered{ Inputsys::hovered( g_vecBoxPos, g_vecBoxSize ) };
	const bool popupOpen{ m_pFocusItem.m_pItem != nullptr };

	// open: click the box or ctrl + f
	if ( !popupOpen && ( ( boxHovered && Inputsys::pressed( VK_LBUTTON ) )
		|| ( Inputsys::down( VK_CONTROL ) && Inputsys::pressed( 'F' ) ) ) ) {
		m_bSearchActive = true;
		blockClicks = boxHovered;
	}

	if ( m_bSearchActive ) {
		// ctrl + f itself must not type an "f"
		if ( !Inputsys::down( VK_CONTROL ) )
			TypeInto( g_strQuery );

		CollectHits( );

		const int rows{ std::max( 1, static_cast< int >( g_vecHits.size( ) ) ) };
		g_vecPanelSize = Vector2D( SEARCH_PANEL_WIDTH, rows * SEARCH_ROW + 8.f );
		g_vecPanelPos = Vector2D( g_vecBoxPos.x + g_vecBoxSize.x - SEARCH_PANEL_WIDTH, g_vecBoxPos.y + g_vecBoxSize.y + 6.f );

		g_iSearchHover = -1;
		for ( int i{ }; i < static_cast< int >( g_vecHits.size( ) ); ++i ) {
			if ( Inputsys::hovered( Vector2D( g_vecPanelPos.x, g_vecPanelPos.y + 4.f + SEARCH_ROW * i ), Vector2D( SEARCH_PANEL_WIDTH, SEARCH_ROW ) ) )
				g_iSearchHover = i;
		}

		const bool panelHovered{ !g_strQuery.empty( ) && Inputsys::hovered( g_vecPanelPos, g_vecPanelSize ) };

		if ( Inputsys::pressed( VK_ESCAPE ) ) {
			if ( g_strQuery.empty( ) )
				m_bSearchActive = false;
			g_strQuery.clear( );
			g_vecHits.clear( );
		}
		else if ( Inputsys::pressed( VK_RETURN ) && !g_vecHits.empty( ) )
			JumpTo( g_vecHits.front( ) );
		else if ( Inputsys::pressed( VK_LBUTTON ) ) {
			if ( g_iSearchHover >= 0 ) {
				JumpTo( g_vecHits[ g_iSearchHover ] );
				blockClicks = true;
			}
			else if ( panelHovered || boxHovered )
				blockClicks = true;
			else
				m_bSearchActive = false;   // clicked somewhere else: close, the click goes through
		}
	}
	else
		g_vecHits.clear( );
}

void Menu::RenderElements( ) {
	auto& activeTab{ m_cTabs[ m_iSelectedTab ] };

	const auto backupSubtab{ activeTab.m_pSelectedSubtab };

	if ( !activeTab.m_pSelectedSubtab && !activeTab.m_vecSubtabs.empty( ) )
		activeTab.m_pSelectedSubtab = &activeTab.m_vecSubtabs.front( );

	// the search box / its results eat clicks that land on them ( nothing underneath reacts )
	bool searchBlocks{ };
	RenderSearch( searchBlocks );
	if ( searchBlocks )
		Menu::m_pFocusItem.m_bFrameAfterFocus = true;

	const bool canClick{ !Menu::m_pFocusItem.m_pItem && !Menu::m_pFocusItem.m_bFrameAfterFocus && !m_bDraggingMenu && !m_bDraggingSize };

	// selection pill glides between tabs ( offset from the window, so dragging the menu doesn't make it lag )
	{
		static float pillOffset{ -1.f };
		const float target{ LOGO_HEIGHT + 18.f + m_iSelectedTab * ( TAB_HEIGHT + 6.f ) };
		pillOffset = pillOffset < 0.f ? target : Anim::Approach( pillOffset, target, 18.f );

		const Vector2D pillPos{ m_vecPos.x + 10.f, m_vecPos.y + pillOffset };
		const Vector2D pillSize{ BAR_SIZE - 20.f, static_cast< float >( TAB_HEIGHT ) };
		Render::FilledRoundedBox( pillPos, pillSize, 5, 5, MENU_CONTROL );
		Render::FilledRectangle( Vector2D( pillPos.x, pillPos.y + 9.f ), Vector2D( 3.f, TAB_HEIGHT - 18.f ), ACCENT );
	}

	// sidebar tabs: icon + label, selected one gets a pill and an accent bar
	for ( int i{ }; i < 5; ++i ) {
		auto& tab{ m_cTabs[ i ] };

		const Vector2D tabPos{ m_vecPos.x + 10.f, m_vecPos.y + LOGO_HEIGHT + 18.f + i * ( TAB_HEIGHT + 6.f ) };
		const Vector2D tabSize{ BAR_SIZE - 20.f, static_cast< float >( TAB_HEIGHT ) };

		const auto hovered{ Inputsys::hovered( tabPos, tabSize ) };
		if ( hovered && canClick && !tab.m_vecSubtabs.empty( )
			&& ( Inputsys::pressed( VK_LBUTTON ) || Inputsys::pressed( VK_RBUTTON ) || Inputsys::pressed( VK_MBUTTON ) ) ) {
			m_iSelectedTab = i;

			// right / middle click jump straight to the 2nd / 3rd subtab
			if ( Inputsys::pressed( VK_RBUTTON ) && tab.m_vecSubtabs.size( ) > 1 )
				tab.m_pSelectedSubtab = &tab.m_vecSubtabs.at( 1 );
			else if ( Inputsys::pressed( VK_MBUTTON ) && tab.m_vecSubtabs.size( ) > 2 )
				tab.m_pSelectedSubtab = &tab.m_vecSubtabs.at( 2 );
			else
				tab.m_pSelectedSubtab = &tab.m_vecSubtabs.front( );
		}

		const bool selected{ m_iSelectedTab == i };

		tab.m_flAnimation = Math::Interpolate( tab.m_flAnimation, selected ? 1.f : 0.f, ANIMATION_SPEED );
		if ( tab.m_flAnimation <= 0.005f )
			tab.m_flAnimation = 0.f;
		else if ( tab.m_flAnimation >= 0.995f )
			tab.m_flAnimation = 1.f;

		LerpToCol( tab.m_cColor, selected ? ACCENT : hovered ? SELECTED_ELEMENT : MENU_TEXT_DIM );

		const float anim{ tab.m_flAnimation };

		if ( hovered && !selected )
			Render::FilledRoundedBox( tabPos, tabSize, 5, 5, MENU_CONTROL.Set<COLOR_A>( 110 ) );

		MenuDraw::TabIcon( i, Vector2D( tabPos.x + 22.f, tabPos.y + TAB_HEIGHT / 2.f ), 18.f, tab.m_cColor );
		Render::Text( Fonts::MenuBold, Vector2D( tabPos.x + 42.f + 3.f * anim, tabPos.y + TAB_HEIGHT / 2.f - 7.f ), tab.m_cColor, FONT_LEFT, tab.m_szName );
	}

	auto& newActiveTab{ m_cTabs[ m_iSelectedTab ] };

	if ( !newActiveTab.m_pSelectedSubtab && !newActiveTab.m_vecSubtabs.empty( ) )
		newActiveTab.m_pSelectedSubtab = &newActiveTab.m_vecSubtabs.front( );

	// subtabs across the top of the content area
	{
		float x{ m_vecPos.x + BAR_SIZE + MARGIN };
		const float y{ m_vecPos.y + 10.f };
		const float height{ HEADER_SIZE - 20.f };

		struct SubtabButton_t { CMenuSubtab* m_pSubtab; Vector2D m_vecPos; Vector2D m_vecSize; bool m_bHovered; };
		std::vector< SubtabButton_t > buttons{ };
		buttons.reserve( newActiveTab.m_vecSubtabs.size( ) );

		float selectedX{ -1.f }, selectedW{ };
		for ( auto& subtab : newActiveTab.m_vecSubtabs ) {
			const auto textWidth{ Render::GetTextSize( subtab.m_szName, Fonts::MenuBold ).x };
			const Vector2D buttonPos{ x, y };
			const Vector2D buttonSize{ textWidth + 24.f, height };

			const auto hovered{ Inputsys::hovered( buttonPos, buttonSize ) };

			// no subtab clicks on the frame the tab itself changed
			if ( hovered && canClick && Inputsys::pressed( VK_LBUTTON ) && &activeTab == &newActiveTab )
				newActiveTab.m_pSelectedSubtab = &subtab;

			buttons.push_back( { &subtab, buttonPos, buttonSize, hovered } );
			x += buttonSize.x + 6.f;
		}

		for ( const auto& button : buttons ) {
			if ( newActiveTab.m_pSelectedSubtab == button.m_pSubtab ) {
				selectedX = button.m_vecPos.x - m_vecPos.x;
				selectedW = button.m_vecSize.x;
			}
		}

		// selection background + underline glide to the selected subtab ( and stretch to its width )
		static float highlightX{ -1.f }, highlightW{ };
		if ( selectedX >= 0.f ) {
			highlightX = highlightX < 0.f ? selectedX : Anim::Approach( highlightX, selectedX, 18.f );
			highlightW = highlightW <= 0.f ? selectedW : Anim::Approach( highlightW, selectedW, 18.f );

			const Vector2D highlightPos{ m_vecPos.x + highlightX, y };
			Render::FilledRoundedBox( highlightPos, Vector2D( highlightW, height ), 5, 5, MENU_CONTROL );
			Render::FilledRectangle( Vector2D( highlightPos.x + 10.f, highlightPos.y + height - 2.f ), Vector2D( std::max( highlightW - 20.f, 0.f ), 2.f ), ACCENT );
		}

		for ( const auto& button : buttons ) {
			auto& subtab{ *button.m_pSubtab };
			const bool selected{ newActiveTab.m_pSelectedSubtab == &subtab };

			LerpToCol( subtab.m_cColor, selected ? MENU_TEXT : button.m_bHovered ? SELECTED_ELEMENT : MENU_TEXT_DIM );

			if ( !selected && button.m_bHovered )
				Render::FilledRoundedBox( button.m_vecPos, button.m_vecSize, 5, 5, MENU_CONTROL.Set<COLOR_A>( 110 ) );

			Render::Text( Fonts::MenuBold, Vector2D( button.m_vecPos.x + button.m_vecSize.x / 2.f, button.m_vecPos.y + button.m_vecSize.y / 2.f - 7.f ), subtab.m_cColor, FONT_CENTER, subtab.m_szName );
		}
	}

	if ( !newActiveTab.m_pSelectedSubtab )
		return;

	// page transition: a new tab / subtab fades in and settles from slightly below
	static const CMenuSubtab* shownPage{ };
	static float pageProgress{ 1.f };
	if ( shownPage != newActiveTab.m_pSelectedSubtab ) {
		shownPage = newActiveTab.m_pSelectedSubtab;
		pageProgress = 0.f;
	}
	pageProgress = Anim::Step( pageProgress, true, Anim::PageTime );
	const float pageEase{ Anim::EaseOut( pageProgress ) };

	m_vecDrawPos = m_vecPos + Vector2D( BAR_SIZE + MARGIN, HEADER_SIZE + MARGIN + ( 1.f - pageEase ) * 12.f );

	const float backupAlpha{ Render::GlobalAlpha };
	Render::GlobalAlpha = backupAlpha * ( 0.15f + 0.85f * pageEase );

	if ( newActiveTab.m_pSelectedSubtab->m_vecGroups.size( ) ) {
		if ( backupSubtab != newActiveTab.m_pSelectedSubtab ) {
			for ( auto& group : newActiveTab.m_pSelectedSubtab->m_vecGroups )
				group.Reset( );
		}

		int i{ };
		for ( auto& group : newActiveTab.m_pSelectedSubtab->m_vecGroups ) {
			i++;
			group.Render( i, newActiveTab.m_pSelectedSubtab->m_vecGroups );
		}
	}

	Render::GlobalAlpha = backupAlpha;

	// dim the content behind an open popup
	if ( Menu::m_pFocusItem.m_flFocusAnim > 0.01f )
		Render::FilledRectangle( m_vecPos + Vector2D( BAR_SIZE + 1, HEADER_SIZE + 1 ), m_vecSize - Vector2D( BAR_SIZE + 2, HEADER_SIZE + 2 ),
			BACKGROUND.Set<COLOR_A>( Menu::m_pFocusItem.m_flFocusAnim * 0.55f * 255.f ) );

	if ( Menu::m_pFocusItem.m_pItem ) {
		auto& element{ *Menu::m_pFocusItem.m_pItem };
		Menu::m_pFocusItem.m_flFocusAnim = Math::Interpolate( Menu::m_pFocusItem.m_flFocusAnim, m_pFocusItem.m_bFadeOut ? 0.f : 1.f, ANIMATION_SPEED );

		element.RenderFocus( );
	}
	else
		Menu::m_pFocusItem.m_flFocusAnim = Math::Interpolate( Menu::m_pFocusItem.m_flFocusAnim, 0.f, ANIMATION_SPEED );

	if ( Menu::m_pFocusItem.m_flFocusAnim <= 0.01f )
		Menu::m_pFocusItem.m_pItem = nullptr;

	DrawSearch( );
}
