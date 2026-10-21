#include "antiaim.h"
#include "../animations/animation.h"
#include "../rage/exploits.h"
#include "../../core/lua/lua_engine.h"
#include <cfloat>
#include <utility>
// TODO: antiresolver!
// check if we are able to be resolved from animlayers, and balance adjust then just flip desync
// also force balance adjust to be triggered when stopping or randomly when we are standing

void CAntiAim::Pitch( CUserCmd& cmd ) {
	if ( Condition( cmd, true ) )
		return;

	if ( Interfaces::ClientState->nChokedCommands < 1 && DesyncActive( ) )
		ctx.m_bSendPacket = false;

	if ( IsCustom( ) ) {
		CustomPitch( cmd );
		return;
	}

	if ( IsLua( ) ) {
		LuaPitch( cmd );
		return;
	}

	const auto cond{ ctx.m_bSafeFromDefensive && Features::Exploits.m_bWasDefensiveTick ? Config::Get<int>( Vars.AntiaimSafePitch ) : Config::Get<int>( Vars.AntiaimPitch ) };

	switch ( cond ) {
	case 1: cmd.viewAngles.x = -89.f; break;// up
	case 2: cmd.viewAngles.x =  89.f; break;// down
	case 3: cmd.viewAngles.x =  0.f;  break;// zero
	case 4: cmd.viewAngles.x = Math::RandomFloat( -89.f, 89 );  break;// random
	default: break;
	}
}

void CAntiAim::PickYaw( float& yaw ) {
	static bool invert{ };
	static int rotatedYaw{ };

	const int& yawRange{ Config::Get<int>( Vars.AntiaimYawRange ) };

	switch ( Config::Get<int>( Vars.AntiaimYaw ) )
	{
	case 0: yaw += 0.f; break;	// forward
	case 1: yaw += 180.f; break;// backward
	case 2: yaw += 90.f; break;	// left
	case 3: yaw -= 90.f; break;	// right
	}

	switch ( Config::Get<int>( Vars.AntiaimYawAdd ) )
	{
	case 1:// jitter 
		yaw += yawRange * ( ChokeCycleJitter ? 0.5f : -0.5f );
		break;
	case 2: // rotate
		{
		rotatedYaw -= invert ? Config::Get<int>( Vars.AntiaimYawSpeed ) : -Config::Get<int>( Vars.AntiaimYawSpeed );

		if ( rotatedYaw < yawRange * -0.5f )
			invert = false;
		else if ( rotatedYaw > yawRange * 0.5f )
			invert = true;

		rotatedYaw = std::clamp<int>( rotatedYaw, yawRange * -0.5f, yawRange * 0.5f );
		
		yaw += rotatedYaw;
	}break;
	case 3: // spin
	{
		rotatedYaw += Config::Get<int>( Vars.AntiaimYawSpeed );
		rotatedYaw = std::remainderf( rotatedYaw, 360.f );
		yaw = rotatedYaw;
		break;
	}
	case 4: // random
	{
		yaw += Math::RandomFloat( -yawRange / 2.f, yawRange / 2.f );
		break;
	}
	default: break;
	}

	if ( ctx.m_bSafeFromDefensive && Features::Exploits.m_bWasDefensiveTick )
	{
		const auto amount{ Config::Get<int>( Vars.AntiaimSafeYawRandomisation ) / 100.f };
		yaw += Math::RandomFloat( -180 * amount, 180 * amount );
	}
}

int CAntiAim::Freestanding( ) {
	if ( !Config::Get<int>( Vars.AntiaimFreestanding ) || !Config::Get<keybind_t>( Vars.AntiaimFreestandingKey ).enabled )
		return 0;

	return RawFreestanding( );
}

// 1 = right side is open, 2 = left side is open, 0 = neither
int CAntiAim::RawFreestanding( ) {
	Vector forward, right;
	Math::AngleVectors( ctx.m_angOriginalViewangles, &forward, &right );

	CGameTrace tr;

	// middle
	Interfaces::EngineTrace->TraceRay(
		{ ctx.m_vecEyePos, ctx.m_vecEyePos + forward * 100.0f }, MASK_PLAYERSOLID,
		nullptr, &tr
	);
	const auto middleDist{ ( tr.vecEnd - tr.vecStart ).Length( ) };

	// right
	Interfaces::EngineTrace->TraceRay(
		{ ctx.m_vecEyePos + right * 35.0f, ( ctx.m_vecEyePos + forward * 100.0f ) + right * 35.0f }, MASK_PLAYERSOLID,
		nullptr, &tr
	);
	const auto rightDist{  ( tr.vecEnd - tr.vecStart ).Length( ) };

	// left
	Interfaces::EngineTrace->TraceRay(
		{ ctx.m_vecEyePos - right * 35.0f, ( ctx.m_vecEyePos + forward * 100.0f ) - right * 35.0f }, MASK_PLAYERSOLID,
		nullptr, &tr
	);
	const auto leftDist{ ( tr.vecEnd - tr.vecStart ).Length( ) };

	if ( rightDist > leftDist + 20.f )
	{
		if ( rightDist > middleDist + 20.f )
			return 1;
	}

	if ( leftDist > rightDist + 20.f ) 
	{
		if ( leftDist > middleDist + 20.f )
			return 2;
	}

	return 0;
}

