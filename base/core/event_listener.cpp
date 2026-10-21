#include "event_listener.h"
#include "../utils/xorstr.h"
// sequence:
// weapon_fire -> bullet_impact -> player_hurt

void CEventListener::Setup( const std::deque<const char*>& arrEvents ) {
	if ( arrEvents.empty( ) )
		return;

	for ( auto szEvent : arrEvents ) {
		Interfaces::GameEvent->AddListener( this, szEvent, false );
	}
}

void CEventListener::Destroy( ) {
	Interfaces::GameEvent->RemoveListener( this );
}

void BulletImpact( IGameEvent* pEvent ) {
	if ( !pEvent || !ctx.m_pLocal )
		return;

	Vector pos{ pEvent->GetFloat( _( "x" ) ), pEvent->GetFloat( _( "y" ) ), pEvent->GetFloat( _( "z" ) ) };

	if (auto ent{ static_cast<CBasePlayer*>(Interfaces::ClientEntityList->GetClientEntity(Interfaces::Engine->GetPlayerForUserID(pEvent->GetInt(_("userid"))))) }; ent)
	{
		if (ent != ctx.m_pLocal)
		{
			if (ent->IsTeammate())
				return;

			const auto& col = Config::Get<Color>(Vars.VisServerBulletImpactsCol);
			if (Config::Get<bool>(Vars.VisServerBulletImpacts))
			{
				Interfaces::DebugOverlay->AddBoxOverlay(pos, Vector(-2.0f, -2.0f, -2.0f), Vector(2.0f, 2.0f, 2.0f), QAngle(0.f, -0.f, 0.f),
					col.Get<COLOR_R>(), col.Get<COLOR_G>(), col.Get<COLOR_B>(), col.Get<COLOR_A>(), 4.0f);
			}

			if (Config::Get<bool>(Vars.VisOtherBulletTracers))
				Features::Visuals.BulletTracers.AddTracer(ent->GetEyePosition(ent->m_angEyeAngles().y, ent->m_angEyeAngles().x), pos, ent);

			return;
		}

		if (const auto shot = Features::Shots.LastUnprocessed())
		{
			// Events have no command id. If multiple fired bullets still lack an
			// impact in this server tick, do not learn from an ambiguous association.
			int awaiting{ };
			for ( const auto& pending : Features::Shots.m_vecShots )
				if ( pending.m_iServerProcessTick == Interfaces::ClientState->iServerTick && !pending.m_bHasImpact ) ++awaiting;
			if ( awaiting > 1 )
				for ( auto& pending : Features::Shots.m_vecShots )
					if ( pending.m_iServerProcessTick == Interfaces::ClientState->iServerTick ) pending.m_bResolverUnreliable = true;
			shot->m_vecServerEnd = pos;
			shot->m_bHasImpact = true;

			if (Config::Get<bool>(Vars.VisLocalBulletTracers))
				Features::Visuals.BulletTracers.AddTracer(shot->m_vecStart, pos, ent);
		}
		else if (Config::Get<bool>(Vars.VisLocalBulletTracers))
			Features::Visuals.BulletTracers.AddTracer(ctx.m_vecEyePos, pos, ent);
	}

	const auto col = Config::Get<Color>( Vars.VisLocalBulletImpactsCol );

	if ( Config::Get<bool>( Vars.VisLocalBulletImpacts ) )
		Interfaces::DebugOverlay->AddBoxOverlay( pos, Vector( -2.0f, -2.0f, -2.0f ), Vector( 2.0f, 2.0f, 2.0f ), QAngle( 0.f, -0.f, 0.f ),
			col.Get<COLOR_R>( ), col.Get<COLOR_G>( ), col.Get<COLOR_B>( ), col.Get<COLOR_A>( ), 4.0f );
}

