#include "visuals.h"
#include "../animations/animation.h"
#include "../rage/antiaim.h"
#include "../rage/ragebot.h"
#include <ctime>
#include <cctype>
#include <functional>
#include "../../core/prop_manager.h"

// shared CorgiHak HUD panel: shadow, dark body, hairline outline, accent strip on top
static void DrawHudPanel( const Vector2D& pos, const Vector2D& size, float alpha ) {
	const auto a{ [ alpha ]( Color c ) { return c.Set<COLOR_A>( c.Get<COLOR_A>( ) * alpha ); } };

	Render::FilledRoundedBox( pos - Vector2D( 2, 2 ), size + Vector2D( 4, 4 ), 5, 6, a( Color( 0, 0, 0, 60 ) ) );
	Render::FilledRoundedBox( pos - Vector2D( 1, 1 ), size + Vector2D( 2, 2 ), 5, 5, a( OUTLINE_DARK ) );
	Render::FilledRoundedBox( pos, size, 5, 5, a( Color( 17, 17, 20, 235 ) ) );
	Render::RoundedBox( pos, size, 5, 5, a( OUTLINE_LIGHT ) );
	Render::Gradient( static_cast< int >( pos.x + 5 ), static_cast< int >( pos.y ), static_cast< int >( size.x - 10 ), 2, a( ACCENT ), a( ACCENT2 ), true );
}

void CVisuals::Main( ) {
	if ( !ctx.m_pLocal )
		return;

	DormantESP.Start( );

	if ( Interfaces::GameResources )
		ctx.m_iBombCarrier = Interfaces::GameResources->GetC4Carrier( );
	else
		ctx.m_iBombCarrier = -1;

	for ( int i{ }; i <= Interfaces::ClientEntityList->GetHighestEntityIndex( ); i++ ) {
		const auto ent{ static_cast< CBaseEntity* >( Interfaces::ClientEntityList->GetClientEntity( i ) ) };
		if ( !ent )
			continue;

		if ( ent->IsPlayer( ) )
			PlayerESP.Main( static_cast< CBasePlayer* >( ent ) );
		else {
			EntModulate( ent );
			OtherEntities( ent );
		}
	}

	auto& w{ ctx.m_ve2ScreenSize.x };
	auto& h{ ctx.m_ve2ScreenSize.y };

	if ( Config::Get<bool>( Vars.VisCustomScope ) )
		CustomScope( );
	else if ( Config::Get<bool>( Vars.RemovalScope ) && ctx.m_pLocal->m_bIsScoped( ) 
		&& ctx.m_pWeaponData && ctx.m_pWeaponData->nWeaponType == WEAPONTYPE_SNIPER ) {
		Render::Line( Vector2D( 0, h / 2 ), Vector2D( w, h / 2 ), Color( 0, 0, 0 ) );
		Render::Line( Vector2D( w / 2, 0 ), Vector2D( w / 2, h ), Color( 0, 0, 0 ) );
	}

	ManageHitmarkers( );
	AutoPeekIndicator( );
	BulletTracers.Draw( );
	KillEffects.Draw( );

	if ( !ctx.m_pLocal->IsDead( ) ) {
		if ( Config::Get<bool>( Vars.VisPenetrationCrosshair ) ) {
			const auto center = ctx.m_ve2ScreenSize / 2;
			const Color color = ctx.m_bCanPenetrate ? Color( 0, 255, 0, 155 ) : Color( 255, 0, 0, 155 );

			int add{ 2 };
			static auto aa{ Interfaces::ConVar->FindVar( "mat_antialias" ) };
			if ( aa ) {
				if ( aa->GetInt( ) )
					add = 1;
			}

			Render::Line( center - Vector2D( 1, 0 ), center + Vector2D( add, 0 ), color );
			Render::Line( center - Vector2D( 0, 1 ), center + Vector2D( 0, add ), color );
		}

		if ( Config::Get<bool>( Vars.AntiAimManualDirInd ) )
			ManualArrows( );

		if ( Config::Get<bool>( Vars.VisIndicators ) )
			CrosshairIndicators( );

		if ( Config::Get<bool>( Vars.VisMinDmgIndicator ) )
			MinDamageIndicator( );
	}

	//GrenadePrediction.Paint( );

	GrenadePrediction.View( );
	GrenadePrediction.PaintLocal( );
}

const char* modelsForChange[ ] = {
	_( "models/player/custom_player/legacy/tm_jumpsuit_varianta.mdl" ),
	_( "models/player/custom_player/legacy/tm_jumpsuit_variantb.mdl" ),
	_( "models/player/custom_player/legacy/tm_jumpsuit_variantc.mdl" ),
	_( "models/player/custom_player/legacy/ctm_st6_variantm.mdl" ),
	_( "models/player/custom_player/legacy/ctm_st6_variantg.mdl" ),
	_( "models/player/custom_player/legacy/ctm_st6_variante.mdl" ),
	_( "models/player/custom_player/legacy/tm_balkan_varianth.mdl" ),
	_( "models/player/custom_player/legacy/tm_phoenix_varianth.mdl" ),
	_( "models/player/custom_player/legacy/tm_leet_variantf.mdl" ),
	_( "models/player/custom_player/legacy/tm_leet_variantg.mdl" ),
	_( "models/player/custom_player/legacy/ctm_sas_variantf.mdl" ),
	_( "models/player/custom_player/legacy/tm_anarchist.mdl" ),
	_( "models/player/custom_player/legacy/tm_anarchist_varianta.mdl" ),
};

void CVisuals::ModelChanger( ) {
	static int lastModelIndex{ };

	if ( Config::Get<bool>( Vars.MiscCustomModelChanger ) ) {
		int iModelIndex = 0;
		iModelIndex = Interfaces::ModelInfo->GetModelIndex( ( "models/player/custom_player/legacy/" + ( Config::Get<std::string>( Vars.MiscCustomModelChangerString ) ) + ".mdl" ).c_str( ) );
		if ( iModelIndex != 0
			&& lastModelIndex != iModelIndex )
			ctx.m_pLocal->SetModelIndex( iModelIndex );

		lastModelIndex = iModelIndex;
		return;
	}

	if ( ctx.m_pLocal->m_nModelIndex( ) != lastModelIndex ) {
		lastModelIndex = 0;
	}


	const auto team{ ctx.m_pLocal->m_iTeamNum( ) };

	int modelIndex{ };
	if ( team == TEAM_TT )
		modelIndex = Interfaces::ModelInfo->GetModelIndex( modelsForChange[ Config::Get<int>( Vars.MiscPlayerModelT ) ] );
	else if ( team == TEAM_CT )
		modelIndex = Interfaces::ModelInfo->GetModelIndex( modelsForChange[ Config::Get<int>( Vars.MiscPlayerModelCT ) ] );

	if ( modelIndex == -1 ) {
		if ( team == TEAM_TT ) {
			PrecacheModel( modelsForChange[ Config::Get<int>( Vars.MiscPlayerModelT ) ] );
			modelIndex = Interfaces::ModelInfo->GetModelIndex( modelsForChange[ Config::Get<int>( Vars.MiscPlayerModelT ) ] );
		}
		else if ( team == TEAM_CT ) {
			PrecacheModel( modelsForChange[ Config::Get<int>( Vars.MiscPlayerModelCT ) ] );
			modelIndex = Interfaces::ModelInfo->GetModelIndex( modelsForChange[ Config::Get<int>( Vars.MiscPlayerModelCT ) ] );
		}
	}

	if ( modelIndex != -1
		&& lastModelIndex != modelIndex ) {
		ctx.m_pLocal->SetModelIndex( modelIndex );
		lastModelIndex = modelIndex;
	}
}