float CAntiAim::BaseYaw( CUserCmd& cmd ) {
	if ( IsLua( ) )
		return LuaBaseYaw( cmd );

	if ( IsCustom( ) )
		return CustomBaseYaw( cmd );

	m_bAntiBackstab = false;
	ChokeCycleJitter = !ChokeCycleJitter;

	if ( Config::Get<bool>( Vars.AntiaimConstantInvert ) )
		Invert = !Invert;
	else 
	{
		static auto old{ Config::Get<keybind_t>( Vars.AntiaimInvert ).enabled };
		if ( old != Config::Get<keybind_t>( Vars.AntiaimInvert ).enabled )
			Invert = old = Config::Get<keybind_t>( Vars.AntiaimInvert ).enabled;
	}

	auto yaw = cmd.viewAngles.y;

	const auto side{ Freestanding( ) };

	if ( Config::Get<bool>( Vars.AntiAimManualDir ) )
	{
		if ( ManualSide == 1 ) {
			yaw += 90.f;
			return yaw;
		}
		else if ( ManualSide == 2 ) {
			yaw -= 90.f;
			return yaw;
		}
	}

	if ( side && Config::Get<int>( Vars.AntiaimFreestanding ) == 1 )
	{
		if ( side == 1 )
		{
			yaw += 90.f;
			return yaw;
		}
		else if ( side == 2 )
		{
			yaw -= 90.f;
			return yaw;
		}
	}
	else if ( Config::Get<int>( Vars.AntiaimFreestanding ) == 2 || Config::Get<int>( Vars.AntiaimFreestanding ) == 3 )
	{
		if ( side == 1 )
			Invert = Config::Get<int>( Vars.AntiaimFreestanding ) == 2;
		else if ( side == 2 )
			Invert = Config::Get<int>( Vars.AntiaimFreestanding ) != 2;
	}

	AtTarget( yaw );

	if ( m_bFlickNow )
	{
		if ( Config::Get<bool>( Vars.AntiaimConstantInvertFlick ) )
			m_bInvertFlick = !m_bInvertFlick;
		else
			m_bInvertFlick = Config::Get<keybind_t>( Vars.AntiaimFlickInvert ).enabled;

		yaw += ( m_bInvertFlick ? -Config::Get<int>( Vars.AntiaimFlickAdd ) : Config::Get<int>( Vars.AntiaimFlickAdd ) );
	}

	if ( m_bAntiBackstab )
		return yaw;

	PickYaw( yaw );
	return yaw;
}

void CAntiAim::AtTarget( float& yaw ) {
	CBasePlayer* bestPlayer{ nullptr };
	auto bestValue = INT_MAX;

	if ( !Config::Get<int>( Vars.AntiaimAtTargets ) && !Config::Get<bool>( Vars.AntiaimAntiBackStab ) )
		return;

	for ( auto i = 1; i < 64; ++i ) 
	{
		const auto player{ static_cast< CBasePlayer* >( Interfaces::ClientEntityList->GetClientEntity( i ) ) };
		if ( !player
			|| !player->IsPlayer( )
			|| player->Dormant( )
			|| player->IsDead( )
			|| player->IsTeammate( ) )
			continue;

		const auto dist{ ( ctx.m_pLocal->m_vecOrigin( ) - player->m_vecOrigin( ) ).Length( ) };
		if ( Config::Get<bool>( Vars.AntiaimAntiBackStab ) )
		{
			if ( player->GetWeapon( )->IsKnife( ) )
			{
				if ( dist < 250 )
				{
					bestPlayer = player;
					m_bAntiBackstab = true;
					break;
				}
			}
		}

		switch ( Config::Get<int>( Vars.AntiaimAtTargets ) )
		{
		case 1:  // FOV/closest to crosshair
		{
			const auto fov = Math::GetFov( ctx.m_angOriginalViewangles, Math::CalcAngle( ctx.m_vecEyePos, player->GetAbsOrigin( ) ) );
			if ( fov > bestValue )
				continue;

			bestValue = fov;
			bestPlayer = player;
		}break;
		case 2: // Distance
		{
			if ( dist > bestValue )
				continue;

			bestValue = dist;
			bestPlayer = player;
		} break;
		default: break;
		}
	}

	if ( !bestPlayer )
		return;

	const auto x{ bestPlayer->m_vecOrigin( ).x - ctx.m_pLocal->m_vecOrigin( ).x };
	const auto y{ bestPlayer->m_vecOrigin( ).y - ctx.m_pLocal->m_vecOrigin( ).y };

	yaw = x == 0.f && y == 0.f ? 0.f : RAD2DEG( std::atan2( y, x ) );
}

bool CAntiAim::Condition( CUserCmd& cmd, bool checkCmd ) {
	if ( !Config::Get<bool>( Vars.AntiaimEnable ) )
		return true;

	if ( ctx.m_pLocal->m_MoveType( ) == MOVETYPE_NOCLIP || ctx.m_pLocal->m_MoveType( ) == MOVETYPE_LADDER )
		return true;

	if ( !ctx.m_pWeapon )
		return true;

	if ( Interfaces::GameRules && Interfaces::GameRules->IsFreezeTime( ) )
		return true;

	if ( ctx.m_pWeapon->IsGrenade( ) && ctx.m_pWeapon->m_fThrowTime( ) )
		return true;

	if ( !checkCmd )
		return false;

	if ( cmd.iButtons & IN_ATTACK && !ctx.m_pWeapon->IsGrenade( )
		&& ( ctx.m_pWeapon->m_iItemDefinitionIndex( ) != WEAPON_REVOLVER || ctx.m_bRevolverCanShoot ) )
		return true;

	if ( cmd.iButtons & IN_USE && !OnUseAllowed( cmd ) )
		return true;

	if ( cmd.iButtons & IN_ATTACK2 && ctx.m_pWeapon->IsKnife( ) )
		return true;

	return false;
}

