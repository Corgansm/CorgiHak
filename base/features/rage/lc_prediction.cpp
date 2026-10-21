#include "lc_prediction.h"
#include "ragebot.h"

#include <algorithm>
#include <cmath>

namespace {
	float NormalizeYaw( float yaw ) {
		return std::remainderf( yaw, 360.f );
	}

	float YawOf( const Vector& v ) {
		return RAD2DEG( std::atan2( v.y, v.x ) );
	}

	// world only hull trace with the player's own bounds ( other players never block the server's movement here
	// in a way we could predict, and tracing them would make the target collide with itself )
	Trace_t Hull( const Vector& start, const Vector& end, const Vector& mins, const Vector& maxs ) {
		Trace_t trace{};
		CTraceFilter filter{ []( IHandleEntity*, int ) { return false; }, TRACE_WORLD_ONLY };
		Interfaces::EngineTrace->TraceRay( Ray_t{ start, end, mins, maxs }, MASK_PLAYERSOLID, &filter, &trace );
		return trace;
	}

	float CvarOr( CConVar* variable, float fallback ) {
		return variable ? variable->GetFloat( ) : fallback;
	}
}

void LcTrack_t::Reset( ) {
	m_pPlayer = nullptr;
	m_flSpawnTime = 0.f;
	m_dqSamples.clear( );
	m_cLast = { };
	m_iLastPredictTick = -1;
}

LcTrack_t& CLagCompPredictor::TrackFor( int index ) {
	return m_arrTracks.at( std::clamp( index, 1, 64 ) - 1 );
}

const LcTrack_t& CLagCompPredictor::Track( int index ) const {
	return m_arrTracks.at( std::clamp( index, 1, 64 ) - 1 );
}

void CLagCompPredictor::ResetAll( ) {
	for ( auto& track : m_arrTracks )
		track.Reset( );
}

void CLagCompPredictor::Update( int index, PlayerEntry& entry ) {
	auto& track{ TrackFor( index ) };
	const auto player{ entry.m_pPlayer };

	if ( !player || player->IsDormant( ) || player->IsDead( ) ) {
		track.Reset( );
		return;
	}

	if ( track.m_pPlayer != player || track.m_flSpawnTime != player->m_flSpawnTime( ) ) {
		track.Reset( );
		track.m_pPlayer = player;
		track.m_flSpawnTime = player->m_flSpawnTime( );
	}

	if ( entry.m_pRecords.empty( ) || !entry.m_pRecords.back( ) )
		return;

	const auto& newest{ entry.m_pRecords.back( ) };
	const float interval{ Interfaces::Globals->flIntervalPerTick };
	const float simTime{ newest->m_cAnimData.m_flSimulationTime };

	if ( newest->m_bFirst )
		track.m_dqSamples.clear( );

	if ( !track.m_dqSamples.empty( ) ) {
		const float last{ track.m_dqSamples.back( ).m_flSimTime };

		// a big step back is a new life / reconnect, anything else is "nothing new"
		if ( last - simTime > 1.f )
			track.m_dqSamples.clear( );
		else if ( simTime <= last + interval * 0.5f )
			return;
	}

	LcSample_t sample{ };
	// the record came in the snapshot that was current when it was received
	sample.m_iServerTick = Interfaces::ClientState->iServerTick - ( Interfaces::Globals->iTickCount - newest->m_iReceiveTick );
	sample.m_flSimTime = simTime;
	sample.m_iTicks = track.m_dqSamples.empty( )
		? newest->m_iNewCmds
		: TIME_TO_TICKS( simTime - track.m_dqSamples.back( ).m_flSimTime );
	sample.m_vecOrigin = newest->m_cAnimData.m_vecOrigin;
	sample.m_vecVelocity = newest->m_cAnimData.m_vecVelocity;
	sample.m_bOnGround = ( newest->m_cAnimData.m_iFlags & FL_ONGROUND ) != 0;

	// a gap longer than any choke cycle ( packet loss, dormancy ) says nothing about their movement trend
	if ( sample.m_iTicks > LcTuning::MaxCycle ) {
		track.m_dqSamples.clear( );
		sample.m_iTicks = std::clamp( newest->m_iNewCmds, 1, LcTuning::MaxCycle );
	}
	sample.m_iTicks = std::clamp( sample.m_iTicks, 1, LcTuning::MaxCycle );

	track.m_dqSamples.push_back( sample );
	while ( track.m_dqSamples.size( ) > static_cast< size_t >( LcTuning::MaxSamples ) )
		track.m_dqSamples.pop_front( );
}

