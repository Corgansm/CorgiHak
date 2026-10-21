#include "engine_prediction.h"
#include "logger.h"

namespace {
	// networked precision of the compressed props ( anything inside this is rounding, not a real change )
	constexpr float PUNCH_TOLERANCE{ 0.03125f };
	constexpr float VIEW_OFFSET_TOLERANCE{ 0.125f };
	// m_flVelocityModifier is networked with 8 bits over 0..1
	constexpr float VELOCITY_MODIFIER_TOLERANCE{ 0.01f };

	FORCEINLINE bool WithinTolerance( const Vector& a, const Vector& b, float tolerance ) {
		return std::abs( a.x - b.x ) <= tolerance
			&& std::abs( a.y - b.y ) <= tolerance
			&& std::abs( a.z - b.z ) <= tolerance;
	}

	FORCEINLINE bool WithinTolerance( const QAngle& a, const QAngle& b, float tolerance ) {
		return std::abs( a.x - b.x ) <= tolerance
			&& std::abs( a.y - b.y ) <= tolerance
			&& std::abs( a.z - b.z ) <= tolerance;
	}
}

void CEnginePrediction::Reset( )
{
	m_cCompressionVars = { };
	m_flVelocityModifier = 1.f;
	m_cPredictedModifier = { };
	m_bForceRepredict = false;
	m_bActive = false;
	Spread = 0.f;
	Inaccuracy = 0.f;
}

void CEnginePrediction::PreUpdate( )
{
	const auto local{ ctx.m_pLocal };
	if ( !local || local->IsDead( ) )
	{
		m_flVelocityModifier = 1.f;
		m_bForceRepredict = false;
		return;
	}

	// m_flVelocityModifier ( slowdown after taking damage ) is not predicted by the client.
	// only when the server disagreed with the value we predicted for the acknowledged command are the
	// predicted commands wrong: throw them away once and re-predict from the last acknowledged command.
	// ( comparing against the newest predicted value instead fired every tick while the slowdown recovered,
	//   and each full re-predict replayed the weapon animations, which showed up as viewmodel jitter with doubletap )
	if ( !m_bForceRepredict )
		return;

	m_bForceRepredict = false;
	Interfaces::Prediction->Split->iPreviousAckHadErrors = 1;
	Interfaces::Prediction->Split->nCommandsPredicted = 0;
	m_flVelocityModifier = local->m_flVelocityModifier( );
}

void CEnginePrediction::OnNetUpdate( )
{
	const auto local{ ctx.m_pLocal };
	if ( !local || local->IsDead( ) )
		return;

	const auto ack{ Interfaces::ClientState->iLastCommandAck };
	if ( ack <= 0 )
		return;

	const auto& predicted{ m_cPredictedModifier.at( ack % 150 ) };
	if ( predicted.m_iCommandNumber != ack )
		return;

	if ( std::abs( local->m_flVelocityModifier( ) - predicted.m_flValue ) > VELOCITY_MODIFIER_TOLERANCE )
		m_bForceRepredict = true;
}