void CAntiAim::FakeLag( int cmdNum ) {
	if ( Interfaces::GameRules && Interfaces::GameRules->IsFreezeTime( ) ) {
		ctx.m_bSendPacket = true;
		return;
	}

	static int maxChoke = Config::Get<int>( Vars.AntiaimFakeLagLimit );

	const int& max = Config::Get<int>( Vars.AntiaimFakeLagLimit );

	if ( !Interfaces::ClientState->nChokedCommands )
		maxChoke = Math::RandomInt( static_cast< int >( max * ( 1.f - ( static_cast< float >( Config::Get<int>( Vars.AntiaimFakeLagVariance ) ) / 100.f ) ) ), max );

	if ( DesyncActive( ) )
		maxChoke = std::max( maxChoke, 1 );

	// gamesense "Fake lag" references
	if ( IsLua( ) ) {
		if ( !m_cLua.m_bFakeLag )
			maxChoke = DesyncActive( ) ? 1 : 0;
		else if ( m_cLua.m_iFakeLagAmount == 1 )
			maxChoke = std::max( max, DesyncActive( ) ? 1 : 0 );
		else if ( m_cLua.m_iFakeLagAmount == 2 && !Interfaces::ClientState->nChokedCommands ) {
			m_bLuaFluctuate = !m_bLuaFluctuate;
			maxChoke = m_bLuaFluctuate ? max : std::max( max / 2, DesyncActive( ) ? 1 : 0 );
		}
	}

	const auto& localData = ctx.m_cLocalData.at( Interfaces::ClientState->iLastOutgoingCommand % 150 );

	if ( Interfaces::ClientState->nChokedCommands >= maxChoke )
		ctx.m_bSendPacket = true;

	if ( Config::Get<bool>( Vars.AntiaimFakeLagBreakLC )
		&& ( ctx.m_pLocal->m_vecOrigin( ) - localData.PredictedNetvars.m_vecOrigin ).LengthSqr( ) > 4096.f )
		ctx.m_bSendPacket = true;

	if ( Config::Get<bool>( Vars.AntiaimFakeLagInPeek ) ) {
		
		if ( cmdNum - ctx.m_iLastPeekCmdNum < 15 && ctx.m_bInPeek ) {
			ctx.m_bSendPacket = false;
			//return;
		}
		else if ( ctx.m_bInPeek )
			ctx.m_bSendPacket = true;
	}

	if ( m_bFlickNow )
		m_bFlickNow = false;

	if ( !IsCustom( ) && !IsLua( ) && Config::Get<bool>( Vars.AntiaimFlickHead ) && !ctx.m_bFakeDucking )
	{
		if ( ++m_iFlickTimer > Config::Get<int>( Vars.AntiaimFlickSpeed ) )
		{
			m_bFlickNow = ctx.m_bSendPacket = true;
			m_iFlickTimer = 0;
		}
	}
}