bool CLagCompPredictor::BreaksLagComp( int index, const PlayerEntry& entry ) const {
	if ( entry.m_bBrokeLC )
		return true;

	const auto& samples{ Track( index ).m_dqSamples };
	if ( samples.empty( ) )
		return false;

	const auto& newest{ samples.back( ) };

	if ( samples.size( ) >= 2 ) {
		const auto& previous{ samples[ samples.size( ) - 2 ] };
		if ( ( newest.m_vecOrigin - previous.m_vecOrigin ).Length2DSqr( ) > LcTuning::TeleportDistSqr )
			return true;
	}

	// their next packet will carry them past 64 units at the speed they move now
	const float perCycle{ newest.m_vecVelocity.Length2D( ) * Interfaces::Globals->flIntervalPerTick * static_cast< float >( newest.m_iTicks ) };
	return perCycle >= std::sqrt( LcTuning::TeleportDistSqr ) * LcTuning::BreakMargin;
}

bool CLagCompPredictor::EstimateStart( const LcTrack_t& track, CBasePlayer* player, State_t& state, Model_t& model ) const {
	const auto& samples{ track.m_dqSamples };
	const size_t count{ samples.size( ) };
	if ( !count )
		return false;

	const float dt{ Interfaces::Globals->flIntervalPerTick };
	const float gravity{ CvarOr( Displacement::Cvars.sv_gravity, 800.f ) };

	const auto& newest{ samples[ count - 1 ] };

	state.m_vecOrigin = newest.m_vecOrigin;
	state.m_bOnGround = newest.m_bOnGround;
	state.m_bLanded = false;

	Vector velocity{ newest.m_vecVelocity };

	const float maxSpeed{ player->m_flMaxSpeed( ) };
	model.m_flMaxGroundSpeed = maxSpeed > 1.f ? maxSpeed : 250.f;
	model.m_flTurn = 0.f;
	model.m_flAccel = 0.f;

	if ( count >= 2 ) {
		const auto& previous{ samples[ count - 2 ] };
		const float ticks{ static_cast< float >( newest.m_iTicks ) };
		const Vector delta{ newest.m_vecOrigin - previous.m_vecOrigin };

		if ( ticks > 0.f && delta.Length2DSqr( ) > 0.0001f ) {
			// the displacement over a cycle is the velocity in the middle of that cycle
			const float avgSpeed{ delta.Length2D( ) / ( ticks * dt ) };
			const float avgYaw{ YawOf( delta ) };

			float turn{ }, accel{ };
			if ( count >= 3 ) {
				const auto& older{ samples[ count - 3 ] };
				const float olderTicks{ static_cast< float >( previous.m_iTicks ) };
				const Vector olderDelta{ previous.m_vecOrigin - older.m_vecOrigin };

				if ( olderTicks > 0.f && olderDelta.Length2DSqr( ) > 0.0001f ) {
					const float prevSpeed{ olderDelta.Length2D( ) / ( olderTicks * dt ) };
					const float span{ ( ticks + olderTicks ) * 0.5f };

					if ( avgSpeed > LcTuning::MinTurnSpeed && prevSpeed > LcTuning::MinTurnSpeed )
						turn = NormalizeYaw( avgYaw - YawOf( olderDelta ) ) / span;

					// a jump or a landing between the samples changes speed for reasons a trend can't follow
					if ( older.m_bOnGround == previous.m_bOnGround && previous.m_bOnGround == newest.m_bOnGround )
						accel = ( avgSpeed - prevSpeed ) / span;
				}
			}

			turn = std::clamp( turn, -LcTuning::MaxTurnPerTick, LcTuning::MaxTurnPerTick );
			accel = std::clamp( accel, -LcTuning::MaxAccelPerTick, LcTuning::MaxAccelPerTick );

			// move the mid cycle velocity forward to the end of the cycle
			const float speed{ std::max( 0.f, avgSpeed + accel * ticks * 0.5f ) };
			const float yaw{ DEG2RAD( avgYaw + turn * ticks * 0.5f ) };

			velocity.x = std::cos( yaw ) * speed;
			velocity.y = std::sin( yaw ) * speed;

			// airborne the whole cycle: the average vertical speed is the middle one, gravity takes the rest
			if ( !previous.m_bOnGround && !newest.m_bOnGround )
				velocity.z = delta.z / ( ticks * dt ) - 0.5f * gravity * ticks * dt;

			model.m_flTurn = turn;
			model.m_flAccel = accel;
		}
	}

	if ( state.m_bOnGround )
		velocity.z = 0.f;

	state.m_vecVelocity = velocity;

	// bunny hop: they just landed, or they keep leaving the ground
	int airborne{ };
	for ( size_t i{ count > 4 ? count - 4 : 0 }; i < count; ++i )
		airborne += samples[ i ].m_bOnGround ? 0 : 1;

	const bool justLanded{ count >= 2 && newest.m_bOnGround && !samples[ count - 2 ].m_bOnGround };
	model.m_bBhop = justLanded || ( !newest.m_bOnGround && airborne >= 2 );

	return true;
}

