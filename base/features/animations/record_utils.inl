#pragma once
#include "../rage/exploits.h"
#include "animation.h"

inline void PlayerEntry::Clear( ) 
{
	this->m_flPreviousYaws.clear( );
	this->m_pRecords.clear( );
}

inline void PlayerEntry::OnPlayerChange( CBasePlayer* player ) 
{
	OnNewRound( );

	const auto playerInfo{ Interfaces::Engine->GetPlayerInfo( player->Index( ) ) };
	if ( playerInfo.has_value( ) )
		this->m_bBot = playerInfo->bFakePlayer;

	this->m_pPlayer = player;
	this->m_flSpawnTime = 0;
	this->m_iResolverSide = 0;
}

inline void PlayerEntry::OnNewRound( ) 
{
	if ( !this->m_pPlayer )
		return;

	OutOfDormancy( );

	this->m_iMissedShots = 0;
	this->m_iResolverSide = 0;

	this->m_flSpawnTime = this->m_pPlayer->m_flSpawnTime( );
}

inline void PlayerEntry::OutOfDormancy( ) 
{
	this->m_optPreviousData.reset( );
	this->m_pRecords.clear( );
	this->m_flPreviousYaws.clear( );

	this->m_iLastNewCmds = 0;
	this->m_bBrokeLC = false;
	this->m_bRecordAdded = false;
	this->m_flHighestSimulationTime = 0.f;
	this->m_vecUpdatedOrigin = { 0, 0, 0 };
}

inline float ClampCycle( float cycleIn ) 
{
	cycleIn -= int( cycleIn );

	if ( cycleIn < 0 )
		cycleIn += 1;
	else if ( cycleIn > 1 )
		cycleIn -= 1;

	return cycleIn;
}

inline void LagRecord_t::FinalAdjustments( CBasePlayer* player, const std::optional<AnimData_t>& previous )
{
	if ( !previous.has_value( ) )
	{
		this->m_iNewCmds = 1;
		return;
	}

	const auto& prevLayers{ previous->m_pLayers };
	const auto& curLayers{ this->m_cAnimData.m_pLayers };
	auto& curVel{ this->m_cAnimData.m_vecVelocity };

	/* choked fix */
	if ( curLayers[ 11 ].flPlaybackRate > 0.f && prevLayers[ 11 ].flPlaybackRate > 0.f && prevLayers[ 11 ].nSequence == curLayers[ 11 ].nSequence ) {

		this->m_iNewCmds = 0;

		auto simCycle{ prevLayers[ 11 ].flCycle };
		auto targetCycle{ curLayers[ 11 ].flCycle };
		auto simPlayBackRate{ prevLayers[ 11 ].flPlaybackRate * Interfaces::Globals->flIntervalPerTick };

		if ( curLayers[ 11 ].flPlaybackRate != simCycle ) 
		{
			if ( simCycle > targetCycle )
				targetCycle += 1.f;
		}

		do 
		{
			++this->m_iNewCmds;

			// IsLayerSequenceCompleted takes old playback into account
			if ( simCycle + simPlayBackRate >= 1.f )
				simPlayBackRate = curLayers[ 11 ].flPlaybackRate * Interfaces::Globals->flIntervalPerTick;

			simCycle += simPlayBackRate;
		} while ( simCycle < targetCycle );
	}

	/* jump_fall fix */
	if ( curLayers[ 4 ].flCycle > 0.f && curLayers[ 4 ].flCycle < 0.99f && !( this->m_cAnimData.m_iFlags & FL_ONGROUND ) ) 
	{
		this->m_flLeftGroundTime = this->m_cAnimData.m_flSimulationTime - TICKS_TO_TIME( curLayers[ 4 ].flCycle / ( Interfaces::Globals->flIntervalPerTick * curLayers[ 4 ].flPlaybackRate ) );
		if ( !( this->m_cAnimData.m_iFlags & FL_ONGROUND ) )
			this->m_bFixJumpFall = this->m_flLeftGroundTime >= this->m_cAnimData.m_flSimulationTime - TICKS_TO_TIME( this->m_iNewCmds );
	}

	auto chokedTime{ TICKS_TO_TIME( this->m_iNewCmds ) };

	/* velo fix */
	if ( this->m_iNewCmds > 0 && this->m_iNewCmds < 16 )
		curVel = ( this->m_cAnimData.m_vecOrigin - previous->m_vecOrigin ) / chokedTime;

	auto maxCurrentSpeed{ player->m_flMaxSpeed( ) };

	if ( this->m_cAnimData.m_iFlags & FL_ONGROUND && previous->m_iFlags & FL_ONGROUND ) 
	{
		while ( curVel.Length( ) > maxCurrentSpeed ) 
		{
			++this->m_iNewCmds;
			chokedTime = TICKS_TO_TIME( this->m_iNewCmds );
			curVel = ( this->m_cAnimData.m_vecOrigin - previous->m_vecOrigin ) / chokedTime;
		}
	}

	// fix shuffling animations due to rounding errors
	if ( std::abs( curVel.x ) < 0.001f )
		curVel.x = 0.f;
	if ( std::abs( curVel.y ) < 0.001f )
		curVel.y = 0.f;
	if ( std::abs( curVel.z ) < 0.001f )
		curVel.z = 0.f;

	if ( curLayers[ 6 ].flPlaybackRate == 0.f )
		curVel = { 0, 0, 0 };
	else if ( this->m_cAnimData.m_iFlags & FL_ONGROUND )
	{
		curVel.z = 0.f;

		const auto avgSpeed{ curVel.Length( ) };

		if ( avgSpeed > 0.f )
		{
			if ( prevLayers[ 11 ].nSequence == curLayers[ 11 ].nSequence && curLayers[ 11 ].flPlaybackRate == prevLayers[ 11 ].flPlaybackRate && !player->m_flDuckAmount( ) && !player->m_bIsWalking( ) ) 
			{
				if ( curLayers[ 11 ].flWeight > 0.f && curLayers[ 11 ].flWeight < 1.f )
				{
					const auto m_flSpeedAsPortionOfRunTopSpeed{ 0.55f + ( 1.f - curLayers[ 11 ].flWeight ) * 0.35f };
					const auto m_flVelocityLengthXY{ m_flSpeedAsPortionOfRunTopSpeed * maxCurrentSpeed };

					if ( m_flSpeedAsPortionOfRunTopSpeed > 0.f && m_flSpeedAsPortionOfRunTopSpeed < 1.f ) 
					{
						this->m_bAccurateVelocity = true;

						curVel.x *= ( m_flVelocityLengthXY / avgSpeed );
						curVel.y *= ( m_flVelocityLengthXY / avgSpeed );
					}
				}
			}
		}
	}
}

