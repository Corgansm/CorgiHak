#pragma once
#include <algorithm>
#include <array>
#include <cmath>

// Pure evidence rules, independent of game state and event timing.
namespace ResolverEvidence {
	constexpr int SideCount = 3;
	enum class Outcome { Ignored, Hit, Miss };

	template <std::size_t N>
	inline int Preferred(const std::array<float, N>& confidence, const std::array<bool, N>& available) {
		int best{ -1 };
		float runnerUp{ -INFINITY };
		for (std::size_t side{}; side < N; ++side) {
			if (!available[side] || !std::isfinite(confidence[side])) continue;
			if (best < 0 || confidence[side] > confidence[best]) {
				if (best >= 0) runnerUp = confidence[best];
				best = static_cast<int>(side);
			}
			else runnerUp = std::max(runnerUp, confidence[side]);
		}
		return best >= 0 && confidence[best] >= 1.5f && confidence[best] - runnerUp >= 1.f ? best : -1;
	}

	inline int PlaybackSide( const std::array< float, SideCount >& error ) {
		for ( float value : error )
			if ( !std::isfinite( value ) || value < 0.f ) return -1;
		const int best = static_cast< int >( std::min_element( error.begin( ), error.end( ) ) - error.begin( ) );
		float second = INFINITY;
		for ( int side = 0; side < SideCount; ++side )
			if ( side != best ) second = std::min( second, error[ side ] );
		return error[ best ] < 0.001f && second - error[ best ] > 0.0001f ? best : -1;
	}

	template <std::size_t N>
	inline Outcome Learn( std::array< float, N >& confidence, bool reliable,
		bool hit, int chosen, const std::array< bool, N >& intersects,
		const std::array< bool, N >& hitgroupMatches ) {
		if ( !reliable || chosen < 0 || chosen >= static_cast<int>(N) ) return Outcome::Ignored;
		const auto matches = std::count( hitgroupMatches.begin( ), hitgroupMatches.end( ), true );
		if ( hit && matches == 1 ) {
			for ( std::size_t side = 0; side < N; ++side )
				confidence[ side ] = std::clamp( confidence[ side ] * 0.5f + ( hitgroupMatches[ side ] ? 2.f : -1.f ), -3.f, 3.f );
			return Outcome::Hit;
		}
		if ( !hit && intersects[ chosen ]
			&& std::find( intersects.begin( ), intersects.end( ), false ) != intersects.end( ) ) {
			// New contradictory evidence invalidates a previous lock immediately.
			confidence[ chosen ] = std::max( -3.f, std::min(0.f, confidence[ chosen ]) - 1.f );
			return Outcome::Miss;
		}
		return Outcome::Ignored;
	}
}