void CVisuals::SkyboxChanger( ) {
	std::string sv_skyname{ Displacement::Cvars.sv_skyname->GetString( ) };

	switch ( Config::Get<int>( Vars.VisWorldSkybox ) )
	{
	case 1:
		sv_skyname = _( "cs_tibet" );
		break;
	case 2:
		sv_skyname = _( "cs_baggage_kybox_" );
		break;
	case 3:
		sv_skyname = _( "italy" );
		break;
	case 4:
		sv_skyname = _( "jungle" );
		break;
	case 5:
		sv_skyname = _( "office" );
		break;
	case 6:
		sv_skyname = _( "sky_cs15_daylight01_hdr" );
		break;
	case 7:
		sv_skyname = _( "sky_cs15_daylight02_hdr" );
		break;
	case 8:
		sv_skyname = _( "vertigoblue_hdr" );
		break;
	case 9:
		sv_skyname = _( "vertigo" );
		break;
	case 10:
		sv_skyname = _( "sky_day02_05_hdr" );
		break;
	case 11:
		sv_skyname = _( "nukeblank" );
		break;
	case 12:
		sv_skyname = _( "sky_venice" );
		break;
	case 13:
		sv_skyname = _( "sky_cs15_daylight03_hdr" );
		break;
	case 14:
		sv_skyname = _( "sky_cs15_daylight04_hdr" );
		break;
	case 15:
		sv_skyname = _( "sky_csgo_cloudy01" );
		break;
	case 16:
		sv_skyname = _( "sky_csgo_night02" );
		break;
	case 17:
		sv_skyname = _( "sky_csgo_night02b" );
		break;
	case 18:
		sv_skyname = _( "sky_csgo_night_flat" );
		break;
	case 19:
		sv_skyname = _( "sky_dust" );
		break;
	case 20:
		sv_skyname = _( "vietnam" );
		break;
	case 21:
		sv_skyname = Config::Get<std::string>( Vars.VisWorldSkyboxCustom );
		break;
	}

	static std::string last{ };
	if ( last == sv_skyname )
		return;

	last = sv_skyname;

	if ( Config::Get<int>( Vars.VisWorldSkybox ) <= 0 ) {
		if ( !Displacement::Cvars.r_3dsky->GetBool( ) )
			Displacement::Cvars.r_3dsky->SetValue( true );
	}
	else if ( Displacement::Cvars.r_3dsky->GetBool( ) )
		Displacement::Cvars.r_3dsky->SetValue( false );

	static auto fnLoadNamedSkys{ ( void( __fastcall* )( const char* ) )Displacement::Sigs.LoadNamedSkys };

	return fnLoadNamedSkys( sv_skyname.c_str( ) );
}

void CVisuals::Watermark( ) {
	// debug output stays where it was
	if ( ctx.m_pLocal && !ctx.m_pLocal->IsDead( ) ) {
		const auto debugPos{ Vector2D( ctx.m_ve2ScreenSize.x - 220.f, 40.f ) };
		int i{ 1 };
		for ( auto& dbg : ctx.m_strDbgLogs ) {
			Render::Text( Fonts::Menu, debugPos + Vector2D( 0, 16 * i ), Color( 255, 255, 255 ), 0, dbg->c_str( ) );
			++i;
		}
	}

	static Anim::Fade_t fade{ };
	fade.Update( Config::Get<bool>( Vars.MiscWatermark ) );
	if ( !fade.Visible( ) )
		return;

	const float alpha{ fade.Alpha( ) };
	const auto a{ [ alpha ]( Color c ) { return c.Set<COLOR_A>( c.Get<COLOR_A>( ) * alpha ); } };

	// CorgiHak | 32 ms | 240 fps | 14:05
	static float smoothFps{ 60.f };
	if ( Interfaces::Globals->flAbsFrameTime > 0.f )
		smoothFps = smoothFps * 0.9f + ( 1.f / Interfaces::Globals->flAbsFrameTime ) * 0.1f;

	int ping{ };
	if ( const auto nci{ Interfaces::Engine->GetNetChannelInfo( ) }; nci && Interfaces::Engine->IsInGame( ) )
		ping = static_cast< int >( nci->GetLatency( 0 ) * 1000.f );

	char timeBuf[ 16 ]{ };
	{
		const auto now{ std::time( nullptr ) };
		std::tm local{ };
		localtime_s( &local, &now );
		std::strftime( timeBuf, sizeof( timeBuf ), "%H:%M", &local );
	}

	const std::string info{ " |  " + std::to_string( ping ) + " ms  |  " + std::to_string( static_cast< int >( smoothFps ) ) + " fps  |  " + timeBuf };

	const auto firstW{ Render::GetTextSize( MENU_NAME_FIRST, Fonts::MenuBold ).x };
	const auto secondW{ Render::GetTextSize( MENU_NAME_SECOND, Fonts::MenuBold ).x };
	const auto infoW{ Render::GetTextSize( info.c_str( ), Fonts::Menu ).x };

	// width glides when the numbers change length instead of snapping
	static float width{ };
	const float targetWidth{ 12.f + firstW + secondW + infoW + 10.f };
	width = width <= 0.f ? targetWidth : Anim::Approach( width, targetWidth );

	const auto size{ Vector2D( width, 24.f ) };
	const auto pos{ Vector2D( ctx.m_ve2ScreenSize.x - size.x - 16.f, 14.f - fade.Offset( ) ) };

	DrawHudPanel( pos, size, alpha );

	Render::Text( Fonts::MenuBold, pos + Vector2D( 10.f, 6.f ), a( Color( 235, 235, 240 ) ), FONT_LEFT, MENU_NAME_FIRST );
	Render::Text( Fonts::MenuBold, pos + Vector2D( 10.f + firstW, 6.f ), a( ACCENT ), FONT_LEFT, MENU_NAME_SECOND );
	Render::Text( Fonts::Menu, pos + Vector2D( 10.f + firstW + secondW, 6.f ), a( Color( 170, 170, 182 ) ), FONT_LEFT, info.c_str( ) );
}

void CVisuals::OtherEntities( CBaseEntity* ent ) {
	if ( !ent )
		return;

	auto client_class = ent->GetClientClass( );
	if ( !client_class )
		return;

	const auto& class_id = client_class->nClassID;


	if ( class_id == EClassIndex::CPlantedC4 ) {
		const auto owner{ static_cast< CBaseEntity* >( Interfaces::ClientEntityList->GetClientEntityFromHandle( ent->m_hOwnerEntity( ) ) ) };
		if ( owner )
			ctx.m_iBombCarrier = owner->Index( );

		if ( strstr( client_class->szNetworkName, _( "Planted" ) ) ) {
			ctx.m_iBombCarrier = -1;
			if ( Config::Get<bool>( Vars.VisBomb ) ) {
				Vector2D world;
					if ( Math::WorldToScreen( ent->m_vecOrigin( ), world ) )
						Render::Text( Fonts::HealthESP, world, Color( 255, 255, 255 ), FONT_CENTER, _( "BOMB" ) );
			}
		}
	}

	const auto distTo = ent->GetAbsOrigin( ).DistTo( ctx.m_pLocal->GetAbsOrigin( ) );
	if ( distTo > 1500.f )
		return;

	float maxAlpha{ std::min( distTo < 1245.f ? 255.f : 1500.f - distTo, 255.f ) };

	if ( !maxAlpha )
		return;

	if ( class_id != EClassIndex::CBaseWeaponWorldModel && ( strstr( client_class->szNetworkName, _( "Weapon" ) ) ) ) {
		if ( Config::Get<bool>( Vars.VisDroppedWeapon ) ) {
			const auto& origin{ ent->m_vecOrigin( ) };
			if ( origin.IsZero( ) )
				return;

			Vector2D world;
			if ( Math::WorldToScreen( origin, world ) )
				Render::Text( Fonts::HealthESP, world,
					Config::Get<Color>( Vars.VisDroppedWeaponCol ).Set<COLOR_A>( maxAlpha ),
					FONT_CENTER, static_cast< CWeaponCSBase* >( ent )->GetGunName( ).c_str( ) );
		}
	}
	else {
		const auto owner{ static_cast< CBasePlayer* >( Interfaces::ClientEntityList->GetClientNetworkableFromHandle( ent->m_hOwnerEntity( ) ) ) };
		if ( !owner || !owner->IsPlayer( ) )
			return;

		if ( strstr( client_class->szNetworkName, _( "Projectile" ) ) ) {
			if ( ( ( owner->IsTeammate( ) && Config::Get<bool>( Vars.VisGrenadesTeam ) )
				|| ( !owner->IsTeammate( ) && Config::Get<bool>( Vars.VisGrenadesEnemy ) ) ) )
				DrawGrenade( ent, maxAlpha );

			//if ( class_id == EClassIndex::CMolotovProjectile || class_id == EClassIndex::CBaseCSGrenadeProjectile || class_id == EClassIndex::CIncendiaryGrenade )
			//	GrenadePrediction.SimulateThrownGrenade( owner, reinterpret_cast< CWeaponCSBase*>( ent ) );
		}

		if ( ( ( owner->IsTeammate( ) && Config::Get<bool>( Vars.VisGrenadesTeam ) )
			|| ( !owner->IsTeammate( ) && Config::Get<bool>( Vars.VisGrenadesEnemy ) ) )
			&& class_id == EClassIndex::CInferno ) {
			Vector min, max;
			ent->GetClientRenderable( )->GetRenderBounds( min, max );

			const auto radius = ( max - min ).Length2D( ) * 0.5f;

			DrawWrappingRing( ent, 7, _( "FIRE" ), static_cast< CBaseCSGrenadeProjectile* >( ent )->m_flSpawnTime( ), radius, maxAlpha );
		}
	}
}

