#pragma once
#include "../../utils/render.h"
#include <algorithm>
#include <cmath>
#include <vector>

// CorgiHak menu drawing helpers + vector icons.
// Everything here is drawn with surface primitives, so no icon font has to be installed.
namespace MenuDraw {
	constexpr float DEG_TO_RAD{ 3.14159265358979f / 180.f };

	// Render::Line / FilledRoundedBox / Text already apply the menu fade, raw surface calls don't
	inline Color Faded( Color col ) {
		if ( Render::GlobalAlpha < 1.f )
			col = col.Set<COLOR_A>( col.Get<COLOR_A>( ) * Render::GlobalAlpha );
		return col;
	}

	// point on a circle where 0 degrees is straight up and angles grow clockwise ( same winding as Render::FilledRoundedBox )
	inline Vector2D OnCircle( const Vector2D& center, float radius, float degrees ) {
		const float rad{ degrees * DEG_TO_RAD };
		return { center.x + radius * std::sin( rad ), center.y - radius * std::cos( rad ) };
	}

	// filled convex polygon, any winding
	inline void ConvexPolygon( std::vector< Vector2D > points, Color col ) {
		if ( points.size( ) < 3 )
			return;

		float area{ };
		for ( size_t i{ }; i < points.size( ); ++i ) {
			const auto& a{ points[ i ] };
			const auto& b{ points[ ( i + 1 ) % points.size( ) ] };
			area += a.x * b.y - b.x * a.y;
		}

		if ( area < 0.f )
			std::reverse( points.begin( ), points.end( ) );

		std::vector< Vertex_t > vertices{ };
		vertices.reserve( points.size( ) );
		for ( const auto& point : points )
			vertices.emplace_back( Vertex_t( point ) );

		static int texture{ Interfaces::Surface->CreateNewTextureID( true ) };
		unsigned char buffer[ 4 ]{ 255, 255, 255, 255 };

		Interfaces::Surface->DrawSetTextureRGBA( texture, buffer, 1, 1 );
		Interfaces::Surface->DrawSetColor( Faded( col ) );
		Interfaces::Surface->DrawSetTexture( texture );
		Interfaces::Surface->DrawTexturedPolygon( static_cast< int >( vertices.size( ) ), vertices.data( ), true );
	}

	inline void Circle( const Vector2D& center, float radius, Color col, int segments = 24 ) {
		std::vector< Vector2D > points{ };
		points.reserve( segments );
		for ( int i{ }; i < segments; ++i )
			points.push_back( OnCircle( center, radius, 360.f * i / segments ) );

		ConvexPolygon( points, col );
	}

	// line with thickness, drawn as parallel 1px lines
	inline void ThickLine( const Vector2D& a, const Vector2D& b, Color col, float thickness = 1.5f ) {
		const Vector2D delta{ b - a };
		const float length{ std::sqrt( delta.x * delta.x + delta.y * delta.y ) };
		if ( length <= 0.f )
			return;

		const float nx{ -delta.y / length };
		const float ny{ delta.x / length };
		for ( float t{ -thickness * 0.5f }; t <= thickness * 0.5f + 0.01f; t += 0.5f ) {
			const Vector2D offset{ nx * t, ny * t };
			Render::Line( a + offset, b + offset, col );
		}
	}

	// arc outline from -> to ( degrees, 0 = up, clockwise )
	inline void Arc( const Vector2D& center, float radius, Color col, float thickness = 1.5f, float from = 0.f, float to = 360.f, int segments = 32 ) {
		for ( float r{ radius - thickness * 0.5f }; r <= radius + thickness * 0.5f + 0.01f; r += 0.5f ) {
			Vector2D previous{ OnCircle( center, r, from ) };
			for ( int i{ 1 }; i <= segments; ++i ) {
				const Vector2D next{ OnCircle( center, r, from + ( to - from ) * i / segments ) };
				Render::Line( previous, next, col );
				previous = next;
			}
		}
	}

