#include "visuals.h"

#include <cmath>

// Kill effects: a burst of sparks from the head, a shock ring on the ground, a pop-in label
// ( KILL / HEADSHOT / DOUBLE KILL ... ) that rises and fades, and an optional screen edge flash.

namespace {
	constexpr float Duration{ 1.6f };      // whole effect
	constexpr float BurstTime{ 0.45f };    // sparks
	constexpr float RingTime{ 0.75f };     // ground ring
	constexpr float FlashTime{ 0.35f };    // screen edges
	constexpr float StreakWindow{ 4.f };   // seconds between kills that still count as a streak
	constexpr int Sparks{ 14 };

	const char* StreakLabel( int streak, bool headshot ) {
		switch ( streak ) {
		case 1: return headshot ? "HEADSHOT" : "KILL";
		case 2: return "DOUBLE KILL";
		case 3: return "TRIPLE KILL";
		case 4: return "QUAD KILL";
		default: return "RAMPAGE";
		}
	}

	Color WithAlpha( Color col, float alpha ) {
		return col.Set<COLOR_A>( std::clamp( col.Get<COLOR_A>( ) * alpha, 0.f, 255.f ) );
	}
}

void CKillEffects::Add( CBasePlayer* victim, bool headshot ) {
	if ( !victim )
		return;

	const float now{ Interfaces::Globals->flRealTime };
	m_iStreak = now - m_flLastKill <= StreakWindow ? m_iStreak + 1 : 1;
	m_flLastKill = now;

	if ( Config::Get<int>( Vars.MiscKillSound ) )
		SoundPack::Play( Config::Get<int>( Vars.MiscKillSound ), Config::Get<std::string>( Vars.MiscCustomHitSound ) );

	if ( !Config::Get<bool>( Vars.VisKillEffect ) )
		return;

	KillEffect_t effect{ };
	effect.m_vecFeet = victim->GetAbsOrigin( );
	effect.m_vecHead = effect.m_vecFeet + Vector( 0.f, 0.f, ( victim->m_fFlags( ) & FL_DUCKING ) ? 50.f : 66.f );
	effect.m_flStart = now;
	effect.m_bHeadshot = headshot;
	effect.m_iStreak = m_iStreak;

	if ( const auto info{ Interfaces::Engine->GetPlayerInfo( victim->Index( ) ) }; info.has_value( ) )
		effect.m_strName = info->szName;

	// names can carry printf characters: the text renderer formats its input
	effect.m_strName.erase( std::remove( effect.m_strName.begin( ), effect.m_strName.end( ), '%' ), effect.m_strName.end( ) );

	m_vecEffects.push_back( std::move( effect ) );
	if ( m_vecEffects.size( ) > 6 )
		m_vecEffects.erase( m_vecEffects.begin( ) );
}