void CVisuals::AutoPeekIndicator( ) {
	if ( ctx.m_pLocal->IsDead( ) )
		return;

	if ( !Config::Get<bool>( Vars.MiscAutoPeek ) )
		return;

	if ( !ctx.m_pWeapon 
		|| ctx.m_pLocal->IsDead( )
		|| ctx.m_pWeapon->IsKnife( ) || ctx.m_pWeapon->IsGrenade( ) )
		return;

	if ( !Features::Misc.AutoPeeking && !m_flAutoPeekSize )
		return;

	const float multiplier = static_cast< float >( 20.f * Interfaces::Globals->flFrameTime );

	if ( Config::Get<keybind_t>( Vars.MiscAutoPeekKey ).enabled )
		m_flAutoPeekSize += multiplier * ( 1.0f - m_flAutoPeekSize );
	else {
		if ( m_flAutoPeekSize > 0.01f )
			m_flAutoPeekSize += multiplier * -m_flAutoPeekSize;
		else
			m_flAutoPeekSize = 0.0f;
	}

	m_flAutoPeekSize = std::clamp<float>( m_flAutoPeekSize, 0.0f, 1.f );

	const auto& col = Config::Get<Color>( Vars.MiscAutoPeekCol );

	Render::WorldCircle( Features::Misc.OldOrigin, static_cast<int>( m_flAutoPeekSize * 20.0f ), col, col.Set<COLOR_A>( col.Get<COLOR_A>( ) / 5.f ) );
}

void CVisuals::DrawGrenade( CBaseEntity* ent, int maxAlpha ) {
	const auto model = ent->GetModel( );
	if ( !model ) 
		return;

	const auto hdr = Interfaces::ModelInfo->GetStudioModel( model );
	if ( !hdr ) 
		return;

	const char* model_name = hdr->szName;

	if ( !strstr( model_name, _( "thrown" ) ) && !strstr( model_name, _( "dropped" ) ) )
		return;

	const auto grenade = ( CBaseCSGrenadeProjectile* )ent;
	const char* name = _( "HE GRENADE" );

	if ( strstr( hdr->szName, _( "flash" ) ) )
		name = _( "FLASH" );
	else if ( strstr( hdr->szName, _( "smoke" ) ) ) {
		name = _( "SMOKE" );
		DrawWrappingRing( ent, 18.f, name, TICKS_TO_TIME( ( ( CWeaponCSBase* )ent )->m_nSmokeEffectTickBegin( ) ), 120.f, maxAlpha );
		return;
	}
	else if ( strstr( hdr->szName, _( "decoy" ) ) ) {
		name = _( "DECOY" );
		if ( grenade->m_nExplodeEffectTickBegin( ) )
			return;
	}
	else if ( strstr( hdr->szName, _( "incendiary" ) ) || strstr( hdr->szName, _( "molotov" ) ) )
		name = _( "FIRE" );
	else {
		if ( grenade->m_nExplodeEffectTickBegin( ) )
			return;
	}

	Vector2D world;
	if ( Math::WorldToScreen( ent->m_vecOrigin( ), world ) )
		Render::Text( Fonts::HealthESP, world, Color( 255, 255, 255, maxAlpha ), FONT_CENTER, name );
}

void CVisuals::DrawWrappingRing( CBaseEntity* entity, float seconds, const char* name, float spawntime, float radius, int maxAlpha ) {
	const auto fadeAlpha = std::min( std::max( seconds - ( Interfaces::Globals->flCurTime - spawntime ), 0.f ) * 2, 1.f );

	const auto owner = static_cast< CBasePlayer * >( Interfaces::ClientEntityList->GetClientEntityFromHandle( entity->m_hOwnerEntity( ) ) );
	if ( !owner || !owner->IsPlayer( ) )
		return;

	const auto color{ owner->IsTeammate() && owner != ctx.m_pLocal ? Config::Get<Color>( Vars.VisGrenadesTeamCol ) : Config::Get<Color>( Vars.VisGrenadesEnemyCol ) };

	Vector text_pos = entity->m_vecOrigin( );
	text_pos.z += 5.f;

	const float delta = Interfaces::Globals->flCurTime - spawntime;
	Vector2D world;
	if ( Math::WorldToScreen( entity->GetAbsOrigin( ), world ) ) {
		Vector last_pos;

		const float fill = ( seconds - delta ) / ( seconds ) * 180.f;

		for ( float rot = -fill; rot <= fill; rot += 3.f ) {
			auto rotation = rot + delta * 90.f;

			while ( rotation > 360.f )
				rotation -= 360.f;

			Vector rotated_pos = entity->m_vecOrigin( );

			rotated_pos.z -= 5.f;

			rotated_pos.x += std::cos( DEG2RAD( rotation ) ) * radius;
			rotated_pos.y += std::sin( DEG2RAD( rotation ) ) * radius;

			if ( rot != -fill ) {
				Vector2D w2s_new, w2s_old;

				if ( Math::WorldToScreen( rotated_pos, w2s_new ) && Math::WorldToScreen( last_pos, w2s_old ) ) {
					auto alpha = 1.f - ( std::abs( rot ) / fill );

					float threshold = seconds * 0.2f;

					if ( seconds - delta < threshold ) {
						float diff = ( seconds - delta ) / ( seconds ) * 5.f;

						alpha *= diff;
					}

					Vertex_t v[ ] = {
						{ world },
						{ w2s_old },
						{ w2s_new }
					};
					Color col = color;
					col[ 3 ] *= alpha;

					Render::Line( Vector2D( w2s_old.x, w2s_old.y ), Vector2D( w2s_new.x, w2s_new.y ), col.Set<COLOR_A>( std::min( maxAlpha, static_cast< int >( col[ 3 ] * fadeAlpha ) ) ) );
				}
			}

			last_pos = rotated_pos;
		}

		Render::Text( Fonts::HealthESP, world, Color( 255, 255, 255, static_cast<int>( maxAlpha * fadeAlpha ) ), FONT_CENTER, name );//color.Set<COLOR_A>( static_cast<int>( maxAlpha * fadeAlpha ) )
	}
}

