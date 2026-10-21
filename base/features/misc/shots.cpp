#include "shots.h"
#include "../../core/lua/lua_engine.h"
#include "../rage/exploits.h"
#include "../../utils/crash_log.h"

#include <fstream>

namespace {
	std::string CsvField( std::string text ) {
		// quote everything that could break a column
		std::string out{ "\"" };
		for ( const char c : text ) {
			if ( c == '"' )
				out += "\"\"";
			else if ( c != '\n' && c != '\r' )
				out += c;
		}
		return out + "\"";
	}

	// one row per finished shot in Desktop\CorgiHak_logs\shots_<session>.csv
	void LogShotCsv( const Shot_t& shot, bool hit, const char* reason, bool resolverMiss ) {
		if ( !CrashLog::g_LogDir[ 0 ] )
			return;

		static std::string path{ };
		static bool header{ };
		if ( path.empty( ) )
			path = std::string( CrashLog::g_LogDir ) + "\\shots_" + CrashLog::g_Stamp + ".csv";

		std::ofstream file( path, std::ios::app );
		if ( !file.is_open( ) )
			return;

		if ( !header ) {
			header = true;
			file << "time,target,weapon,aim_hitgroup,server_hitgroup,result,reason,resolver_miss,pred_damage,server_damage,"
				"hitchance,backtrack_ticks,extrap_ticks,extrap_spread,resolver_side,pattern,safepoint,dt_followup\n";
		}

		const auto now{ std::time( nullptr ) };
		std::tm local{ };
		localtime_s( &local, &now );
		char time[ 16 ]{ };
		std::strftime( time, sizeof( time ), "%H:%M:%S", &local );

		const char* detail{ hit ? "" : shot.m_szMissReason ? shot.m_szMissReason : reason ? reason : "" };

		file << time << ','
			<< CsvField( shot.m_strTarget ) << ','
			<< CsvField( shot.m_strWeaponName ) << ','
			<< Features::Ragebot.HitgroupToString( shot.m_iHitgroup ) << ','
			<< ( shot.m_iServerProcessTick && shot.m_bHitPlayer ? Features::Ragebot.HitgroupToString( shot.m_iServerHitgroup ) : std::string( "-" ) ) << ','
			<< ( hit ? "hit" : "miss" ) << ','
			<< CsvField( detail ) << ','
			<< ( resolverMiss ? 1 : 0 ) << ','
			<< shot.m_iPredDamage << ','
			<< shot.m_iServerDamage << ','
			<< shot.m_iHitchance << ','
			<< shot.m_iBacktrack << ','
			<< shot.m_iExtrapTicks << ','
			<< shot.m_iExtrapSpread << ','
			<< shot.m_iResolverSide << ','
			<< ( shot.m_bResolverTracked && shot.m_iResolverPattern >= 0 && shot.m_iResolverPattern < static_cast< int >( EAimPattern::Count )
				? CResolverBrain::PatternName( static_cast< EAimPattern >( shot.m_iResolverPattern ) ) : "-" ) << ','
			<< ( shot.m_bSafepoint ? 1 : 0 ) << ','
			<< ( shot.m_bAfterShift ? 1 : 0 ) << '\n';
	}

	// every finished shot: resolver miss streaks + lua aim_hit / aim_miss ( once per shot )
	// resolverMiss: the bullet went through the side we picked and still missed ( the only miss that is the
	// resolver's fault, spread / flicks / defensive / extrapolation never count toward body aim streaks )
	void ReportShot( Shot_t& shot, bool hit, const char* reason, bool resolverMiss = false ) {
		if ( shot.m_bLuaReported )
			return;

		if ( shot.m_pPlayer )
			Features::Ragebot.m_cBrain.NoteOutcome( shot.m_pPlayer->Index( ), hit, !hit && resolverMiss );

		Lua::OnAimResult( shot, hit, reason );
		LogShotCsv( shot, hit, reason, !hit && resolverMiss );
		shot.m_bLuaReported = true;
	}

