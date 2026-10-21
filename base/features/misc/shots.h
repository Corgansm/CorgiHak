#pragma once
#include "../../context.h"
#include "../animations/animation.h"
#include "logger.h"

struct CBBShit_t {
	float m_flCBBMaxz{ };
	float m_flCBBTime{ };
	float m_flViewOffsetZ{ };
	CBaseHandle m_hGroundEntity{ };
};


struct Shot_t {
	Shot_t( CBasePlayer* player,
		std::shared_ptr< LagRecord_t > record,
		int hitgroup, bool safe, 
		matrix3x4_t mat[ ],
		int resolverSide, 
		Vector start, Vector end, 
		float yaw, bool extrapolated, CBBShit_t cbbShit ) : m_pPlayer( player ), m_pRecord( record ), m_iHitgroup( hitgroup ),
		m_vecStart( start ), m_iResolverSide( resolverSide ), m_flFireTime( Interfaces::Globals->flRealTime ), 
		m_vecPredEnd( end ), m_bSafepoint( safe ), m_flCurTime( ctx.m_flFixedCurtime ), m_flYaw( yaw ), m_cCBBShit( cbbShit ),
		m_bExtrapolated( extrapolated )
	{
		std::memcpy( m_pMatrix, mat, 256 * sizeof( matrix3x4_t ) ); 
	}

	/* our inital data */
	CBasePlayer* m_pPlayer{ };
	std::shared_ptr< LagRecord_t > m_pRecord{ };
	int m_iHitgroup{ };
	matrix3x4_t m_pMatrix[ 256 ]{ };
	Vector m_vecStart{ };
	Vector m_vecPredEnd{ };
	int m_iResolverSide{ };
	bool m_bSafepoint{ };
	float m_flFireTime{ };
	float m_flCurTime{ };
	float m_flYaw{ };
	bool m_bExtrapolated{ };
	// Resolver evidence belongs to this bullet, including each double-tap bullet.
	bool m_bResolverTracked{ };
	bool m_bResolverUnreliable{ };
	bool m_bResolverReported{ };
	bool m_bResolverMiss{ };
	// why a resolver tracked shot missed, set by CResolverBrain::OnShotResult
	const char* m_szMissReason{ };
	bool m_bHasImpact{ };
	int m_iResolverPattern{ };
	int m_iResolverPhase{ -1 };
	int m_iResolverContext{ };
	float m_flResolverSpawnTime{ };
	std::string m_strResolverKey{ };
	std::string m_strWeaponName{ };

	/* shot log ( CorgiHak_logs\shots_*.csv ) */
	std::string m_strTarget{ };
	int m_iBacktrack{ };        // ticks between the record arriving and the shot
	int m_iExtrapTicks{ };
	int m_iExtrapSpread{ };
	int m_iPredDamage{ };
	int m_iHitchance{ };

	/* double tap follow-up tracking */
	bool m_bAfterShift{ };      // fired before the server acknowledged our last tickbase shift
	bool m_bFollowupNoted{ };

	/* lua aim_fire / aim_hit / aim_miss */
	int m_iLuaId{ };
	int m_iLuaTick{ };
	int m_iLuaHitchance{ };
	bool m_bLuaReported{ };

	/* data given by server */
	bool m_bHitPlayer{ };
	int m_iServerProcessTick{ };
	int m_iServerHitgroup{ };
	int m_iServerDamage{ };
	Vector m_vecServerEnd{ };

	CBBShit_t m_cCBBShit{ };

	/* set after added */
	//int m_iDelayTime{ };
};


class CShots {
public:
	void AddShot( CBasePlayer* player,
		std::shared_ptr< LagRecord_t > record,
		int hitgroup, bool safe,
		matrix3x4_t mat[ ],
		int resolverSide,
		Vector start, Vector end, 
		float yaw, bool extrapolated, CBBShit_t cbbShit ) {
		m_vecShots.emplace_back( player, record, hitgroup, safe, mat, resolverSide, start, end, yaw, extrapolated, cbbShit );
	}

	void ProcessShots( );

	FORCEINLINE Shot_t* LastUnprocessed( ) {
		if ( m_vecShots.empty( ) )
			return nullptr;

		const auto shot = std::find_if(
			m_vecShots.rbegin( ), m_vecShots.rend( ),
			[ ]( const Shot_t& shot ) {
				return /*!shot.m_iDelayTime
					&& */shot.m_iServerProcessTick
					&& shot.m_iServerProcessTick == Interfaces::ClientState->iServerTick;
			}
		);

		return shot == m_vecShots.rend( ) ? nullptr : &*shot;
	}

	std::vector<Shot_t> m_vecShots{ };
};

namespace Features { inline CShots Shots; };