void CKillEffects::Draw( ) {
	if ( m_vecEffects.empty( ) )
		return;

	const float now{ Interfaces::Globals->flRealTime };
	m_vecEffects.erase( std::remove_if( m_vecEffects.begin( ), m_vecEffects.end( ),
		[ now ]( const KillEffect_t& effect ) { return now - effect.m_flStart > Duration || now < effect.m_flStart; } ), m_vecEffects.end( ) );

	const auto& base{ Config::Get<Color>( Vars.VisKillEffectCol ) };

	for ( const auto& effect : m_vecEffects ) {
		const float age{ now - effect.m_flStart };
		const Color col{ effect.m_bHeadshot ? Color( base ).Lerp( Color( 255, 80, 70, static_cast< int >( base.Get<COLOR_A>( ) ) ), 0.45f ) : base };

		// shock ring on the ground
		if ( age < RingTime ) {
			const float t{ age / RingTime };
			const float radius{ 8.f + 52.f * Anim::EaseOut( t ) };
			const float alpha{ 1.f - Anim::EaseIn( t ) };
			Render::WorldCircle( effect.m_vecFeet + Vector( 0.f, 0.f, 2.f ), radius, WithAlpha( col, alpha ), WithAlpha( col, alpha * 0.12f ) );
		}

		Vector2D head{ };
		if ( !Math::WorldToScreen( effect.m_vecHead, head ) || head.IsZero( ) )
			continue;

		// spark burst from the head
		if ( age < BurstTime ) {
			const float t{ age / BurstTime };
			const float travel{ Anim::EaseOut( t ) };
			const float alpha{ 1.f - Anim::EaseIn( t ) };
			const float seed{ effect.m_flStart * 97.f };

			for ( int i{ }; i < Sparks; ++i ) {
				const float angle{ ( 360.f / Sparks ) * i + std::fmod( seed * ( i + 1 ), 18.f ) };
				const float rad{ angle * MenuDraw::DEG_TO_RAD };
				const Vector2D dir{ std::cos( rad ), std::sin( rad ) };
				const float reach{ 10.f + ( 34.f + static_cast< float >( i % 3 ) * 8.f ) * travel };
				const float length{ 7.f * ( 1.f - t ) + 2.f };

				const Vector2D from{ head.x + dir.x * reach, head.y + dir.y * reach };
				const Vector2D to{ head.x + dir.x * ( reach + length ), head.y + dir.y * ( reach + length ) };
				MenuDraw::ThickLine( from, to, WithAlpha( col, alpha ), 1.5f );
			}

			// bright core that shrinks away
			MenuDraw::Circle( head, 5.f * ( 1.f - t ), WithAlpha( Color( 255, 255, 255 ), alpha * 0.9f ), 16 );
		}

		// label: pops in ( slight overshoot ), rises, fades at the end
		{
			const float popIn{ Anim::EaseOutBack( std::min( age / 0.18f, 1.f ) ) };
			const float fadeOut{ age > Duration - 0.45f ? 1.f - Anim::EaseIn( ( age - ( Duration - 0.45f ) ) / 0.45f ) : 1.f };
			const float alpha{ std::clamp( popIn, 0.f, 1.f ) * fadeOut };
			const float rise{ 18.f + 22.f * Anim::EaseOut( age / Duration ) };

			const char* label{ StreakLabel( effect.m_iStreak, effect.m_bHeadshot ) };
			const Vector2D labelPos{ head.x, head.y - rise - ( 1.f - std::min( popIn, 1.f ) ) * 6.f };

			Render::Text( Fonts::MenuBold, labelPos + Vector2D( 1.f, 1.f ), Color( 0, 0, 0, static_cast< int >( 160 * alpha ) ), FONT_CENTER, label );
			Render::Text( Fonts::MenuBold, labelPos, WithAlpha( col.Set<COLOR_A>( 255 ), alpha ), FONT_CENTER, label );

			if ( !effect.m_strName.empty( ) )
				Render::Text( Fonts::Logs, labelPos + Vector2D( 0.f, 14.f ), Color( 235, 235, 240, static_cast< int >( 200 * alpha ) ), FONT_CENTER, effect.m_strName.c_str( ) );
		}
	}

	// screen edge flash for the newest kill
	if ( Config::Get<bool>( Vars.VisKillFlash ) && !m_vecEffects.empty( ) ) {
		const auto& newest{ m_vecEffects.back( ) };
		const float age{ now - newest.m_flStart };
		if ( age < FlashTime ) {
			const float alpha{ 1.f - Anim::EaseOut( age / FlashTime ) };
			const Color edge{ WithAlpha( base.Set<COLOR_A>( 90 ), alpha ) };
			const Color clear{ edge.Set<COLOR_A>( 0 ) };

			const int w{ static_cast< int >( ctx.m_ve2ScreenSize.x ) };
			const int h{ static_cast< int >( ctx.m_ve2ScreenSize.y ) };
			const int depth{ std::max( 40, h / 10 ) };

			Render::Gradient( 0, 0, w, depth, edge, clear, false );
			Render::Gradient( 0, h - depth, w, depth, clear, edge, false );
			Render::Gradient( 0, 0, depth, h, edge, clear, true );
			Render::Gradient( w - depth, 0, depth, h, clear, edge, true );
		}
	}
}