void CVisuals::EntModulate( CBaseEntity* ent ) {
	if ( !ent || ent->IsDormant( ) )
		return;

	const auto client_class = ent->GetClientClass( );
	if ( !client_class )
		return;

	auto rgb_to_int = [ ]( int red, int green, int blue ) -> int {
		int r;
		int g;
		int b;

		r = red & 0xFF;
		g = green & 0xFF;
		b = blue & 0xFF;
		return ( r << 16 | g << 8 | b );
	};

	// bloom
	if ( client_class->nClassID == EClassIndex::CEnvTonemapController ) {
		if ( Config::Get<bool>( Vars.VisWorldBloom ) ) {
			*( bool* )( uintptr_t( ent ) + Displacement::Netvars->m_bUseCustomAutoExposureMin ) = true;
			*( bool* )( uintptr_t( ent ) + Displacement::Netvars->m_bUseCustomAutoExposureMax ) = true;
			*( bool* )( uintptr_t( ent ) + Displacement::Netvars->m_bUseCustomBloomScale ) = true;

			*( float* )( uintptr_t( ent ) + Displacement::Netvars->m_flCustomAutoExposureMin ) = float( Config::Get<int>( Vars.VisWorldBloomExposure ) ) / 50.f;
			*( float* )( uintptr_t( ent ) + Displacement::Netvars->m_flCustomAutoExposureMax ) = float( Config::Get<int>( Vars.VisWorldBloomExposure ) ) / 50.f;
			*( float* )( uintptr_t( ent ) + Displacement::Netvars->m_flCustomBloomScale ) = float( Config::Get<int>( Vars.VisWorldBloomScale ) ) / 50.f;
		}
		else {
			*( bool* )( uintptr_t( ent ) + Displacement::Netvars->m_bUseCustomAutoExposureMin ) = true;
			*( bool* )( uintptr_t( ent ) + Displacement::Netvars->m_bUseCustomAutoExposureMax ) = true;
			*( bool* )( uintptr_t( ent ) + Displacement::Netvars->m_bUseCustomBloomScale ) = true;
			*( float* )( uintptr_t( ent ) + Displacement::Netvars->m_flCustomAutoExposureMin ) = 0.f;
			*( float* )( uintptr_t( ent ) + Displacement::Netvars->m_flCustomAutoExposureMax ) = 0.f;
			*( float* )( uintptr_t( ent ) + Displacement::Netvars->m_flCustomBloomScale ) = 0.f;
		}
	}

	// fog
	/*if ( client_class->nClassID == EClassIndex::CFogController ) {
		*( byte* )( ( uintptr_t )ent + Displacement::Netvars->m_fog_enable ) = Config::Get<bool>( Vars.VisWorldFog );				// m_fog.enable

		*( bool* )( uintptr_t( ent ) + 0xA1D ) = Config::Get<bool>( Vars.VisWorldFog );					// m_fog.blend
		*( float* )( uintptr_t( ent ) + 0x9F8 ) = 0;
		*( float* )( uintptr_t( ent ) + 0x9FC ) = Config::Get<int>( Vars.VisWorldFogDistance );			// m_fog.start
		*( float* )( uintptr_t( ent ) + 0xA04 ) = Config::Get<int>( Vars.VisWorldFogDensity ) * 0.01f;	// m_fog.maxdensity
		*( float* )( uintptr_t( ent ) + 0xA24 ) = Config::Get<int>( Vars.VisWorldFogHDR ) * 0.01f;		// fog_hdrcolorscale

		const auto& col = Config::Get<Color>( Vars.VisWorldFogCol );
		*( int* )( uintptr_t( ent ) + 0x9E8 ) = rgb_to_int( ( int )( col.Get<COLOR_R>( ) ), ( int )( col.Get<COLOR_G>( ) ), ( int )( col.Get<COLOR_R>( ) ) ); // m_fog.colorPrimary
		*( int* )( uintptr_t( ent ) + 0x9EC ) = rgb_to_int( ( int )( col.Get<COLOR_B>( ) ), ( int )( col.Get<COLOR_G>( ) ), ( int )( col.Get<COLOR_R>( ) ) ); // m_fog.colorSecondary
	}*/
}

/* ------------------------------------------------------------------ */
/* HUD lists ( keybinds + spectators ): one look, snapping drag        */
/* ------------------------------------------------------------------ */

namespace HudList {
	constexpr float HeaderHeight{ 26.f };
	constexpr float Gap{ 4.f };
	constexpr float RowHeight{ 20.f };
	constexpr float BodyPadding{ 4.f };
	constexpr float MinWidth{ 172.f };
	constexpr float SnapDistance{ 8.f };
	constexpr float ScreenMargin{ 10.f };
	constexpr float StackGap{ 8.f };

	const Color Warn{ 255, 92, 92 };

	struct Row_t {
		std::string m_strId{ };
		std::string m_strName{ };
		std::string m_strKey{ };      // dim chip ( bound key ), empty = none
		std::string m_strTag{ };      // coloured chip ( mode ), empty = none
		Color m_cTag{ };
		char m_cInitial{ };           // avatar letter, 0 = no avatar
		Color m_cAvatar{ };
		bool m_bWarn{ };              // row drawn in the warning colour
		Anim::Fade_t m_cFade{ };
		bool m_bActive{ };
	};

	struct List_t {
		std::vector< Row_t > m_vecRows{ };
		Anim::Fade_t m_cPanel{ };
		Anim::Fade_t m_cPlaceholder{ };
		Anim::Fade_t m_cCount{ };
		float m_flWidth{ };
		float m_flGlow{ };
		int m_iShownCount{ };
		bool m_bDragging{ };
		Vector2D m_vecRaw{ };         // unsnapped position while dragging
		Vector2D m_vecPos{ };         // last frame, read by the other list for snapping
		Vector2D m_vecSize{ };
		bool m_bSnapX{ }, m_bSnapY{ };
		float m_flLineX{ }, m_flLineY{ };
	};

	List_t Keybinds{ };
	List_t Spectators{ };

	bool AnyDragging( ) {
		return Keybinds.m_bDragging || Spectators.m_bDragging;
	}

	// keeps the animation state of rows that already exist, adds new ones, marks missing ones as leaving
	void Sync( List_t& list, std::vector< Row_t >& incoming ) {
		for ( auto& row : list.m_vecRows )
			row.m_bActive = false;

		for ( auto& in : incoming ) {
			auto it{ std::find_if( list.m_vecRows.begin( ), list.m_vecRows.end( ), [ & ]( const Row_t& row ) { return row.m_strId == in.m_strId; } ) };
			if ( it == list.m_vecRows.end( ) ) {
				in.m_bActive = true;
				list.m_vecRows.push_back( std::move( in ) );
				continue;
			}

			const auto fade{ it->m_cFade };
			*it = std::move( in );
			it->m_cFade = fade;
			it->m_bActive = true;
		}
	}

	float ChipWidth( const std::string& text ) {
		return Render::GetTextSize( text, Fonts::Logs ).x + 10.f;
	}

	void DrawChip( const Vector2D& pos, float width, const std::string& text, Color back, Color fore ) {
		Render::FilledRoundedBox( pos, Vector2D( width, 14.f ), 4, 4, back );
		Render::Text( Fonts::Logs, Vector2D( pos.x + width / 2.f, pos.y + 1.f ), fore, FONT_CENTER, text.c_str( ) );
	}

	struct Snap_t {
		float m_flPos;    // where the panel edge goes
		float m_flLine;   // where the guide is drawn
	};

	float SnapAxis( float value, const std::vector< Snap_t >& targets, bool& snapped, float& line ) {
		float best{ SnapDistance + 1.f };
		float result{ value };
		snapped = false;

		for ( const auto& target : targets ) {
			const float distance{ std::abs( value - target.m_flPos ) };
			if ( distance <= SnapDistance && distance < best ) {
				best = distance;
				result = target.m_flPos;
				line = target.m_flLine;
				snapped = true;
			}
		}

		return result;
	}