void CAntiAim::RunLocalModifications( CUserCmd& cmd, int tickbase ) {

	const auto animstate{ ctx.m_pLocal->m_pAnimState( ) };
	const auto totalCmds{ Interfaces::ClientState->nChokedCommands + 1 };
	if ( totalCmds < 1 || !animstate )
		return;

	// state detection runs in both modes ( the crosshair indicator shows it ), the builder only steps in custom
	m_iRawState = DetectRawState( cmd );
	if ( IsCustom( ) )
		AdvanceCustomCycle( cmd );
	else if ( IsLua( ) )
		AdvanceLuaCycle( cmd );

	auto yaw{ std::remainderf( BaseYaw( cmd ), 360.f ) };

	if ( !IsCustom( ) && !IsLua( ) && ctx.m_bSafeFromDefensive && Features::Exploits.m_bWasDefensiveTick )
	{
		const auto amount{ Config::Get<int>( Vars.AntiaimSafeYawRandomisation ) / 100.f };
		yaw += Math::RandomFloat( -180 * amount, 180 * amount );
	}

	const auto inShot{ ctx.m_iLastShotNumber > Interfaces::ClientState->iLastOutgoingCommand && ctx.m_iLastShotNumber <= ( Interfaces::ClientState->iLastOutgoingCommand + Interfaces::ClientState->nChokedCommands + 1 ) };

	if ( animstate->bFirstUpdate )
		ctx.m_cFakeData.init = false;

	const auto backupState{ *animstate };
	CAnimationLayer backupLayers[ 13 ]{ };
	std::memcpy( backupLayers, ctx.m_pLocal->m_AnimationLayers( ), 13 * sizeof CAnimationLayer );

	bool did{ };
	const auto oldLBY{ ctx.m_pLocal->m_flLowerBodyYawTarget( ) };

	QAngle* holdAngle{ };

	if ( !IsCustom( ) && !IsLua( ) )
	{
		// avoid overlap
		if ( Config::Get<bool>( Vars.AntiaimTrickLBY ) )
			Invert = Math::AngleDiff( oldLBY, yaw ) <= 0;

		// use brain: this can cause avoid overlap to be wrong... what do i do here...
		if ( Invert )
			yaw += Config::Get<int>( Vars.AntiaimInvertedYawAdd );
		else
			yaw += Config::Get<int>( Vars.AntiaimNormalYawAdd );
	}

	for ( auto i{ 1 }; i <= totalCmds; ++i )
	{
		const auto j{ ( Interfaces::ClientState->iLastOutgoingCommand + i ) % 150 };

		auto& curUserCmd{ Interfaces::Input->pCommands[ j ] };
		auto& curLocalData{ ctx.m_cLocalData.at( j ) };

		const auto lastCmd{ i == totalCmds };

		if ( curLocalData.m_flSpawnTime != ctx.m_pLocal->m_flSpawnTime( ) )
			continue;

		if ( curUserCmd.iTickCount == INT_MAX )
			continue;

		if ( curLocalData.PredictedNetvars.m_MoveType != MOVETYPE_LADDER && curLocalData.m_MoveType != MOVETYPE_LADDER )
		{
			if ( curLocalData.m_bCanAA )
			{
				const auto oldViewAngles{ curUserCmd.viewAngles };

				curUserCmd.viewAngles.y = yaw;

				if ( !lastCmd && !inShot && DesyncActive( ) )
					curUserCmd.viewAngles.y += ( Invert ? -DesyncAmount( ) : DesyncAmount( ) );

				if ( IsLua( ) && m_cLua.m_iRoll )
					curUserCmd.viewAngles.z = static_cast< float >( m_cLua.m_iRoll );

				Features::Misc.MoveMINTFix(
					curUserCmd, oldViewAngles,
					curLocalData.PredictedNetvars.m_iFlags,
					curLocalData.PredictedNetvars.m_MoveType
				);

			}

			Features::Misc.NormalizeMovement( curUserCmd );
		}

		if ( lastCmd )
		{
			if ( ctx.m_pLocal->m_MoveType( ) == MOVETYPE_WALK )
			{
				cmd.iButtons &= ~( IN_FORWARD | IN_BACK | IN_MOVERIGHT | IN_MOVELEFT );

				if ( cmd.flForwardMove != 0.f )
					cmd.iButtons |=
					( Config::Get<bool>( Vars.MiscSlideWalk ) ? cmd.flForwardMove < 0.f : cmd.flForwardMove > 0.f )
					? IN_FORWARD : IN_BACK;

				if ( cmd.flSideMove )
				{
					cmd.iButtons |=
						( Config::Get<bool>( Vars.MiscSlideWalk ) ? cmd.flSideMove < 0.f : cmd.flSideMove > 0.f )
						? IN_MOVERIGHT : IN_MOVELEFT;
				}
			}

			Pitch( curUserCmd );
		}

		if ( curUserCmd.iButtons & IN_ATTACK )
			holdAngle = &curUserCmd.viewAngles;

		ctx.m_pLocal->m_nTickBase( ) = tickbase - ( totalCmds - i );
		ctx.m_pLocal->m_fFlags( ) = curLocalData.PredictedNetvars.m_iFlags;
		ctx.m_pLocal->m_vecAbsVelocity( ) = curLocalData.PredictedNetvars.m_vecVelocity;
		ctx.m_pLocal->m_flDuckAmount( ) = curLocalData.PredictedNetvars.m_flDuckAmount;

		Features::AnimSys.UpdateLocal( holdAngle ? *holdAngle : curUserCmd.viewAngles, lastCmd, curUserCmd );

		Interfaces::Input->pVerifiedCommands[ j ].userCmd = curUserCmd;
		Interfaces::Input->pVerifiedCommands[ j ].uHashCRC = curUserCmd.GetChecksum( );
	}

	ctx.m_pLocal->m_AnimationLayers( )[ 3 ].flWeight = ctx.m_pAnimationLayers[ 3 ].flWeight;
	ctx.m_pLocal->m_AnimationLayers( )[ 3 ].flCycle = ctx.m_pAnimationLayers[ 3 ].flCycle;

	std::memcpy( ctx.m_pAnimationLayers, ctx.m_pLocal->m_AnimationLayers( ), 13 * sizeof CAnimationLayer );

	if ( !Config::Get<bool>( Vars.VisRecordAnims ) || !ctx.m_bSafeFromDefensive )
	{
		ctx.m_pLocal->SetAbsAngles( { 0.f, animstate->flAbsYaw, 0.f } );

		static auto lookupBone{ *reinterpret_cast< int( __thiscall* )( void*, const char* ) >( Displacement::Sigs.LookupBone ) };
		const auto boneIndex{ lookupBone( ctx.m_pLocal, _( "lean_root" ) ) };

		if ( ctx.m_pLocal->m_pStudioHdr( )->vecBoneFlags[ boneIndex ] != BONE_USED_BY_SERVER )
			ctx.m_pLocal->m_pStudioHdr( )->vecBoneFlags[ boneIndex ] = BONE_USED_BY_SERVER;

		const auto backup12Weight{ ctx.m_pLocal->m_AnimationLayers( )[ 12 ].flWeight };
		const auto backup3Weight{ ctx.m_pLocal->m_AnimationLayers( )[ 3 ].flWeight };
		const auto backup3Cycle{ ctx.m_pLocal->m_AnimationLayers( )[ 3 ].flCycle };

		ctx.m_pLocal->m_AnimationLayers( )[ 12 ].flWeight *= float( Config::Get<int>( Vars.MiscLocalAnimSway ) / 10.f );
		ctx.m_pLocal->m_AnimationLayers( )[ 3 ].flCycle = 0.f;
		ctx.m_pLocal->m_AnimationLayers( )[ 3 ].flWeight = 0.f;

		if ( Config::Get<bool>( Vars.ExploitsBreakCBB ) && !ctx.m_iTicksAllowed )
		{
			QAngle angle{ 0, 0, 0 };
			Interfaces::Prediction->SetLocalViewAngles( angle );
		}

		// lua animation breakers ( poses / layers written in pre_render or with entity.set_prop )
		Lua::ApplyLocalAnimOverrides( ctx.m_pLocal );

		Features::AnimSys.SetupBonesRebuilt( ctx.m_pLocal, ctx.m_matRealLocalBones, BONE_USED_BY_SERVER,
			Interfaces::Globals->flCurTime, true );

		Lua::RestoreLocalAnimOverrides( ctx.m_pLocal );

		std::memcpy( ctx.m_pLocal->m_CachedBoneData( ).Base( ), ctx.m_matRealLocalBones, ctx.m_pLocal->m_CachedBoneData().Size() * sizeof( matrix3x4a_t ) );

		ctx.m_pLocal->m_AnimationLayers( )[ 12 ].flWeight = backup12Weight;
		ctx.m_pLocal->m_AnimationLayers( )[ 3 ].flCycle = backup3Cycle;
		ctx.m_pLocal->m_AnimationLayers( )[ 3 ].flWeight = backup3Weight;

		ctx.m_vecSetupBonesOrigin = ctx.m_pLocal->GetAbsOrigin( );
	}

	/* fake matrix */
	if ( !Config::Get<bool>( Vars.ChamDesync ) )
		return;

	const auto backupState2{ *animstate };

	if ( ctx.m_cFakeData.init )
		*animstate = ctx.m_cFakeData.m_sState;
	else
		*animstate = backupState;

	std::memcpy( ctx.m_pLocal->m_AnimationLayers( ), backupLayers, 13 * sizeof CAnimationLayer );

	for ( auto i{ 1 }; i <= totalCmds; ++i ) {
		const auto j{ ( Interfaces::ClientState->iLastOutgoingCommand + i ) % 150 };

		auto& curUserCmd{ Interfaces::Input->pCommands[ j ] };
		auto& curLocalData{ ctx.m_cLocalData.at( j ) };

		ctx.m_pLocal->m_nTickBase( ) = tickbase - ( totalCmds - i );
		ctx.m_pLocal->m_fFlags( ) = curLocalData.PredictedNetvars.m_iFlags;
		ctx.m_pLocal->m_vecAbsVelocity( ) = curLocalData.PredictedNetvars.m_vecVelocity;

		Features::AnimSys.UpdateLocal( holdAngle ? *holdAngle : cmd.viewAngles, true, curUserCmd );
	}

	if ( !Config::Get<bool>( Vars.VisRecordAnims ) || !ctx.m_bSafeFromDefensive ) {

		ctx.m_pLocal->SetAbsAngles( { 0.f, animstate->flAbsYaw, 0.f } );

		// enemy gets the real layers networked to them
		std::memcpy( ctx.m_pLocal->m_AnimationLayers( ), ctx.m_pAnimationLayers, 13 * sizeof CAnimationLayer );

		//const auto backupWeight{ ctx.m_pLocal->m_AnimationLayers( )[ 12 ].flWeight };
		//ctx.m_pLocal->m_AnimationLayers( )[ 12 ].flWeight *= 2.f;

		//ctx.m_pLocal->m_flNewBoundsTime( ) = 0;

		//Features::Logger.Log( std::to_string( cmd.viewAngles.y ), false );

		Features::AnimSys.SetupBonesRebuilt( ctx.m_pLocal, ctx.m_cFakeData.m_matMatrix, BONE_USED_BY_SERVER,
			Interfaces::Globals->flCurTime, true );

		//ctx.m_pLocal->m_flNewBoundsTime( ) = backupCBBTime;
		//ctx.m_pLocal->m_AnimationLayers( )[ 12 ].flWeight = backupWeight;

		ctx.m_cFakeData.m_sState = *animstate;
		ctx.m_cFakeData.init = true;
		*animstate = backupState2;

		std::memcpy( ctx.m_pLocal->m_CachedBoneData( ).Base( ), ctx.m_matRealLocalBones, ctx.m_pLocal->m_CachedBoneData( ).Size( ) * sizeof( matrix3x4_t ) );
	}
}

