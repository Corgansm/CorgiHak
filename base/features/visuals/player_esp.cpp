#include "visuals.h"
#include "../animations/animation.h"
#include "../../core/lua/lua_engine.h"

void CPlayerESP::Main( CBasePlayer* ent ) {
	int type{ ENEMY };
	if ( ent == ctx.m_pLocal )
		type = LOCAL;
	else if ( ent->IsTeammate( ) ) 
		type = TEAM;

	if ( ent->IsDead( ) || ent->m_iHealth( ) <= 0 )
		return;

	CheckPlayerBoolFig( type, VisEnable )

	auto& entry = Entries.at( ent->Index( ) - 1 );
	entry.type = type;
	entry.ent = ent;

	// health: the bar drains smoothly, and a lighter chunk shows the damage just taken before catching up
	const float hp{ static_cast< float >( std::clamp( ent->m_iHealth( ), 0, 100 ) ) };
	if ( entry.m_flHealth < 0.f || hp > entry.m_flHealth + 0.5f ) {
		entry.m_flHealth = hp;
		entry.m_flHealthTrail = hp;
	}
	else {
		if ( hp < entry.m_flHealth - 0.5f && entry.m_flHealthTrail <= entry.m_flHealth + 0.5f )
			entry.m_flTrailDelay = 0.35f;
		entry.m_flHealth = Anim::Approach( entry.m_flHealth, hp, 12.f );

		entry.m_flTrailDelay = std::max( 0.f, entry.m_flTrailDelay - Anim::Dt( ) );
		if ( entry.m_flTrailDelay <= 0.f )
			entry.m_flHealthTrail = Anim::Approach( entry.m_flHealthTrail, entry.m_flHealth, 6.f );
		entry.m_flHealthTrail = std::max( entry.m_flHealthTrail, entry.m_flHealth );
	}
	entry.health = static_cast< int >( std::round( entry.m_flHealth ) );

	// fades: quick in, slow out while dormant ( shared easing )
	entry.m_flFade = ent->Dormant( ) ? Anim::Step( entry.m_flFade, false, 5.f ) : Anim::Step( entry.m_flFade, true, 0.35f );
	entry.Alpha = Anim::EaseOut( entry.m_flFade );

	if ( ent->Dormant( ) )
		entry.DormancyFade += Interfaces::Globals->flFrameTime;
	else
		entry.DormancyFade -= Interfaces::Globals->flFrameTime * 2.f;

	entry.DormancyFade = std::clamp<float>( entry.DormancyFade, 0.f, 1.f );

	DrawOOF( entry );

	if ( !GetBBox( ent, entry.BBox ) )
		return;

	DrawBox( entry );
	DrawHealth( entry );
	DrawName( entry );
	DrawSkeleton( entry );
	DrawFlags( entry );
	DrawWeapon( entry, DrawAmmo( entry ) );

	if ( entry.m_iNadeDamage > 0 )
		Render::Text( Fonts::HealthESP, entry.BBox.x + entry.BBox.w / 2, entry.BBox.y - 23, Color( 255, 255, 255, static_cast<int>( 255 * entry.Alpha ) ), FONT_CENTER, std::to_string( entry.m_iNadeDamage ).c_str( ) );

	entry.m_iNadeDamage = 0;
}

void CPlayerESP::DrawBox( VisualPlayerEntry& entry ) {
	CheckPlayerBoolFig( entry.type, VisBox );

	Color last{};
	GetPlayerColorFig( entry.type, VisBoxCol, last );

	last = last.Set<COLOR_A>( last.Get<COLOR_A>( ) * entry.Alpha );
	last = last.Lerp( DormantCol.Set<COLOR_A>( last.Get<COLOR_A>( ) * 0.4f ), entry.DormancyFade );

	const auto outline{ Color( 0, 0, 0, static_cast< int >( last.Get<COLOR_A>( ) ) ) };

	Render::Rectangle( entry.BBox.x - 1, entry.BBox.y - 1, entry.BBox.w + 2, entry.BBox.h + 2, outline );
	Render::Rectangle( entry.BBox.x + 1, entry.BBox.y + 1,  entry.BBox.w - 2, entry.BBox.h - 2, outline );
	Render::Rectangle( entry.BBox.x, entry.BBox.y,  entry.BBox.w, entry.BBox.h, last );
}

