#pragma once
#include <algorithm>
#include <cmath>

namespace AnimationReplay {
	struct YawLimits { float lower{}, upper{}; };
	// client.dll RVA 0x443984..0x443A14, verified in CLIENT_BINARY_AUDIT.md.
	inline YawLimits Limits(float walkSpeed, float walkToRun, float duck,
		float duckSpeed, float minimum, float maximum) {
		// garbage or empty limits ( never a valid desync range ): use the engine defaults instead of giving up
		if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum > -1.f || maximum < 1.f
			|| minimum < -90.f || maximum > 90.f) { minimum = -58.f; maximum = 58.f; }
		if (!std::isfinite(walkSpeed)) walkSpeed = 0.f;
		if (!std::isfinite(walkToRun)) walkToRun = 0.f;
		if (!std::isfinite(duck)) duck = 0.f;
		if (!std::isfinite(duckSpeed)) duckSpeed = 0.f;
		// Keep reconstructed inputs in their normalized domains. The fallback above
		// also protects a newly reset state; it does not replace the SDK offset checks.
		walkToRun = std::clamp(walkToRun, 0.f, 1.f);
		duck = std::clamp(duck, 0.f, 1.f);
		float factor = (walkToRun * -0.3f - 0.2f) * std::clamp(walkSpeed, 0.f, 1.f) + 1.f;
		if (duck > 0.f)
			factor += duck * std::clamp(duckSpeed, 0.f, 1.f) * (0.5f - factor);
		return {minimum * factor, maximum * factor};
	}

	inline float FeetYaw(float eyeYaw, int side, YawLimits limits) {
		const float scale = side >= 3 ? 0.5f : 1.f;
		const float delta = (side == 1 || side == 3) ? -limits.lower : -limits.upper;
		return std::remainder(eyeYaw + delta * scale, 360.f);
	}

	inline void PrepareClock(float time, float interval, int frame, float& lastTime, int& lastFrame) {
		// Both checks exist in the binary. Preserve a legitimate earlier update time.
		if (!std::isfinite(lastTime) || lastTime >= time) lastTime = time - interval;
		if (lastFrame >= frame) lastFrame = frame - 1;
	}
}