/* ------------------------------------------------------------------ */
/* custom anti-aim ( momentum builder )                                */
/* ------------------------------------------------------------------ */

namespace {
	constexpr const char* STATE_NAMES[ AA_STATE_COUNT ]{
		"DEFAULT", "STAND", "MOVE", "SLOW", "DUCK", "AIR", "AIR+C", "FAKELAG", "DEFENSIVE"
	};

	enum EYawMode : int {
		YAW_DEFAULT = 0,
		YAW_3WAY,
		YAW_EXTRAPOLATED,
		YAW_SKITTER,
		YAW_SYNCED_BODY,
		YAW_SYNCED_RANDOM,
		YAW_DELAYED_JITTER
	};

	enum EBodyMode : int {
		BODY_STATIC = 0,
		BODY_JITTER,
		BODY_OPPOSITE,
		BODY_OFF
	};

	int RandomBetween( int a, int b ) {
		if ( a > b )
			std::swap( a, b );
		return a == b ? a : Math::RandomInt( a, b );
	}
}

bool CAntiAim::IsCustom( ) const {
	return Config::Get<int>( Vars.AntiaimMode ) == 1;
}

bool CAntiAim::DesyncActive( ) const {
	if ( IsLua( ) )
		return m_bLuaDesync;

	return IsCustom( ) ? m_bCustomDesync : Config::Get<bool>( Vars.AntiaimDesync );
}

const char* CAntiAim::StateName( ) const {
	return STATE_NAMES[ std::clamp( m_iRawState, 0, AA_STATE_COUNT - 1 ) ];
}

bool CAntiAim::DefensiveTick( ) const {
	return ctx.m_bSafeFromDefensive && Features::Exploits.m_bWasDefensiveTick;
}

bool CAntiAim::OnUseAllowed( CUserCmd& cmd ) const {
	if ( !IsCustom( ) || !Config::Get<bool>( Vars.AACOnUse ) || !( cmd.iButtons & IN_USE ) )
		return false;

	if ( !ctx.m_pLocal || !ctx.m_pWeapon )
		return false;

	// planting has to look at the ground normally
	if ( ctx.m_pWeapon->m_iItemDefinitionIndex( ) == WEAPON_C4 )
		return false;

	// defusing / rescuing needs the real view direction
	const auto& origin{ ctx.m_pLocal->m_vecOrigin( ) };
	const auto highest{ Interfaces::ClientEntityList->GetHighestEntityIndex( ) };
	for ( int i{ 65 }; i <= highest; ++i ) {
		const auto ent{ static_cast< CBaseEntity* >( Interfaces::ClientEntityList->GetClientEntity( i ) ) };
		if ( !ent || ent->Dormant( ) )
			continue;

		const auto clientClass{ ent->GetClientClass( ) };
		if ( !clientClass )
			continue;

		if ( clientClass->nClassID != EClassIndex::CPlantedC4 && clientClass->nClassID != EClassIndex::CHostage )
			continue;

		if ( ( ent->GetAbsOrigin( ) - origin ).LengthSqr( ) < 100.f * 100.f )
			return false;
	}

	return true;
}

int CAntiAim::DetectRawState( CUserCmd& cmd ) const {
	if ( DefensiveTick( ) )
		return AA_STATE_DEFENSIVE;

	const auto doubletap{ Config::Get<bool>( Vars.ExploitsDoubletap ) && Config::Get<keybind_t>( Vars.ExploitsDoubletapKey ).enabled };
	const auto hideshots{ Config::Get<bool>( Vars.ExploitsHideshots ) && Config::Get<keybind_t>( Vars.ExploitsHideshotsKey ).enabled };

	const auto& data{ ctx.m_cLocalData.at( cmd.iCommandNumber % 150 ).PredictedNetvars };
	const auto onGround{ ( data.m_iFlags & FL_ONGROUND ) != 0 && !( cmd.iButtons & IN_JUMP ) };
	const auto ducking{ ctx.m_bFakeDucking || data.m_flDuckAmount > 0.5f || ( cmd.iButtons & IN_DUCK ) };
	const auto speed{ data.m_vecVelocity.Length2D( ) };

	if ( !doubletap && !hideshots && Config::Get<int>( Vars.AntiaimFakeLagLimit ) > 1 && !ctx.m_bFakeDucking )
		return AA_STATE_FAKELAG;

	if ( !onGround )
		return ducking ? AA_STATE_AIRDUCK : AA_STATE_AIR;

	if ( ducking )
		return AA_STATE_DUCK;

	if ( speed > 5.f ) {
		if ( Config::Get<bool>( Vars.MiscSlowWalk ) && Config::Get<keybind_t>( Vars.MiscSlowWalkKey ).enabled )
			return AA_STATE_SLOW;

		return AA_STATE_MOVE;
	}

	return AA_STATE_STAND;
}