void CPlayerESP::DrawHealth( VisualPlayerEntry& entry ) {
	CheckPlayerBoolFig( entry.type, VisHealth );

	Color last;
	GetPlayerColorFig( entry.type, VisHealthCol, last );

	switch ( entry.type ) {
		case 0:
			if ( Config::Get<bool>( Vars.VisHealthOverrideLocal ) )
				break;
		case 1:
			if ( Config::Get<bool>( Vars.VisHealthOverrideTeam ) )
				break;
		case 2:
			if ( Config::Get<bool>( Vars.VisHealthOverrideEnemy ) )
				break;

		const int green{ static_cast<int>( static_cast< float >( std::min( entry.health, 100 ) ) * 2.55f ) };
		const int red{ 255 - green };
		last = Color( red, green, 0 );
	}

	last = last.Set<COLOR_A>( last.Get<COLOR_A>( ) * entry.Alpha );
	last = last.Lerp( DormantCol.Set<COLOR_A>( last.Get<COLOR_A>( ) * 0.4f ), entry.DormancyFade );

	const auto outline{ Color( 0, 0, 0, static_cast< int >( last.Get<COLOR_A>( ) ) ) };

	const auto bar_height{ static_cast<int>( std::max( entry.m_flHealth, 0.f ) * static_cast< float >( entry.BBox.h ) / 100.0f ) };
	const auto offset{ entry.BBox.h - bar_height };

	// dark track, coloured fill with a lighter top, outline
	Render::FilledRectangle( entry.BBox.x - 6, entry.BBox.y - 1, 4, entry.BBox.h + 2, outline.Set<COLOR_A>( outline.Get<COLOR_A>( ) * .6f ) );

	// damage just taken: pale segment above the fill that shrinks after a short delay
	const auto trail_height{ static_cast<int>( std::max( entry.m_flHealthTrail, 0.f ) * static_cast< float >( entry.BBox.h ) / 100.0f ) };
	if ( trail_height > bar_height + 1 )
		Render::FilledRectangle( entry.BBox.x - 5, entry.BBox.y + entry.BBox.h - trail_height, 2, trail_height - bar_height,
			Color( 255, 255, 255, static_cast< int >( last.Get<COLOR_A>( ) * 0.55f ) ) );
	if ( bar_height > 0 ) {
		const Color white( 255, 255, 255, static_cast< int >( last.Get<COLOR_A>( ) ) );
		const Color top{ last.Lerp( white, 0.25f ) };
		Render::Gradient( static_cast< int >( entry.BBox.x - 5 ), static_cast< int >( entry.BBox.y + offset ), 2, static_cast< int >( bar_height ), top, last, false );
	}

	Render::Rectangle( entry.BBox.x - 6, entry.BBox.y - 1, 4, entry.BBox.h + 2, outline );
	//Render::Text( Fonts::HealthESP, entry.BBox.x + entry.BBox.w + 15, entry.BBox.y, Color( 255, 255, 255 ).Set<COLOR_A>( last.Get<COLOR_A>( ) ), FONT_RIGHT, std::to_string( Features::AnimSys.m_arrEntries.at( entry.ent->Index( ) - 1 ).m_iLastChoked ).c_str( ) );

	if ( entry.health < 100 )
		Render::Text( Fonts::HealthESP, entry.BBox.x - 5, entry.BBox.y + offset - 3, Color( 255, 255, 255 ).Set<COLOR_A>( last.Get<COLOR_A>( ) ), FONT_CENTER, std::to_string( entry.health ).c_str( ) );
}