void CLagCompPredictor::FlyMove( State_t& state, const Vector& velocity, float time, const Vector& mins, const Vector& maxs ) const {
	Vector move{ velocity };
	float left{ time };

	for ( int bump{ }; bump < 4; ++bump ) {
		if ( move.LengthSqr( ) < 0.0001f )
			break;

		const Vector end{ state.m_vecOrigin + move * left };
		const auto trace{ Hull( state.m_vecOrigin, end, mins, maxs ) };

		// stuck in geometry ( our bounds are an approximation ): stop instead of tunnelling through
		if ( trace.bAllSolid ) {
			move = Vector{ 0.f, 0.f, 0.f };
			break;
		}

		if ( trace.flFraction > 0.f )
			state.m_vecOrigin = trace.vecEnd;

		if ( trace.flFraction >= 1.f )
			break;

		left -= left * trace.flFraction;

		// slide along what we hit ( ClipVelocity, overbounce 1 )
		const Vector& normal{ trace.plane.vecNormal };
		move = move - normal * move.DotProduct( normal );

		// moving back into where we came from: a corner, stop
		if ( move.DotProduct( velocity ) <= 0.f ) {
			move = Vector{ 0.f, 0.f, 0.f };
			break;
		}
	}

	state.m_vecVelocity = move;
}

void CLagCompPredictor::GroundMove( State_t& state, float time, const Vector& mins, const Vector& maxs ) const {
	const Vector flat{ state.m_vecVelocity.x, state.m_vecVelocity.y, 0.f };
	const Vector start{ state.m_vecOrigin };
	const Vector wanted{ start + flat * time };

	State_t plain{ state };
	FlyMove( plain, flat, time, mins, maxs );

	State_t result{ plain };

	// blocked: try the same move one step higher ( stairs, small ledges )
	if ( ( plain.m_vecOrigin - wanted ).Length2DSqr( ) > 0.01f ) {
		State_t step{ state };
		const auto up{ Hull( start, start + Vector{ 0.f, 0.f, LcTuning::StepSize }, mins, maxs ) };
		if ( !up.bAllSolid && !up.bStartSolid ) {
			step.m_vecOrigin = up.vecEnd;
			FlyMove( step, flat, time, mins, maxs );

			const auto down{ Hull( step.m_vecOrigin, step.m_vecOrigin - Vector{ 0.f, 0.f, LcTuning::StepSize }, mins, maxs ) };
			if ( !down.bAllSolid && !down.bStartSolid && down.flFraction < 1.f && down.plane.vecNormal.z >= 0.7f ) {
				step.m_vecOrigin = down.vecEnd;

				if ( ( step.m_vecOrigin - start ).Length2DSqr( ) > ( plain.m_vecOrigin - start ).Length2DSqr( ) )
					result = step;
			}
		}
	}

	state.m_vecOrigin = result.m_vecOrigin;
	state.m_vecVelocity = { result.m_vecVelocity.x, result.m_vecVelocity.y, 0.f };

	// stay on ground: follow slopes and stairs down instead of flying off them
	const auto stick{ Hull( state.m_vecOrigin + Vector{ 0.f, 0.f, 2.f }, state.m_vecOrigin - Vector{ 0.f, 0.f, LcTuning::StepSize }, mins, maxs ) };
	if ( !stick.bAllSolid && !stick.bStartSolid && stick.flFraction < 1.f && stick.plane.vecNormal.z >= 0.7f )
		state.m_vecOrigin = stick.vecEnd;
}