void RoundStart( IGameEvent* pEvent )
{
	if ( !pEvent || !ctx.m_pLocal )
		return;

	ctx.m_bClearKillfeed = true;
	ctx.m_iBombCarrier = -1;
	ctx.m_iLastShotNumber = 0;

	for ( int i{ }; i < 64; i++ ) {
		Features::Visuals.DormantESP.m_cSoundPlayers[ i ].valid = false;
		Features::Visuals.PlayerESP.Entries.at( i ).Alpha = 0;
		Features::Visuals.PlayerESP.Entries.at( i ).health = 100;
		Features::AnimSys.m_arrEntries.at( i ).OnNewRound( );
	}

	Features::Visuals.GrenadePrediction.m_vecGrenadeSimulations.clear( );

	if ( Config::Get<bool>( Vars.MiscBuyBot ) ) {
		std::string buy;

		switch ( Config::Get<int>( Vars.MiscBuyBotPrimary ) ) {
		case 1:
			buy += "buy scar20; ";
			break;
		case 2:
			buy += "buy ssg08; ";
			break;
		case 3:
			buy += "buy awp; ";
			break;
		case 4:
			buy += "buy negev; ";
			break;		
		case 5:
			buy += "buy ak47; ";
			break;
		}

		switch ( Config::Get<int>( Vars.MiscBuyBotSecondary ) ) {
		case 1:
			buy += "buy deagle; ";
			break;
		case 2:
			buy += "buy elite; ";
			break;
		case 3:
			buy += "buy glock; ";
			break;		
		
		case 4:
			buy += "buy tec9; ";
			break;
		}

		if ( Config::Get<bool>( Vars.MiscBuyBotOtherArmor ) ) {
			buy += "buy vesthelm; ";
			buy += "buy vest; ";
		}

		// team items: a defuser only exists for CT, and CT's molotov is the incendiary
		const bool ct{ ctx.m_pLocal->m_iTeamNum( ) == TEAM_CT };

		if ( Config::Get<bool>( Vars.MiscBuyBotOtherKit ) && ct )
			buy += "buy defuser; ";

		if ( Config::Get<bool>( Vars.MiscBuyBotOtherSmoke ) )
			buy += "buy smokegrenade; ";

		if ( Config::Get<bool>( Vars.MiscBuyBotOtherNade ) )
			buy += "buy hegrenade; ";


		if ( Config::Get<bool>( Vars.MiscBuyBotOtherMolotov ) )
			buy += ct ? "buy incgrenade; " : "buy molotov; ";

		if ( Config::Get<bool>( Vars.MiscBuyBotOtherFlashbang ) )
			buy += "buy flashbang; ";

		if ( Config::Get<bool>( Vars.MiscBuyBotOtherTaser ) )
			buy += "buy taser; ";

		if ( !buy.empty( ) ) {
			// console only: shows which item a "usage: buy <item>" line came from
			Features::Logger.Log( _( "auto buy: " ) + buy, false );
			Interfaces::Engine->ClientCmdUnrestricted( buy.c_str( ) );
		}
	}

}

void WeaponFire( IGameEvent* pEvent ) {
	if ( !pEvent || !ctx.m_pLocal )
		return;

	if ( Interfaces::ClientEntityList->GetClientEntity( Interfaces::Engine->GetPlayerForUserID( pEvent->GetInt( _( "userid" ) ) ) ) != ctx.m_pLocal )
		return;

	if ( Features::Shots.m_vecShots.empty( ) )
		return;

	const auto shot{ std::find_if(
		Features::Shots.m_vecShots.begin( ), Features::Shots.m_vecShots.end( ),
		[ ]( const Shot_t& shot ) {
			return !shot.m_iServerProcessTick;
		}
	) };

	if ( shot == Features::Shots.m_vecShots.end( ) )
		return;

	shot->m_iServerProcessTick = Interfaces::ClientState->iServerTick;
	shot->m_strWeaponName = pEvent->GetString( _( "weapon" ) );
}

void PlayerDeath( IGameEvent* pEvent ) {
	const auto user{ pEvent->GetInt( _( "userid" ) ) };
	ctx.m_iLastID = Interfaces::Engine->GetPlayerForUserID( user );

	if ( !ctx.m_pLocal )
		return;

	// our kill: kill effect + kill sound
	const auto attackerIndex{ Interfaces::Engine->GetPlayerForUserID( pEvent->GetInt( _( "attacker" ) ) ) };
	if ( attackerIndex != Interfaces::Engine->GetLocalPlayer( ) || ctx.m_iLastID == attackerIndex )
		return;

	const auto victim{ static_cast< CBasePlayer* >( Interfaces::ClientEntityList->GetClientEntity( ctx.m_iLastID ) ) };
	Features::Visuals.KillEffects.Add( victim, pEvent->GetBool( _( "headshot" ) ) );
}
namespace {
	// weapon_fire says "weapon_ssg08", player_hurt says "ssg08": compare without the prefix
	std::string BareWeaponName( std::string name ) {
		constexpr const char* prefix{ "weapon_" };
		if ( name.rfind( prefix, 0 ) == 0 )
			name.erase( 0, std::char_traits< char >::length( prefix ) );
		return name;
	}

	// damage that never came from one of our bullets ( fire, grenades )
	bool IsNonBulletDamage( const std::string& weapon ) {
		return weapon == "inferno" || weapon == "hegrenade" || weapon == "molotov" || weapon == "incgrenade"
			|| weapon == "decoy" || weapon == "flashbang" || weapon == "smokegrenade" || weapon == "world";
	}

