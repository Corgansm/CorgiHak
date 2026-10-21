#pragma once
// CorgiHak shared animation helpers: every panel, popup and indicator moves with the same timing and curves.
//
//   Anim::Approach( value, target )          exponential follow, frame rate independent ( menus, sizes, colours )
//   Anim::Step( progress, visible, seconds ) linear 0..1 progress for a fade of a fixed length
//   Anim::EaseOut / EaseInOut / EaseOutBack  curves applied to a 0..1 progress when drawing
//   Anim::Fade_t                             fade in / out state for one panel, row or indicator
#include "../core/interfaces.h"

#include <algorithm>
#include <cmath>

namespace Anim {
	// standard timings ( seconds )
	constexpr float FadeTime{ 0.18f };      // panels, rows, indicators appearing / leaving
	constexpr float PopupTime{ 0.14f };     // dropdowns, pickers
	constexpr float PageTime{ 0.22f };      // menu page ( tab / subtab ) transition
	constexpr float FollowSpeed{ 14.f };    // Approach( ) rate: ~95% of the way in 0.2 s

	// standard distances ( pixels )
	constexpr float SlideDistance{ 10.f };  // how far things travel while fading in

	// frame time clamped so a hitch never jumps an animation to its end
	inline float Dt( ) {
		const float dt{ Interfaces::Globals ? Interfaces::Globals->flAbsFrameTime : 0.016f };
		return std::clamp( dt, 0.f, 0.1f );
	}

	// fraction of the remaining distance to cover this frame for a given rate ( same feel at 60 or 300 fps )
	inline float Factor( float speed = FollowSpeed ) {
		return 1.f - std::exp( -speed * Dt( ) );
	}

	inline float Approach( float current, float target, float speed = FollowSpeed ) {
		const float next{ current + ( target - current ) * Factor( speed ) };
		return std::abs( target - next ) < 0.001f ? target : next;
	}

	// linear progress toward 1 ( visible ) or 0 ( hidden ) taking `seconds` for the full way
	inline float Step( float progress, bool visible, float seconds = FadeTime ) {
		const float delta{ seconds > 0.f ? Dt( ) / seconds : 1.f };
		return std::clamp( progress + ( visible ? delta : -delta ), 0.f, 1.f );
	}

	inline float EaseOut( float t ) {
		t = std::clamp( t, 0.f, 1.f );
		const float inv{ 1.f - t };
		return 1.f - inv * inv * inv;
	}

	inline float EaseIn( float t ) {
		t = std::clamp( t, 0.f, 1.f );
		return t * t * t;
	}

	inline float EaseInOut( float t ) {
		t = std::clamp( t, 0.f, 1.f );
		return t < 0.5f ? 4.f * t * t * t : 1.f - std::pow( -2.f * t + 2.f, 3.f ) * 0.5f;
	}

	// overshoots slightly before settling ( pop-in )
	inline float EaseOutBack( float t ) {
		t = std::clamp( t, 0.f, 1.f );
		constexpr float c1{ 1.70158f };
		constexpr float c3{ c1 + 1.f };
		return 1.f + c3 * std::pow( t - 1.f, 3.f ) + c1 * std::pow( t - 1.f, 2.f );
	}

	// one fading element: Update( ) every frame, then draw with Alpha( ) / Offset( )
	struct Fade_t {
		float m_flProgress{ };

		void Update( bool visible, float seconds = FadeTime ) {
			m_flProgress = Step( m_flProgress, visible, seconds );
		}

		bool Visible( ) const { return m_flProgress > 0.001f; }
		float Alpha( ) const { return EaseOut( m_flProgress ); }

		// pixels left to travel ( SlideDistance when hidden, 0 when fully in )
		float Offset( float distance = SlideDistance ) const { return ( 1.f - EaseOut( m_flProgress ) ) * distance; }
	};
}