	void DrawGuide( bool vertical, float at, float alpha ) {
		const auto col{ ACCENT.Set<COLOR_A>( 140.f * alpha ) };
		const float length{ vertical ? ctx.m_ve2ScreenSize.y : ctx.m_ve2ScreenSize.x };

		for ( float t{ }; t < length; t += 10.f ) {
			if ( vertical )
				Render::FilledRectangle( Vector2D( at, t ), Vector2D( 1.f, 5.f ), col );
			else
				Render::FilledRectangle( Vector2D( t, at ), Vector2D( 5.f, 1.f ), col );
		}
	}

	void DrawIcon( int icon, const Vector2D& center, Color col ) {
		if ( icon == 0 ) {
			// keyboard: body + two rows of keys + space bar
			Render::RoundedBox( center - Vector2D( 6.f, 4.f ), Vector2D( 12.f, 8.f ), 4, 2, col );
			for ( int i{ }; i < 3; ++i ) {
				Render::FilledRectangle( center + Vector2D( -3.5f + i * 3.f, -2.f ), Vector2D( 1.f, 1.f ), col );
				Render::FilledRectangle( center + Vector2D( -3.5f + i * 3.f, 0.f ), Vector2D( 1.f, 1.f ), col );
			}
			Render::FilledRectangle( center + Vector2D( -2.f, 2.f ), Vector2D( 4.f, 1.f ), col );
		}
		else
			MenuDraw::IconVisuals( center, 12.f, col );
	}

