#include "../../core/hooks.h"
#include "../../context.h"
#include "../../features/misc/engine_prediction.h"
#include "../../core/variables.h"
#include "../../features/misc/logger.h"
#include "../../features/rage/exploits.h"
#include <intrin.h>

void FASTCALL Hooks::hkPhysicsSimulate( CBasePlayer* player, int time ) 
{
	static auto oPhysicsSimulate = DTR::PhysicsSimulate.GetOriginal<decltype( &hkPhysicsSimulate )>( );
	if ( player != ctx.m_pLocal || player->IsDead( ) || Interfaces::Globals->iTickCount == player->m_nSimulationTick( ) )
		return oPhysicsSimulate( player, time );

	auto cctx = &player->m_CmdContext( );

	if ( cctx->cmd.iTickCount == INT_MAX )
	{
		player->m_nSimulationTick( ) = Interfaces::Globals->iTickCount;
		return Features::EnginePrediction.StoreNetvars( cctx->cmd.iCommandNumber );
	}

	Features::EnginePrediction.RestoreNetvars( cctx->cmd.iCommandNumber - 1 );
	oPhysicsSimulate( player, time );
	Features::EnginePrediction.StoreNetvars( cctx->cmd.iCommandNumber );
}