inline uint8_t LagRecord_t::Validity( ) 
{
	// never trust more than the stock 0.2s: scripts ( momentum "Manipulate backtracking" ) raise the client copy of
	// sv_maxunlag, which made us shoot 60+ tick old records the server had already thrown away
	const auto maxUnlag{ std::min( Displacement::Cvars.sv_maxunlag->GetFloat( ), 0.2f ) };

	const auto serverCurtime{ TICKS_TO_TIME( Interfaces::ClientState->iServerTick + ctx.m_iRealOutLatencyTicks + 1 ) };
	// the server stores this as an int ( CLagCompensationManager ), emulate the truncation
	const auto flDeadtime{ static_cast< int >( serverCurtime - maxUnlag ) };

	if ( this->m_cAnimData.m_flSimulationTime < flDeadtime )
		return 0;

	const auto curtime{ ctx.m_flFixedCurtime };

	// mirror CLagCompensationManager::BacktrackPlayer exactly:
	//   correct    = clamp( latency + TICKS_TO_TIME( lerpTicks ), 0, sv_maxunlag )
	//   targettick = cmd->tick_count - lerpTicks, where tick_count = TIME_TO_TICKS( simtime + lerp ) ( what the ragebot sends )
	const auto lerpTicks{ TIME_TO_TICKS( ctx.m_flLerpTime ) };
	const auto correct{ std::clamp( ctx.m_flOutLatency + TICKS_TO_TIME( lerpTicks ), 0.f, maxUnlag ) };
	const auto targetTime{ TICKS_TO_TIME( TIME_TO_TICKS( this->m_cAnimData.m_flSimulationTime + ctx.m_flLerpTime ) - lerpTicks ) };

	auto delta{ correct - ( curtime - targetTime ) };

	// pred not right ( server limit is 0.2, keep a small margin )
	if ( std::fabsf( delta ) > 0.19f )
		return 0;

	auto ret{ 1 };

	// how many ticks of tickbase error ( either direction ) this record survives
	for ( int i{ 1 }; i <= 14; ++i ) {
		bool invalid{ };
		if ( std::fabsf( correct - ( ( curtime + TICKS_TO_TIME( i ) ) - targetTime ) ) <= 0.19f )
			ret++;
		else
			invalid = true;

		if ( std::fabsf( correct - ( ( curtime - TICKS_TO_TIME( i ) ) - targetTime ) ) <= 0.19f )
			ret++;
		else
			invalid = true;

		if ( invalid )
			break;
	}

	return ret;
}

#define SWAP_RESIK_SIDE( side ) ( side == 2 ? 1 : 2 )

FORCEINLINE void LagRecord_t::Apply( CBasePlayer* ent, int side ) 
{
	ent->SetCollisionBounds(
		this->m_cAnimData.m_vecMins, this->m_cAnimData.m_vecMaxs
	);

	ent->m_iEFlags( ) |= EFL_DIRTY_ABSTRANSFORM;

	ent->m_vecOrigin() = this->m_cAnimData.m_vecOrigin;
	ent->SetAbsOrigin( this->m_cAnimData.m_vecOrigin );

	std::memcpy( ent->m_CachedBoneData( ).Base( ), this->m_cAnimData.m_arrSides.at( side ).m_pMatrix, ent->m_CachedBoneData().Size() * sizeof( matrix3x4_t ) );
}

FORCEINLINE void LagBackup_t::Apply( CBasePlayer* ent )
{
	ent->SetCollisionBounds(
		this->m_vecMins, this->m_vecMaxs
	);

	ent->m_iEFlags( ) |= EFL_DIRTY_ABSTRANSFORM;

	ent->m_vecOrigin() = this->m_vecOrigin;
	ent->SetAbsOrigin( this->m_vecAbsOrigin );

	std::memcpy( ent->m_CachedBoneData( ).Base( ), this->m_matMatrix, ent->m_CachedBoneData().Size() * sizeof( matrix3x4_t ) );
}

FORCEINLINE LagBackup_t::LagBackup_t( CBasePlayer* ent )
{
	this->m_vecMins = ent->m_vecMins( );
	this->m_vecMaxs = ent->m_vecMaxs( );

	this->m_vecOrigin = ent->m_vecOrigin();
	this->m_vecAbsOrigin = ent->GetAbsOrigin();

	memcpy( this->m_matMatrix, ent->m_CachedBoneData( ).Base( ), ent->m_CachedBoneData().Size() * sizeof( matrix3x4_t ) );
}