	// panel + rows, dragging ( snaps to screen edges / centre and to the other list, shift = free ), clamped on screen
	void Draw( List_t& list, const List_t& other, int& cfgX, int& cfgY, const char* title, int icon, bool show, const char* placeholder ) {
		const Vector2D screen{ ctx.m_ve2ScreenSize };
		if ( screen.x <= 0.f || screen.y <= 0.f )
			return;

		float rowsHeight{ };
		float width{ MinWidth };
		int count{ }, warnings{ };

		for ( auto& row : list.m_vecRows ) {
			row.m_cFade.Update( row.m_bActive && show );
			rowsHeight += RowHeight * row.m_cFade.Alpha( );

			if ( !row.m_bActive )
				continue;

			++count;
			warnings += row.m_bWarn ? 1 : 0;

			float rowWidth{ ( row.m_cInitial ? 34.f : 16.f ) + Render::GetTextSize( row.m_strName, Fonts::Menu ).x + 20.f };
			if ( !row.m_strTag.empty( ) )
				rowWidth += ChipWidth( row.m_strTag ) + 4.f;
			if ( !row.m_strKey.empty( ) )
				rowWidth += ChipWidth( row.m_strKey ) + 4.f;
			width = std::max( width, rowWidth );
		}

		list.m_vecRows.erase( std::remove_if( list.m_vecRows.begin( ), list.m_vecRows.end( ),
			[ ]( const Row_t& row ) { return !row.m_bActive && !row.m_cFade.Visible( ); } ), list.m_vecRows.end( ) );

		// "nothing here" line while the menu is open, so an empty list can still be found and moved
		list.m_cPlaceholder.Update( show && count == 0 && Menu::m_bOpened );
		const float placeholderHeight{ RowHeight * list.m_cPlaceholder.Alpha( ) };

		const float content{ rowsHeight + placeholderHeight };
		const float bodyHeight{ content > 0.5f ? content + BodyPadding * 2.f * std::min( 1.f, content / RowHeight ) : 0.f };

		list.m_cPanel.Update( show && ( count > 0 || Menu::m_bOpened ) );
		const float alpha{ list.m_cPanel.Alpha( ) };

		if ( alpha < 0.01f ) {
			list.m_bDragging = false;
			list.m_vecSize = { };
			return;
		}

		list.m_flWidth = list.m_flWidth <= 0.f ? width : Anim::Approach( list.m_flWidth, width );
		const float panelWidth{ std::round( list.m_flWidth ) };
		const float panelHeight{ HeaderHeight + ( bodyHeight > 0.5f ? Gap + bodyHeight : 0.f ) };
		const Vector2D headerSize{ panelWidth, HeaderHeight };

		/* dragging */
		Vector2D pos{ static_cast< float >( cfgX ), static_cast< float >( cfgY ) };

		const bool overMenu{ Menu::m_flAlpha > 0.f && Inputsys::hovered( Menu::m_vecPos, Menu::m_vecSize ) };
		const bool canDrag{ Menu::m_bOpened && Menu::m_flAlpha > 0.5f && !Menu::m_pFocusItem.m_pItem && !Menu::m_bDraggingMenu && !Menu::m_bDraggingSize };
		const bool hovered{ canDrag && !overMenu && Inputsys::hovered( pos, headerSize ) };

		if ( !list.m_bDragging && hovered && Inputsys::pressed( VK_LBUTTON ) && !AnyDragging( ) ) {
			list.m_bDragging = true;
			list.m_vecRaw = pos;
		}
		else if ( list.m_bDragging && Inputsys::down( VK_LBUTTON ) && canDrag ) {
			list.m_vecRaw -= Inputsys::MouseDelta;
			pos = list.m_vecRaw;

			list.m_bSnapX = list.m_bSnapY = false;
			if ( !Inputsys::down( VK_SHIFT ) ) {
				std::vector< Snap_t > xs{
					{ ScreenMargin, ScreenMargin },
					{ screen.x - panelWidth - ScreenMargin, screen.x - ScreenMargin },
					{ std::round( screen.x / 2.f - panelWidth / 2.f ), screen.x / 2.f } };
				std::vector< Snap_t > ys{
					{ ScreenMargin, ScreenMargin },
					{ screen.y - panelHeight - ScreenMargin, screen.y - ScreenMargin },
					{ std::round( screen.y / 2.f - panelHeight / 2.f ), screen.y / 2.f } };

				if ( other.m_vecSize.x > 0.f ) {
					xs.push_back( { other.m_vecPos.x, other.m_vecPos.x } );
					xs.push_back( { other.m_vecPos.x + other.m_vecSize.x - panelWidth, other.m_vecPos.x + other.m_vecSize.x } );
					ys.push_back( { other.m_vecPos.y, other.m_vecPos.y } );
					ys.push_back( { other.m_vecPos.y + other.m_vecSize.y + StackGap, other.m_vecPos.y + other.m_vecSize.y + StackGap / 2.f } );
					ys.push_back( { other.m_vecPos.y - panelHeight - StackGap, other.m_vecPos.y - StackGap / 2.f } );
				}

				pos.x = SnapAxis( pos.x, xs, list.m_bSnapX, list.m_flLineX );
				pos.y = SnapAxis( pos.y, ys, list.m_bSnapY, list.m_flLineY );
			}
		}
		else if ( list.m_bDragging )
			list.m_bDragging = false;

		// on screen whatever the resolution
		pos.x = std::clamp( pos.x, 0.f, std::max( 0.f, screen.x - panelWidth ) );
		pos.y = std::clamp( pos.y, 0.f, std::max( 0.f, screen.y - panelHeight ) );
		pos = Vector2D( std::round( pos.x ), std::round( pos.y ) );
		cfgX = static_cast< int >( pos.x );
		cfgY = static_cast< int >( pos.y );

		list.m_vecPos = pos;
		list.m_vecSize = Vector2D( panelWidth, panelHeight );
		list.m_flGlow = Anim::Approach( list.m_flGlow, list.m_bDragging ? 1.f : hovered ? 0.55f : 0.f, 16.f );

		/* drawing */
		const auto a{ [ alpha ]( Color c, float extra = 1.f ) { return c.Set<COLOR_A>( std::clamp( c.Get<COLOR_A>( ) * alpha * extra, 0.f, 255.f ) ); } };

		Render::FilledRoundedBox( pos - Vector2D( 3.f, 3.f ), list.m_vecSize + Vector2D( 6.f, 6.f ), 5, 8, a( Color( 0, 0, 0, 55 ) ) );

		if ( list.m_flGlow > 0.01f )
			MenuDraw::Glow( pos, headerSize, list.m_flGlow * alpha, ACCENT, 5 );

		// header card
		Render::FilledRoundedBox( pos - Vector2D( 1.f, 1.f ), headerSize + Vector2D( 2.f, 2.f ), 5, 6, a( OUTLINE_DARK ) );
		Render::FilledRoundedBox( pos, headerSize, 5, 5, a( Color( 21, 21, 25, 245 ) ) );
		Render::RoundedBox( pos, headerSize, 5, 5, a( Color( OUTLINE_LIGHT ).Lerp( ACCENT, list.m_flGlow ) ) );

		// icon tile
		Render::FilledRoundedBox( pos + Vector2D( 5.f, 4.f ), Vector2D( 18.f, 18.f ), 5, 4, a( ACCENT, 0.16f ) );
		DrawIcon( icon, pos + Vector2D( 14.f, 13.f ), a( ACCENT ) );

		Render::Text( Fonts::MenuBold, pos + Vector2D( 30.f, 6.f ), a( Color( 236, 236, 242 ) ), FONT_LEFT, title );

		// count pill ( red while a warning row is in the list )
		list.m_cCount.Update( count > 0 );
		if ( count > 0 )
			list.m_iShownCount = count;
		if ( list.m_cCount.Visible( ) ) {
			const auto text{ std::to_string( list.m_iShownCount ) };
			const float pillWidth{ std::max( 20.f, Render::GetTextSize( text, Fonts::Logs ).x + 12.f ) };
			const float pillAlpha{ list.m_cCount.Alpha( ) };
			const Vector2D pillPos{ pos.x + panelWidth - pillWidth - 6.f + list.m_cCount.Offset( 6.f ), pos.y + 6.f };
			DrawChip( pillPos, pillWidth, text, a( warnings > 0 ? Warn : ACCENT, pillAlpha ), a( Color( 22, 16, 10 ), pillAlpha ) );
		}

		// accent hairline under the title, fading out to the right
		Render::Gradient( static_cast< int >( pos.x + 6.f ), static_cast< int >( pos.y + HeaderHeight - 2.f ), static_cast< int >( panelWidth - 12.f ), 1,
			a( warnings > 0 ? Warn : ACCENT, 0.9f ), a( ACCENT, 0.f ), true );

		if ( bodyHeight <= 0.5f )
			return;

		// body card
		const Vector2D bodyPos{ pos.x, pos.y + HeaderHeight + Gap };
		const Vector2D bodySize{ panelWidth, bodyHeight };

		Render::FilledRoundedBox( bodyPos - Vector2D( 1.f, 1.f ), bodySize + Vector2D( 2.f, 2.f ), 5, 6, a( OUTLINE_DARK ) );
		Render::FilledRoundedBox( bodyPos, bodySize, 5, 5, a( Color( 15, 15, 18, 238 ) ) );
		Render::RoundedBox( bodyPos, bodySize, 5, 5, a( OUTLINE_LIGHT ) );

		Interfaces::Surface->SetClipRect( static_cast< int >( bodyPos.x ), static_cast< int >( bodyPos.y ), static_cast< int >( bodySize.x ), static_cast< int >( bodySize.y ) );

		float rowY{ bodyPos.y + BodyPadding * std::min( 1.f, content / RowHeight ) };
		int visibleIndex{ };

		for ( const auto& row : list.m_vecRows ) {
			const float rowAlpha{ row.m_cFade.Alpha( ) };
			if ( rowAlpha <= 0.001f )
				continue;

			const float slide{ row.m_cFade.Offset( ) };
			const float mid{ rowY + RowHeight * rowAlpha / 2.f };
			const auto r{ [ & ]( Color c ) { return a( c, rowAlpha ); } };

			if ( visibleIndex++ % 2 == 1 )
				Render::FilledRoundedBox( Vector2D( bodyPos.x + 3.f, rowY ), Vector2D( panelWidth - 6.f, RowHeight * rowAlpha ), 4, 3, r( Color( 255, 255, 255, 6 ) ) );

			if ( row.m_bWarn )
				Render::FilledRoundedBox( Vector2D( bodyPos.x + 3.f, rowY ), Vector2D( panelWidth - 6.f, RowHeight * rowAlpha ), 4, 3, r( Warn.Set<COLOR_A>( 22 ) ) );

			// state tick
			Render::FilledRoundedBox( Vector2D( bodyPos.x + 6.f - slide, mid - 5.f ), Vector2D( 2.f, 10.f ), 3, 1, r( row.m_bWarn ? Warn : ACCENT ) );

			float x{ bodyPos.x + 14.f - slide };

			if ( row.m_cInitial ) {
				const Vector2D center{ x + 7.f, mid };
				MenuDraw::Circle( center, 7.5f, r( OUTLINE_DARK ), 18 );
				MenuDraw::Circle( center, 6.5f, r( row.m_cAvatar ), 18 );
				const char letter[ 2 ]{ row.m_cInitial, '\0' };
				Render::Text( Fonts::Logs, Vector2D( center.x, mid - 6.f ), r( Color( 18, 18, 22 ) ), FONT_CENTER, letter );
				x += 20.f;
			}

			Render::Text( Fonts::Menu, Vector2D( x, mid - 7.f ), r( row.m_bWarn ? Color( 255, 186, 186 ) : Color( 230, 230, 236 ) ), FONT_LEFT, row.m_strName.c_str( ) );

			// chips from the right edge: mode tag, then the bound key
			float chipRight{ bodyPos.x + panelWidth - 7.f + slide };

			if ( !row.m_strTag.empty( ) ) {
				const float chipWidth{ ChipWidth( row.m_strTag ) };
				chipRight -= chipWidth;
				DrawChip( Vector2D( chipRight, mid - 7.f ), chipWidth, row.m_strTag, r( row.m_cTag.Set<COLOR_A>( 42 ) ), r( row.m_cTag ) );
				chipRight -= 4.f;
			}

			if ( !row.m_strKey.empty( ) ) {
				const float chipWidth{ ChipWidth( row.m_strKey ) };
				chipRight -= chipWidth;
				DrawChip( Vector2D( chipRight, mid - 7.f ), chipWidth, row.m_strKey, r( MENU_CONTROL ), r( Color( 168, 168, 180 ) ) );
			}

			rowY += RowHeight * rowAlpha;
		}

		if ( list.m_cPlaceholder.Visible( ) ) {
			const float placeholderAlpha{ list.m_cPlaceholder.Alpha( ) };
			Render::Text( Fonts::Logs, Vector2D( bodyPos.x + panelWidth / 2.f, rowY + placeholderHeight / 2.f - 6.f ),
				a( Color( 128, 128, 140 ), placeholderAlpha ), FONT_CENTER, placeholder );
		}

		Interfaces::Surface->SetClipRect( 0, 0, static_cast< int >( screen.x ), static_cast< int >( screen.y ) );

		// snap guides while dragging
		if ( list.m_bDragging ) {
			if ( list.m_bSnapX )
				DrawGuide( true, list.m_flLineX, alpha );
			if ( list.m_bSnapY )
				DrawGuide( false, list.m_flLineY, alpha );
		}
	}
}

