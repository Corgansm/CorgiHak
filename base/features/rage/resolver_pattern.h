#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace ResolverPattern {
	struct Result { bool jitter{}; int phase{}; };

	// Newest sample first. Recognize two compact, repeatedly visited yaw bands.
	// Random defensive angles, a single turn, and near-antipodal ambiguity abstain.
	inline Result Classify(const float* yaw, std::size_t count) {
		if (count < 6 || count > 12) return {};
		std::array<float, 12> offset{};
		float lo{}, hi{};
		for (std::size_t i{}; i < count; ++i) {
			if (!std::isfinite(yaw[i])) return {};
			offset[i] = std::remainder(yaw[i] - yaw[0], 360.f);
			lo = std::min(lo, offset[i]);
			hi = std::max(hi, offset[i]);
		}
		if (hi - lo < 25.f || hi - lo > 160.f) return {};
		for (int iteration{}; iteration < 4; ++iteration) {
			float sums[2]{};
			int sizes[2]{};
			for (std::size_t i{}; i < count; ++i) {
				const int band = std::abs(offset[i] - lo) <= std::abs(offset[i] - hi) ? 0 : 1;
				sums[band] += offset[i];
				++sizes[band];
			}
			if (sizes[0] < 2 || sizes[1] < 2) return {};
			lo = sums[0] / sizes[0];
			hi = sums[1] / sizes[1];
		}
		const float separation = hi - lo;
		if (separation < 25.f) return {};
		const float tolerance = std::min(12.f, separation * 0.2f);
		int changes{}, previous{ -1 }, current{};
		for (std::size_t i{}; i < count; ++i) {
			const int band = std::abs(offset[i] - lo) <= std::abs(offset[i] - hi) ? 0 : 1;
			if (std::abs(offset[i] - (band ? hi : lo)) > tolerance) return {};
			if (previous >= 0 && previous != band) ++changes;
			if (i == 0) current = band;
			previous = band;
		}
		return changes >= 2 ? Result{ true, current ? 1 : -1 } : Result{};
	}
}