void CLagCompPredictor::Categorize( State_t& state, const Vector& mins, const Vector& maxs ) const {
	if ( state.m_vecVelocity.z > LcTuning::NonJumpVelocity ) {
		state.m_bOnGround = false;
		return;
	}

	const auto trace{ Hull( state.m_vecOrigin, state.m_vecOrigin - Vector{ 0.f, 0.f, 2.f }, mins, maxs ) };
	if ( trace.flFraction < 1.f && !trace.bStartSolid && trace.plane.vecNormal.z >= 0.7f ) {
		if ( !state.m_bOnGround )
			state.m_bLanded = true;

		state.m_bOnGround = true;
		state.m_vecOrigin = trace.vecEnd;
		state.m_vecVelocity.z = 0.f;
	}
	else
		state.m_bOnGround = false;
}

void CLagCompPredictor::SimulateTick( State_t& state, const Model_t& model, const Vector& mins, const Vector& maxs ) const {
	const float dt{ Interfaces::Globals->flIntervalPerTick };
	const float gravity{ CvarOr( Displacement::Cvars.sv_gravity, 800.f ) };

	if ( state.m_bOnGround && model.m_bBhop ) {
		state.m_vecVelocity.z = CvarOr( Displacement::Cvars.sv_jump_impulse, 301.993377f );
		state.m_bOnGround = false;
	}

	// steer: their measured turn rate and acceleration
	float speed{ state.m_vecVelocity.Length2D( ) };
	if ( speed > 0.1f ) {
		const float yaw{ DEG2RAD( YawOf( state.m_vecVelocity ) + model.m_flTurn ) };

		if ( state.m_bOnGround ) {
			if ( speed > model.m_flMaxGroundSpeed ) {
				// faster than they can run ( a landing without a hop ): ground friction
				const float stop{ CvarOr( Displacement::Cvars.sv_stopspeed, 80.f ) };
				const float friction{ CvarOr( Displacement::Cvars.sv_friction, 5.2f ) };
				speed = std::max( model.m_flMaxGroundSpeed, speed - std::max( speed, stop ) * friction * dt );
			}
			else
				speed = std::clamp( speed + model.m_flAccel, 0.f, model.m_flMaxGroundSpeed );
		}
		else
			speed = std::clamp( speed + model.m_flAccel, 0.f, CvarOr( Displacement::Cvars.sv_maxvelocity, 3500.f ) );

		state.m_vecVelocity.x = std::cos( yaw ) * speed;
		state.m_vecVelocity.y = std::sin( yaw ) * speed;
	}

	if ( state.m_bOnGround ) {
		state.m_vecVelocity.z = 0.f;
		GroundMove( state, dt, mins, maxs );
	}
	else {
		// gravity is applied half before and half after the move, like the game does
		state.m_vecVelocity.z -= 0.5f * gravity * dt;
		const Vector velocity{ state.m_vecVelocity };
		FlyMove( state, velocity, dt, mins, maxs );
		state.m_vecVelocity.z -= 0.5f * gravity * dt;
	}

	Categorize( state, mins, maxs );
}

CLagCompPredictor::State_t CLagCompPredictor::Simulate( State_t state, const Model_t& model, int ticks, const Vector& mins, const Vector& maxs ) const {
	if ( state.m_bOnGround )
		state.m_bLanded = true; // "touched ground": a hop decision matters for this run

	for ( int i{ }; i < ticks; ++i )
		SimulateTick( state, model, mins, maxs );

	return state;
}