	// gamesense aim_miss reasons ( "?" = resolver )
	const char* LuaMissReason( const Shot_t& shot ) {
		const std::string reason{ shot.m_szMissReason ? shot.m_szMissReason : "" };
		if ( reason.find( "spread" ) != std::string::npos || reason.find( "occlusion" ) != std::string::npos )
			return "spread";
		if ( reason.find( "prediction" ) != std::string::npos || reason.find( "lag" ) != std::string::npos
			|| reason.find( "extrapolat" ) != std::string::npos || reason.find( "flick" ) != std::string::npos
			|| reason.find( "defensive" ) != std::string::npos || reason.find( "expired" ) != std::string::npos
			|| reason.find( "double tap" ) != std::string::npos )
			return "prediction error";
		if ( reason.find( "death" ) != std::string::npos || reason.find( "died" ) != std::string::npos || reason.find( "respawn" ) != std::string::npos )
			return "death";
		return "?";
	}
}

void CShots::ProcessShots( ) {
	if ( m_vecShots.empty( ) )
		return;

	if ( !ctx.m_pLocal )
		return m_vecShots.clear( );

	if ( !ctx.m_pWeapon )
		return;

	m_vecShots.erase(
		std::remove_if(
			m_vecShots.begin( ), m_vecShots.end( ),
			[ & ]( const Shot_t& shot ) -> bool {
				const auto remove{ Interfaces::Globals->flRealTime - shot.m_flFireTime > 1.f };

				if ( !shot.m_iServerProcessTick
					&& remove ) {
					ReportShot( const_cast< Shot_t& >( shot ), false, ctx.m_pLocal->IsDead( ) ? "death" : "unregistered shot" );
					if ( ctx.m_pLocal->IsDead( ) )
						Features::Logger.Log( _( "local player died before server processed shots" ), true );
					else if ( shot.m_bAfterShift ) {
						Features::Exploits.NoteFollowup( false );
						Features::Logger.Log( _( "shot did not register ( double tap follow-up fired before the server acknowledged the shift )" ), true );
					}
					else
						Features::Logger.Log( _( "shot did not register" ), true );
				}
					
				return remove;
			}
		),
		m_vecShots.end( )
				);

	// new packet eyeangles is different: eyeangles fix miss
	// max intersects: lagcompensation miss

	if ( m_vecShots.empty( ) )
		return;

	ctx.m_bInCreatemove = true;

	for ( auto it{ m_vecShots.begin( ) }; it != m_vecShots.end( ); ) {
		auto& shot{ *it };
		if ( !shot.m_pPlayer ) {
			NONEXISTANT:
			ReportShot( shot, false, "death" );
			Features::Logger.Log( _( "player no longer exists" ), true );
			it = m_vecShots.erase( it );
			if ( m_vecShots.empty( ) )
				break;

			continue;
		}

		// the server fired it: a follow-up that registered counts for the double tap check
		if ( shot.m_iServerProcessTick && shot.m_bAfterShift && !shot.m_bFollowupNoted ) {
			shot.m_bFollowupNoted = true;
			Features::Exploits.NoteFollowup( true );
		}

		if ( !shot.m_iServerProcessTick || !shot.m_bHasImpact ) {
			++it;
			continue;
		}


		const auto playerInfo{ Interfaces::Engine->GetPlayerInfo( shot.m_pPlayer->Index( ) ) };
		if ( !playerInfo.has_value( ) )
			goto NONEXISTANT;// cbf to repaste the same shit

		Features::Ragebot.m_cBrain.OnShotResult( shot );
		if ( shot.m_bResolverTracked ) {
			ReportShot( shot, shot.m_bHitPlayer, LuaMissReason( shot ), shot.m_bResolverMiss );
			if ( shot.m_bHitPlayer )
				Features::Logger.Log( std::string( "hit " ) + playerInfo->szName + " in "
					+ Features::Ragebot.HitgroupToString( shot.m_iServerHitgroup )
					+ " for " + std::to_string( shot.m_iServerDamage ), true );
			else
				Features::Logger.Log( std::string( "missed " ) + playerInfo->szName + " due to "
					+ ( shot.m_szMissReason ? shot.m_szMissReason : shot.m_bResolverMiss ? "resolver" : "unknown" ), true );
			// The evidence-based resolver owns this bullet. Do not also mutate the
			// legacy LBY inversion and aggregate miss heuristics below.
			it = m_vecShots.erase( it );
			continue;
		}

		if ( shot.m_pPlayer->IsDead( ) ) {
			ReportShot( shot, shot.m_bHitPlayer, "death" );
			if ( shot.m_bHitPlayer )
				Features::Logger.Log( ( _( "hit " ) + ( std::string )playerInfo->szName 
					+ _( " in the " ) + Features::Ragebot.HitgroupToString( shot.m_iServerHitgroup ) 
					+ _( " for " ) + std::to_string( shot.m_iServerDamage ).c_str( ) ) + _( " damage" ), true );
			else
				Features::Logger.Log( _( "target died before server processed shot" ), true );

			it = m_vecShots.erase( it );
			if ( m_vecShots.empty( ) )
				break;

			continue;
		}

		LagBackup_t backup{ shot.m_pPlayer };
		CGameTrace trace{ };
		const char* luaReason{ "?" };
		bool resolverMiss{ };

		shot.m_pRecord->Apply( shot.m_pPlayer );
		std::memcpy( shot.m_pPlayer->m_CachedBoneData( ).Base( ), shot.m_pMatrix, shot.m_pPlayer->m_CachedBoneData().Size() * sizeof( matrix3x4_t ) );

		Interfaces::EngineTrace->ClipRayToEntity(
			{ shot.m_vecStart, shot.m_vecServerEnd },
			MASK_SHOT_PLAYER, shot.m_pPlayer, &trace
		);

		/*if ( shot.m_flYaw != -1
			&& std::abs( shot.m_pPlayer->m_angEyeAngles( ).y - shot.m_flYaw ) > 5.f ) {
			Interfaces::ConVar->ConsoleColorPrintf( ACCENT, _( "[ CorgiHak ] " ) );
			const auto& yawRecords{ Features::AnimSys.m_arrEntries.at( shot.m_pPlayer->Index( ) - 1 ).m_flPreviousYaws };

			Interfaces::ConVar->ConsoleColorPrintf( Color( 250, 107, 110 ), _( "Eyeangles differed | " ) );
			Interfaces::ConVar->ConsoleColorPrintf( Color( 250, 107, 110 ),
				( _( "pred: " ) + std::to_string( int( shot.m_flYaw ) ) + _( " actual: " ) + std::to_string( int( shot.m_pPlayer->m_angEyeAngles( ).y ) )
					+ "\n" ).c_str( ) );
		}*/

		auto& entry{ Features::AnimSys.m_arrEntries.at( shot.m_pPlayer->Index( ) - 1 ) };

		if ( shot.m_bHitPlayer ) {
			if ( shot.m_iServerHitgroup != shot.m_iHitgroup ) {
				// we misresolved but still hit the player
				if ( trace.iHitGroup == shot.m_iHitgroup
					&& trace.pHitEntity == shot.m_pPlayer ) {

					std::memcpy( shot.m_pPlayer->m_CachedBoneData( ).Base( ), shot.m_pRecord->m_cAnimData.m_arrSides.at( shot.m_iResolverSide ).m_pMatrix, shot.m_pPlayer->m_CachedBoneData().Size() * sizeof( matrix3x4_t ) );

					const auto backupCurtime{ Interfaces::Globals->flCurTime };
					Interfaces::Globals->flCurTime = shot.m_flCurTime;
					//shot.m_pPlayer->ClampBonesInBBOX( shot.m_pPlayer->m_CachedBoneData( ).Base( ), BONE_USED_BY_SERVER );
					Interfaces::Globals->flCurTime = backupCurtime;

					Interfaces::EngineTrace->ClipRayToEntity(
						{ shot.m_vecStart, shot.m_vecServerEnd },
						MASK_SHOT_PLAYER, shot.m_pPlayer, &trace
					);

					if ( trace.pHitEntity != shot.m_pPlayer
						|| trace.iHitGroup != shot.m_iServerHitgroup ) {
						std::string log{ _( "hit incorrect hitbox due to inconsistency" ) };
						if ( shot.m_flYaw != shot.m_pPlayer->m_angEyeAngles( ).y )
							log += " | YAW " + std::to_string( int( shot.m_flYaw ) ) + " - " + std::to_string( int( shot.m_pPlayer->m_angEyeAngles( ).y ) );

						//if ( shot.m_cCBBShit.m_flCBBMaxz != shot.m_pPlayer->m_flNewBoundsMaxs( ) )
							//log += " | MAXS " + std::to_string( shot.m_cCBBShit.m_flCBBMaxz ) + " - " + std::to_string( shot.m_pPlayer->m_flNewBoundsMaxs( ) );

						//if ( shot.m_cCBBShit.m_flCBBTime != shot.m_pPlayer->m_flNewBoundsTime( ) )
						//	log += " | TIME " + std::to_string( shot.m_cCBBShit.m_flCBBTime ) + " - " + std::to_string( shot.m_pPlayer->m_flNewBoundsTime( ) );

						if ( shot.m_cCBBShit.m_flViewOffsetZ != shot.m_pPlayer->m_vecViewOffset( ).z )
							log += " | VIEW " + std::to_string( shot.m_cCBBShit.m_flViewOffsetZ ) + " - " + std::to_string( shot.m_pPlayer->m_vecViewOffset( ).z );

						if ( ( shot.m_cCBBShit.m_hGroundEntity != INVALID_EHANDLE_INDEX ) != ( shot.m_pPlayer->m_hGroundEntity( ) != INVALID_EHANDLE_INDEX ) )
							log += " | GENT " + std::to_string( ( shot.m_cCBBShit.m_hGroundEntity != INVALID_EHANDLE_INDEX ) ) + " - " + std::to_string( ( shot.m_pPlayer->m_hGroundEntity( ) != INVALID_EHANDLE_INDEX ) );

						Features::Logger.Log( log, false );
					}
					else {
						std::memcpy( shot.m_pPlayer->m_CachedBoneData( ).Base( ), shot.m_pRecord->m_cAnimData.m_arrSides.at( SWAP_RESIK_SIDE( shot.m_iResolverSide ) ).m_pMatrix, shot.m_pPlayer->m_CachedBoneData().Size() * sizeof( matrix3x4_t ) );

						Interfaces::Globals->flCurTime = shot.m_flCurTime;
						//shot.m_pPlayer->ClampBonesInBBOX( shot.m_pPlayer->m_CachedBoneData( ).Base( ), BONE_USED_BY_SERVER );
						Interfaces::Globals->flCurTime = backupCurtime;

						Interfaces::EngineTrace->ClipRayToEntity(
							{ shot.m_vecStart, shot.m_vecServerEnd },
							MASK_SHOT_PLAYER, shot.m_pPlayer, &trace
						);

						if ( trace.pHitEntity == shot.m_pPlayer
							&& trace.iHitGroup == shot.m_iServerHitgroup ) {
							Features::Logger.Log( _( "hit incorrect hitbox due to resolver" ), false );
							entry.m_iResolverSide = SWAP_RESIK_SIDE( shot.m_iResolverSide );

							if ( entry.m_bLBYResolved )
								entry.m_bInvertResolverOrientation = !entry.m_bInvertResolverOrientation;

							entry.m_bLBYResolved = false;
						}
						else {
							const auto lastSide{ shot.m_iResolverSide == 0 ? 1 : 0 };

							std::memcpy( shot.m_pPlayer->m_CachedBoneData( ).Base( ), shot.m_pRecord->m_cAnimData.m_arrSides.at( lastSide ).m_pMatrix, shot.m_pPlayer->m_CachedBoneData().Size() * sizeof( matrix3x4_t ) );

							Interfaces::Globals->flCurTime = shot.m_flCurTime;
							//shot.m_pPlayer->ClampBonesInBBOX( shot.m_pPlayer->m_CachedBoneData( ).Base( ), BONE_USED_BY_SERVER );
							Interfaces::Globals->flCurTime = backupCurtime;

							Interfaces::EngineTrace->ClipRayToEntity(
								{ shot.m_vecStart, shot.m_vecServerEnd },
								MASK_SHOT_PLAYER, shot.m_pPlayer, &trace
							);

							if ( trace.pHitEntity == shot.m_pPlayer
								&& trace.iHitGroup == shot.m_iServerHitgroup )
								Features::Logger.Log( _( "hit incorrect hitbox due to unknown" ), false );
							else {
								Features::Logger.Log( _( "hit incorrect hitbox due to resolver" ), false );
								entry.m_iResolverSide = lastSide;

								if ( entry.m_bLBYResolved )
									entry.m_bInvertResolverOrientation = !entry.m_bInvertResolverOrientation;

								entry.m_bLBYResolved = false;
							}
						}
					}
				}
				else
					Features::Logger.Log( _( "hit incorrect hitbox due to spread" ), false );
			}

			Features::Logger.Log( ( _( "hit " ) + ( std::string )playerInfo->szName
				+ _( " in " ) + Features::Ragebot.HitgroupToString( shot.m_iServerHitgroup )
				+ _( " for " ) + std::to_string( shot.m_iServerDamage ).c_str( ) ) + _( " damage" ), true );
		}
		else {
			if ( trace.pHitEntity == shot.m_pPlayer ) {
				// new eye yaw was networked by now
				std::memcpy( shot.m_pPlayer->m_CachedBoneData( ).Base( ), shot.m_pRecord->m_cAnimData.m_arrSides.at( shot.m_iResolverSide ).m_pMatrix, shot.m_pPlayer->m_CachedBoneData().Size() * sizeof( matrix3x4_t ) );

				const auto backupCurtime{ Interfaces::Globals->flCurTime };
				Interfaces::Globals->flCurTime = shot.m_flCurTime;
				//shot.m_pPlayer->ClampBonesInBBOX( shot.m_pPlayer->m_CachedBoneData( ).Base( ), BONE_USED_BY_SERVER );
				Interfaces::Globals->flCurTime = backupCurtime;

				Interfaces::EngineTrace->ClipRayToEntity(
					{ shot.m_vecStart, shot.m_vecServerEnd },
					MASK_SHOT_PLAYER, shot.m_pPlayer, &trace
				);

				if ( trace.pHitEntity != shot.m_pPlayer ) {
					std::string log{ _( "missed shot due to inconsistency" ) };
					if ( shot.m_flYaw != shot.m_pPlayer->m_angEyeAngles( ).y )
						log += " | YAW " + std::to_string( int( shot.m_flYaw ) ) + " - " + std::to_string( int( shot.m_pPlayer->m_angEyeAngles( ).y ) );

					//if ( shot.m_cCBBShit.m_flCBBMaxz != shot.m_pPlayer->m_flNewBoundsMaxs( ) )
					//	log += " | MAXS " + std::to_string( shot.m_cCBBShit.m_flCBBMaxz ) + " - " + std::to_string( shot.m_pPlayer->m_flNewBoundsMaxs( ) );

					//if ( shot.m_cCBBShit.m_flCBBTime != shot.m_pPlayer->m_flNewBoundsTime( ) )
					//	log += " | TIME " + std::to_string( shot.m_cCBBShit.m_flCBBTime ) + " - " + std::to_string( shot.m_pPlayer->m_flNewBoundsTime( ) );

					if ( shot.m_cCBBShit.m_flViewOffsetZ != shot.m_pPlayer->m_vecViewOffset( ).z )
						log += " | VIEW " + std::to_string( shot.m_cCBBShit.m_flViewOffsetZ ) + " - " + std::to_string( shot.m_pPlayer->m_vecViewOffset( ).z );

					if ( ( shot.m_cCBBShit.m_hGroundEntity != INVALID_EHANDLE_INDEX ) != ( shot.m_pPlayer->m_hGroundEntity( ) != INVALID_EHANDLE_INDEX ) )
						log += " | GENT " + std::to_string( ( shot.m_cCBBShit.m_hGroundEntity != INVALID_EHANDLE_INDEX ) ) + " - " + std::to_string( ( shot.m_pPlayer->m_hGroundEntity( ) != INVALID_EHANDLE_INDEX ) );

					Features::Logger.Log( log, true );
				}
				else {
					std::memcpy( shot.m_pPlayer->m_CachedBoneData( ).Base( ), shot.m_pRecord->m_cAnimData.m_arrSides.at( SWAP_RESIK_SIDE( shot.m_iResolverSide ) ).m_pMatrix, shot.m_pPlayer->m_CachedBoneData().Size() * sizeof( matrix3x4_t ) );

					Interfaces::Globals->flCurTime = shot.m_flCurTime;
					//shot.m_pPlayer->ClampBonesInBBOX( shot.m_pPlayer->m_CachedBoneData( ).Base( ), BONE_USED_BY_SERVER );
					Interfaces::Globals->flCurTime = backupCurtime;

					Interfaces::EngineTrace->ClipRayToEntity(
						{ shot.m_vecStart, shot.m_vecServerEnd },
						MASK_SHOT_PLAYER, shot.m_pPlayer, &trace
					);

					if ( trace.pHitEntity != shot.m_pPlayer ) {
						{ luaReason = "?"; resolverMiss = true; Features::Logger.Log( _( "missed shot due to resolver" ), true ); }
						entry.m_iResolverSide = SWAP_RESIK_SIDE( shot.m_iResolverSide );

						if ( entry.m_bLBYResolved )
							entry.m_bInvertResolverOrientation = !entry.m_bInvertResolverOrientation;

						entry.m_bLBYResolved = false;
					}
					else {
						const auto lastSide{ shot.m_iResolverSide == 0 ? 1 : 0 };

						// TODO: decipher if it was an extrapolation issue that caused us to miss by shifting the bones to the networked origin
						if ( shot.m_bExtrapolated 
							&& ( ( !Config::Get<bool>( Vars.RagebotLagcompensation ) && int( shot.m_pRecord->m_cAnimData.m_vecOrigin != shot.m_pPlayer->m_vecOrigin( ) ) )
								|| ( Config::Get<bool>( Vars.RagebotLagcompensation ) && entry.m_pRecords.size( ) && int( shot.m_pRecord->m_cAnimData.m_vecOrigin != entry.m_pRecords.back( )->m_cAnimData.m_vecOrigin ) ) ) ) {
							Interfaces::ConVar->ConsoleColorPrintf( ACCENT, _( "[ CorgiHak ] " ) );
							Interfaces::ConVar->ConsoleColorPrintf( Color( 250, 107, 110 ), _( "Extrapolation origin differed | " ) );

							const auto origin{ Config::Get<bool>( Vars.RagebotLagcompensation ) ? entry.m_pRecords.back( )->m_cAnimData.m_vecOrigin : shot.m_pPlayer->m_vecOrigin( ) };

							Interfaces::ConVar->ConsoleColorPrintf( Color( 250, 107, 110 ),
								( _( "difference: " ) + std::to_string( int( ( shot.m_pRecord->m_cAnimData.m_vecOrigin - origin ).Length( ) ) )
									+ "\n" ).c_str( ) );
						}

						std::memcpy( shot.m_pPlayer->m_CachedBoneData( ).Base( ), shot.m_pRecord->m_cAnimData.m_arrSides.at( lastSide ).m_pMatrix, shot.m_pPlayer->m_CachedBoneData().Size() * sizeof( matrix3x4_t ) );

						Interfaces::Globals->flCurTime = shot.m_flCurTime;
						//shot.m_pPlayer->ClampBonesInBBOX( shot.m_pPlayer->m_CachedBoneData( ).Base( ), BONE_USED_BY_SERVER );
						Interfaces::Globals->flCurTime = backupCurtime;

						Interfaces::EngineTrace->ClipRayToEntity(
							{ shot.m_vecStart, shot.m_vecServerEnd },
							MASK_SHOT_PLAYER, shot.m_pPlayer, &trace
						);

						if ( trace.pHitEntity == shot.m_pPlayer ) {
							const auto& records{ Features::AnimSys.m_arrEntries.at( shot.m_pPlayer->Index( ) - 1 ).m_pRecords };
							if ( shot.m_bExtrapolated )
								{ luaReason = "prediction error"; Features::Logger.Log( _( "missed shot due to extrapolation error" ), true ); }
							else if ( records.size( ) && records.front( )->m_iReceiveTick > shot.m_pRecord->m_iReceiveTick )
								{ luaReason = "prediction error"; Features::Logger.Log( _( "missed shot due to lagcompensation" ), true ); }
							else
								Features::Logger.Log( _( "missed shot due to unknown" ), true );
						}
						else {
							{ luaReason = "?"; resolverMiss = true; Features::Logger.Log( _( "missed shot due to resolver" ), true ); }
							entry.m_iResolverSide = lastSide;

							if ( entry.m_bLBYResolved )
								entry.m_bInvertResolverOrientation = !entry.m_bInvertResolverOrientation;

							entry.m_bLBYResolved = false;
						}
					}
				}

				Features::AnimSys.m_arrEntries.at( shot.m_pPlayer->Index( ) - 1 ).m_iMissedShots++;
			}
			else {
				const auto dir{ ( shot.m_vecServerEnd - shot.m_vecStart ).Normalized( ) };

				const auto vecEnd{ shot.m_vecStart + ( dir * 8192.f ) };

				Interfaces::EngineTrace->ClipRayToEntity(
					{ shot.m_vecStart, vecEnd },
					MASK_SHOT_PLAYER, shot.m_pPlayer, &trace
				);

				// cheeky curb the crash with a spread miss
				if ( trace.pHitEntity == shot.m_pPlayer
					&& ctx.m_pWeaponData ) {
					const auto data{ Features::Autowall.FireBullet( ctx.m_pLocal, shot.m_pPlayer, ctx.m_pWeaponData,
						ctx.m_pWeapon->m_iItemDefinitionIndex( ) == WEAPON_TASER,
						shot.m_vecStart, vecEnd, true ) };

					if ( data.dmg >= 1 )
						Features::Logger.Log( _( "missed shot due to autowall issue" ), true );
					else
						{ luaReason = "spread"; Features::Logger.Log( _( "missed shot due to spread" ), true ); }
				}
				else
					{ luaReason = "spread"; Features::Logger.Log( _( "missed shot due to spread" ), true ); }
			}
		}

		backup.Apply( shot.m_pPlayer );

		ReportShot( shot, shot.m_bHitPlayer, luaReason, resolverMiss );
		it = m_vecShots.erase( it );

		if ( m_vecShots.empty( ) )
			break;
	}

	ctx.m_bInCreatemove = false;
}