bool CPlayerESP::DrawAmmo( VisualPlayerEntry& entry ) {
	switch ( entry.type ) {
		case 0: if ( !Config::Get<bool>( Vars.VisAmmoLocal ) ) return false; break;
		case 1: if ( !Config::Get<bool>( Vars.VisAmmoTeam ) ) return false; break;
		case 2: if ( !Config::Get<bool>( Vars.VisAmmoEnemy ) ) return false; break;
	}

	const auto weapon = entry.ent->GetWeapon( );
	if ( !weapon )
		return false;

	//if ( weapon->m_iItemDefinitionIndex( ) == WEAPON_C4 || weapon->m_iItemDefinitionIndex( ) == WEAPON_HEALTHSHOT || weapon->IsGrenade( ) || weapon->IsKnife( ) )
		//return false;

	const auto weapon_info = weapon->GetCSWeaponData( );
	if ( !weapon_info )
		return false;

	const auto ammo = weapon->m_iClip1( );
	const auto max_clip = weapon_info->iMaxClip1;

	Color last;
	GetPlayerColorFig( entry.type, VisAmmoCol, last )

	//last = last.Set<COLOR_A>( last.Get<COLOR_A>( ) * entry.Alpha );
	//last = last.Lerp( DormantCol.Set<COLOR_A>( last.Get<COLOR_A>( ) * 0.4f ), entry.DormancyFade );

	const auto outline = Color( 0, 0, 0, static_cast< int >( last.Get<COLOR_A>( ) ) );

	// outline
	//Render::FilledRectangle( entry.BBox.x - 1, entry.BBox.y + entry.BBox.h + 2,  entry.BBox.w + 2, 4, outline.Set<COLOR_A>( outline.Get<COLOR_A>( ) * .5f ) );
	// color
	//if ( ammo )
	//	Render::FilledRectangle( entry.BBox.x, entry.BBox.y + entry.BBox.h + 3,  std::min( entry.BBox.w, ammo * entry.BBox.w / max_clip ), 2, last );

	//Render::Rectangle( entry.BBox.x - 1, entry.BBox.y + entry.BBox.h + 2, entry.BBox.w + 2, 4, outline );

	return true;
}

void CPlayerESP::DrawName( VisualPlayerEntry& entry ) {
	CheckPlayerBoolFig( entry.type, VisName )

	Color last;
	GetPlayerColorFig( entry.type, VisNameCol, last )

	last = last.Set<COLOR_A>( last.Get<COLOR_A>( ) * entry.Alpha );
	last = last.Lerp( DormantCol.Set<COLOR_A>( last.Get<COLOR_A>( ) * 0.4f ), entry.DormancyFade );

	static auto sanitize = [ ]( char* name ) -> std::string {
		name[ 127 ] = '\0';

		std::string tmp( name );

		if ( tmp.length( ) > 20 ) {
			tmp.erase( 20, tmp.length( ) - 20 );
			tmp.append( "..." );
		}

		return tmp;
	};

	auto player_info = Interfaces::Engine->GetPlayerInfo( entry.ent->Index( ) );
	if ( !player_info.has_value() )
		return;

	const auto name = sanitize( player_info.value( ).szName );

	Render::Text( Fonts::NameESP, entry.BBox.x + entry.BBox.w / 2, entry.BBox.y - 13, last, FONT_CENTER, name.c_str( ) );
}