	// soft halo around a control ( drawn before it, the control covers the middle ); amount 0..1
	inline void Glow( const Vector2D& pos, const Vector2D& size, float amount, Color col, int radius = 4 ) {
		if ( amount <= 0.01f )
			return;

		constexpr float strength[ 3 ]{ 14.f, 30.f, 62.f };
		for ( int ring{ 3 }; ring >= 1; --ring ) {
			const float grow{ static_cast< float >( ring + 1 ) };
			Render::FilledRoundedBox( pos - Vector2D( grow, grow ), size + Vector2D( grow * 2.f, grow * 2.f ), 5, radius + ring + 1,
				col.Set<COLOR_A>( strength[ 3 - ring ] * amount ) );
		}
	}

	inline void Chevron( const Vector2D& center, bool open, Color col ) {
		const float dir{ open ? -1.f : 1.f };
		ThickLine( center + Vector2D( -3.5f, -1.75f * dir ), center + Vector2D( 0.f, 1.75f * dir ), col, 1.5f );
		ThickLine( center + Vector2D( 0.f, 1.75f * dir ), center + Vector2D( 3.5f, -1.75f * dir ), col, 1.5f );
	}

	inline void CheckMark( const Vector2D& topLeft, float size, Color col ) {
		ThickLine( topLeft + Vector2D( size * 0.22f, size * 0.52f ), topLeft + Vector2D( size * 0.43f, size * 0.74f ), col, 1.5f );
		ThickLine( topLeft + Vector2D( size * 0.43f, size * 0.74f ), topLeft + Vector2D( size * 0.80f, size * 0.28f ), col, 1.5f );
	}

	/* icons: center + size ( roughly the icon's width ) */

	// rage: crosshair
	inline void IconRage( const Vector2D& c, float s, Color col ) {
		Arc( c, s * 0.34f, col, 1.6f );
		ThickLine( c + Vector2D( 0.f, -s * 0.52f ), c + Vector2D( 0.f, -s * 0.18f ), col, 1.6f );
		ThickLine( c + Vector2D( 0.f, s * 0.18f ), c + Vector2D( 0.f, s * 0.52f ), col, 1.6f );
		ThickLine( c + Vector2D( -s * 0.52f, 0.f ), c + Vector2D( -s * 0.18f, 0.f ), col, 1.6f );
		ThickLine( c + Vector2D( s * 0.18f, 0.f ), c + Vector2D( s * 0.52f, 0.f ), col, 1.6f );
		Circle( c, 1.6f, col, 12 );
	}

	// players: head and shoulders
	inline void IconPlayers( const Vector2D& c, float s, Color col ) {
		Circle( c + Vector2D( 0.f, -s * 0.2f ), s * 0.2f, col, 20 );

		const Vector2D shoulders{ c.x, c.y + s * 0.46f };
		std::vector< Vector2D > body{ };
		for ( int i{ }; i <= 16; ++i )
			body.push_back( OnCircle( shoulders, s * 0.38f, -90.f + 180.f * i / 16 ) );

		ConvexPolygon( body, col );
	}

	// visuals: eye
	inline void IconVisuals( const Vector2D& c, float s, Color col ) {
		const float lidRadius{ s * 0.62f };
		const float lidOffset{ s * 0.36f };
		Arc( c + Vector2D( 0.f, lidOffset ), lidRadius, col, 1.6f, -52.f, 52.f, 20 );
		Arc( c - Vector2D( 0.f, lidOffset ), lidRadius, col, 1.6f, 128.f, 232.f, 20 );
		Arc( c, s * 0.2f, col, 1.4f, 0.f, 360.f, 20 );
		Circle( c, s * 0.1f, col, 12 );
	}

