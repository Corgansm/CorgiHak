#include "logger.h"

void CLogger::PrintToScreen( ) {
	constexpr float rowHeight{ 24.f };
	constexpr std::size_t maxRows{ 8 };

	m_cOnscreenText.erase(
		std::remove_if(
			m_cOnscreenText.begin( ), m_cOnscreenText.end( ),
			[ & ]( const OnsCreen_t& onscreen ) -> bool {
				return onscreen.m_flTimeOnScreen <= 0.f && !onscreen.m_cFade.Visible( );
			}
		),
		m_cOnscreenText.end( )
				);

	// too many rows: the oldest ones leave early
	if ( m_cOnscreenText.size( ) > maxRows ) {
		for ( std::size_t i{ }; i < m_cOnscreenText.size( ) - maxRows; ++i )
			m_cOnscreenText[ i ].m_flTimeOnScreen = std::min( m_cOnscreenText[ i ].m_flTimeOnScreen, 0.f );
	}

	float slot{ };
	for ( auto& screenText : m_cOnscreenText ) {
		screenText.m_flTimeOnScreen -= Interfaces::Globals->flFrameTime;
		screenText.m_cFade.Update( screenText.m_flTimeOnScreen > 0.f, Anim::FadeTime * 1.5f );

		const float alpha{ screenText.m_cFade.Alpha( ) };

		// rows glide up into the gap a leaving row opens
		const float targetY{ 8.f + slot };
		screenText.m_flY = screenText.m_flY < 0.f ? targetY : Anim::Approach( screenText.m_flY, targetY );
		slot += rowHeight * alpha;

		if ( alpha <= 0.001f )
			continue;

		const auto textSize = Render::GetTextSize( screenText.m_strText.c_str( ), Fonts::Logs );
		const auto fade{ [ alpha ]( Color c ) { return c.Set<COLOR_A>( c.Get<COLOR_A>( ) * alpha ); } };

		// strip colour by kind of message
		Color strip{ ACCENT };
		if ( screenText.m_strText.rfind( "missed", 0 ) == 0 || screenText.m_strText.find( "did not register" ) != std::string::npos )
			strip = Color( 255, 110, 100 );
		else if ( screenText.m_strText.rfind( "hit ", 0 ) == 0 )
			strip = Color( 130, 220, 140 );

		// slide in from the left while fading in
		const Vector2D pos{ 8.f - screenText.m_cFade.Offset( 20.f ), screenText.m_flY };
		const Vector2D size{ textSize.x + 22.f, 20.f };

		Render::FilledRoundedBox( pos - Vector2D( 1, 1 ), size + Vector2D( 2, 2 ), 5, 5, fade( OUTLINE_DARK ) );
		Render::FilledRoundedBox( pos, size, 5, 5, fade( Color( 17, 17, 20, 235 ) ) );
		Render::RoundedBox( pos, size, 5, 5, fade( OUTLINE_LIGHT ) );
		Render::FilledRoundedBox( pos + Vector2D( 4.f, 5.f ), Vector2D( 3.f, 10.f ), 4, 1, fade( strip ) );
		Render::Text( Fonts::Logs, pos + Vector2D( 13.f, 4.f ), fade( Color( 235, 235, 240 ) ), 0, screenText.m_strText.c_str( ) );
	}
}