void CPlayerESP::DrawWeapon( VisualPlayerEntry& entry, bool AmmoBar ) {
	switch ( entry.type ) {
		case 0: if ( !Config::Get<bool>( Vars.VisWeapIconLocal ) && !Config::Get<bool>( Vars.VisWeapTextLocal ) ) return; break;
		case 1: if ( !Config::Get<bool>( Vars.VisWeapIconTeam ) && !Config::Get<bool>( Vars.VisWeapTextTeam ) ) return; break;
		case 2: if ( !Config::Get<bool>( Vars.VisWeapIconEnemy ) && !Config::Get<bool>( Vars.VisWeapTextEnemy ) ) return; break;
	}

	const auto weapon = entry.ent->GetWeapon( );
	if ( !weapon )
		return;

	Color last;
	GetPlayerColorFig( entry.type, VisWeapCol, last )

	last = last.Set<COLOR_A>( last.Get<COLOR_A>( ) * entry.Alpha );
	last = last.Lerp( DormantCol.Set<COLOR_A>( last.Get<COLOR_A>( ) * 0.4f ), entry.DormancyFade );

	int append{ 2 };
	if ( AmmoBar )
		append += 6;
	CheckIfPlayer( VisWeapText, entry.type ) {
		Render::Text( Fonts::WeaponIcon, entry.BBox.x + entry.BBox.w / 2, entry.BBox.y + entry.BBox.h + append, last.Set<COLOR_A>( last.Get<COLOR_A>( ) * .9f ), FONT_CENTER, weapon->GetIcon( ).c_str( ) );

		append += 12;
	}

	CheckIfPlayer( VisWeapIcon, entry.type ) {
		Render::Text( Fonts::HealthESP, entry.BBox.x + entry.BBox.w / 2, entry.BBox.y + entry.BBox.h + append, last, FONT_CENTER, weapon->GetGunName( ).c_str( ) );
	}
}