void CEnginePrediction::RunCommand( CUserCmd& cmd )
{
	m_bActive = false;

	if ( !ctx.m_pLocal || ctx.m_pLocal->IsDead( ) )
		return;

	m_bActive = true;

	m_flCurtime = Interfaces::Globals->flCurTime;
	m_flFrametime = Interfaces::Globals->flFrameTime;
	m_TickCount = Interfaces::Globals->iTickCount;

	// same globals CPrediction::RunCommand uses: tickcount / curtime come from the tickbase this command will run at
	Interfaces::Globals->flCurTime = ctx.m_flFixedCurtime;
	Interfaces::Globals->flFrameTime = Interfaces::Prediction->bEnginePaused ? 0.f : Interfaces::Globals->flIntervalPerTick;
	Interfaces::Globals->iTickCount = TIME_TO_TICKS( ctx.m_flFixedCurtime );

	m_bBackupInPrediction = Interfaces::Prediction->bInPrediction;
	m_bBackupFirstTimePredicted = Interfaces::Prediction->Split->bIsFirstTimePredicted;

	ctx.m_pLocal->CurrentCommand( ) = &cmd;

	*( *reinterpret_cast< unsigned int** >( Displacement::Sigs.uPredictionRandomSeed ) ) = ( ( int( __thiscall* )( int ) )Displacement::Sigs.MD5PseudoRandom )( cmd.iCommandNumber ) & 0x7fffffff;
	*( *reinterpret_cast< CBasePlayer*** >( Displacement::Sigs.pPredictionPlayer ) ) = ctx.m_pLocal;

	Interfaces::Prediction->bInPrediction = true;
	Interfaces::Prediction->Split->bIsFirstTimePredicted = false;

	std::memset( &MoveData, 0, sizeof( CMoveData ) );

	ctx.m_bProhibitSounds = true;
	{
		Interfaces::GameMovement->StartTrackPredictionErrors( ctx.m_pLocal );

		Interfaces::Prediction->CheckMovingGround( ctx.m_pLocal, Interfaces::Globals->flFrameTime );
		Interfaces::Prediction->SetLocalViewAngles( cmd.viewAngles );

		Interfaces::MoveHelper->SetHost( ctx.m_pLocal );

		Interfaces::Prediction->SetupMove( ctx.m_pLocal, &cmd, Interfaces::MoveHelper, &MoveData );
		Interfaces::GameMovement->ProcessMovement( ctx.m_pLocal, &MoveData );

		Interfaces::Prediction->FinishMove( ctx.m_pLocal, &cmd, &MoveData );
		Interfaces::MoveHelper->ProcessImpacts( );
	}
	ctx.m_bProhibitSounds = false;

	if ( const auto ViewModel{ static_cast< CBaseViewModel* >( Interfaces::ClientEntityList->GetClientEntityFromHandle( ctx.m_pLocal->m_hViewModel( ) ) ) }; ViewModel )
	{
		m_ViewmodelData.m_flCycle = ViewModel->m_flCycle( );
		m_ViewmodelData.m_flAnimationTime = ViewModel->m_flAnimTime( );
	}

	if ( ctx.m_pWeapon )
	{
		ctx.m_pWeapon->UpdateAccuracyPenalty( );

		Spread = ctx.m_pWeapon->GetSpread( );
		Inaccuracy = ctx.m_pWeapon->GetInaccuracy( );
	}
	else
	{
		Spread = 0.f;
		Inaccuracy = 0.f;
	}

	// value we predicted: PreUpdate compares the next networked value against this
	m_flVelocityModifier = ctx.m_pLocal->m_flVelocityModifier( );
	m_cPredictedModifier.at( cmd.iCommandNumber % 150 ) = { cmd.iCommandNumber, m_flVelocityModifier };

	auto& localData{ ctx.m_cLocalData.at( cmd.iCommandNumber % 150 ) };
	localData.SavePredVars( ctx.m_pLocal, cmd );

	Interfaces::Prediction->bInPrediction = m_bBackupInPrediction;
	Interfaces::Prediction->Split->bIsFirstTimePredicted = m_bBackupFirstTimePredicted;

	// movement and accuracy are predicted; the rest of createmove ( ragebot, lag records ) counts in client ticks
	Interfaces::Globals->iTickCount = m_TickCount;
}

void CEnginePrediction::Finish( )
{
	if ( !m_bActive )
		return;

	m_bActive = false;

	if ( ctx.m_pLocal )
	{
		Interfaces::GameMovement->FinishTrackPredictionErrors( ctx.m_pLocal );
		ctx.m_pLocal->CurrentCommand( ) = nullptr;
	}

	Interfaces::MoveHelper->SetHost( nullptr );

	Interfaces::Globals->flCurTime = m_flCurtime;
	Interfaces::Globals->flFrameTime = m_flFrametime;
	Interfaces::Globals->iTickCount = m_TickCount;

	*( *reinterpret_cast< unsigned int** >( Displacement::Sigs.uPredictionRandomSeed ) ) = -1;
	*( *reinterpret_cast< CBasePlayer*** >( Displacement::Sigs.pPredictionPlayer ) ) = nullptr;

	Interfaces::GameMovement->Reset( );
}

void CEnginePrediction::RestoreNetvars( int commandNumber )
{
	const auto& local{ ctx.m_pLocal };
	if ( !local || local->IsDead( ) || commandNumber < 0 )
		return;

	const auto& data{ m_cCompressionVars.at( commandNumber % 150 ) };
	if ( data.m_iCommandNumber != commandNumber )
		return;

	// the server rounds these when networking them: if the difference is only rounding, keep our precise value
	if ( WithinTolerance( local->m_aimPunchAngle( ), data.m_aimPunchAngle, PUNCH_TOLERANCE ) )
		local->m_aimPunchAngle( ) = data.m_aimPunchAngle;

	if ( WithinTolerance( local->m_aimPunchAngleVel( ), data.m_aimPunchAngleVel, PUNCH_TOLERANCE ) )
		local->m_aimPunchAngleVel( ) = data.m_aimPunchAngleVel;

	if ( std::abs( local->m_vecViewOffset( ).z - data.m_vecViewOffsetZ ) <= VIEW_OFFSET_TOLERANCE )
		local->m_vecViewOffset( ).z = data.m_vecViewOffsetZ;
}

void CEnginePrediction::StoreNetvars( int commandNumber )
{
	const auto& local{ ctx.m_pLocal };
	if ( !local || local->IsDead( ) || commandNumber < 0 )
		return;

	auto& data{ m_cCompressionVars.at( commandNumber % 150 ) };

	data.m_iCommandNumber = commandNumber;
	data.m_aimPunchAngleVel = local->m_aimPunchAngleVel( );
	data.m_aimPunchAngle = local->m_aimPunchAngle( );
	data.m_vecViewOffsetZ = local->m_vecViewOffset( ).z;
}