	// our shot and this damage event are the same bullet
	bool SameWeapon( const std::string& fired, const std::string& hurt ) {
		const auto a{ BareWeaponName( fired ) };
		const auto b{ BareWeaponName( hurt ) };
		if ( a == b )
			return true;

		// aliases ( revolver fires as deagle, knives / skins ): any bullet damage in the same tick from us counts
		return !IsNonBulletDamage( b );
	}
}

void PlayerHurt( IGameEvent* pEvent ) {
	static auto GetHitboxByHitGroup = [ ]( int hitgroup ) -> int
	{
		switch ( hitgroup )
		{
		case HITGROUP_HEAD:
			return HITBOX_HEAD;
		case HITGROUP_CHEST:
			return HITBOX_CHEST;
		case HITGROUP_STOMACH:
			return HITBOX_STOMACH;
		case HITGROUP_LEFTARM:
			return HITBOX_LEFT_HAND;
		case HITGROUP_RIGHTARM:
			return HITBOX_RIGHT_HAND;
		case HITGROUP_LEFTLEG:
			return HITBOX_LEFT_CALF;
		case HITGROUP_RIGHTLEG:
			return HITBOX_RIGHT_CALF;
		default:
			return HITBOX_PELVIS;
		}
	};


	if ( !pEvent || !ctx.m_pLocal )
		return;

	const auto attacker = pEvent->GetInt( _( "attacker" ) );
	const auto user = pEvent->GetInt( _( "userid" ) );
	const auto damage = pEvent->GetInt( _( "dmg_health" ) );
	const auto hitgroup = pEvent->GetInt( _( "hitgroup" ) );

	const auto attacker_id = Interfaces::Engine->GetPlayerForUserID( attacker );
	if ( attacker_id != Interfaces::Engine->GetLocalPlayer( ) )
		return;

	const auto user_id = Interfaces::Engine->GetPlayerForUserID( user );
	const auto victim = static_cast<CBasePlayer*>( Interfaces::ClientEntityList->GetClientEntity( user_id ) );
	if ( !victim )
		return;

	const bool lethal{ pEvent->GetInt( _( "health" ) ) <= 0 };

	// a kill plays the kill sound instead ( when one is set )
	if ( !( lethal && Config::Get<int>( Vars.MiscKillSound ) ) )
		SoundPack::Play( Config::Get<int>( Vars.MiscHitSound ), Config::Get<std::string>( Vars.MiscCustomHitSound ) );

	const auto shot = Features::Shots.LastUnprocessed( );

	if ( !shot || !shot->m_bHasImpact || shot->m_bHitPlayer
		|| !SameWeapon( shot->m_strWeaponName, pEvent->GetString( _( "weapon" ) ) )
		|| ( shot->m_pPlayer != victim ) ) {
		if ( victim ) {
			const auto hdr = Interfaces::ModelInfo->GetStudioModel( victim->GetModel( ) );
			if ( !hdr )
				return;

			const auto HitboxSet = hdr->GetHitboxSet( victim->m_nHitboxSet( ) );
			if ( !HitboxSet )
				return;

			const auto hitbox = HitboxSet->GetHitbox( GetHitboxByHitGroup( hitgroup ) );
			if ( !hitbox )
				return;

			const auto min = Math::VectorTransform( hitbox->vecBBMin, victim->m_CachedBoneData( ).Base( )[ hitbox->iBone ] );
			const auto max = Math::VectorTransform( hitbox->vecBBMax, victim->m_CachedBoneData( ).Base( )[ hitbox->iBone ] );

			const auto point = ( min + max ) * 0.5f;

			Features::Visuals.AddHit( { point, damage, Interfaces::Globals->flRealTime, hitgroup, lethal } );
		}
		return;
	}

	Features::Visuals.AddHit( { shot->m_vecPredEnd, damage, Interfaces::Globals->flRealTime, hitgroup, lethal } );

	shot->m_iServerHitgroup = hitgroup;
	shot->m_iServerDamage = damage;
	shot->m_bHitPlayer = true;
}

void CEventListener::FireGameEvent( IGameEvent* pEvent ) {
	if ( pEvent == nullptr )
		return;

	const FNV1A_t uNameHash = FNV1A::Hash( pEvent->GetName( ) );

	switch ( uNameHash ) {
	case FNV1A::HashConst( "bullet_impact" ): BulletImpact( pEvent ); break;
	case FNV1A::HashConst( "round_start" ): RoundStart( pEvent ); break;
	case FNV1A::HashConst( "player_hurt" ): PlayerHurt( pEvent ); break;
	case FNV1A::HashConst( "player_death" ): PlayerDeath( pEvent ); break;
	case FNV1A::HashConst( "weapon_fire" ): WeaponFire( pEvent ); break;
	default: break;
	}
}