	// misc: gear
	inline void IconMisc( const Vector2D& c, float s, Color col ) {
		const float inner{ s * 0.24f };
		const float outer{ s * 0.46f };
		const float halfWidth{ 6.5f };

		for ( int i{ }; i < 8; ++i ) {
			const float angle{ 45.f * i };
			ConvexPolygon( {
				OnCircle( c, inner, angle - halfWidth * 1.6f ),
				OnCircle( c, outer, angle - halfWidth ),
				OnCircle( c, outer, angle + halfWidth ),
				OnCircle( c, inner, angle + halfWidth * 1.6f ) }, col );
		}

		Arc( c, s * 0.27f, col, 2.2f, 0.f, 360.f, 24 );
		Circle( c, s * 0.08f, col, 10 );
	}

	// config: folder
	inline void IconConfig( const Vector2D& c, float s, Color col ) {
		const Vector2D topLeft{ c.x - s * 0.46f, c.y - s * 0.3f };
		const Vector2D bodySize{ s * 0.92f, s * 0.64f };

		ConvexPolygon( {
			topLeft,
			topLeft + Vector2D( s * 0.36f, 0.f ),
			topLeft + Vector2D( s * 0.46f, s * 0.12f ),
			topLeft + Vector2D( 0.f, s * 0.12f ) }, col );

		Render::RoundedBox( topLeft + Vector2D( 0.f, s * 0.1f ), bodySize, 4, 2, col );
		Render::RoundedBox( topLeft + Vector2D( 0.5f, s * 0.1f + 0.5f ), bodySize - Vector2D( 1.f, 1.f ), 4, 2, col );
		ThickLine( topLeft + Vector2D( 2.f, s * 0.28f ), topLeft + Vector2D( bodySize.x - 2.f, s * 0.28f ), col, 1.f );
	}

	inline void TabIcon( int tab, const Vector2D& c, float s, Color col ) {
		switch ( tab ) {
		case 0: IconRage( c, s, col ); break;
		case 1: IconPlayers( c, s, col ); break;
		case 2: IconVisuals( c, s, col ); break;
		case 3: IconMisc( c, s, col ); break;
		default: IconConfig( c, s, col ); break;
		}
	}

	// CorgiHak logo: corgi head
	inline void Logo( const Vector2D& c, float s, Color fur, Color cream, Color dark ) {
		// ears
		ConvexPolygon( { c + Vector2D( -s * 0.40f, -s * 0.02f ), c + Vector2D( -s * 0.33f, -s * 0.50f ), c + Vector2D( -s * 0.06f, -s * 0.20f ) }, fur );
		ConvexPolygon( { c + Vector2D( s * 0.40f, -s * 0.02f ), c + Vector2D( s * 0.33f, -s * 0.50f ), c + Vector2D( s * 0.06f, -s * 0.20f ) }, fur );
		ConvexPolygon( { c + Vector2D( -s * 0.31f, -s * 0.10f ), c + Vector2D( -s * 0.29f, -s * 0.36f ), c + Vector2D( -s * 0.14f, -s * 0.20f ) }, cream );
		ConvexPolygon( { c + Vector2D( s * 0.31f, -s * 0.10f ), c + Vector2D( s * 0.29f, -s * 0.36f ), c + Vector2D( s * 0.14f, -s * 0.20f ) }, cream );

		// head
		Circle( c + Vector2D( 0.f, s * 0.06f ), s * 0.31f, fur, 28 );

		// white blaze + muzzle
		ConvexPolygon( {
			c + Vector2D( -s * 0.04f, -s * 0.22f ),
			c + Vector2D( s * 0.04f, -s * 0.22f ),
			c + Vector2D( s * 0.09f, s * 0.08f ),
			c + Vector2D( -s * 0.09f, s * 0.08f ) }, cream );
		Circle( c + Vector2D( 0.f, s * 0.19f ), s * 0.16f, cream, 20 );

		// eyes + nose
		Circle( c + Vector2D( -s * 0.13f, -s * 0.01f ), s * 0.045f, dark, 10 );
		Circle( c + Vector2D( s * 0.13f, -s * 0.01f ), s * 0.045f, dark, 10 );
		Circle( c + Vector2D( 0.f, s * 0.13f ), s * 0.055f, dark, 10 );
	}
}