void CVisuals::KeybindsList( ) {
	const bool enabled{ Config::Get<bool>( Vars.MiscKeybindList ) };
	const bool alive{ ctx.m_pLocal && !ctx.m_pLocal->IsDead( ) };

	std::vector< HudList::Row_t > rows{ };

	const auto add{ [ & ]( const char* name, const keybind_t& bind ) {
		if ( bind.mode == EKeyMode::AlwaysOn )
			return;

		HudList::Row_t row{ };
		row.m_strId = name;
		row.m_strName = name;
		row.m_strKey = bind.key ? Menu::KeyName( bind.key ) : std::string{ };

		switch ( bind.mode ) {
		case EKeyMode::Hold:
			row.m_strTag = _( "hold" );
			row.m_cTag = ACCENT;
			break;
		case EKeyMode::Toggle:
			row.m_strTag = _( "toggle" );
			row.m_cTag = ACCENT2;
			break;
		default:
			row.m_strTag = _( "off key" );
			row.m_cTag = Color( 140, 180, 255 );
			break;
		}

		rows.push_back( std::move( row ) );
	} };

	if ( enabled && alive ) {
		const auto& dt{ Config::Get<keybind_t>( Vars.ExploitsDoubletapKey ) };
		const auto& hs{ Config::Get<keybind_t>( Vars.ExploitsHideshotsKey ) };
		const auto& dmg{ Config::Get<keybind_t>( Vars.RagebotDamageOverrideKey ) };
		const auto& baim{ Config::Get<keybind_t>( Vars.RagebotForceBaimKey ) };
		const auto& safe{ Config::Get<keybind_t>( Vars.RagebotForceSafePointKey ) };
		const auto& safeYaw{ Config::Get<keybind_t>( Vars.RagebotForceYawSafetyKey ) };
		const auto& flick{ Config::Get<keybind_t>( Vars.AntiaimFlickInvert ) };
		const auto& invert{ Config::Get<keybind_t>( Vars.AntiaimInvert ) };
		const auto& slow{ Config::Get<keybind_t>( Vars.MiscSlowWalkKey ) };
		const auto& peek{ Config::Get<keybind_t>( Vars.MiscAutoPeekKey ) };
		const auto& freestand{ Config::Get<keybind_t>( Vars.AntiaimFreestandingKey ) };
		const auto& duck{ Config::Get<keybind_t>( Vars.MiscFakeDuckKey ) };

		if ( Config::Get<bool>( Vars.ExploitsDoubletap ) && dt.enabled )
			add( _( "Doubletap" ), dt );
		if ( Config::Get<bool>( Vars.ExploitsHideshots ) && hs.enabled )
			add( _( "Hide shots" ), hs );
		if ( Config::Get<bool>( Vars.RagebotDamageOverride ) && dmg.enabled )
			add( _( "Damage override" ), dmg );
		if ( baim.enabled )
			add( _( "Force baim" ), baim );
		if ( safe.enabled )
			add( _( "Force safe point" ), safe );
		if ( safeYaw.enabled )
			add( _( "Force safe yaw" ), safeYaw );
		if ( !Features::Antiaim.IsCustom( ) && !Config::Get<bool>( Vars.AntiaimConstantInvertFlick ) && flick.enabled )
			add( _( "Flick invert" ), flick );
		if ( !Features::Antiaim.IsCustom( ) && !Config::Get<bool>( Vars.AntiaimConstantInvert ) && invert.enabled )
			add( _( "Invert desync" ), invert );
		if ( Config::Get<bool>( Vars.MiscSlowWalk ) && slow.enabled )
			add( _( "Slow walk" ), slow );
		if ( Config::Get<bool>( Vars.MiscAutoPeek ) && peek.enabled )
			add( _( "Auto peek" ), peek );
		if ( ( Features::Antiaim.IsCustom( ) || Config::Get<int>( Vars.AntiaimFreestanding ) ) && freestand.enabled )
			add( _( "Freestanding" ), freestand );
		if ( Config::Get<bool>( Vars.MiscFakeDuck ) && duck.enabled )
			add( _( "Fake duck" ), duck );
	}

	HudList::Sync( HudList::Keybinds, rows );
	HudList::Draw( HudList::Keybinds, HudList::Spectators, Config::Get<int>( Vars.MiscKeybindPosX ), Config::Get<int>( Vars.MiscKeybindPosY ),
		_( "keybinds" ), 0, enabled, _( "no active binds" ) );

	m_vec2KeyBindPos = HudList::Keybinds.m_vecPos;
	m_vec2KeyBindAbsSize = HudList::Keybinds.m_vecSize;
	m_bKeybindDragging = HudList::Keybinds.m_bDragging;
}

void CVisuals::SpectatorList( ) {
	const bool enabled{ Config::Get<bool>( Vars.MiscSpectatorList ) };

	std::vector< HudList::Row_t > rows{ };

	if ( enabled && ctx.m_pLocal && Interfaces::Engine->IsInGame( ) ) {
		static const int targetOffset{ PropManager::Get( ).GetOffset( _( "DT_BasePlayer" ), _( "m_hObserverTarget" ) ) };
		static const int modeOffset{ PropManager::Get( ).GetOffset( _( "DT_BasePlayer" ), _( "m_iObserverMode" ) ) };

		// spectating us, or ( while dead ) spectating the same player we watch
		CBaseEntity* watched{ ctx.m_pLocal };
		if ( ctx.m_pLocal->IsDead( ) && targetOffset > 0 ) {
			const auto handle{ *reinterpret_cast< CBaseHandle* >( reinterpret_cast< std::uintptr_t >( ctx.m_pLocal ) + targetOffset ) };
			if ( const auto target{ Interfaces::ClientEntityList->GetClientEntityFromHandle( handle ) } )
				watched = static_cast< CBaseEntity* >( target );
		}

		const bool watchingUs{ watched == ctx.m_pLocal };

		for ( int i{ 1 }; targetOffset > 0 && i <= Interfaces::Globals->nMaxClients; ++i ) {
			const auto player{ static_cast< CBasePlayer* >( Interfaces::ClientEntityList->GetClientEntity( i ) ) };
			if ( !player || player == ctx.m_pLocal || player->IsDormant( ) || !player->IsDead( ) )
				continue;

			const auto handle{ *reinterpret_cast< CBaseHandle* >( reinterpret_cast< std::uintptr_t >( player ) + targetOffset ) };
			if ( Interfaces::ClientEntityList->GetClientEntityFromHandle( handle ) != watched )
				continue;

			const auto info{ Interfaces::Engine->GetPlayerInfo( i ) };
			if ( !info.has_value( ) )
				continue;

			const int mode{ modeOffset > 0 ? *reinterpret_cast< int* >( reinterpret_cast< std::uintptr_t >( player ) + modeOffset ) : 0 };

			std::string name{ info->szName };
			name.erase( std::remove( name.begin( ), name.end( ), '%' ), name.end( ) );
			if ( name.size( ) > 20 )
				name = name.substr( 0, 19 ) + ".";
			if ( name.empty( ) )
				name = _( "unnamed" );

			HudList::Row_t row{ };
			row.m_strId = std::to_string( i );
			row.m_strName = name;

			// avatar: first letter on a colour picked from the name
			const auto first{ static_cast< unsigned char >( name.front( ) ) };
			row.m_cInitial = std::isalnum( first ) ? static_cast< char >( std::toupper( first ) ) : '?';

			static const Color palette[ ]{ Color( 255, 149, 46 ), Color( 255, 214, 153 ), Color( 126, 200, 255 ), Color( 160, 230, 150 ), Color( 214, 160, 255 ), Color( 255, 140, 170 ) };
			row.m_cAvatar = palette[ std::hash< std::string >{ }( name ) % ( sizeof( palette ) / sizeof( palette[ 0 ] ) ) ];

			// first person on us is the one that sees everything
			row.m_bWarn = watchingUs && mode == 4;

			switch ( mode ) {
			case 4:
				row.m_strTag = _( "1st" );
				row.m_cTag = row.m_bWarn ? HudList::Warn : ACCENT2;
				break;
			case 5:
				row.m_strTag = _( "3rd" );
				row.m_cTag = ACCENT;
				break;
			case 6:
				row.m_strTag = _( "free" );
				row.m_cTag = Color( 150, 150, 162 );
				break;
			default:
				break;
			}

			rows.push_back( std::move( row ) );
		}
	}

	HudList::Sync( HudList::Spectators, rows );
	HudList::Draw( HudList::Spectators, HudList::Keybinds, Config::Get<int>( Vars.MiscSpectatorPosX ), Config::Get<int>( Vars.MiscSpectatorPosY ),
		_( "spectators" ), 1, enabled, _( "nobody is watching" ) );

	m_bSpectatorDragging = HudList::Spectators.m_bDragging;
}

