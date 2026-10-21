#pragma once
// CorgiHak lag compensation breaker prediction.
//
// When a target's own update cycle moves them more than 64 units ( 2D ) between two server lag records, the server
// refuses to backtrack past that jump and the shot is tested against wherever the target is on the server at the
// moment our command runs. That position is the newest record plus every packet of theirs the server has processed
// since, each packet adding one choke cycle of movement. This predicts how many of those packets there will be,
// moves the target that far with their own movement trend ( turn rate, acceleration, gravity, bunny hops, world
// collision ), and keeps every other plausible outcome so the ragebot only fires at a point that is inside the
// hitbox in all of them.
#include "../animations/animation.h"

#include <array>
#include <deque>
#include <vector>

namespace LcTuning {
	constexpr float TeleportDistSqr{ 4096.f };   // server LAG_COMPENSATION_TELEPORTED_DISTANCE_SQR ( 64 units, 2D )
	constexpr float BreakMargin{ 0.9f };         // a cycle already covering 90% of 64 units breaks it as soon as they speed up
	constexpr int   MaxSamples{ 12 };            // updates remembered per player ( survives the record wipe on an lc break )
	constexpr int   MaxTicks{ 24 };              // never predict further ahead than this
	constexpr int   MaxCycle{ 22 };              // longer gaps are dormancy / packet loss, not a choke cycle
	constexpr int   TimingSlack{ 1 };            // ticks of arrival jitter: a packet this close to our shot may or may not be in
	constexpr float MaxTurnPerTick{ 12.f };      // degrees of velocity rotation per tick ( hard air strafe )
	constexpr float MaxAccelPerTick{ 25.f };     // units / s gained or lost per tick
	constexpr float MinTurnSpeed{ 30.f };        // below this speed the direction is noise, no turn rate is measured
	constexpr float MaxSpread{ 40.f };           // outcomes further apart than this can never share a hitbox: no shot
	constexpr float StepSize{ 18.f };            // sv_stepsize
	constexpr float NonJumpVelocity{ 140.f };    // NON_JUMP_VELOCITY: moving up faster than this is never on ground
}

struct LcSample_t {
	int m_iServerTick{};       // server tick the update was created on ( estimated from the snapshot it came in )
	float m_flSimTime{};
	int m_iTicks{};            // ticks simulated since the previous update ( their choke cycle )
	Vector m_vecOrigin{};
	Vector m_vecVelocity{};
	bool m_bOnGround{};
};

struct LcPrediction_t {
	bool m_bValid{};
	int m_iTicks{};                               // primary: ticks the server simulated past the newest record
	int m_iPackets{};                             // their packets the server processes before our shot
	int m_iCycle{};                               // their choke cycle ( ticks per packet )
	bool m_bFixedCycle{};                         // cycle is constant ( fakelag limit ) rather than random
	Vector m_vecOrigin{};                         // primary predicted origin
	std::vector< Vector > m_vecAlternatives{};    // every other plausible origin ( timing, turn and hop errors )
	float m_flSpread{};                           // largest distance between the primary and an alternative
	const char* m_szReason{ "" };                 // why it is not valid ( debug overlay )
};

struct LcTrack_t {
	CBasePlayer* m_pPlayer{};
	float m_flSpawnTime{};
	std::deque< LcSample_t > m_dqSamples{};
	LcPrediction_t m_cLast{};                     // last prediction ( debug overlay )
	int m_iLastPredictTick{ -1 };

	void Reset( );
};

class CLagCompPredictor {
public:
	// call every command for every enemy, before any prediction is asked for
	void Update( int index, PlayerEntry& entry );

	// their own update cycle moves them further than the server's teleport distance ( or is about to )
	bool BreaksLagComp( int index, const PlayerEntry& entry ) const;

	// where the server will have them when a shot sent this command is processed
	LcPrediction_t Predict( int index, PlayerEntry& entry );

	const LcTrack_t& Track( int index ) const;
	void ResetAll( );

private:
	struct Model_t {
		float m_flTurn{};          // degrees per tick
		float m_flAccel{};         // units / s per tick
		float m_flMaxGroundSpeed{};
		bool m_bBhop{};
	};

	struct State_t {
		Vector m_vecOrigin{};
		Vector m_vecVelocity{};
		bool m_bOnGround{};
		bool m_bLanded{};
	};

	LcTrack_t& TrackFor( int index );

	bool EstimateStart( const LcTrack_t& track, CBasePlayer* player, State_t& state, Model_t& model ) const;
	State_t Simulate( State_t state, const Model_t& model, int ticks, const Vector& mins, const Vector& maxs ) const;
	void SimulateTick( State_t& state, const Model_t& model, const Vector& mins, const Vector& maxs ) const;
	void FlyMove( State_t& state, const Vector& velocity, float time, const Vector& mins, const Vector& maxs ) const;
	void GroundMove( State_t& state, float time, const Vector& mins, const Vector& maxs ) const;
	void Categorize( State_t& state, const Vector& mins, const Vector& maxs ) const;

	std::array< LcTrack_t, 64 > m_arrTracks{};
};

namespace Features { inline CLagCompPredictor LagCompPredictor; };