int CAntiAim::ResolveState( int raw ) const {
	static const auto enabled{ AA_STATE_IDS( AACEnable ) };

	if ( raw > AA_STATE_DEFAULT && raw < AA_STATE_COUNT && Config::Get<bool>( enabled[ raw ] ) )
		return raw;

	// defensive / fakelag states fall through to the movement state underneath them
	if ( raw == AA_STATE_DEFENSIVE || raw == AA_STATE_FAKELAG ) {
		const auto& data{ ctx.m_cLocalData.at( Interfaces::ClientState->iLastOutgoingCommand % 150 ).PredictedNetvars };
		int movement{ AA_STATE_STAND };
		if ( !( data.m_iFlags & FL_ONGROUND ) )
			movement = data.m_flDuckAmount > 0.5f ? AA_STATE_AIRDUCK : AA_STATE_AIR;
		else if ( data.m_flDuckAmount > 0.5f || ctx.m_bFakeDucking )
			movement = AA_STATE_DUCK;
		else if ( data.m_vecVelocity.Length2D( ) > 5.f )
			movement = Config::Get<bool>( Vars.MiscSlowWalk ) && Config::Get<keybind_t>( Vars.MiscSlowWalkKey ).enabled ? AA_STATE_SLOW : AA_STATE_MOVE;

		if ( Config::Get<bool>( enabled[ movement ] ) )
			return movement;
	}

	return AA_STATE_DEFAULT;
}

void CAntiAim::AdvanceCustomCycle( CUserCmd& cmd ) {
	m_iCustomState = ResolveState( m_iRawState );
	m_bOnUse = OnUseAllowed( cmd );

	const auto yawMode{ StateVar<int>( AA_STATE_IDS( AACYawMode ) ) };
	const auto bodyMode{ StateVar<int>( AA_STATE_IDS( AACBody ) ) };
	const auto left{ static_cast< float >( StateVar<int>( AA_STATE_IDS( AACYawLeft ) ) ) };
	const auto right{ static_cast< float >( StateVar<int>( AA_STATE_IDS( AACYawRight ) ) ) };

	ChokeCycleJitter = !ChokeCycleJitter;

	/* yaw offset ( modes that sync to the body side are finished after the body step ) */
	switch ( yawMode ) {
	case YAW_DEFAULT:
	{
		m_bJitterSide = !m_bJitterSide;
		const auto center{ static_cast< float >( StateVar<int>( AA_STATE_IDS( AACYawCenter ) ) ) };
		m_flCustomOffset = center * ( m_bJitterSide ? 0.5f : -0.5f );
	} break;
	case YAW_3WAY:
	{
		m_iWayIndex = ( m_iWayIndex + 1 ) % 3;
		const int ways[ 3 ]{
			StateVar<int>( AA_STATE_IDS( AACWay1 ) ),
			StateVar<int>( AA_STATE_IDS( AACWay2 ) ),
			StateVar<int>( AA_STATE_IDS( AACWay3 ) ) };
		m_flCustomOffset = static_cast< float >( ways[ m_iWayIndex ] );
	} break;
	case YAW_EXTRAPOLATED:
		m_bJitterSide = Math::RandomInt( 0, 1 ) == 1;
		m_flCustomOffset = m_bJitterSide ? left : right;
		break;
	case YAW_SKITTER:
	{
		m_iSkitterIndex = ( m_iSkitterIndex + 1 ) % 3;
		const float steps[ 3 ]{ left, 0.f, right };
		m_flCustomOffset = steps[ m_iSkitterIndex ];
	} break;
	case YAW_DELAYED_JITTER:
		if ( ++m_iDelayCounter >= m_iDelayTarget ) {
			m_iDelayCounter = 0;
			m_bDelaySide = !m_bDelaySide;
			m_iDelayTarget = std::max( 1, StateVar<int>( AA_STATE_IDS( AACJitterDelay ) )
				+ RandomBetween( 0, std::max( 0, StateVar<int>( AA_STATE_IDS( AACJitterRandom ) ) ) ) );
		}
		m_flCustomOffset = m_bDelaySide ? left : right;
		break;
	default: break;
	}

	/* body yaw ( desync side ) */
	m_bCustomDesync = bodyMode != BODY_OFF;

	switch ( bodyMode ) {
	case BODY_STATIC:
		Invert = StateVar<int>( AA_STATE_IDS( AACBodySide ) ) == 1;
		break;
	case BODY_JITTER:
		if ( yawMode == YAW_DELAYED_JITTER ) {
			// desync follows the delayed jitter so head and body swap together
			Invert = !m_bDelaySide;
		}
		else {
			const auto speed{ StateVar<int>( AA_STATE_IDS( AACBodySpeed ) ) };
			if ( speed <= 0 )
				Invert = Math::RandomInt( 0, 1 ) == 1;
			else if ( ++m_iBodyCounter >= speed ) {
				m_iBodyCounter = 0;
				Invert = !Invert;
			}
		}
		break;
	case BODY_OPPOSITE:
		Invert = m_flCustomOffset > 0.f;
		break;
	default: break;
	}

	/* yaw modes synced to the body side */
	if ( yawMode == YAW_SYNCED_BODY )
		m_flCustomOffset = Invert ? left : right;
	else if ( yawMode == YAW_SYNCED_RANDOM ) {
		m_flCustomOffset = static_cast< float >( Invert
			? RandomBetween( StateVar<int>( AA_STATE_IDS( AACRandMinLeft ) ), StateVar<int>( AA_STATE_IDS( AACRandMaxLeft ) ) )
			: RandomBetween( StateVar<int>( AA_STATE_IDS( AACRandMinRight ) ), StateVar<int>( AA_STATE_IDS( AACRandMaxRight ) ) ) );
	}

	/* defensive angle stepping */
	if ( DefensiveTick( ) ) {
		m_iDefensiveSpin = ( m_iDefensiveSpin + 45 ) % 360;
		m_iDefensiveIndex = ( m_iDefensiveIndex + 1 ) % 5;
		m_bDefensiveSide = !m_bDefensiveSide;
	}

	/* safe head: knife / zeus while crouch-jumping keeps the head straight back with no desync */
	if ( Config::Get<bool>( Vars.AACSafeHead ) && m_iRawState == AA_STATE_AIRDUCK && ctx.m_pWeapon
		&& ( ctx.m_pWeapon->IsKnife( ) || ctx.m_pWeapon->m_iItemDefinitionIndex( ) == WEAPON_TASER ) )
		m_bCustomDesync = false;
}