LcPrediction_t CLagCompPredictor::Predict( int index, PlayerEntry& entry ) {
	auto& track{ TrackFor( index ) };
	LcPrediction_t out{ };

	const auto finish{ [ & ]( const char* reason ) {
		out.m_szReason = reason;
		track.m_cLast = out;
		track.m_iLastPredictTick = Interfaces::Globals->iTickCount;
		return out;
	} };

	if ( entry.m_pRecords.empty( ) || !entry.m_pRecords.back( ) || !entry.m_pPlayer )
		return finish( "no record" );

	const auto& samples{ track.m_dqSamples };
	if ( samples.empty( ) )
		return finish( "no history" );

	const auto& record{ entry.m_pRecords.back( ) };
	const auto& newest{ samples.back( ) };
	const float interval{ Interfaces::Globals->flIntervalPerTick };

	if ( std::abs( newest.m_flSimTime - record->m_cAnimData.m_flSimulationTime ) > interval * 0.5f )
		return finish( "stale history" );

	/* their choke cycle */
	int minCycle{ LcTuning::MaxCycle }, maxCycle{ 1 }, sum{ }, used{ };
	for ( size_t i{ samples.size( ) > 4 ? samples.size( ) - 4 : 0 }; i < samples.size( ); ++i ) {
		const int ticks{ samples[ i ].m_iTicks };
		minCycle = std::min( minCycle, ticks );
		maxCycle = std::max( maxCycle, ticks );
		sum += ticks;
		++used;
	}

	const bool fixed{ maxCycle - minCycle <= 1 };
	const int cycle{ fixed ? newest.m_iTicks : std::max( 1, static_cast< int >( std::lround( static_cast< float >( sum ) / std::max( used, 1 ) ) ) ) };

	out.m_iCycle = cycle;
	out.m_bFixedCycle = fixed;

	/* how many of their packets the server will have run when our shot is processed */
	const int shotTick{ Interfaces::ClientState->iServerTick + ctx.m_iRealOutLatencyTicks + 1 };
	const int elapsed{ std::max( 0, shotTick - newest.m_iServerTick ) };

	std::vector< int > tickOptions{ };
	int primary{ };

	if ( fixed ) {
		const int packets{ elapsed / cycle };
		const int phase{ elapsed - packets * cycle };
		primary = packets * cycle;

		// right after a packet was due it may still be on its way, right before the next one it may already be in
		if ( phase <= LcTuning::TimingSlack && packets >= 1 )
			tickOptions.push_back( ( packets - 1 ) * cycle );
		if ( phase >= cycle - LcTuning::TimingSlack )
			tickOptions.push_back( ( packets + 1 ) * cycle );
	}
	else {
		// random choke: the last packet before our shot arrived somewhere in the last cycle
		if ( elapsed >= minCycle ) {
			const int low{ std::max( minCycle, elapsed - maxCycle + 1 ) };
			primary = std::clamp( ( elapsed / cycle ) * cycle, low, elapsed );
			tickOptions.push_back( low );
			tickOptions.push_back( elapsed );
		}
		else if ( elapsed >= minCycle - LcTuning::TimingSlack )
			tickOptions.push_back( minCycle );
	}

	out.m_iTicks = primary;
	out.m_iPackets = cycle > 0 ? primary / cycle : 0;

	// menu limits ( ragebot: "Extrapolation max ticks" / "Extrapolation max spread" )
	const int maxTicks{ std::clamp( Config::Get<int>( Vars.RagebotExtrapMaxTicks ), 1, 64 ) };
	const float maxSpread{ static_cast< float >( std::clamp( Config::Get<int>( Vars.RagebotExtrapMaxSpread ), 1, 128 ) ) };

	if ( primary > maxTicks )
		return finish( "too far ahead" );

	for ( const int option : tickOptions ) {
		if ( option > maxTicks )
			return finish( "too far ahead" );
	}

	const Vector& mins{ record->m_cAnimData.m_vecMins };
	const Vector& maxs{ record->m_cAnimData.m_vecMaxs };

	State_t start{ };
	Model_t model{ };
	if ( !EstimateStart( track, entry.m_pPlayer, start, model ) )
		return finish( "no motion data" );

	// the newest record itself is where the server has them: nothing to move
	const auto primaryState{ Simulate( start, model, primary, mins, maxs ) };
	out.m_vecOrigin = primaryState.m_vecOrigin;

	const auto addAlternative{ [ & ]( const Vector& origin ) {
		const float distance{ ( origin - out.m_vecOrigin ).Length( ) };
		if ( distance < 0.25f )
			return;

		out.m_vecAlternatives.push_back( origin );
		out.m_flSpread = std::max( out.m_flSpread, distance );
	} };

	// timing error: one packet more or less
	for ( const int option : tickOptions ) {
		if ( option != primary )
			addAlternative( Simulate( start, model, option, mins, maxs ).m_vecOrigin );
	}

	if ( primary > 0 ) {
		// model error: they stop turning / accelerating
		if ( std::abs( model.m_flTurn ) > 0.5f || std::abs( model.m_flAccel ) > 1.f ) {
			Model_t straight{ model };
			straight.m_flTurn = 0.f;
			straight.m_flAccel = 0.f;
			addAlternative( Simulate( start, straight, primary, mins, maxs ).m_vecOrigin );
		}

		// hop error: they touch the ground during the prediction and may or may not jump again
		if ( primaryState.m_bLanded ) {
			Model_t hop{ model };
			hop.m_bBhop = !model.m_bBhop;
			addAlternative( Simulate( start, hop, primary, mins, maxs ).m_vecOrigin );
		}
	}

	if ( out.m_flSpread > maxSpread )
		return finish( "outcomes too far apart" );

	out.m_bValid = true;
	return finish( "" );
}
