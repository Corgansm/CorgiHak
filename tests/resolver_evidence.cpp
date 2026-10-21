#include "../base/features/rage/resolver_evidence.h"
#include "../base/features/rage/resolver_pattern.h"
#include "../base/features/rage/resolver_rules.h"
#include <cassert>
#include <limits>
#include <iostream>

int main() {
	using namespace ResolverEvidence;
	assert( PlaybackSide( { 0.002f, 0.f, 0.003f } ) == 1 );
	assert( PlaybackSide( { 0.f, 0.f, 0.003f } ) == -1 );
	assert( PlaybackSide( { 0.f, 0.00005f, 0.003f } ) == -1 );
	assert( PlaybackSide( { 0.004f, 0.005f, 0.006f } ) == -1 );
	assert( PlaybackSide( { std::numeric_limits<float>::quiet_NaN(), 0.f, 1.f } ) == -1 );
	std::array<float, 3> score{};
	const auto initial = score;
	const std::array<bool, 3> all{ true, true, true }, none{};
	// Defensive/unmatched events and overlapping hits must not teach a side.
	assert( Learn( score, false, true, 1, all, { false, true, false } ) == Outcome::Ignored );
	assert( score == initial );
	assert( Learn( score, true, true, 1, all, all ) == Outcome::Ignored );
	assert( Learn( score, true, true, 1, none, none ) == Outcome::Ignored );
	assert( score == initial );
	// Spread/obstruction misses and failures common to every candidate are ambiguous.
	assert( Learn( score, true, false, 1, none, none ) == Outcome::Ignored );
	assert( Learn( score, true, false, 1, all, none ) == Outcome::Ignored );
	assert( score == initial );
	// A unique server hitgroup match identifies the matching candidate, even if
	// it differs from the side chosen when firing.
	assert( Learn( score, true, true, 1, all, { false, false, true } ) == Outcome::Hit );
	assert( score[2] > score[0] && score[2] > score[1] );
	const float other = score[0];
	assert( Learn( score, true, false, 2, { false, false, true }, none ) == Outcome::Miss );
	assert( score[0] == other );
	// Repeated feedback remains bounded; a later unique hit can reverse preference.
	for ( int i = 0; i < 20; ++i ) Learn( score, true, false, 2, { false, false, true }, none );
	assert( score[2] == -3.f );
	Learn( score, true, true, 0, all, { false, false, true } );
	assert( score[2] > score[0] );
	const auto saved = score;
	assert( Learn( score, true, false, 9, all, none ) == Outcome::Ignored );
	assert( score == saved );
	// Half-delta poses must learn independently from their full-delta parents.
	std::array<float, 5> five{};
	const std::array<bool, 5> allFive{true, true, true, true, true}, noFive{};
	assert(Learn(five, true, true, 1, allFive, {false, false, false, true, false}) == Outcome::Hit);
	assert(five[3] > five[1]);
	assert(Preferred(five, allFive) == 3);
	assert(Preferred(five, std::array<bool, 5>{true, true, true, false, true}) == -1);
	const auto beforeMiss = five;
	assert(Learn(five, true, false, 3, {false, false, false, true, false}, noFive) == Outcome::Miss);
	assert(five[1] == beforeMiss[1] && five[3] < beforeMiss[3]);
	assert(five[3] < 0.f && Preferred(five, allFive) == -1);
	const auto beforeAmbiguous = five;
	assert(Learn(five, true, true, 3, allFive, {false, true, false, true, false}) == Outcome::Ignored);
	assert(Learn(five, true, false, 3, allFive, noFive) == Outcome::Ignored);
	assert(Learn(five, false, true, 3, allFive, {false, false, false, true, false}) == Outcome::Ignored);
	assert(five == beforeAmbiguous);
	assert(Learn(five, true, false, 5, allFive, noFive) == Outcome::Ignored);

	const auto classify = [](std::initializer_list<float> yaws) {
		return ResolverPattern::Classify(yaws.begin(), yaws.size());
	};
	assert(classify({26, -22, 26, -22, 26, -22}).phase == 1);
	assert(classify({-22, 26, -22, 26, -22, 26}).phase == -1);
	assert(classify({26, 26, -22, -22, 26, 26, -22, -22}).jitter);
	// Circular wrap must preserve phase; random angles and a single turn abstain.
	assert(classify({-174, 138, -174, 138, -174, 138}).phase == 1);
	assert(!classify({0, 60, -80, 140, -160, 30}).jitter);
	assert(!classify({26, 26, 26, -22, -22, -22}).jitter);
	assert(!classify({0, 180, 0, 180, 0, 180}).jitter);
	assert(!classify({26, -22, 26, -22}).jitter);
	assert(!classify({0, 0, 0, 0, 0, 0}).jitter);
	assert(!classify({26, -22, 26, -22, 26, std::numeric_limits<float>::quiet_NaN()}).jitter);
	// The same rules must hold over orientations and jitter amplitudes, not a recording's yaw values.
	for (float amplitude : {30.f, 48.f, 75.f, 120.f, 150.f}) {
		for (int rotation = -180; rotation <= 180; rotation += 15) {
			std::array<float, 8> angles{};
			for (std::size_t i{}; i < angles.size(); ++i)
				angles[i] = std::remainder(rotation + (i % 2 ? -amplitude * 0.5f : amplitude * 0.5f), 360.f);
			assert(ResolverPattern::Classify(angles.data(), angles.size()).phase == 1);
		}
	}
	using ResolverRules::Context;
	assert(Context(true, false, 0.f) == 0);
	assert(Context(true, false, 100.f) == 1);
	assert(Context(true, true, 0.f) == 2);
	assert(Context(true, true, 100.f) == 3);
	assert(Context(false, false, 100.f) == 4);
	assert(Context(false, true, 100.f) == 5);
	std::array<float, 5> fresh{};
	assert(!ResolverRules::Decide(fresh, allFive, -1).confident);
	assert(ResolverRules::Decide(fresh, allFive, 1).side == 1);
	assert(!ResolverRules::Decide(fresh, noFive, 1).confident);
	Learn(fresh, true, true, 3, allFive, {false, false, false, true, false});
	assert(ResolverRules::Decide(fresh, allFive, -1).side == 3);
	assert(ResolverRules::Decide(fresh, allFive, 3).confident);
	const auto conflict = ResolverRules::Decide(fresh, allFive, 2);
	assert(conflict.conflict && !conflict.confident && conflict.side == 2);
	Learn(fresh, true, false, 3, {false, false, false, true, false}, noFive);
	assert(!ResolverRules::Decide(fresh, allFive, 3).confident);
	// Independent contexts cannot share a learned side by accident.
	std::array<std::array<float, 5>, ResolverRules::ContextCount> contexts{};
	Learn(contexts[Context(true, false, 0.f)], true, true, 1, allFive, {false, true, false, false, false});
	assert(ResolverRules::Decide(contexts[Context(true, false, 0.f)], allFive, -1).confident);
	assert(!ResolverRules::Decide(contexts[Context(true, false, 100.f)], allFive, -1).confident);
	std::cout << "Resolver evidence, pattern, and general policy regression cases passed\n";
}
