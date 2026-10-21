#pragma once
#include "resolver_evidence.h"

// Shared policy for every player. Identity selects memory, never a special rule.
namespace ResolverRules {
	constexpr int ContextCount = 6;
	inline int Context(bool grounded, bool crouched, float speed) {
		if (!grounded) return crouched ? 5 : 4;
		return (crouched ? 2 : 0) + (std::isfinite(speed) && speed > 5.f ? 1 : 0);
	}

	struct Decision {
		int side{ -1 };
		bool confident{};
		bool conflict{};
	};

	template <std::size_t N>
	inline Decision Decide(const std::array<float, N>& confidence,
		const std::array<bool, N>& available, int animationSide) {
		const int learned = ResolverEvidence::Preferred(confidence, available);
		const bool animation = animationSide >= 0 && animationSide < static_cast<int>(N)
			&& available[animationSide] && std::isfinite(confidence[animationSide]);
		if (animation && learned >= 0 && learned != animationSide)
			return {animationSide, false, true};
		if (animation && confidence[animationSide] > -0.5f) return {animationSide, true, false};
		if (learned >= 0) return {learned, true, false};
		return {};
	}
}
