#include "visuals.h"

void CBulletTracers::Draw( ) {
	if ( m_vecTracers.empty( ) )
		return;

	m_vecTracers.erase(
		std::remove_if(
			m_vecTracers.begin( ), m_vecTracers.end( ),
			[ & ]( const TracerData_t& tracer ) -> bool {
				return Interfaces::Globals->flRealTime >= tracer.m_flStartTime + 2.5f;
			}
		),
		m_vecTracers.end( )
				);

	if ( m_vecTracers.empty( ) )
		return;

	switch ( Config::Get<int>( Vars.VisBulletTracersType ) ) {
	case 1:
		if ( !PrecacheModel( _( "materials/sprites/laserbeam.vmt" ) ) )
			return;
		break;
	}

	for ( const auto& tracer : m_vecTracers ) {
		const float age{ Interfaces::Globals->flRealTime - tracer.m_flStartTime };
		// hold, then ease out ( shared animation curves )
		const auto alpha{ 1.f - Anim::EaseIn( std::clamp( ( age - 0.6f ) / 1.9f, 0.f, 1.f ) ) };

		const auto& col{ tracer.m_pShooter == ctx.m_pLocal ? Config::Get<Color>( Vars.VisLocalBulletTracersCol ) : Config::Get<Color>( Vars.VisOtherBulletTracersCol ) };

		// impact spark: a few short streaks that fly out and fade
		const auto spark{ [ & ]( ) {
			if ( age >= 0.35f )
				return;

			Vector2D hit{ };
			if ( !Math::WorldToScreen( tracer.m_vecEnd, hit ) || hit.IsZero( ) )
				return;

			const float t{ age / 0.35f };
			const float reach{ 3.f + 12.f * Anim::EaseOut( t ) };
			const auto sparkCol{ Color( col ).Lerp( Color( 255, 255, 255, static_cast< int >( col.Get<COLOR_A>( ) ) ), 0.5f ).Set<COLOR_A>( 255.f * ( 1.f - t ) ) };
			for ( int i{ }; i < 6; ++i ) {
				const float rad{ ( 60.f * i + 15.f ) * MenuDraw::DEG_TO_RAD };
				const float dx{ std::cos( rad ) }, dy{ std::sin( rad ) };
				Render::Line( Vector2D( hit.x + dx * reach, hit.y + dy * reach ), Vector2D( hit.x + dx * ( reach + 4.f ), hit.y + dy * ( reach + 4.f ) ), sparkCol );
			}
		} };

		switch ( Config::Get<int>( Vars.VisBulletTracersType ) ) {
		case 0: {
			Vector2D start{ };
			if ( !Math::WorldToScreen( tracer.m_vecStart, start ) )
				continue;

			Vector2D end{ };
			if ( !Math::WorldToScreen( tracer.m_vecEnd, end ) )
				continue;

			// soft glow under a bright core
			MenuDraw::ThickLine( start, end, col.Set<COLOR_A>( col.Get<COLOR_A>( ) * alpha * 0.3f ), 3.f );
			Render::Line( start, end, Color( col ).Lerp( Color( 255, 255, 255, static_cast< int >( col.Get<COLOR_A>( ) ) ), 0.25f ).Set<COLOR_A>( col.Get<COLOR_A>( ) * alpha ) );
			spark( );

			break;
		}
		case 1: {
			BeamInfo_t info{ };

			info.nType = 0;
			info.pszModelName = _( "materials/sprites/laserbeam.vmt" );
			info.nModelIndex = Interfaces::ModelInfo->GetModelIndex( _( "materials/sprites/laserbeam.vmt" ) );
			info.flHaloScale = 0.0f;
			info.flLife = 0.09f; //0.09
			info.flWidth = .6f;
			info.flEndWidth = .75f;
			info.flFadeLength = 3.0f;
			info.flAmplitude = 0.f;
			info.flBrightness = col.Get<COLOR_A>( ) * alpha;
			info.flSpeed = 1.f;
			info.iStartFrame = 1;
			info.flFrameRate = 60;
			info.flRed = col.Get<COLOR_R>( );
			info.flGreen = col.Get<COLOR_G>( );
			info.flBlue = col.Get<COLOR_B>( );
			info.nSegments = 4;
			info.bRenderable = true;
			info.nFlags = 0;

			info.vecStart = tracer.m_vecEnd;
			info.vecEnd = tracer.m_vecStart;

			const auto beam{ Interfaces::ViewRenderBeams->CreateBeamPoints( info ) };
			if ( beam )
				Interfaces::ViewRenderBeams->DrawBeam( beam );

			spark( );

			break;
		}
		}
	}
}