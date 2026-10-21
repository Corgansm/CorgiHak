#include "../base/features/animations/replay_rules.h"
#include <cassert>
#include <limits>
#include <iostream>

int main() {
	using namespace AnimationReplay;
	const auto near = [](float a, float b) { return std::abs(a-b) < 0.001f; };
	const auto standing = Limits(0, 0, 0, 0, -58, 58);
	assert(near(standing.lower, -58) && near(standing.upper, 58));
	assert(near(Limits(1, 0, 0, 0, -58, 58).upper, 46.4f));
	assert(near(Limits(1, 1, 0, 0, -58, 58).upper, 29));
	assert(near(Limits(0, 0, 1, 1, -58, 58).upper, 29));
	const auto asymmetric = Limits(0, 0, 0, 0, -40, 60);
	assert(near(FeetYaw(0, 1, asymmetric), 40));
	assert(near(FeetYaw(0, 2, asymmetric), -60));
	assert(near(FeetYaw(0, 3, asymmetric), 20));
	assert(near(FeetYaw(0, 4, asymmetric), -30));
	assert(near(FeetYaw(170, 1, asymmetric), -150));
	assert(near(Limits(0, 0, 0, 0, std::numeric_limits<float>::quiet_NaN(), 58).upper, 58));
	assert(near(Limits(0, 0, 0, 0, 0, 0).lower, -58));
	assert(near(Limits(1, 1, 0, 0, 0, 0).upper, 29));
	assert(near(Limits(1, 10, 10, 1, -58, 58).upper, 29));
	assert(near(Limits(1, -10, -10, 1, -58, 58).upper, 46.4f));
	// All four hypotheses must remain distinct, including after a zeroed state.
	const auto reset = Limits(0, 0, 0, 0, 0, 0);
	for (int a = 1; a <= 4; ++a)
		for (int b = a + 1; b <= 4; ++b)
			assert(!near(FeetYaw(0, a, reset), FeetYaw(0, b, reset)));
	float last = 10.f;
	int frame = 100;
	PrepareClock(10, 1.f/64.f, 100, last, frame);
	assert(last < 10 && frame < 100);
	last = 9.f; frame = 98;
	PrepareClock(10, 1.f/64.f, 100, last, frame);
	assert(last == 9.f && frame == 98);
	// Repeated tick replay must clear both guards for every independently replayed side.
	for (int side = 1; side <= 4; ++side) {
		last = 10.f; frame = 100;
		PrepareClock(10, 1.f/64.f, 100, last, frame);
		assert(last < 10.f && frame < 100);
		assert(std::isfinite(FeetYaw(170, side, standing)));
	}
	std::cout << "Animation replay rules passed\n";
}