float CAntiAim::CustomBaseYaw( CUserCmd& cmd ) {
	m_bAntiBackstab = false;

	auto yaw{ cmd.viewAngles.y };

	// anti-aim on use: real faces the crosshair so doors / buttons still work, desync stays
	if ( m_bOnUse && cmd.iButtons & IN_USE )
		return yaw;

	const auto safeHead{ Config::Get<bool>( Vars.AACSafeHead ) && m_iRawState == AA_STATE_AIRDUCK && ctx.m_pWeapon
		&& ( ctx.m_pWeapon->IsKnife( ) || ctx.m_pWeapon->m_iItemDefinitionIndex( ) == WEAPON_TASER ) };

	if ( Config::Get<bool>( Vars.AntiAimManualDir ) && !safeHead ) {
		if ( ManualSide == 1 )
			return yaw + 90.f;
		if ( ManualSide == 2 )
			return yaw - 90.f;
	}

	if ( !safeHead && Config::Get<keybind_t>( Vars.AntiaimFreestandingKey ).enabled && !StateVar<bool>( AA_STATE_IDS( AACNoFreestand ) ) ) {
		const auto side{ RawFreestanding( ) };
		if ( side == 1 )
			return yaw + 90.f;
		if ( side == 2 )
			return yaw - 90.f;
	}

	AtTarget( yaw );

	if ( m_bAntiBackstab )
		return yaw;

	const auto base{ yaw };

	if ( safeHead )
		return base + 180.f;

	yaw = base + 180.f + m_flCustomOffset;

	if ( !DefensiveTick( ) )
		return yaw;

	switch ( Config::Get<int>( Vars.AACDefYaw ) ) {
	case 1: return base;                                                      // forward
	case 2: return base + 180.f + static_cast< float >( m_iDefensiveSpin );    // spin
	case 3: return base + ( m_bDefensiveSide ? 90.f : -90.f );                // sideways
	case 4: return base + 180.f + ( m_bDefensiveSide ? 45.f : -45.f );        // 90 degree jitter
	case 5: return base + 180.f - m_flCustomOffset;                           // opposite of the current offset
	case 6: return base + ( m_bDefensiveSide ? 0.f : 180.f );                 // opposite jitter
	case 7:                                                                     // 3-way
	{
		constexpr float ways[ 3 ]{ -90.f, 0.f, 90.f };
		return base + 180.f + ways[ m_iDefensiveIndex % 3 ];
	}
	case 8:                                                                     // 5-way
	{
		constexpr float ways[ 5 ]{ -90.f, -45.f, 0.f, 45.f, 90.f };
		return base + 180.f + ways[ m_iDefensiveIndex % 5 ];
	}
	case 9: return Math::RandomFloat( -180.f, 180.f );                       // random
	default: return yaw;
	}
}

// option: 1 zero, 2 up, 3 down, 4 random, 5 semi-up, 6 semi-down ( 0 keeps the fallback )
float CAntiAim::DefensivePitchValue( int option, float fallback ) const {
	switch ( option ) {
	case 1: return 0.f;
	case 2: return -89.f;
	case 3: return 89.f;
	case 4: return Math::RandomFloat( -89.f, 89.f );
	case 5: return -45.f;
	case 6: return 45.f;
	default: return fallback;
	}
}

// vertical angle to the closest visible-to-us enemy, negative = they are above us
float CAntiAim::ThreatPitch( ) const {
	float bestDist{ FLT_MAX };
	float pitch{ };

	for ( auto i{ 1 }; i < 64; ++i ) {
		const auto player{ static_cast< CBasePlayer* >( Interfaces::ClientEntityList->GetClientEntity( i ) ) };
		if ( !player || !player->IsPlayer( ) || player->Dormant( ) || player->IsDead( ) || player->IsTeammate( ) )
			continue;

		const auto dist{ ( player->m_vecOrigin( ) - ctx.m_pLocal->m_vecOrigin( ) ).LengthSqr( ) };
		if ( dist >= bestDist )
			continue;

		bestDist = dist;
		pitch = Math::CalcAngle( ctx.m_vecEyePos, player->m_vecOrigin( ) + Vector( 0.f, 0.f, 64.f ) ).x;
	}

	return pitch;
}

bool CAntiAim::CustomPitch( CUserCmd& cmd ) {
	// on use keeps the real view pitch
	if ( m_bOnUse && cmd.iButtons & IN_USE )
		return false;

	if ( DefensiveTick( ) && ( Config::Get<int>( Vars.AACDefPitch ) || Config::Get<bool>( Vars.AACDefHeightPitch ) ) ) {
		auto option{ Config::Get<int>( Vars.AACDefPitch ) };

		if ( Config::Get<bool>( Vars.AACDefHeightPitch ) ) {
			const auto threat{ ThreatPitch( ) };
			const auto threshold{ static_cast< float >( Config::Get<int>( Vars.AACDefHeightThreshold ) ) };

			if ( threat < -threshold && Config::Get<int>( Vars.AACDefPitchAbove ) )
				option = Config::Get<int>( Vars.AACDefPitchAbove );
			else if ( threat > threshold && Config::Get<int>( Vars.AACDefPitchBelow ) )
				option = Config::Get<int>( Vars.AACDefPitchBelow );
		}

		if ( option ) {
			cmd.viewAngles.x = DefensivePitchValue( option, cmd.viewAngles.x );
			return true;
		}
	}

	switch ( StateVar<int>( AA_STATE_IDS( AACPitch ) ) ) {
	case 1: cmd.viewAngles.x = 89.f; break;                                   // down
	case 2: cmd.viewAngles.x = -89.f; break;                                  // up
	case 3: cmd.viewAngles.x = Math::RandomFloat( -89.f, 89.f ); break;       // random
	default: return false;                                                     // disabled
	}

	return true;
}