void CPlayerESP::DrawFlags( VisualPlayerEntry& entry ) {
	if ( entry.ent->Dormant( ) )
		return;

	std::vector<std::pair< std::string, Color>> flags{ };
	CheckIfPlayer( VisFlagDefusing, entry.type ) {
		if ( entry.ent->m_bIsDefusing( ) )
			flags.push_back( std::make_pair( _( "DEFUSING" ), Color( 255, 90, 71 ) ) );
	}

	CheckIfPlayer( VisFlagBLC, entry.type ) {
		if ( Features::AnimSys.m_arrEntries.at( entry.ent->Index( ) - 1 ).m_bBrokeLC )
			flags.push_back( std::make_pair( _( "LC" ), Color( 255, 132, 117 ) ) );
	}

	CheckIfPlayer( VisFlagBLC, entry.type ) {
		const auto& brain{ Features::Ragebot.m_cBrain.Get( entry.ent->Index( ) ) };
		if ( brain.m_bDefensive )
			flags.push_back( std::make_pair( _( "DEFENSIVE" ), Color( 255, 158, 97 ) ) );
		else if ( brain.m_bDefensiveUser )
			flags.push_back( std::make_pair( _( "DEF" ), Color( 255, 205, 160 ) ) );

		if ( brain.m_bFlicker )
			flags.push_back( std::make_pair( _( "FLICK" ), Color( 160, 200, 255 ) ) );

		// defensive profile: what they flick to inside shifts, and how often they shift
		if ( brain.m_bDefensiveUser && brain.m_iDefYawSamples >= 3 ) {
			const int pitchMost{ std::max( { brain.m_iDefPitchUp, brain.m_iDefPitchZero, brain.m_iDefPitchDown } ) };
			const char* pitch{ pitchMost == brain.m_iDefPitchUp ? "UP" : pitchMost == brain.m_iDefPitchZero ? "ZERO" : "DOWN" };
			std::string profile{ std::string( pitch ) + " " + std::to_string( static_cast< int >( brain.m_flDefYawOffset ) ) };
			if ( brain.m_flShiftInterval > 0.f )
				profile += " /" + std::to_string( static_cast< int >( brain.m_flShiftInterval ) ) + "t";
			flags.push_back( std::make_pair( profile, Color( 200, 200, 200 ) ) );
		}
	}

	// defensive detector readout: drops seen, then what each was classed as, last drop size ( * = had defensive angles )
	if ( Config::Get<bool>( Vars.RagebotDefDebug ) && entry.ent && !entry.ent->IsTeammate( ) ) {
		const auto& brain{ Features::Ragebot.m_cBrain.Get( entry.ent->Index( ) ) };
		flags.push_back( std::make_pair( "drops " + std::to_string( brain.m_iDbgShifts ) + " def " + std::to_string( brain.m_iDbgDefensive )
			+ " push " + std::to_string( brain.m_iDbgPushed ),
			Color( 255, 205, 160 ) ) );
		flags.push_back( std::make_pair( "dt " + std::to_string( brain.m_iDbgDoubletap ) + " sz " + std::to_string( brain.m_iDbgSize )
			+ " net " + std::to_string( brain.m_iDbgNetwork ) + " sim " + std::to_string( brain.m_iDbgSimultaneous )
			+ " ang " + std::to_string( brain.m_iDbgNoAngles ), Color( 190, 190, 200 ) ) );
		if ( brain.m_iDbgShifts > 0 )
			flags.push_back( std::make_pair( "last " + std::to_string( brain.m_iDbgLastShift ) + "t" + ( brain.m_bDbgLastAngles ? "*" : "" ), Color( 190, 190, 200 ) ) );
	}

	CheckIfPlayer( VisFlagC4, entry.type ) {
		if ( entry.ent->Index( ) == ctx.m_iBombCarrier )
			flags.push_back( std::make_pair( _( "C4" ), Color( 255, 255, 255 ) ) );
	}

	CheckIfPlayer( VisFlagArmor, entry.type ) {
		if ( entry.ent->m_ArmorValue( ) > 0 )
			flags.push_back( std::make_pair( entry.ent->m_bHasHelmet( ) ? _( "HK" ) : _( "K" ), Color( 255, 255, 255 ) ) );
	}	
	
	CheckIfPlayer( VisFlagFlash, entry.type ) {
		if ( entry.ent->m_flFlashDuration( ) > 1.f )
			flags.push_back( std::make_pair( _( "FLASH" ), Color( 255, 255, 255 ) ) );
	}	
	
	CheckIfPlayer( VisFlagReload, entry.type ) {
		const auto& layer{ entry.ent->m_AnimationLayers( )[ 1 ] };

		if ( entry.ent->GetSequenceActivity( layer.nSequence ) == 967u && layer.flWeight != 0.f )
			flags.push_back( std::make_pair( _( "RELOAD" ), Color( 255, 255, 255 ) ) );
	}	
	
	CheckIfPlayer( VisFlagScoped, entry.type ) {
		if ( entry.ent->m_bIsScoped( ) )
			flags.push_back( std::make_pair( _( "SCOPED" ), Color( 255, 255, 255 ) ) );
	}

	// client.register_esp_flag
	Lua::AppendEspFlags( entry.ent, flags );

	// ragebot debug overlay: what the aimbot decided for this enemy on its last pass
	if ( Config::Get<bool>( Vars.RagebotDebugOverlay ) && entry.type == ENEMY ) {
		const auto& debug{ Features::Ragebot.Debug( entry.ent->Index( ) ) };
		if ( debug.m_iTick >= 0 && Interfaces::Globals->iTickCount - debug.m_iTick <= 8 ) {
			Color statusColor{ 200, 200, 200 };
			if ( debug.m_bFired )
				statusColor = Color( 120, 255, 120 );
			else if ( debug.m_bTarget )
				statusColor = Color( 255, 214, 153 );
			else if ( debug.m_strStatus != "OK" )
				statusColor = Color( 255, 120, 110 );

			// strip format characters: Render::Text treats the text as a format string
			std::string status{ debug.m_strStatus };
			status.erase( std::remove( status.begin( ), status.end( ), '%' ), status.end( ) );
			flags.push_back( std::make_pair( ( debug.m_bTarget ? std::string( "> " ) : std::string( ) ) + status, statusColor ) );

			if ( debug.m_iRecordAge >= 0 ) {
				const int limit{ TIME_TO_TICKS( 0.2f ) };
				std::string record{ "rec " + std::to_string( debug.m_iRecordAge ) + "/" + std::to_string( limit ) + "t" };
				if ( !debug.m_iValidity )
					record += " expired";
				flags.push_back( std::make_pair( record, debug.m_iValidity ? Color( 200, 200, 200 ) : Color( 255, 120, 110 ) ) );
			}

			if ( debug.m_bExtrapolated )
				flags.push_back( std::make_pair( "ex +" + std::to_string( debug.m_iExtrapTicks ) + "t / " + std::to_string( debug.m_iSpread ) + "u", Color( 160, 200, 255 ) ) );

			if ( debug.m_iDamage > 0 )
				flags.push_back( std::make_pair( "dmg " + std::to_string( debug.m_iDamage ), Color( 200, 200, 200 ) ) );
		}
	}

	int i{ };
	for ( const auto& flag : flags ) {
		Render::Text( Fonts::HealthESP, entry.BBox.x + entry.BBox.w + 2, entry.BBox.y + 8 * i, flag.second.Set<COLOR_A>( static_cast< int >( 200 * entry.Alpha ) ), 0, flag.first.c_str( ) );

		++i;
	}
}

