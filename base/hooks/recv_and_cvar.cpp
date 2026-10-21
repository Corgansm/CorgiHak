#include "../core/hooks.h"
#include "../features/animations/animation.h"
#include "../context.h"
#include "../features/rage/net_simtime.h"
#include <intrin.h>

bool FASTCALL Hooks::hkSvCheatsGetBool( CConVar* thisptr, int edx ) {
	static auto oSvCheatsGetBool = DTR::SvCheatsGetBool.GetOriginal<decltype( &hkSvCheatsGetBool )>( );
	if ( reinterpret_cast< std::uintptr_t >( _ReturnAddress( ) ) == Displacement::Sigs.uCAM_ThinkReturn )
		return true;

	return oSvCheatsGetBool( thisptr, edx );
}

bool FASTCALL Hooks::hkClDoResetLatchedGetBool( CConVar* thisptr, int edx ) {
	static auto oClDoResetLatchedGetBool = DTR::ClDoResetLatchedGetBool.GetOriginal<decltype( &hkClDoResetLatchedGetBool )>( );
	if ( reinterpret_cast< std::uintptr_t >( _ReturnAddress( ) ) == Displacement::Sigs.DoResetLatchReturn )
		return false;

	return oClDoResetLatchedGetBool( thisptr, edx );
}

int FASTCALL Hooks::hkClPredictGetInt( CConVar* thisptr, int edx ) {
	return 1;
}

void CDECL Hooks::m_bClientSideAnimationHook( const CRecvProxyData* data, void* entity, void* output ) {
	const auto player{ ( ( CBasePlayer* )entity ) };

	if ( !player->IsPlayer( ) )
		return m_bClientSideAnimation( data, entity, output );
}

void CDECL Hooks::m_flSimulationTimeHook( const CRecvProxyData* data, void* entity, void* output ) {
	const auto player{ static_cast< CBaseEntity* >( entity ) };
	const int index{ player ? player->Index( ) : 0 };
	const bool isPlayer{ index >= 1 && index <= 64 };

	// fix simtime being inaccurate due to rounding
	// when its smaller than network base (caused by defensive dt).
	if ( !data->Value.Int ) {
		// the server encodes simulation time relative to a network base ( 100 tick steps, offset per entity ): a value
		// below that base can only be a simulation time that went back, and arrives as 0. keep the entity's value as
		// before, but tell the defensive detector it dropped below the base
		if ( isPlayer && Interfaces::ClientState ) {
			const int tick{ Interfaces::ClientState->iServerTick };
			const int base{ 100 * ( ( tick - index % 32 ) / 100 ) };
			NetSim::Note( index, TICKS_TO_TIME( base - 1 ) );
		}
		return;
	}

	m_flSimulationTime( data, entity, output );

	// every value the server sent, in order ( the animation system may overwrite the entity's copy later )
	if ( isPlayer )
		NetSim::Note( index, *static_cast< float* >( output ) );
}

void CDECL Hooks::m_hWeaponHook( const CRecvProxyData* data, void* entity, void* output ) {
	const auto viewModel{ ( ( CBaseViewModel* ) entity ) };

	const auto backupAnimTime{ viewModel->m_flAnimTime( ) };
	const auto backupCycle{ viewModel->m_flCycle( ) };

	m_hWeapon( data, entity, output );

	viewModel->m_flAnimTime( ) = backupAnimTime;
	viewModel->m_flCycle( ) = backupCycle;
}

void CDECL Hooks::m_flAbsYawHook( const CRecvProxyData* data, void* entity, void* output ) {
	if ( entity == ctx.m_pLocal
		|| ctx.m_iLastID > 64
		|| ctx.m_iLastID < 1 )
		return m_flAbsYaw( data, entity, output );

	//auto& entry{ Features::AnimSys.m_arrEntries.at( ctx.m_iLastID - 1 ) };

	//if ( !entry.m_pRecords.empty( ) )
		//entry.m_iLastResolvedSide =  entry.m_flPreviousYaws.front( ).second < data->Value.Float ? 1 : 2;

	//const auto test{ ( ( CBasePlayer* )entity )->m_angRotation( ).y };

	m_flAbsYaw( data, entity, output );
}

// viewmodel
void CDECL Hooks::m_flCycle_Recv( const CRecvProxyData* data, void* entity, void* output ) {

}

void CDECL Hooks::m_flAnimTime_Recv( const CRecvProxyData* data, void* entity, void* output ) {

}