/* ------------------------------------------------------------------ */
/* momentum-style crosshair HUD                                        */
/* ------------------------------------------------------------------ */

void CVisuals::ManualArrows( ) {
	const auto center{ ctx.m_ve2ScreenSize / 2 };
	const auto& col{ Config::Get<Color>( Vars.AntiaimManualCol ) };
	const auto idle{ Color( 30, 30, 34, 120 ) };

	// chevron arrows, filled when that side is active
	auto arrow = [ & ]( float dir, bool active ) {
		const auto tip{ Vector2D( center.x + dir * 62.f, center.y ) };
		const auto back{ Vector2D( center.x + dir * 48.f, center.y ) };

		const std::vector< Vector2D > shape{
			tip,
			Vector2D( back.x, center.y - 9.f ),
			Vector2D( back.x + dir * 4.f, center.y ),
			Vector2D( back.x, center.y + 9.f ) };

		// shadow, then fill ( the shape is concave at the notch, so draw it as two triangles )
		const auto fill{ active ? col : idle };
		for ( int pass{ }; pass < 2; ++pass ) {
			const auto offset{ pass == 0 ? Vector2D( 1.f, 1.f ) : Vector2D( 0.f, 0.f ) };
			const auto c{ pass == 0 ? Color( 0, 0, 0, active ? 120 : 60 ) : fill };
			MenuDraw::ConvexPolygon( { shape[ 0 ] + offset, shape[ 1 ] + offset, shape[ 2 ] + offset }, c );
			MenuDraw::ConvexPolygon( { shape[ 0 ] + offset, shape[ 2 ] + offset, shape[ 3 ] + offset }, c );
		}
	};

	arrow( -1.f, Features::Antiaim.ManualSide == 1 );
	arrow( 1.f, Features::Antiaim.ManualSide == 2 );
}

void CVisuals::CrosshairIndicators( ) {
	const auto center{ ctx.m_ve2ScreenSize / 2 };
	const auto& col{ Config::Get<Color>( Vars.VisIndicatorsCol ) };
	const auto white{ Color( 235, 235, 240 ) };
	const auto dim{ Color( 120, 120, 130, 200 ) };

	float y{ center.y + 22.f };

	// title with a breathing accent
	{
		const auto pulse{ 0.65f + 0.35f * std::sin( Interfaces::Globals->flRealTime * 3.f ) };
		const auto firstW{ Render::GetTextSize( "CORGI", Fonts::MenuBold ).x };
		const auto totalW{ firstW + Render::GetTextSize( "HAK", Fonts::MenuBold ).x };

		Render::Text( Fonts::MenuBold, Vector2D( center.x - totalW / 2.f, y ), white, FONT_LEFT, "CORGI" );
		Render::Text( Fonts::MenuBold, Vector2D( center.x - totalW / 2.f + firstW, y ), col.Set<COLOR_A>( 255 * pulse ), FONT_LEFT, "HAK" );
		y += 12.f;
	}

	// anti-aim state
	if ( Config::Get<bool>( Vars.AntiaimEnable ) ) {
		const auto state{ Features::Antiaim.StateName( ) };
		const auto w{ Render::GetTextSize( state, Fonts::Logs ).x };

		// small desync side bar under the title
		Render::FilledRectangle( Vector2D( center.x - w / 2.f, y ), Vector2D( w, 1.f ), Color( 30, 30, 34, 160 ) );
		Render::FilledRectangle( Vector2D( Features::Antiaim.Invert ? center.x : center.x - w / 2.f, y ), Vector2D( w / 2.f, 1.f ), col );

		y += 3.f;
		Render::Text( Fonts::Logs, Vector2D( center.x, y ), Color( 170, 170, 182 ), FONT_CENTER, state );
		y += 12.f;
	}

	// every indicator fades and drops in when it turns on; the ones below slide to make room
	static std::unordered_map< std::string, Anim::Fade_t > fades{ };
	auto line = [ & ]( const char* text, Color c, bool active ) {
		auto& fade{ fades[ text ] };
		fade.Update( active );
		if ( !fade.Visible( ) )
			return;

		const float alpha{ fade.Alpha( ) };
		Render::Text( Fonts::Logs, Vector2D( center.x, y - fade.Offset( 6.f ) ), c.Set<COLOR_A>( c.Get<COLOR_A>( ) * alpha ), FONT_CENTER, text );
		y += 11.f * alpha;
	};

	line( "DT", ctx.m_iTicksAllowed > 0 ? col : dim, Config::Get<bool>( Vars.ExploitsDoubletap ) && Config::Get<keybind_t>( Vars.ExploitsDoubletapKey ).enabled );
	line( "OS", col, Config::Get<bool>( Vars.ExploitsHideshots ) && Config::Get<keybind_t>( Vars.ExploitsHideshotsKey ).enabled );
	line( "FS", col, ( Features::Antiaim.IsCustom( ) || Config::Get<int>( Vars.AntiaimFreestanding ) ) && Config::Get<keybind_t>( Vars.AntiaimFreestandingKey ).enabled );
	line( "SAFE", col, Config::Get<keybind_t>( Vars.RagebotForceSafePointKey ).enabled );
	line( "BAIM", col, Config::Get<keybind_t>( Vars.RagebotForceBaimKey ).enabled );
	line( "DUCK", col, Config::Get<bool>( Vars.MiscFakeDuck ) && Config::Get<keybind_t>( Vars.MiscFakeDuckKey ).enabled );
}

void CVisuals::MinDamageIndicator( ) {
	if ( !ctx.m_pWeapon || !ctx.m_pWeaponData || ctx.m_pWeapon->IsKnife( ) || ctx.m_pWeapon->IsGrenade( ) )
		return;

	const auto overriding{ Config::Get<bool>( Vars.RagebotDamageOverride ) && Config::Get<keybind_t>( Vars.RagebotDamageOverrideKey ).enabled };
	const auto damage{ overriding ? Features::Ragebot.MenuVars.RagebotOverrideDamage : Features::Ragebot.MenuVars.RagebotMinimumDamage };

	// 100+ means "health + n"
	const std::string text{ damage > 100 ? "HP+" + std::to_string( damage - 100 ) : std::to_string( damage ) };

	const auto center{ ctx.m_ve2ScreenSize / 2 };
	Render::Text( Fonts::Logs, Vector2D( center.x + 6.f, center.y - 18.f ),
		overriding ? Config::Get<Color>( Vars.VisIndicatorsCol ) : Color( 235, 235, 240, 200 ), FONT_LEFT, text.c_str( ) );
}

void CVisuals::CustomScope( ) {
	if ( !ctx.m_pLocal->m_bIsScoped( ) || !ctx.m_pWeaponData || ctx.m_pWeaponData->nWeaponType != WEAPONTYPE_SNIPER )
		return;

	const auto center{ ctx.m_ve2ScreenSize / 2 };
	const auto& col{ Config::Get<Color>( Vars.VisCustomScopeCol ) };
	const auto clear{ col.Set<COLOR_A>( 0 ) };
	const auto gap{ Config::Get<int>( Vars.VisCustomScopeGap ) };
	const auto length{ Config::Get<int>( Vars.VisCustomScopeSize ) };

	const int cx{ static_cast< int >( center.x ) };
	const int cy{ static_cast< int >( center.y ) };

	Render::Gradient( cx + gap, cy, length, 1, col, clear, true );            // right
	Render::Gradient( cx - gap - length, cy, length, 1, clear, col, true );   // left
	Render::Gradient( cx, cy + gap, 1, length, col, clear, false );           // down
	Render::Gradient( cx, cy - gap - length, 1, length, clear, col, false );  // up
}