void CPlayerESP::DrawSkeleton( VisualPlayerEntry& entry ) {
	CheckPlayerBoolFig( entry.type, VisSkeleton )

	if ( entry.ent->IsDormant( ) )
		return;

	const auto hdr{ entry.ent->m_pStudioHdr( )->pStudioHdr };
	if ( !hdr )
		return;

	Color last;
	GetPlayerColorFig( entry.type, VisSkeletonCol, last )

	last = last.Set<COLOR_A>( last.Get<COLOR_A>( ) * entry.Alpha );
	last = last.Lerp( DormantCol.Set<COLOR_A>( last.Get<COLOR_A>( ) * 0.4f ), entry.DormancyFade );

	Vector2D bone1, bone2;
	for ( size_t n{ }; n < hdr->nBones; ++n ) {
		auto* bone = hdr->GetBone( n );
		if ( !bone || !( bone->iFlags & 256 ) || bone->iParent == -1 ) {
			continue;
		}

		auto BonePos = [ & ]( int n ) -> Vector {
			return {
				entry.ent->m_CachedBoneData( ).Base( )[ n ][ 0 ][ 3 ],
				entry.ent->m_CachedBoneData( ).Base( )[ n ][ 1 ][ 3 ],
				entry.ent->m_CachedBoneData( ).Base( )[ n ][ 2 ][ 3 ]
			};
		};

		if ( !Math::WorldToScreen( BonePos( n ), bone1 ) || !Math::WorldToScreen( BonePos( bone->iParent ), bone2 ) )
			continue;

		Render::Line( bone1, bone2, last );
	}
}

void RotateTriangle( std::array<Vector2D, 3>& points, float rotation ) {
	const auto pointsCenter = ( points.at( 0 ) + points.at( 1 ) + points.at( 2 ) ) / 3;
	for ( auto& point : points ) {
		point -= pointsCenter;

		const auto tempX = point.x;
		const auto tempY = point.y;

		const auto theta = DEG2RAD( rotation );
		const auto c = cos( theta );
		const auto s = sin( theta );

		point.x = tempX * c - tempY * s;
		point.y = tempX * s + tempY * c;

		point += pointsCenter;
	}
}