/* ------------------------------------------------------------------ */
/* lua anti-aim ( gamesense "Anti-aimbot angles" references )          */
/* ------------------------------------------------------------------ */

bool CAntiAim::IsLua( ) const {
	return Config::Get<int>( Vars.AntiaimMode ) == 2;
}

float CAntiAim::DesyncAmount( ) const {
	return IsLua( ) ? m_flLuaDesync : 120.f;
}

void CAntiAim::AdvanceLuaCycle( CUserCmd& cmd ) {
	const auto& aa{ m_cLua };

	ChokeCycleJitter = !ChokeCycleJitter;
	m_bLuaJitterSide = !m_bLuaJitterSide;

	/* yaw jitter */
	const auto jitter{ static_cast< float >( aa.m_iJitterValue ) };
	switch ( aa.m_iJitter ) {
	case 1: // offset: alternate between the base and base + value
		m_flLuaOffset = m_bLuaJitterSide ? jitter : 0.f;
		break;
	case 2: // center: alternate around the base
		m_flLuaOffset = m_bLuaJitterSide ? jitter * 0.5f : -jitter * 0.5f;
		break;
	case 3: // random inside the range
		m_flLuaOffset = Math::RandomFloat( -std::abs( jitter ) * 0.5f, std::abs( jitter ) * 0.5f );
		break;
	case 4: { // skitter: -value, 0, +value
		m_iLuaSkitter = ( m_iLuaSkitter + 1 ) % 3;
		const float steps[ 3 ]{ -jitter, 0.f, jitter };
		m_flLuaOffset = steps[ m_iLuaSkitter ];
	} break;
	default:
		m_flLuaOffset = 0.f;
		break;
	}

	/* body yaw: 60 on the gamesense slider is a full side, the choke offset scales with it */
	const auto bodyValue{ static_cast< float >( aa.m_iBodyValue ) };
	const auto scale{ std::clamp( std::abs( bodyValue ) / 60.f, 0.f, 1.f ) };

	m_flLuaDesync = 120.f * ( aa.m_iBody == 3 || bodyValue != 0.f ? scale : 1.f );
	m_bLuaDesync = aa.m_iBody != 0 && m_flLuaDesync >= 1.f;

	switch ( aa.m_iBody ) {
	case 1: { // opposite: body away from the open side, otherwise away from the jitter
		const auto side{ RawFreestanding( ) };
		Invert = side ? side == 1 : m_flLuaOffset > 0.f;
	} break;
	case 2: // jitter: swap every cycle
		Invert = !Invert;
		break;
	case 3: // static: side from the sign of the slider
		Invert = bodyValue < 0.f;
		break;
	default:
		break;
	}

	if ( aa.m_bFreestandBody && aa.m_iBody != 2 ) {
		const auto side{ RawFreestanding( ) };
		if ( side )
			Invert = side == 2;
	}
}

float CAntiAim::LuaBaseYaw( CUserCmd& cmd ) {
	m_bAntiBackstab = false;
	const auto& aa{ m_cLua };

	auto yaw{ cmd.viewAngles.y };

	if ( Config::Get<bool>( Vars.AntiAimManualDir ) ) {
		if ( ManualSide == 1 )
			return yaw + 90.f;
		if ( ManualSide == 2 )
			return yaw - 90.f;
	}

	if ( aa.m_bFreestanding && Config::Get<keybind_t>( Vars.AntiaimFreestandingKey ).enabled ) {
		const auto side{ RawFreestanding( ) };
		if ( side == 1 )
			return yaw + 90.f;
		if ( side == 2 )
			return yaw - 90.f;
	}

	/* yaw base: at targets = enemy closest to the crosshair */
	if ( aa.m_iYawBase == 1 && aa.m_iYaw != 5 ) {
		CBasePlayer* best{ };
		auto bestFov{ FLT_MAX };

		for ( auto i{ 1 }; i <= Interfaces::Globals->nMaxClients; ++i ) {
			const auto player{ static_cast< CBasePlayer* >( Interfaces::ClientEntityList->GetClientEntity( i ) ) };
			if ( !player || !player->IsPlayer( ) || player == ctx.m_pLocal || player->Dormant( ) || player->IsDead( ) || player->IsTeammate( ) )
				continue;

			const auto fov{ Math::GetFov( ctx.m_angOriginalViewangles, Math::CalcAngle( ctx.m_vecEyePos, player->GetAbsOrigin( ) ) ) };
			if ( fov < bestFov ) {
				bestFov = fov;
				best = player;
			}
		}

		if ( best ) {
			const auto x{ best->m_vecOrigin( ).x - ctx.m_pLocal->m_vecOrigin( ).x };
			const auto y{ best->m_vecOrigin( ).y - ctx.m_pLocal->m_vecOrigin( ).y };
			yaw = x == 0.f && y == 0.f ? yaw : RAD2DEG( std::atan2( y, x ) );
		}
	}

	const auto value{ static_cast< float >( aa.m_iYawValue ) };
	switch ( aa.m_iYaw ) {
	case 1: // 180
	case 4: // 180 Z
		return yaw + 180.f + value + m_flLuaOffset;
	case 2: // spin, slider is the speed
		m_flLuaSpin = std::remainderf( m_flLuaSpin + value, 360.f );
		return yaw + 180.f + m_flLuaSpin;
	case 3: // static
		return yaw + value + m_flLuaOffset;
	case 5: // crosshair
		return cmd.viewAngles.y + value + m_flLuaOffset;
	default: // off
		return yaw;
	}
}

void CAntiAim::LuaPitch( CUserCmd& cmd ) {
	switch ( m_cLua.m_iPitch ) {
	case 1: // default
	case 3: // down
	case 4: // minimal
		cmd.viewAngles.x = 89.f;
		break;
	case 2: // up
		cmd.viewAngles.x = -89.f;
		break;
	case 5: // random
		cmd.viewAngles.x = Math::RandomFloat( -89.f, 89.f );
		break;
	case 6: // custom
		cmd.viewAngles.x = std::clamp( static_cast< float >( m_cLua.m_iPitchValue ), -89.f, 89.f );
		break;
	default: // off
		break;
	}
}