void CPlayerESP::DrawOOF( VisualPlayerEntry& entry ) {
	CheckPlayerBoolFig( entry.type, VisOOF );

	Color last;
	GetPlayerColorFig( entry.type, VisOOFCol, last );

	last = last.Set<COLOR_A>( last.Get<COLOR_A>( ) * entry.Alpha );
	last = last.Lerp( DormantCol.Set<COLOR_A>( last.Get<COLOR_A>( ) * 0.4f ), entry.DormancyFade );

	if ( ctx.m_pLocal->IsDead( ) )
		return;

	Vector screenPos;
	if ( Math::WorldToScreen( entry.ent->GetAbsOrigin( ), screenPos ) )
		return;

	QAngle viewAngles{ ctx.m_angOriginalViewangles };

	// read every frame so a resolution change moves the arrows too
	const int width{ static_cast<int>( ctx.m_ve2ScreenSize.x ) }, height{ static_cast< int >( ctx.m_ve2ScreenSize.y ) };

	const auto screenCenter{ Vector2D( width * 0.5f, height * 0.5f ) };
	const auto angleYawRad{ DEG2RAD( viewAngles.y - Math::CalcAngle( ctx.m_vecEyePos, entry.ent->GetAbsOrigin( ) ).y - 90.0f ) };

	constexpr auto radius{ 20 };
	constexpr auto size{ 10 };

	const auto newPointX{ screenCenter.x + ( ( ( ( width - ( size * 3 ) ) * 0.5f ) * ( radius / 100.0f ) ) * cos( angleYawRad ) ) + ( int )( 6.0f * ( ( ( float )size - 4.0f ) / 16.0f ) ) };
	const auto newPointY{ screenCenter.y + ( ( ( ( height - ( size * 3 ) ) * 0.5f ) * ( radius / 100.0f ) ) * sin( angleYawRad ) ) };

	std::array <Vector2D, 3> points
	{
		Vector2D( newPointX - size, newPointY - size ),
		Vector2D( newPointX + size, newPointY ),
		Vector2D( newPointX - size, newPointY + size )
	};

	RotateTriangle( points, viewAngles.y - Math::CalcAngle( ctx.m_vecEyePos, entry.ent->GetAbsOrigin( ) ).y - 90.0f );


	// breathing fill + solid outline
	const auto pulse{ 0.35f + 0.25f * std::sin( Interfaces::Globals->flRealTime * 4.f ) };
	MenuDraw::ConvexPolygon( { points.at( 0 ), points.at( 1 ), points.at( 2 ) }, last.Set<COLOR_A>( last.Get<COLOR_A>( ) * pulse ) );
	Render::Line( points.at( 0 ), points.at( 1 ), last );
	Render::Line( points.at( 1 ), points.at( 2 ), last );
	Render::Line( points.at( 2 ), points.at( 0 ), last );
}

bool CPlayerESP::GetBBox( CBasePlayer* ent, rect& box ) {
	// pasted i dont care about making this func myself
	const auto min = ent->m_vecMins( );
	const auto max = ent->m_vecMaxs( );

	QAngle dir = ctx.m_angOriginalViewangles;
	Vector vF, vR, vU;
	dir.x = 0;
	dir.z = 0;

	Math::AngleVectors( dir, &vF, &vR, &vU );

	const auto zh = vU * max.z + vF * max.y + vR * min.x; // = Front left front
	const auto e = vU * max.z + vF * max.y + vR * max.x; //  = Front right front
	const auto d = vU * max.z + vF * min.y + vR * min.x; //  = Front left back
	const auto c = vU * max.z + vF * min.y + vR * max.x; //  = Front right back

	const auto g = vU * min.z + vF * max.y + vR * min.x; //  = Bottom left front
	const auto f = vU * min.z + vF * max.y + vR * max.x; //  = Bottom right front
	const auto a = vU * min.z + vF * min.y + vR * min.x; //  = Bottom left back
	const auto b = vU * min.z + vF * min.y + vR * max.x; //  = Bottom right back*-

	Vector pointList[ ] = {
		a,
		b,
		c,
		d,
		e,
		f,
		g,
		zh,
	};

	Vector2D transformed[ ARRAYSIZE( pointList ) ];

	for ( int i = 0; i < ARRAYSIZE( pointList ); i++ )
	{
		auto origin = ent->GetAbsOrigin( );

		pointList[ i ] += origin;

		if ( !Math::WorldToScreen( pointList[ i ], transformed[ i ] ) )
			return false;
	}

	float left = FLT_MAX;
	float top = -FLT_MAX;
	float right = -FLT_MAX;
	float bottom = FLT_MAX;

	for ( int i = 0; i < ARRAYSIZE( pointList ); i++ ) {
		if ( left > transformed[ i ].x )
			left = transformed[ i ].x;
		if ( top < transformed[ i ].y )
			top = transformed[ i ].y;
		if ( right < transformed[ i ].x )
			right = transformed[ i ].x;
		if ( bottom > transformed[ i ].y )
			bottom = transformed[ i ].y;
	}

	box.x = left;
	box.y = bottom;
	box.w = right - left;
	box.h = top - bottom + 5;

	return true;
}