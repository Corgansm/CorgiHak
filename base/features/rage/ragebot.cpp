#include "ragebot.h"
#include "../../utils/crash_log.h"
#include "../../core/lua/lua_engine.h"
#include "resolver_evidence.h"
#include "resolver_pattern.h"
#include "resolver_rules.h"
#include "../animations/replay_rules.h"
#include "lc_prediction.h"
#include "net_simtime.h"
#include "exploits.h"
#include "../../utils/ray_tracer.h"
#include "../../core/prop_manager.h"
#include "../visuals/visuals.h"

#define THIS Features::Ragebot


// defensive detection thresholds come from the menu ( Rage > General > Defensive detection )
namespace DefTune {
	inline bool Enabled( ) { return Config::Get<bool>( Vars.RagebotDefDetect ); }
	inline int MinShift( ) { return std::clamp( Config::Get<int>( Vars.RagebotDefMinShift ), 1, 16 ); }
	inline int MaxShift( ) { return std::clamp( Config::Get<int>( Vars.RagebotDefMaxShift ), MinShift( ), 32 ); }
	inline int ConfirmShifts( ) { return std::clamp( Config::Get<int>( Vars.RagebotDefConfirmShifts ), 1, 10 ); }
	inline int ConfirmWindow( ) { return TIME_TO_TICKS( static_cast< float >( std::clamp( Config::Get<int>( Vars.RagebotDefConfirmTime ), 1, 10 ) ) ); }
	inline bool NeedAngles( ) { return Config::Get<bool>( Vars.RagebotDefNeedAngles ); }
	inline bool AngleConfirms( ) { return Config::Get<bool>( Vars.RagebotDefAngleConfirm ); }
	inline bool PushConfirms( ) { return Config::Get<bool>( Vars.RagebotDefPushConfirm ); }
	inline float PitchJump( ) { return static_cast< float >( Config::Get<int>( Vars.RagebotDefPitchJump ) ); }
	inline float YawJump( ) { return static_cast< float >( Config::Get<int>( Vars.RagebotDefYawJump ) ); }
	inline float NetTolerance( ) { return std::clamp( Config::Get<int>( Vars.RagebotDefNetTolerance ), 0, 100 ) / 100.f; }
	inline bool FilterSimultaneous( ) { return Config::Get<bool>( Vars.RagebotDefFilterSimultaneous ); }
}

// fake flick filter ( Rage > General )
namespace FlickTune {
	constexpr std::size_t CompareRecords{ 6 };   // recent clean records a candidate is compared against
	constexpr std::size_t MinHistory{ 3 };       // fewer than this: nothing to compare with yet
	inline bool HoldNewYaw( ) { return Config::Get<bool>( Vars.RagebotFlickHoldNew ); }
	inline float Jump( ) { return static_cast< float >( std::clamp( Config::Get<int>( Vars.RagebotFlickJump ), 20, 180 ) ); }
	inline float Agree( ) { return static_cast< float >( std::clamp( Config::Get<int>( Vars.RagebotFlickAgree ), 5, 90 ) ); }
}

namespace {
	int RecordContext(const LagRecord_t& record) {
		return ResolverRules::Context((record.m_cAnimData.m_iFlags & FL_ONGROUND) != 0,
			record.m_cAnimData.m_flDuckAmount > 0.5f, record.m_cAnimData.m_vecVelocity.Length2D());
	}
	FORCEINLINE float Normalize180(float yaw) {
		yaw = std::remainderf(yaw, 360.f);
		if (yaw > 180.f)
			yaw -= 360.f;
		else if (yaw < -180.f)
			yaw += 360.f;
		return yaw;
	}

	FORCEINLINE int SwapSide(int side) {
		switch (side) {
		case RESOLVER_SIDE_FIRST: return RESOLVER_SIDE_SECOND;
		case RESOLVER_SIDE_SECOND: return RESOLVER_SIDE_FIRST;
		case RESOLVER_SIDE_LOW_FIRST: return RESOLVER_SIDE_LOW_SECOND;
		case RESOLVER_SIDE_LOW_SECOND: return RESOLVER_SIDE_LOW_FIRST;
		default: return RESOLVER_SIDE_CENTER;
		}
	}

	// full side a low delta side belongs to ( learning and safe points work on the 3 full sides )
	FORCEINLINE int ParentSide(int side) {
		if (side == RESOLVER_SIDE_LOW_FIRST)
			return RESOLVER_SIDE_FIRST;
		if (side == RESOLVER_SIDE_LOW_SECOND)
			return RESOLVER_SIDE_SECOND;
		return std::clamp(side, 0, DESYNC_MATRIX_COUNT - 1);
	}

	FORCEINLINE int LowSideOf(int side) {
		if (side == RESOLVER_SIDE_FIRST)
			return RESOLVER_SIDE_LOW_FIRST;
		if (side == RESOLVER_SIDE_SECOND)
			return RESOLVER_SIDE_LOW_SECOND;
		return side;
	}

	FORCEINLINE bool IsDesyncSide(int side) {
		return side > RESOLVER_SIDE_CENTER && side < RESOLVER_SIDE_COUNT;
	}

	FORCEINLINE bool IsJitterPattern(EAimPattern pattern) {
		return pattern == EAimPattern::Jitter || pattern == EAimPattern::JitterPositive || pattern == EAimPattern::JitterNegative;
	}

	FORCEINLINE bool HasEvidence(const LagRecord_t& record) {
		return record.m_bMultiMatrix && record.m_iResolverSide > RESOLVER_SIDE_CENTER
			&& record.m_iResolverSide < DESYNC_MATRIX_COUNT;
	}

	template < typename T >
	FORCEINLINE void PushCapped(std::deque< T >& dq, const T& value, size_t cap) {
		dq.push_back(value);
		while (dq.size() > cap)
			dq.pop_front();
	}

	std::string PlayerName(CBasePlayer* player) {
		if (!player)
			return _("unknown");

		const auto info{ Interfaces::Engine->GetPlayerInfo(player->Index()) };
		return info.has_value() ? std::string(info->szName) : std::string(_("unknown"));
	}

	// rotates a whole skeleton around the player's origin by deltaYaw degrees
	void RotateBones(const matrix3x4_t* in, matrix3x4_t* out, int count, const Vector& origin, float deltaYaw) {
		matrix3x4_t rotation{};
		Math::AngleMatrix(QAngle{ 0.f, deltaYaw, 0.f }, rotation, Vector{ 0.f, 0.f, 0.f });

		for (int i{}; i < count; ++i) {
			matrix3x4_t local{ in[i] };
			local[0][3] -= origin.x;
			local[1][3] -= origin.y;
			local[2][3] -= origin.z;

			out[i] = rotation.ConcatTransforms(local);
			out[i][0][3] += origin.x;
			out[i][1][3] += origin.y;
			out[i][2][3] += origin.z;
		}
	}

	QAngle MatrixGetAngles(const matrix3x4_t& mat) {
		constexpr auto ONE_RADIAN_IN_DEGREES = 180.f / static_cast<float>(M_PI);
		auto rads2degs = [](float rads) { return rads * ONE_RADIAN_IN_DEGREES; };

		const Vector fwd{ mat[0][0], mat[1][0], mat[2][0] };
		const Vector left{ mat[0][1], mat[1][1], mat[2][1] };

		const float xyDist = fwd.Length2D();
		if (xyDist > 0.001f)
			return QAngle{ rads2degs(std::atan2(-fwd.z, xyDist)), rads2degs(std::atan2(fwd.y, fwd.x)), rads2degs(std::atan2(left.z, mat[2][2])) };

		return QAngle{ rads2degs(std::atan2(-fwd.z, xyDist)), rads2degs(std::atan2(-left.x, left.y)), 0.f };
	}

	int OffsetDelta(CBasePlayer* player, LagRecord_t* record) {
		if (!player || !record || !ctx.m_pLocal || !ctx.m_pLocal->m_pStudioHdr())
			return 90;

		auto eyePos{ record->m_cAnimData.m_vecOrigin };
		eyePos.z += 64.f - 18.f * std::clamp(record->m_cAnimData.m_flDuckAmount, 0.f, 1.f);

		const auto hitboxSet{ ctx.m_pLocal->m_pStudioHdr()->pStudioHdr->GetHitboxSet(ctx.m_pLocal->m_nHitboxSet()) };
		if (!hitboxSet)
			return 90;

		const auto hitbox{ hitboxSet->GetHitbox(HITBOX_CHEST) };
		if (!hitbox)
			return 90;

		const auto point{ Math::VectorTransform((hitbox->vecBBMin + hitbox->vecBBMax) / 2.f, ctx.m_pLocal->m_CachedBoneData().Base()[hitbox->iBone]) };

		QAngle shootAngle{};
		Math::VectorAngles(point - eyePos, shootAngle);

		auto delta{ Math::AngleDiff(shootAngle.y, record->m_angEyeAngles.y) };

		for (; delta > 90.f; delta -= 180.f);
		for (; delta < -90.f; delta += 180.f);

		return static_cast<int>(std::abs(delta));
	}

	int ClipRayToHitbox(const Ray_t& ray, mstudiobbox_t* box, matrix3x4_t& mat, CGameTrace& tr) {
		int retval = -1;

		__asm {
			mov ecx, ray
			mov edx, box
			push tr
			push mat
			call Displacement::Sigs.ClipRayToHitbox
			mov retval, eax
			add esp, 8
		}

		return retval;
	}

	bool IntersectHitbox(Vector end, mstudiobbox_t* hitbox, matrix3x4_t* matrix) {
		matrix3x4_t transform{};
		transform.SetAngles(hitbox->angOffsetOrientation.y, hitbox->angOffsetOrientation.x, hitbox->angOffsetOrientation.z);
		auto hitboxMatrix{ matrix[hitbox->iBone] * transform };

		Trace_t tr{};
		tr.flFraction = 1.f;
		tr.bStartSolid = false;

		return ClipRayToHitbox({ ctx.m_vecEyePos, end }, hitbox, hitboxMatrix, tr) > -1;
	}

	// same test against one bone moved by offset ( where a lag compensation breaker could also be )
	bool IntersectHitboxMoved(Vector end, mstudiobbox_t* hitbox, const matrix3x4_t& bone, const Vector& offset) {
		matrix3x4_t moved{ bone };
		moved[0][3] += offset.x;
		moved[1][3] += offset.y;
		moved[2][3] += offset.z;

		matrix3x4_t transform{};
		transform.SetAngles(hitbox->angOffsetOrientation.y, hitbox->angOffsetOrientation.x, hitbox->angOffsetOrientation.z);
		auto hitboxMatrix{ moved * transform };

		Trace_t tr{};
		tr.flFraction = 1.f;
		tr.bStartSolid = false;

		return ClipRayToHitbox({ ctx.m_vecEyePos, end }, hitbox, hitboxMatrix, tr) > -1;
	}
}

// Momentum Resolver (v5.2+): pattern & delayed-jitter detection, unified jitter/move memory,
// strict layer-6 playback rate resim, standing balance-adjust & LBY foot-yaw matching,
// strict 3-matrix brute-force elimination, near-miss ray detection, and evidence-weighted record selection.

namespace {
	// Confidence is attached to absolute matrix sides. Opposite jitter phases must
	// retain separate evidence; sharing their votes makes alternating sides cancel.
	FORCEINLINE EAimPattern CanonicalBucketPattern(EAimPattern pattern) {
		if (pattern == EAimPattern::Anim)
			return EAimPattern::Move;
		return pattern;
	}

	// Standing desync side detection using Layer 3 (ACT_CSGO_IDLE_TURN_BALANCEADJUST, seq 979)
	// and comparison of simulated side flAbsYaw against networked m_flLowerBodyYawTarget.
	int StandingBalanceOrLbySide(CBasePlayer* player, const LagRecord_t& record, float lbyYaw) {
		if (!record.m_bMultiMatrix)
			return -1;

		const auto& data{ record.m_cAnimData };
		if (!(data.m_iFlags & FL_ONGROUND) || data.m_vecVelocity.Length2D() > 5.f)
			return -1;

		// 1. Layer 3 balance adjust (sequence 979) comparison against simulated sides
		const auto& netL3{ data.m_pLayers[3] };
		if (player && player->GetSequenceActivity(netL3.nSequence) == ACT_CSGO_IDLE_TURN_BALANCEADJUST
			&& netL3.flWeight > 0.01f && netL3.flCycle < 0.95f) {
			const auto& s1{ data.m_arrSides[RESOLVER_SIDE_FIRST] };
			const auto& s2{ data.m_arrSides[RESOLVER_SIDE_SECOND] };
			if (s1.m_bFilled && s2.m_bFilled
				&& s1.m_pLayers[3].nSequence == netL3.nSequence
				&& s2.m_pLayers[3].nSequence == netL3.nSequence) {
				const float err1{ std::abs(s1.m_pLayers[3].flWeight - netL3.flWeight)
					+ std::abs(s1.m_pLayers[3].flPlaybackRate - netL3.flPlaybackRate) };
				const float err2{ std::abs(s2.m_pLayers[3].flWeight - netL3.flWeight)
					+ std::abs(s2.m_pLayers[3].flPlaybackRate - netL3.flPlaybackRate) };
				if (std::abs(err1 - err2) > 0.02f)
					return err1 < err2 ? RESOLVER_SIDE_FIRST : RESOLVER_SIDE_SECOND;
			}
		}

		// 2. Compare networked LBY against each simulated side's flAbsYaw when LBY is pulled off eye yaw
		if (!player)
			return -1;

		const float eyeLbyDelta{ Normalize180(record.m_angEyeAngles.y - lbyYaw) };
		if (std::abs(eyeLbyDelta) >= 25.f) {
			const auto& s1{ data.m_arrSides[RESOLVER_SIDE_FIRST] };
			const auto& s2{ data.m_arrSides[RESOLVER_SIDE_SECOND] };
			if (s1.m_bFilled && s2.m_bFilled) {
				const float dist1{ std::abs(Normalize180(lbyYaw - s1.m_cAnimState.flAbsYaw)) };
				const float dist2{ std::abs(Normalize180(lbyYaw - s2.m_cAnimState.flAbsYaw)) };
				if (std::abs(dist1 - dist2) > 10.f)
					return dist1 < dist2 ? RESOLVER_SIDE_FIRST : RESOLVER_SIDE_SECOND;
			}
			return eyeLbyDelta > 0.f ? RESOLVER_SIDE_SECOND : RESOLVER_SIDE_FIRST;
		}

		return -1;
	}

	// Score backtrack record quality by resolver evidence so GetBestLagRecord prefers high-confidence ticks
	int RecordResolverPriority(int index, const LagRecord_t& record) {
		int priority{ 0 };
		const auto& st{ THIS.m_cBrain.Get(index) };

		// On-shot record: the enemy fired inside this tick, so its eye angles are their real ones
		if (THIS.m_cBrain.IsOnShotRecord(index, record))
			priority += 5;
		// Not pitched down: head is exposed
		else if (std::abs(Normalize180(record.m_angEyeAngles.x)) < 45.f)
			priority += 2;

		// Moving grounded record with a decisive Layer-6 playback rate match
		const auto pattern{ THIS.m_cBrain.PatternFor(index, record) };
		if (pattern == EAimPattern::Anim)
			priority += 2;
		else if ((record.m_cAnimData.m_iFlags & FL_ONGROUND)
			&& record.m_cAnimData.m_vecVelocity.Length2D() > RageTuning::MoveSpeed)
			priority += 1;

		if (HasEvidence(record))
			priority += 1;

		return priority;
	}
}

void CResolverBrain::OnLevelChange() {
	for (auto& st : m_arrPlayers) {
		st.ResetRuntime();
		st.m_pPlayer = nullptr;
		st.m_strKey.clear();
		st.m_flSpawnTime = 0.f;
	}
	m_arrLastShiftTick.fill(0);
}

void ResolverPlayer_t::ResetRuntime() {
	m_pLastWeapon = nullptr;
	m_flLastShotTime = 0.f;
	m_flMaxSimTime = 0.f;
	m_flLastSimTime = 0.f;
	m_flRecordDt = 0.f;
	m_flYawMean = 0.f;
	m_flMaxDesync = 58.f;

	m_vecLastOrigin = Vector{ 0.f, 0.f, 0.f };
	m_bHasLastOrigin = false;

	m_iChokeTicks = 1;
	m_iLastShotTick = -100000;
	m_iDefensiveStart = 0;
	m_iDefensiveUntil = -1;
	m_iDefensiveShift = 0;
	m_iDoubletapUntil = -1;

	m_iDetectedSide = RESOLVER_SIDE_FIRST;
	m_iAnimSide = -1;
	m_iPredictedJitterSide = RESOLVER_SIDE_FIRST;
	m_iLastFiredPhase = -1;

	m_bDefensive = false;
	m_bInShift = false;
	m_bDefensiveUser = false;
	m_bBelowMax = false;
	m_dqShiftTicks.clear();
	m_iLastCleanTick = -100000;
	m_flCleanPitch = 0.f;
	m_flCleanYaw = 0.f;
	m_bHasCleanAngles = false;
	m_bAngleEvidence = false;
	m_iDefensiveEvents = 0;
	m_iDefPitchUp = m_iDefPitchZero = m_iDefPitchDown = 0;
	m_iDefYawSamples = 0;
	m_flDefYawOffset = 0.f;
	m_flShiftInterval = 0.f;
	m_iLastShiftStart = -100000;
	m_dqFlickYaws.clear();
	m_flFlickAnalyzedSimTime = 0.f;
	m_bSuspectFlick = false;
	m_dqFlickRecords.clear();
	m_dqFlickTicks.clear();
	m_bFlicker = false;
	m_iMissStreak = 0;
	m_bDoubletapShift = false;
	m_bLagCompBroken = false;

	m_bJitter = false;
	m_bNoAA = false;
	m_bAAHold = false;

	m_ePattern = EAimPattern::Static;

	m_dqYawHistory.clear();
	m_dqPitchHistory.clear();
	m_dqLbyHistory.clear();
	m_dqDefensiveSimTimes.clear();
}

const char* CResolverBrain::PatternName(EAimPattern pattern) {
	switch (pattern) {
	case EAimPattern::Static: return "static";
	case EAimPattern::Jitter: return "jitter";
	case EAimPattern::JitterPositive: return "jitter-positive";
	case EAimPattern::JitterNegative: return "jitter-negative";
	case EAimPattern::Move: return "move";
	case EAimPattern::NoAA: return "no-aa";
	case EAimPattern::Anim: return "anim";
	case EAimPattern::Air: return "air";
	case EAimPattern::Crouch: return "crouch";
	default: return "?";
	}
}

const char* CResolverBrain::PhaseName(EResolverPhase phase) {
	switch (phase) {
	case EResolverPhase::Detected: return "detected";
	case EResolverPhase::Inverted: return "inverted";
	case EResolverPhase::Native: return "native";
	case EResolverPhase::HalfDetected: return "half";
	case EResolverPhase::HalfInverted: return "half-inverted";
	default: return "?";
	}
}

// Strict 3-matrix brute-force cycle (Detected -> Inverted -> Native) so no shots are wasted on duplicate matrices
EResolverPhase CResolverBrain::NextPhase(EAimPattern pattern, EResolverPhase phase) {
	// static / moving / crouch / air: full sides, then low delta, then center
	static constexpr std::array< EResolverPhase, 5 > kOrderAA{
		EResolverPhase::Detected,
		EResolverPhase::Inverted,
		EResolverPhase::HalfDetected,
		EResolverPhase::HalfInverted,
		EResolverPhase::Native
	};
	// jitter swaps full sides every few ticks: low delta / center rarely apply
	static constexpr std::array< EResolverPhase, 3 > kOrderJitter{
		EResolverPhase::Detected,
		EResolverPhase::Inverted,
		EResolverPhase::Native
	};
	static constexpr std::array< EResolverPhase, 3 > kOrderNoAA{
		EResolverPhase::Native,
		EResolverPhase::Detected,
		EResolverPhase::Inverted
	};

	const auto next = [phase](const auto& order) {
		for (size_t i{}; i < order.size(); ++i) {
			if (order[i] == phase)
				return order[(i + 1) % order.size()];
		}
		return order[0];
	};

	if (pattern == EAimPattern::NoAA)
		return next(kOrderNoAA);
	if (IsJitterPattern(pattern))
		return next(kOrderJitter);
	return next(kOrderAA);
}

float CResolverBrain::ReadLastShotTime(CWeaponCSBase* weapon) {
	static const int offset{ PropManager::Get().GetOffset(_("DT_WeaponCSBase"), _("m_fLastShotTime")) };
	if (!weapon || offset <= 0)
		return 0.f;

	return *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(weapon) + offset);
}

float CResolverBrain::ReadLowerBodyYaw(CBasePlayer* player) {
	static const int offset{ PropManager::Get().GetOffset(_("DT_CSPlayer"), _("m_flLowerBodyYawTarget")) };
	if (!player || offset <= 0)
		return 0.f;

	return *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(player) + offset);
}

// Use the recorded animation state and the limit formula verified in client.dll.
float CResolverBrain::ComputeMaxDesync(const LagRecord_t& record) {
	const auto& center = record.m_cAnimData.m_arrSides[0];
	if (!center.m_bFilled) return 0.f;
	const auto& state = center.m_cAnimState;
	const auto limits = AnimationReplay::Limits(state.flSpeedAsPortionOfWalkTopSpeed,
		state.flWalkToRunTransition, state.flDuckAmount, state.flDuckingSpeed,
		state.flMinBodyYaw, state.flMaxBodyYaw);
	return std::max(std::abs(limits.lower), std::abs(limits.upper));
}

ResolverBucket_t& CResolverBrain::Bucket(const std::string& key, EAimPattern pattern, int context) {
	const EAimPattern canonical{ CanonicalBucketPattern(pattern) };
	auto& memory{ m_mapMemory[key] };
	auto& bucket{ memory.m_arrBuckets.at(static_cast<size_t>(canonical) * ResolverRules::ContextCount
		+ std::clamp(context, 0, ResolverRules::ContextCount - 1)) };
	if (!bucket.m_bPhaseInitialized) {
		bucket.m_ePhase = canonical == EAimPattern::NoAA ? EResolverPhase::Native : EResolverPhase::Detected;
		bucket.m_bPhaseInitialized = true;
	}

	DecayBucket(bucket);
	if (!bucket.m_bPhaseInitialized) {
		bucket.m_ePhase = canonical == EAimPattern::NoAA ? EResolverPhase::Native : EResolverPhase::Detected;
		bucket.m_bPhaseInitialized = true;
	}
	return bucket;
}

// learning that has not been confirmed for a while fades, so one old hit cannot lock a side forever
void CResolverBrain::DecayBucket(ResolverBucket_t& bucket) const {
	const int tick{ Interfaces::Globals->iTickCount };
	if (bucket.m_iEvidenceTick < 0 || tick - bucket.m_iEvidenceTick <= RageTuning::BucketDecayTicks)
		return;

	const bool learned{ bucket.m_bLocked || bucket.m_iMisses > 0
		|| std::any_of(bucket.m_arrConfidence.begin(), bucket.m_arrConfidence.end(), [](float v) { return std::abs(v) > 0.05f; }) };
	if (!learned)
		return;

	for (auto& value : bucket.m_arrConfidence)
		value *= 0.5f;

	bucket.m_bLocked = false;
	bucket.m_iMisses = 0;
	bucket.m_bPhaseInitialized = false;
	bucket.m_iEvidenceTick = tick;
}

// the enemy's weapon fired between the previous and this simulation time
bool CResolverBrain::IsOnShotRecord(int index, const LagRecord_t& record) const {
	const auto& st{ Get(index) };
	if (st.m_flLastShotTime <= 0.f)
		return false;

	const float sim{ record.m_cAnimData.m_flSimulationTime };
	const float oldSim{ std::min(record.m_cAnimData.m_flOldSimulationTime, sim - Interfaces::Globals->flIntervalPerTick) };
	return st.m_flLastShotTime > oldSim - 0.001f && st.m_flLastShotTime <= sim + 0.001f;
}

// step 1: animation evidence ( layer 6 while moving, balance adjust / LBY while standing ) is never flipped by brute force
int CResolverBrain::HardEvidenceSide(int index, const LagRecord_t& record, EAimPattern pattern) const {
	const int animSide{ AnimResolve(record) };
	if (IsDesyncSide(animSide))
		return animSide;

	const auto& st{ Get(index) };
	if (st.m_pPlayer) {
		const int standSide{ StandingBalanceOrLbySide(st.m_pPlayer, record, record.m_flLowerBodyYaw) };
		if (standSide == RESOLVER_SIDE_FIRST || standSide == RESOLVER_SIDE_SECOND)
			return standSide;
	}

	return -1;
}

// step 8: trace from our eye to the head of both full sides; the head that is hidden behind world geometry
// is the likely real one ( players desync their head away from the enemy )
int CResolverBrain::FreestandHeadSide(int index, const LagRecord_t& record) const {
	const auto& st{ Get(index) };
	if (!st.m_pPlayer || !ctx.m_pLocal || !record.m_bMultiMatrix)
		return -1;

	const auto studio{ st.m_pPlayer->m_pStudioHdr() };
	if (!studio || !studio->pStudioHdr)
		return -1;

	const auto set{ studio->pStudioHdr->GetHitboxSet(st.m_pPlayer->m_nHitboxSet()) };
	const auto head{ set ? set->GetHitbox(HITBOX_HEAD) : nullptr };
	if (!head)
		return -1;

	bool hidden[3]{};
	for (int side{ RESOLVER_SIDE_FIRST }; side <= RESOLVER_SIDE_SECOND; ++side) {
		const auto& data{ record.m_cAnimData.m_arrSides[side] };
		if (!data.m_bFilled)
			return -1;

		const Vector point{ Math::VectorTransform((head->vecBBMin + head->vecBBMax) / 2.f, data.m_pMatrix[head->iBone]) };

		CGameTrace tr;
		CTraceFilter filter{ ctx.m_pLocal, TRACE_WORLD_ONLY };
		Interfaces::EngineTrace->TraceRay({ ctx.m_vecEyePos, point }, MASK_NPCWORLDSTATIC, &filter, &tr);
		hidden[side] = tr.flFraction < 0.97f;
	}

	if (hidden[RESOLVER_SIDE_FIRST] == hidden[RESOLVER_SIDE_SECOND])
		return -1;

	return hidden[RESOLVER_SIDE_FIRST] ? RESOLVER_SIDE_FIRST : RESOLVER_SIDE_SECOND;
}

// Momentum v5.2 layer-6 playback rate resim comparison against networked layer 6
// Rejects collinear movement where all 3 simulated sides produce nearly identical playback rates
int CResolverBrain::AnimResolve(const LagRecord_t& record) const {
	if (!RageTuning::AnimlayerResim || !record.m_bMultiMatrix || !record.m_bAccurateVelocity)
		return -1;

	const auto& data{ record.m_cAnimData };
	if (!(data.m_iFlags & FL_ONGROUND) || data.m_vecVelocity.Length2D() <= 5.f)
		return -1;

	const auto& net{ data.m_pLayers[6] };
	if (net.flWeight < 0.05f || net.flPlaybackRate < 0.002f || data.m_pLayers[12].flWeight >= 0.001f)
		return -1;

	// rate is the main signal; weight and cycle separate sides whose rates are close
	std::array< float, RESOLVER_SIDE_COUNT > error{};
	std::array< bool, RESOLVER_SIDE_COUNT > usable{};
	float lo{ std::numeric_limits< float >::max() };
	float hi{ -std::numeric_limits< float >::max() };

	for (int side{}; side < RESOLVER_SIDE_COUNT; ++side) {
		const auto& candidate{ data.m_arrSides[side] };
		if (!candidate.m_bFilled || candidate.m_pLayers[6].nSequence != net.nSequence) {
			if (side < DESYNC_MATRIX_COUNT)
				return -1;
			continue;
		}

		const auto& layer{ candidate.m_pLayers[6] };
		const float rateErr{ std::abs(layer.flPlaybackRate - net.flPlaybackRate) / net.flPlaybackRate };
		const float weightErr{ std::abs(layer.flWeight - net.flWeight) };
		float cycleErr{ std::abs(layer.flCycle - net.flCycle) };
		cycleErr = std::min(cycleErr, 1.f - cycleErr);

		error[side] = rateErr + weightErr * 0.5f + cycleErr * 0.25f;
		if (!std::isfinite(error[side]))
			return -1;

		usable[side] = true;
		if (side < DESYNC_MATRIX_COUNT) {
			lo = std::min(lo, layer.flPlaybackRate);
			hi = std::max(hi, layer.flPlaybackRate);
		}
	}

	// all full sides animate the same way ( running straight at / away from us ): no information
	const float spread{ hi - lo };
	if (spread < std::max(net.flPlaybackRate * 0.004f, 0.0001f))
		return -1;
	if (net.flPlaybackRate < lo - spread * 0.35f || net.flPlaybackRate > hi + spread * 0.35f)
		return -1;

	int best{ -1 };
	for (int side{}; side < RESOLVER_SIDE_COUNT; ++side) {
		if (usable[side] && (best < 0 || error[side] < error[best]))
			best = side;
	}

	if (best < 0 || error[best] > RageTuning::AnimMaxRelError)
		return -1;

	// margin only against the other side groups ( a low delta side naturally sits next to its full side )
	float other{ std::numeric_limits< float >::max() };
	for (int side{}; side < RESOLVER_SIDE_COUNT; ++side) {
		if (usable[side] && ParentSide(side) != ParentSide(best))
			other = std::min(other, error[side]);
	}

	if (error[best] >= other * RageTuning::AnimMarginRatio)
		return -1;

	return best;
}

// Momentum v5.2 no-anti-aim detector over clean pitch and |eye - LBY| history
bool CResolverBrain::EvaluateNoAA(ResolverPlayer_t& st, bool isBot, bool isJitter) const {
	if (!RageTuning::DetectNoAA)
		return false;

	if (isBot)
		return true;

	if (st.m_bAAHold)
		return false;

	if (isJitter) {
		st.m_bAAHold = true;
		return false;
	}

	const auto& ph{ st.m_dqPitchHistory };
	const auto& lh{ st.m_dqLbyHistory };
	if (static_cast<int>(ph.size()) < RageTuning::MinNoAASamples
		|| static_cast<int>(lh.size()) < RageTuning::MinNoAASamples)
		return false;

	int extreme{}, zero{}, jumps{};
	for (size_t i{}; i < ph.size(); ++i) {
		const float absPitch{ std::abs(ph[i]) };
		if (absPitch > 85.f)
			++extreme;
		else if (absPitch < 0.5f)
			++zero;

		if (i > 0 && std::abs(ph[i] - ph[i - 1]) > 40.f)
			++jumps;
	}

	if (extreme * 2 >= static_cast<int>(ph.size())
		|| zero == static_cast<int>(ph.size())
		|| jumps >= 3) {
		st.m_bAAHold = true;
		return false;
	}

	int big{}, tight{};
	for (const float delta : lh) {
		if (delta > RageTuning::LbyBigDelta)
			++big;
		else if (delta < RageTuning::LbyTightDelta)
			++tight;
	}

	if (big >= 2) {
		st.m_bAAHold = true;
		return false;
	}

	if (static_cast<float>(tight) < static_cast<float>(lh.size()) * 0.7f)
		return false;

	return true;
}

EAimPattern CResolverBrain::PatternFor(int index, const LagRecord_t& record) const {
	const auto& entry{ Features::AnimSys.m_arrEntries.at(index - 1) };
	const auto& st{ Get(index) };

	if (entry.m_bBot || st.m_bNoAA)
		return EAimPattern::NoAA;

	// Decisive layer-6 playback rate match routes to EAimPattern::Anim (unified with Move bucket)
	const int animSide{ AnimResolve(record) };
	if (IsDesyncSide(animSide))
		return EAimPattern::Anim;

	// Only classify a repeatable two-band pattern from this record and its past.
	const Vector localOrigin{ ctx.m_pLocal ? ctx.m_pLocal->GetAbsOrigin() : Vector{ 0.f, 0.f, 0.f } };
	const auto relYawFor = [&](const LagRecord_t& sample) {
		if (!ctx.m_pLocal)
			return Normalize180(sample.m_angEyeAngles.y);
		const Vector& orig{ sample.m_cAnimData.m_vecOrigin };
		const float atMe{ RAD2DEG(std::atan2(localOrigin.y - orig.y, localOrigin.x - orig.x)) };
		return Normalize180(sample.m_angEyeAngles.y - atMe - 180.f);
		};

	std::array< float, RageTuning::YawHistory > relSamples{};
	int samples{};

	for (auto it{ entry.m_pRecords.rbegin() }; it != entry.m_pRecords.rend() && samples < RageTuning::YawHistory; ++it) {
		const auto& sample{ **it };
		if (sample.m_cAnimData.m_flSimulationTime > record.m_cAnimData.m_flSimulationTime
			|| sample.m_iReceiveTick > record.m_iReceiveTick)
			continue;
		if (IsOnShotRecord(index, sample) || IsFlickRecord(index, sample)
			|| (RageTuning::IgnoreDefensiveAngles && IsDefensiveRecord(index, sample)))
			continue;
		if (record.m_cAnimData.m_flSimulationTime - sample.m_cAnimData.m_flSimulationTime > 0.6f)
			break;
		if (RecordContext(sample) != RecordContext(record))
			break;

		const float rel{ relYawFor(sample) };
		relSamples[samples++] = rel;
	}

	const auto jitter{ ResolverPattern::Classify(relSamples.data(), samples) };
	if (jitter.jitter && samples > 0 && std::abs(Normalize180(relSamples[0] - relYawFor(record))) < 0.01f)
		return jitter.phase > 0 ? EAimPattern::JitterPositive : EAimPattern::JitterNegative;
	if (!(record.m_cAnimData.m_iFlags & FL_ONGROUND))
		return EAimPattern::Air;
	if (record.m_cAnimData.m_flDuckAmount > 0.5f)
		return EAimPattern::Crouch;

	if (record.m_cAnimData.m_vecVelocity.Length2D() > RageTuning::MoveSpeed)
		return EAimPattern::Move;

	return EAimPattern::Static;
}

ResolverBucket_t& CResolverBrain::BucketFor(int index, const LagRecord_t& record) {
	return Bucket(Get(index).m_strKey, PatternFor(index, record), RecordContext(record));
}

// Momentum v5.2+ heuristic side priority chain:
// 1. Animlayer resim (layer 6 playback rate)
// 2. Standing Layer-3 balance adjust / LBY foot-yaw side
// 3. Synced jitter swing side (flips base side between JitterPositive and JitterNegative)
// 4. Freestand / record orientation evidence
// 5. Sideways / manual yaw offset (>= 35 deg from backwards-at-us leans against offset)
// 6. Default side (RESOLVER_SIDE_FIRST)
int CResolverBrain::HeuristicSide(int index, const LagRecord_t& record, EAimPattern pattern, float relMean) const {
	const auto& st{ Get(index) };

	// hard evidence ( even when SideForRecord vetoed it after repeated misses ) still seeds the detected side
	const int hard{ HardEvidenceSide(index, record, pattern) };
	if (IsDesyncSide(hard))
		return ParentSide(hard);

	// A backtrack record already has its yaw. A prediction for the next network
	// update cannot select the side of this record, even after previous hits.

	int baseSide{ RESOLVER_SIDE_FIRST };
	if (HasEvidence(record))
		baseSide = record.m_iResolverSide;
	else if (std::abs(relMean) >= RageTuning::SidewaysYawThreshold)
		baseSide = relMean > 0.f ? RESOLVER_SIDE_SECOND : RESOLVER_SIDE_FIRST;

	// Synced jitter flips desync side with the eye-yaw swing direction
	if (pattern == EAimPattern::JitterPositive)
		return baseSide;
	if (pattern == EAimPattern::JitterNegative)
		return SwapSide(baseSide);

	return baseSide;
}

void CResolverBrain::Update(PlayerEntry& entry, int index) {
	auto player{ entry.m_pPlayer };
	if (!player || !ctx.m_pLocal)
		return;

	auto& st{ Get(index) };
	const int tick{ Interfaces::Globals->iTickCount };
	const float interval{ Interfaces::Globals->flIntervalPerTick };

	const auto identity{ Interfaces::Engine->GetPlayerInfo(index) };
	const std::string key{ identity.has_value() && !identity->bFakePlayer && identity->ullXuid
		? "s" + std::to_string(identity->ullXuid) : "i" + std::to_string(index) };
	// Entity storage may be reused by a different player in the same slot.
	if (st.m_pPlayer != player || st.m_strKey != key) {
		st = ResolverPlayer_t{};
		st.m_pPlayer = player;
		st.m_strKey = key;

		if (!identity.has_value() || identity->bFakePlayer || !identity->ullXuid)
			m_mapMemory.erase(st.m_strKey);

		st.m_flSpawnTime = player->m_flSpawnTime();
		st.m_pLastWeapon = player->GetWeapon();
		st.m_flLastShotTime = ReadLastShotTime(st.m_pLastWeapon);
	}

	// New life: reset runtime state while preserving per-player phase memory (Momentum "Remember players").
	if (player->m_flSpawnTime() != st.m_flSpawnTime) {
		st.m_flSpawnTime = player->m_flSpawnTime();
		st.ResetRuntime();
	}

	// enemy shot detection from the networked m_fLastShotTime
	const auto weapon{ player->GetWeapon() };
	if (weapon != st.m_pLastWeapon) {
		st.m_pLastWeapon = weapon;
		st.m_flLastShotTime = ReadLastShotTime(weapon);
	}
	else {
		const float shotTime{ ReadLastShotTime(weapon) };
		if (shotTime > st.m_flLastShotTime + 0.0001f) {
			st.m_flLastShotTime = shotTime;
			st.m_iLastShotTick = tick;

			// a "defensive" shift that lines up with a shot was the double tap teleport
			const int recordTicks{ st.m_flRecordDt > 0.f ? TIME_TO_TICKS(st.m_flRecordDt) : st.m_iChokeTicks };
			const int window{ RageTuning::DoubletapWindowTicks + std::clamp(recordTicks, 1, 17) };
			// a shift right before the shot was their double tap, not defensive
			if (!st.m_dqShiftTicks.empty() && tick - st.m_dqShiftTicks.back() <= window) {
				st.m_dqShiftTicks.pop_back();
				st.m_bDefensiveUser = static_cast<int>(st.m_dqShiftTicks.size()) >= DefTune::ConfirmShifts();
			}

			if (st.m_iDefensiveUntil >= tick && tick - st.m_iDefensiveStart <= window) {
				st.m_iDoubletapUntil = st.m_iDefensiveUntil;
				st.m_iDefensiveUntil = -1;
				if (!st.m_dqDefensiveSimTimes.empty())
					st.m_dqDefensiveSimTimes.pop_back();
			}
		}
	}

	// every simulation time the server sent since the last check, oldest first ( recv proxy, see net_simtime.h ).
	// the entity's own value hid defensive: the animation system puts the previous simulation time back on updates
	// without new animation, and a drop + recovery inside one frame was never seen
	std::array< float, NetSim::Capacity > simTimes{};
	int simCount{ NetSim::Drain(index, simTimes) };
	if (simCount == 0) {
		simTimes[0] = st.m_flLastSimTime != 0.f ? st.m_flLastSimTime : player->m_flSimulationTime();
		simCount = 1;
	}

	bool newRecord{};
	for (int sample{}; sample < simCount; ++sample) {
		const float simTime{ simTimes[sample] };
		if (st.m_flMaxSimTime == 0.f || std::abs(simTime - st.m_flMaxSimTime) > 1.f) {
			st.ResetRuntime();
			st.m_pLastWeapon = weapon;
			st.m_flLastShotTime = ReadLastShotTime(weapon);
			st.m_flMaxSimTime = simTime;
			st.m_flLastSimTime = simTime;
		}

		const bool isNew{ simTime != st.m_flLastSimTime };
		newRecord = newRecord || isNew;
		if (isNew) {
			if (simTime < st.m_flMaxSimTime - interval * 0.5f) {
				const int shift{ static_cast<int>((st.m_flMaxSimTime - simTime) / interval + 0.5f) };
				const int recordTicks{ st.m_flRecordDt > 0.f ? TIME_TO_TICKS(st.m_flRecordDt) : st.m_iChokeTicks };
				const int window{ RageTuning::DoubletapWindowTicks + std::clamp(recordTicks, 1, 17) };

				// classify once per span: later updates of the same span only extend it
				if (!st.m_bBelowMax) {
					bool angleEvidence{};
					// measured defensive ( gamesense, defensive_lab ): one update jumps the simulation time *forward* by the
					// shift ( the command run on a pushed tickbase, carrying the defensive angles ), the next falls back by
					// the same amount and it takes ~shift ticks to climb back. a drop right after a forward jump of the same
					// size is that push, whatever the angles
					const int previousStep{ st.m_flRecordDt > 0.f ? TIME_TO_TICKS(st.m_flRecordDt) : 0 };
					const bool pushEvidence{ previousStep >= shift };
					const auto kind{ ClassifyShift(st, index, shift, tick, window, angleEvidence) };

					if (kind == EShiftKind::Doubletap)
						st.m_iDoubletapUntil = tick + shift + 2;
					else if (kind == EShiftKind::Defensive) {
						st.m_dqShiftTicks.push_back(tick);
						st.m_bAngleEvidence = angleEvidence;
						st.m_bPushEvidence = pushEvidence;
						++st.m_iDefensiveEvents;
						if (pushEvidence)
							++st.m_iDbgPushed;

						// the pushed update is the record the server keeps as their newest lag record for the whole span:
						// it carries the defensive angles, so it is a defensive record ( resolver ignores its angles )
						for (auto it{ entry.m_pRecords.rbegin() }; it != entry.m_pRecords.rend(); ++it) {
							if (*it && std::abs((*it)->m_cAnimData.m_flSimulationTime - st.m_flMaxSimTime) < interval * 0.5f) {
								PushCapped(st.m_dqDefensiveSimTimes, std::make_pair((*it)->m_cAnimData.m_flSimulationTime, (*it)->m_iReceiveTick), static_cast<size_t>(RageTuning::DefensiveHistory));
								break;
							}
						}

						// repeated shifts, or one shift that carried defensive angles / the forward push, confirm the player
						st.m_bDefensiveUser = (angleEvidence && DefTune::AngleConfirms())
							|| (pushEvidence && DefTune::PushConfirms())
							|| static_cast<int>(st.m_dqShiftTicks.size()) >= DefTune::ConfirmShifts();

						// learn how often this player shifts
						const int gap{ tick - st.m_iLastShiftStart };
						if (gap > 0 && gap <= DefTune::ConfirmWindow())
							st.m_flShiftInterval = st.m_flShiftInterval > 0.f ? st.m_flShiftInterval * 0.7f + gap * 0.3f : static_cast<float>(gap);
						st.m_iLastShiftStart = tick;

						if (st.m_bDefensiveUser) {
							st.m_iDefensiveShift = shift;
							st.m_iDefensiveStart = tick;
							st.m_iDefensiveUntil = tick + shift + 2;
							st.m_bInShift = true;
						}
					}
				}
				else if (st.m_bInShift && shift > st.m_iDefensiveShift && shift <= DefTune::MaxShift()) {
					// the span grew: keep the window open until it is fully over
					st.m_iDefensiveShift = shift;
					st.m_iDefensiveUntil = std::max(st.m_iDefensiveUntil, tick + shift + 2);
				}

				// every update inside an active span is a shifted ( never lag compensated ) record
				if (st.m_bInShift) {
					PushCapped(st.m_dqDefensiveSimTimes, std::make_pair(simTime, entry.m_iLastRecievedTick), static_cast<size_t>(RageTuning::DefensiveHistory));

					// defensive angle profile
					const float pitch{ Normalize180(player->m_angEyeAngles().x) };
					if (pitch <= -45.f)
						++st.m_iDefPitchUp;
					else if (pitch >= 45.f)
						++st.m_iDefPitchDown;
					else
						++st.m_iDefPitchZero;

					if (st.m_bHasCleanAngles) {
						const float offset{ std::abs(Normalize180(player->m_angEyeAngles().y - st.m_flCleanYaw)) };
						++st.m_iDefYawSamples;
						st.m_flDefYawOffset += (offset - st.m_flDefYawOffset) / static_cast<float>(std::min(st.m_iDefYawSamples, 32));
					}
				}

				st.m_bBelowMax = true;
			}
			else {
				st.m_bBelowMax = false;

				// clean update: reference angles for the next shift
				st.m_flCleanPitch = Normalize180(player->m_angEyeAngles().x);
				st.m_flCleanYawPrev = st.m_bHasCleanAngles ? st.m_flCleanYaw : Normalize180(player->m_angEyeAngles().y);
				st.m_flCleanYaw = Normalize180(player->m_angEyeAngles().y);
				st.m_bHasCleanAngles = true;
				// simulation time is back at / above the highest we saw: this update is a real, lag compensated tick
				// ( the gap between two defensive shifts ), so it can be shot
				// choke is measured from the last real tick, not from the shifted one
				const float from{ st.m_bInShift ? st.m_flMaxSimTime : st.m_flLastSimTime };
				st.m_iChokeTicks = std::clamp(TIME_TO_TICKS(simTime - from), 1, 17);

				st.m_bInShift = false;
				st.m_iLastCleanTick = tick;
			}

			// Distance-based lag compensation break detection (>64 units between records)
			const Vector origin{ player->GetAbsOrigin() };
			const bool distBreak{ st.m_bHasLastOrigin && (origin - st.m_vecLastOrigin).LengthSqr() > RageTuning::LcBreakDistSqr };
			st.m_vecLastOrigin = origin;
			st.m_bHasLastOrigin = true;

			st.m_flRecordDt = simTime - st.m_flLastSimTime;
			st.m_flMaxSimTime = std::max(st.m_flMaxSimTime, simTime);
			st.m_flLastSimTime = simTime;
			st.m_bLagCompBroken = entry.m_bBrokeLC || distBreak;
		}
	}

	// a player that stopped shifting stops counting as a defensive user
	while (!st.m_dqShiftTicks.empty() && tick - st.m_dqShiftTicks.front() > DefTune::ConfirmWindow())
		st.m_dqShiftTicks.pop_front();
	if (st.m_dqShiftTicks.empty()) {
		st.m_bDefensiveUser = false;
		st.m_bAngleEvidence = false;
		st.m_bPushEvidence = false;
	}

	// flicker: repeated one tick head flicks
	while (!st.m_dqFlickTicks.empty() && tick - st.m_dqFlickTicks.front() > RageTuning::FlickWindow)
		st.m_dqFlickTicks.pop_front();
	st.m_bFlicker = static_cast<int>(st.m_dqFlickTicks.size()) >= RageTuning::FlickConfirm;

	// a shift window that ran out without the simulation time recovering is over anyway
	if (st.m_bInShift && tick > st.m_iDefensiveUntil + 64)
		st.m_bInShift = false;

	if (!DefTune::Enabled()) {
		st.m_dqShiftTicks.clear();
		st.m_bDefensiveUser = false;
		st.m_bAngleEvidence = false;
		st.m_bInShift = false;
		st.m_iDefensiveUntil = -1;
	}

	st.m_bDefensive = st.m_bDefensiveUser && tick <= st.m_iDefensiveUntil;
	st.m_bDoubletapShift = tick <= st.m_iDoubletapUntil;

	// fake flicks: every record that arrived since the last check, in order. two records can arrive in one frame,
	// and the old check only looked at the newest, so a flick followed by the return in the same frame was never
	// marked and stayed a backtrack target. it also needed two agreeing records around the flick, which jitter never
	// gives, so flicks on jittering players were never found
	{
		const float jump{ FlickTune::Jump() };
		const float agree{ FlickTune::Agree() };
		const auto delta{ [](float a, float b) { return std::abs(Normalize180(a - b)); } };
		auto& yaws{ st.m_dqFlickYaws };

		// far from every one of the first count samples
		const auto farFromAll{ [&](float yaw, std::size_t count) {
			if (count < FlickTune::MinHistory)
				return false;
			for (std::size_t i{}; i < count; ++i)
				if (delta(yaw, yaws[i].m_flYaw) < jump)
					return false;
			return true;
		} };
		const auto nearAny{ [&](float yaw, std::size_t count) {
			for (std::size_t i{}; i < count; ++i)
				if (delta(yaw, yaws[i].m_flYaw) <= agree)
					return true;
			return false;
		} };

		for (const auto& rec : entry.m_pRecords) {
			if (!rec || rec->m_cAnimData.m_flSimulationTime <= st.m_flFlickAnalyzedSimTime)
				continue;
			st.m_flFlickAnalyzedSimTime = rec->m_cAnimData.m_flSimulationTime;

			// the shot tick carries the aim angle, not their anti-aim: never a flick ( and stays shootable )
			if (std::abs(rec->m_iReceiveTick - st.m_iLastShotTick) <= 2)
				continue;

			const float yaw{ rec->m_angEyeAngles.y };

			// the previous record left every recent yaw and this one went straight back: it was a flick
			if (!yaws.empty()) {
				const std::size_t candidate{ yaws.size() - 1 };
				if (farFromAll(yaws[candidate].m_flYaw, candidate) && nearAny(yaw, candidate)) {
					const auto flick{ yaws[candidate] };
					PushCapped(st.m_dqFlickRecords, std::make_pair(flick.m_flSimTime, flick.m_iReceiveTick), static_cast<size_t>(RageTuning::FlickHistory));
					st.m_dqFlickTicks.push_back(tick);

					// the flick never was their real side: take it back out of the jitter history
					for (auto it{ st.m_dqYawHistory.rbegin() }; it != st.m_dqYawHistory.rend(); ++it) {
						if (std::abs(Normalize180(*it - flick.m_flRelYaw)) < 0.5f) {
							st.m_dqYawHistory.erase(std::next(it).base());
							break;
						}
					}

					yaws.erase(yaws.begin() + static_cast<std::ptrdiff_t>(candidate));
				}
			}

			const Vector localOrigin{ ctx.m_pLocal->GetAbsOrigin() };
			const Vector& origin{ rec->m_cAnimData.m_vecOrigin };
			const float atMe{ RAD2DEG(std::atan2(localOrigin.y - origin.y, localOrigin.x - origin.x)) };

			yaws.push_back({ yaw, Normalize180(yaw - atMe - 180.f), rec->m_cAnimData.m_flSimulationTime, rec->m_iReceiveTick });
			while (yaws.size() > FlickTune::CompareRecords + 1)
				yaws.pop_front();

			// the newest record is on a yaw none of the recent records had: a flick until the next record agrees
			// ( anyone with "hold new yaw" on, otherwise only known flickers ). a real side change costs one record
			const std::size_t newest{ yaws.size() - 1 };
			st.m_bFlicker = static_cast<int>(st.m_dqFlickTicks.size()) >= RageTuning::FlickConfirm;
			st.m_bSuspectFlick = farFromAll(yaws[newest].m_flYaw, newest) && (FlickTune::HoldNewYaw() || st.m_bFlicker);
			st.m_flSuspectSimTime = yaws[newest].m_flSimTime;
			st.m_iSuspectReceiveTick = yaws[newest].m_iReceiveTick;
		}
	}

	if (newRecord && !entry.m_pRecords.empty()) {
		const auto& latest{ *entry.m_pRecords.back() };
		const bool onShot{ st.m_bDoubletapShift || (tick - st.m_iLastShotTick <= 2) };

		// Pitch + eye/LBY delta history for no-anti-aim detection (skip defensive & on-shot ticks)
		if (!st.m_bDefensive && !onShot) {
			PushCapped(st.m_dqPitchHistory, Normalize180(latest.m_angEyeAngles.x), static_cast<size_t>(RageTuning::PitchLbyHistory));
			const float lby{ ReadLowerBodyYaw(player) };
			PushCapped(st.m_dqLbyHistory, std::abs(Normalize180(latest.m_angEyeAngles.y - lby)), static_cast<size_t>(RageTuning::PitchLbyHistory));
		}

		// Sample yaw relative to "backwards at us"; skip on-shot, shifted and hidden defensive angles
		// ( a shifted update is never stored, so "latest" would be the previous clean record again )
		if (!onShot && !st.m_bBelowMax && !(st.m_bDefensive && RageTuning::IgnoreDefensiveAngles)) {
			const Vector localOrigin{ ctx.m_pLocal->GetAbsOrigin() };
			const Vector& playerOrigin{ latest.m_cAnimData.m_vecOrigin };
			const float atMe{ RAD2DEG(std::atan2(localOrigin.y - playerOrigin.y, localOrigin.x - playerOrigin.x)) };
			const float relYaw{ Normalize180(latest.m_angEyeAngles.y - atMe - 180.f) };
			PushCapped(st.m_dqYawHistory, relYaw, static_cast<size_t>(RageTuning::YawHistory));

		}

		// Circular mean & delayed-jitter run-length analysis (Momentum res_classify)
		float sinSum{}, cosSum{};
		for (const float y : st.m_dqYawHistory) {
			sinSum += std::sin(DEG2RAD(y));
			cosSum += std::cos(DEG2RAD(y));
		}
		st.m_flYawMean = !st.m_dqYawHistory.empty() ? RAD2DEG(std::atan2(sinSum, cosSum)) : 0.f;

		st.m_bJitter = false;
		st.m_iPredictedJitterSide = RESOLVER_SIDE_FIRST;
		if (st.m_dqYawHistory.size() >= 4) {
			const float threshold{ RageTuning::JitterThreshold };
			const float dead{ threshold * 0.5f };
			int big{}, left{}, right{};
			std::vector< int > sides{};
			sides.reserve(st.m_dqYawHistory.size());

			for (size_t i{}; i < st.m_dqYawHistory.size(); ++i) {
				if (i > 0 && std::abs(Normalize180(st.m_dqYawHistory[i] - st.m_dqYawHistory[i - 1])) >= threshold)
					++big;

				const float off{ Normalize180(st.m_dqYawHistory[i] - st.m_flYawMean) };
				if (off >= dead) {
					sides.push_back(1);
					++right;
				}
				else if (off <= -dead) {
					sides.push_back(-1);
					++left;
				}
			}

			st.m_bJitter = (big >= 2 && left >= 2 && right >= 2);
			if (st.m_bJitter && !sides.empty()) {
				int runs{ 1 }, run{ 1 }, prev{ sides.front() };
				for (size_t i{ 1 }; i < sides.size(); ++i) {
					if (sides[i] == prev)
						++run;
					else {
						++runs;
						run = 1;
					}
					prev = sides[i];
				}
				const int avgRun{ std::max(1, static_cast<int>(std::floor(static_cast<float>(sides.size()) / static_cast<float>(runs) + 0.5f))) };
				const int predictedSign{ (run >= avgRun) ? -prev : prev };
				// Momentum uses -predicted for next-record jitter side
				st.m_iPredictedJitterSide = (-predictedSign > 0) ? RESOLVER_SIDE_FIRST : RESOLVER_SIDE_SECOND;
			}
		}

		st.m_bNoAA = EvaluateNoAA(st, entry.m_bBot, st.m_bJitter);
		st.m_flMaxDesync = RageTuning::ScaleToMaxDesync ? ComputeMaxDesync(latest) : 58.f;
		st.m_iAnimSide = AnimResolve(latest);
		st.m_ePattern = PatternFor(index, latest);
		if (HasEvidence(latest))
			st.m_iDetectedSide = latest.m_iResolverSide;
		else if (std::abs(st.m_flYawMean) >= RageTuning::SidewaysYawThreshold)
			st.m_iDetectedSide = st.m_flYawMean > 0.f ? RESOLVER_SIDE_SECOND : RESOLVER_SIDE_FIRST;
	}
}

int CResolverBrain::SideForRecord(int index, const LagRecord_t& record) {
	if (!record.m_bMultiMatrix)
		return RESOLVER_SIDE_CENTER;

	const auto& st{ Get(index) };
	const EAimPattern pattern{ PatternFor(index, record) };
	auto& bucket{ Bucket(st.m_strKey, pattern, RecordContext(record)) };

	std::array<bool, RESOLVER_SIDE_COUNT> available{};
	for (int side{}; side < RESOLVER_SIDE_COUNT; ++side)
		available[side] = record.m_cAnimData.m_arrSides[side].m_bFilled;
	const int hard{ HardEvidenceSide(index, record, pattern) };
	const auto decision{ ResolverRules::Decide(bucket.m_arrConfidence, available, hard) };
	// Conflicting fresh animation and historical evidence is explicitly uncertain.
	// HeadSafetyRequired uses the same rule before accepting a head point.
	if (decision.side >= 0)
		return decision.side;

	// Compute record-relative at-target yaw offset for sideways heuristic fallback
	float relMean{};
	if (ctx.m_pLocal) {
		const Vector localOrigin{ ctx.m_pLocal->GetAbsOrigin() };
		const Vector& orig{ record.m_cAnimData.m_vecOrigin };
		const float atMe{ RAD2DEG(std::atan2(localOrigin.y - orig.y, localOrigin.x - orig.x)) };
		relMean = Normalize180(record.m_angEyeAngles.y - atMe - 180.f);
	}

	const int detectedSide{ HeuristicSide(index, record, pattern, relMean) };
	const int invertedSide{ SwapSide(detectedSide) };
	const bool inverted{ bucket.m_ePhase == EResolverPhase::Inverted || bucket.m_ePhase == EResolverPhase::HalfInverted };

	// step 3: half phases aim at the real low delta matrices
	int phaseSide{ detectedSide };
	switch (bucket.m_ePhase) {
	case EResolverPhase::Detected: phaseSide = detectedSide; break;
	case EResolverPhase::Inverted: phaseSide = invertedSide; break;
	case EResolverPhase::HalfDetected: phaseSide = LowSideOf(detectedSide); break;
	case EResolverPhase::HalfInverted: phaseSide = LowSideOf(invertedSide); break;
	case EResolverPhase::Native: phaseSide = RESOLVER_SIDE_CENTER; break;
	default: break;
	}
	phaseSide = std::clamp(phaseSide, 0, RESOLVER_SIDE_COUNT - 1);

	// Each real candidate has independent evidence, including the half-delta poses.
	std::array< float, RESOLVER_SIDE_COUNT > score{};
	for (int side{}; side < RESOLVER_SIDE_COUNT; ++side)
		score[side] = bucket.m_arrConfidence[side] * 2.f - (side >= DESYNC_MATRIX_COUNT ? 0.25f : 0.f);

	// The exploration order is a prior, not stronger than a unique server hit.
	score[phaseSide] += 1.5f;

	// soft evidence follows the brute-force inversion
	if (HasEvidence(record)) {
		const int evSide{ inverted ? SwapSide(record.m_iResolverSide) : record.m_iResolverSide };
		score[std::clamp(evSide, 0, RESOLVER_SIDE_COUNT - 1)] += 0.5f;
	}

	// step 8: head hidden from us
	const int freestand{ FreestandHeadSide(index, record) };
	if (IsDesyncSide(freestand)) {
		const int fsSide{ inverted ? SwapSide(freestand) : freestand };
		score[std::clamp(fsSide, 0, RESOLVER_SIDE_COUNT - 1)] += RageTuning::FreestandWeight;
	}

	// For NoAA before any misses, strongly favor native center (Momentum RES_ORDER_NOAA starts at RES_NATIVE)
	if (pattern == EAimPattern::NoAA && bucket.m_iMisses == 0)
		score[RESOLVER_SIDE_CENTER] += 4.f;

	for (int side{}; side < RESOLVER_SIDE_COUNT; ++side)
		if (!record.m_cAnimData.m_arrSides[side].m_bFilled)
			score[side] = -std::numeric_limits<float>::infinity();
	return static_cast<int>(std::max_element(score.begin(), score.end()) - score.begin());
}

float CResolverBrain::PhaseScaleForRecord(int index, const LagRecord_t& record) {
	// Keep server-animated matrices intact (CAnimationSys::UpdateSide already clamps to true max desync)
	return 1.f;
}

void CResolverBrain::OnFire(Shot_t& shot, bool unreliable) {
	const int index{ shot.m_pPlayer->Index() };
	auto& st{ Get(index) };
	const EAimPattern pattern{ PatternFor(index, *shot.m_pRecord) };
	const auto& bucket{ Bucket(st.m_strKey, pattern, RecordContext(*shot.m_pRecord)) };

	shot.m_bResolverTracked = true;
	shot.m_bResolverUnreliable = unreliable || !shot.m_pRecord->m_bMultiMatrix
		|| (ctx.m_pWeaponData && ctx.m_pWeaponData->nWeaponType == WEAPONTYPE_SHOTGUN)
		|| IsDefensiveRecord(index, *shot.m_pRecord)
		|| st.m_bLagCompBroken
		|| st.m_bDoubletapShift;
	shot.m_iResolverPattern = static_cast<int>(pattern);
	shot.m_iResolverPhase = static_cast<int>(bucket.m_ePhase);
	shot.m_iResolverContext = RecordContext(*shot.m_pRecord);
	shot.m_strResolverKey = st.m_strKey;
	shot.m_flResolverSpawnTime = st.m_flSpawnTime;
	st.m_iLastFiredPhase = static_cast<int>(bucket.m_ePhase);
}

void CResolverBrain::OnShotResult(Shot_t& shot) {
	if (!shot.m_bResolverTracked || shot.m_bResolverReported) return;
	shot.m_bResolverReported = true;
	if (!shot.m_bHasImpact || !shot.m_pRecord || !shot.m_pPlayer) {
		shot.m_szMissReason = "no impact data";
		return;
	}
	const int index{ shot.m_pPlayer->Index() };
	auto& st{ Get(index) };

	// say why a miss can't be blamed on the resolver instead of one generic message
	if (shot.m_bExtrapolated) {
		if (!shot.m_bHitPlayer)
			shot.m_szMissReason = "extrapolation ( lag-comp breaker was not where predicted )";
		return;
	}

	// where the server's bullet went against every desync matrix of the record we aimed at.
	// done first: occlusion and plain spread follow from the server impact alone, whatever the resolver state
	LagBackup_t backup{ shot.m_pPlayer };
	shot.m_pRecord->Apply(shot.m_pPlayer);
	const int bones{ std::clamp(shot.m_pPlayer->m_CachedBoneData().Size(), 0, 256) };
	std::array< bool, RESOLVER_SIDE_COUNT > intersects{};
	std::array< bool, RESOLVER_SIDE_COUNT > matches{};
	bool completeCandidates{ true };
	for (int side{}; side < RESOLVER_SIDE_COUNT; ++side) {
		if (!shot.m_pRecord->m_cAnimData.m_arrSides[side].m_bFilled) {
			completeCandidates = false;
			continue;
		}
		std::memcpy(shot.m_pPlayer->m_CachedBoneData().Base(),
			shot.m_pRecord->m_cAnimData.m_arrSides[side].m_pMatrix, bones * sizeof(matrix3x4_t));
		CGameTrace trace{};
		Interfaces::EngineTrace->ClipRayToEntity({ shot.m_vecStart, shot.m_vecServerEnd }, MASK_SHOT_PLAYER, shot.m_pPlayer, &trace);
		intersects[side] = trace.pHitEntity == shot.m_pPlayer;
		matches[side] = intersects[side] && trace.iHitGroup == shot.m_iServerHitgroup;
	}
	backup.Apply(shot.m_pPlayer);

	// Catch near-misses where the bullet reached the enemy's depth without hitting cover,
	// even if slight spread clipped just outside the chosen matrix capsule.
	const float rayDistSqr{ (shot.m_vecServerEnd - shot.m_vecStart).LengthSqr() };
	const float targetDistSqr{ (shot.m_pRecord->m_cAnimData.m_vecOrigin - shot.m_vecStart).LengthSqr() };
	const bool reachedTargetDepth{ rayDistSqr >= targetDistSqr * 0.85f };
	const bool anyIntersects{ std::find(intersects.begin(), intersects.end(), true) != intersects.end() };
	const bool allIntersect{ std::find(intersects.begin(), intersects.end(), false) == intersects.end() };

	if (!shot.m_bHitPlayer && !reachedTargetDepth) { shot.m_szMissReason = "occlusion ( bullet stopped before the target )"; return; }

	// the record turned out to be a one tick head flick ( confirmed by the record after it )
	if (!shot.m_bHitPlayer && IsFlickRecord(index, *shot.m_pRecord)) { shot.m_szMissReason = "flick ( head was only there for one tick )"; return; }
	if (IsDefensiveRecord(index, *shot.m_pRecord)) { shot.m_szMissReason = "defensive record"; return; }
	if (st.m_bDoubletapShift) { shot.m_szMissReason = "target double tap shift"; return; }
	if (st.m_pPlayer != shot.m_pPlayer || st.m_strKey != shot.m_strResolverKey
		|| shot.m_pPlayer->m_flSpawnTime() != shot.m_flResolverSpawnTime) {
		shot.m_szMissReason = "target changed / respawned";
		return;
	}

	// was the record still inside the server's lag compensation window *when the server ran the shot*?
	// ( Validity( ) judges against the current time: by the time the impact arrives a backtracked record is always
	//   "expired", which made this the reason for nearly every miss )
	{
		const float maxUnlag{ std::min(Displacement::Cvars.sv_maxunlag->GetFloat(), 0.2f) };
		const float serverTime{ TICKS_TO_TIME(shot.m_iServerProcessTick) };
		const float simTime{ shot.m_pRecord->m_cAnimData.m_flSimulationTime };
		const int lerpTicks{ TIME_TO_TICKS(ctx.m_flLerpTime) };
		const float correct{ std::clamp(ctx.m_flOutLatency + TICKS_TO_TIME(lerpTicks), 0.f, maxUnlag) };
		const float targetTime{ TICKS_TO_TIME(TIME_TO_TICKS(simTime + ctx.m_flLerpTime) - lerpTicks) };
		// the dead time uses the server's clock, but the 200 ms window is measured from *our* tickbase ( the curtime the
		// server runs our command at ). with double tap charged our tickbase sits ~12 ticks behind the server's clock
		// ( the "diff" in the shot log ), so judging the window by the server tick called valid records expired
		const float shooterTime{ shot.m_flCurTime > 0.f ? shot.m_flCurTime : serverTime };
		const bool dead{ simTime < static_cast<float>(static_cast<int>(serverTime - maxUnlag)) };
		const bool outOfWindow{ std::fabs(correct - (shooterTime - targetTime)) > 0.2f + Interfaces::Globals->flIntervalPerTick };
		if (shot.m_iServerProcessTick > 0 && (dead || outOfWindow)) { shot.m_szMissReason = "record expired ( server could not backtrack that far )"; return; }
	}

	if (Features::AnimSys.m_arrEntries.at(index - 1).m_bBrokeLC || st.m_bLagCompBroken) { shot.m_szMissReason = "lag compensation broken"; return; }

	// the bullet went through none of the record's matrices: spread, whatever side we picked
	if (!shot.m_bHitPlayer && !anyIntersects) { shot.m_szMissReason = "spread"; return; }

	if (shot.m_bResolverUnreliable || !completeCandidates) {
		shot.m_szMissReason = !shot.m_pRecord->m_bMultiMatrix ? "unknown ( single matrix record, no desync data to judge the side )"
			: "unknown ( bullet crossed the target, record too unreliable to judge the side )";
		return;
	}
	if (shot.m_iResolverPattern < 0 || shot.m_iResolverPattern >= static_cast<int>(EAimPattern::Count)) { shot.m_szMissReason = "unknown"; return; }

	const auto pattern{ static_cast<EAimPattern>(shot.m_iResolverPattern) };
	if (shot.m_iResolverContext < 0 || shot.m_iResolverContext >= ResolverRules::ContextCount) return;
	auto& bucket{ Bucket(shot.m_strResolverKey, pattern, shot.m_iResolverContext) };
	const int chosen{ shot.m_iResolverSide };
	if (chosen < 0 || chosen >= RESOLVER_SIDE_COUNT) return;

	// A headshot shared by several poses does not identify one of those poses.
	const auto outcome{ ResolverEvidence::Learn(bucket.m_arrConfidence, true,
		shot.m_bHitPlayer, chosen, intersects, matches) };

	if (!shot.m_bHitPlayer) {
		if (!intersects[chosen])
			shot.m_szMissReason = "spread ( hit a different desync side )";
		else if (allIntersect)
			shot.m_szMissReason = "prediction / lag compensation ( every side was in the bullet path )";
		else
			shot.m_szMissReason = "resolver ( wrong side )";
	}

	if (outcome == ResolverEvidence::Outcome::Hit) {
		bucket.m_iHits++;
		bucket.m_iMisses = 0;
		bucket.m_bLocked = true;
		Features::AnimSys.m_arrEntries.at(index - 1).m_iMissedShots = 0;
		bucket.m_iEvidenceTick = Interfaces::Globals->iTickCount;
	}
	// step 2: only a bullet that went through the chosen side and still missed blames the resolver
	else if (outcome == ResolverEvidence::Outcome::Miss) {
		bucket.m_iMisses++;
		bucket.m_bLocked = false;
		shot.m_bResolverMiss = true;
		Features::AnimSys.m_arrEntries.at(index - 1).m_iMissedShots++;
		bucket.m_iEvidenceTick = Interfaces::Globals->iTickCount;

		if (shot.m_iResolverPhase >= 0 && static_cast<int>(bucket.m_ePhase) == shot.m_iResolverPhase) {
			bucket.m_ePhase = NextPhase(CanonicalBucketPattern(pattern), bucket.m_ePhase);
			st.m_iLastFiredPhase = static_cast<int>(bucket.m_ePhase);
		}
	}
}
// One simulation time drop ( a "shift" ) can come from many places. Only defensive ones are kept:
//   doubletap    the drop lines up with a shot ( teleport after firing )
//   noise        1 tick clock correction, absurd sizes, our own packet loss / choke,
//                or several players dropping on the same tick ( that is our connection, not theirs )
//   defensive    everything else; angles that jump away from the last clean tick are direct evidence
CResolverBrain::EShiftKind CResolverBrain::ClassifyShift(ResolverPlayer_t& st, int index, int shift, int tick, int window, bool& angleEvidence) {
	angleEvidence = false;

	++st.m_iDbgShifts;
	st.m_iDbgLastShift = shift;
	st.m_bDbgLastAngles = false;

	if (tick - st.m_iLastShotTick <= window) {
		++st.m_iDbgDoubletap;
		return EShiftKind::Doubletap;
	}

	if (!DefTune::Enabled())
		return EShiftKind::Noise;

	if (shift < DefTune::MinShift() || shift > DefTune::MaxShift()) {
		++st.m_iDbgSize;
		return EShiftKind::Noise;
	}

	// our own connection dropping / choking packets makes everyone look shifted
	if (const auto nci{ Interfaces::Engine->GetNetChannelInfo() }) {
		const float tolerance{ DefTune::NetTolerance() };
		if (tolerance < 1.f && (nci->GetAvgLoss(FLOW_INCOMING) > tolerance || nci->GetAvgChoke(FLOW_INCOMING) > tolerance)) {
			++st.m_iDbgNetwork;
			return EShiftKind::Noise;
		}
	}

	// another player shifted on ( nearly ) the same tick: network hiccup, undo theirs too
	bool simultaneous{};
	for (int other{ 1 }; DefTune::FilterSimultaneous() && other <= 64; ++other) {
		if (other == index)
			continue;

		const int otherTick{ m_arrLastShiftTick[other] };
		if (otherTick > 0 && std::abs(tick - otherTick) <= RageTuning::SimultaneousShiftTicks) {
			simultaneous = true;
			ForgetShift(Get(other), otherTick);
		}
	}
	m_arrLastShiftTick[index] = tick;

	if (simultaneous) {
		++st.m_iDbgSimultaneous;
		return EShiftKind::Noise;
	}

	// defensive anti-aim flicks pitch / yaw only inside the shifted ticks
	if (st.m_bHasCleanAngles && st.m_pPlayer) {
		const float pitch{ Normalize180(st.m_pPlayer->m_angEyeAngles().x) };
		const float yaw{ Normalize180(st.m_pPlayer->m_angEyeAngles().y) };
		// yaw against both of their last clean yaws: a jitter swap ( 180 between ticks ) is not a defensive flick
		const float yawJump{ std::min(std::abs(Normalize180(yaw - st.m_flCleanYaw)), std::abs(Normalize180(yaw - st.m_flCleanYawPrev))) };
		angleEvidence = std::abs(pitch - st.m_flCleanPitch) >= DefTune::PitchJump()
			|| yawJump >= DefTune::YawJump();
	}

	st.m_bDbgLastAngles = angleEvidence;

	if (DefTune::NeedAngles() && !angleEvidence) {
		++st.m_iDbgNoAngles;
		return EShiftKind::Noise;
	}

	++st.m_iDbgDefensive;
	return EShiftKind::Defensive;
}

// a shift that turned out to be noise: take it back out of the confirmation history
void CResolverBrain::ForgetShift(ResolverPlayer_t& st, int tick) {
	if (!st.m_dqShiftTicks.empty() && st.m_dqShiftTicks.back() == tick) {
		st.m_dqShiftTicks.pop_back();
		st.m_bAngleEvidence = false;
		st.m_bPushEvidence = false;
	}

	if (st.m_iDefensiveStart == tick) {
		st.m_iDefensiveUntil = -1;
		st.m_bInShift = false;
	}

	st.m_bDefensiveUser = (st.m_bAngleEvidence && DefTune::AngleConfirms()) || (st.m_bPushEvidence && DefTune::PushConfirms())
		|| static_cast<int>(st.m_dqShiftTicks.size()) >= DefTune::ConfirmShifts();
}

void CResolverBrain::NoteOutcome(int index, bool hit, bool resolverMiss) {
	if (index <= 0 || index > 64)
		return;

	auto& st{ Get(index) };
	if (hit)
		st.m_iMissStreak = 0;
	else if (resolverMiss)
		++st.m_iMissStreak;
}

bool CResolverBrain::IsFlickRecord(int index, const LagRecord_t& record) const {
	const auto& st{ Get(index) };
	const float halfTick{ Interfaces::Globals->flIntervalPerTick * 0.5f };

	for (const auto& sample : st.m_dqFlickRecords) {
		if (record.m_iReceiveTick == sample.second
			&& std::abs(sample.first - record.m_cAnimData.m_flSimulationTime) < halfTick)
			return true;
	}

	if (st.m_bSuspectFlick
		&& record.m_iReceiveTick == st.m_iSuspectReceiveTick
		&& std::abs(st.m_flSuspectSimTime - record.m_cAnimData.m_flSimulationTime) < halfTick)
		return true;

	return false;
}

bool CResolverBrain::IsDefensiveRecord(int index, const LagRecord_t& record) const {
	const auto& st{ Get(index) };
	const float halfTick{ Interfaces::Globals->flIntervalPerTick * 0.5f };

	for (const auto& sample : st.m_dqDefensiveSimTimes) {
		if (record.m_iReceiveTick == sample.second
			&& std::abs(sample.first - record.m_cAnimData.m_flSimulationTime) < halfTick)
			return true;
	}

	return false;
}

// only the shifted ticks themselves are unshootable; records from the real ticks in between stay valid
bool CResolverBrain::ShouldHoldFire(int index) const {
	const auto& st{ Get(index) };
	return RageTuning::HoldFireOnDefensive && st.m_bInShift
		&& Interfaces::Globals->iTickCount <= st.m_iDefensiveUntil
		&& st.m_iDefensiveShift > 0 && st.m_iDefensiveShift <= DefTune::MaxShift();
}

bool CResolverBrain::InDefensiveShift(int index) const {
	return Get(index).m_bInShift;
}

bool CResolverBrain::IsUnreliable(int index) const {
	const auto& st{ Get(index) };
	// a defensive user's clean records are exact ( shifted ones are never stored or shot ), and the lc break a shift
	// causes is handled by shooting their newest record: neither is a reason to demand a safer head point
	return st.m_bDoubletapShift || (st.m_bLagCompBroken && !st.m_bDefensiveUser);
}

bool CResolverBrain::ShouldPreferBody(int index, const LagRecord_t& record) {
	// unreliable ( defensive / lc / double tap ) no longer forces body: HeadSafetyRequired asks for a safer head point instead

	// head flickers move the head every few ticks: after a resolver miss go for the body, which barely moves
	const auto& streak{ Get(index) };
	if ((streak.m_bFlicker && streak.m_iMissStreak >= 1) || streak.m_iMissStreak >= 3)
		return true;

	if (!(record.m_cAnimData.m_iFlags & FL_ONGROUND))
		return true;
	if (EvidenceSafety(index, record) >= DESYNC_MATRIX_COUNT)
		return true;

	const EAimPattern pattern{ PatternFor(index, record) };
	const auto& bucket{ BucketFor(index, record) };
	const bool jitterOrCrouch{ pattern == EAimPattern::Jitter
		|| pattern == EAimPattern::JitterPositive
		|| pattern == EAimPattern::JitterNegative
		|| pattern == EAimPattern::Crouch };

	const int threshold{ jitterOrCrouch ? 1 : RageTuning::PreferBodyAfterMisses };
	return bucket.m_iMisses >= threshold;
}

bool CResolverBrain::ShouldForceBody(int index, const LagRecord_t& record) {
	// Ambiguous orientation must not permanently suppress available headshots.
	return false;
}

int CResolverBrain::EvidenceSafety(int index, const LagRecord_t& record) {
	if (!record.m_bMultiMatrix) return 1;
	const auto& bucket{ BucketFor(index, record) };
	std::array<bool, RESOLVER_SIDE_COUNT> available{};
	for (int side{}; side < RESOLVER_SIDE_COUNT; ++side)
		available[side] = record.m_cAnimData.m_arrSides[side].m_bFilled;
	const auto decision{ ResolverRules::Decide(bucket.m_arrConfidence, available,
		HardEvidenceSide(index, record, PatternFor(index, record))) };
	if (decision.conflict) return DESYNC_MATRIX_COUNT;
	return decision.confident ? 1 : 2;
}

int CResolverBrain::HeadSafetyRequired(int index, const LagRecord_t& record) {
	if (!record.m_bMultiMatrix)
		return 1;

	const EAimPattern pattern{ PatternFor(index, record) };
	const auto& bucket{ BucketFor(index, record) };
	const bool isJitter{ pattern == EAimPattern::Jitter
		|| pattern == EAimPattern::JitterPositive
		|| pattern == EAimPattern::JitterNegative };

	// step 11: the less we trust the side, the more desync sides the head point must be inside
	int required{ EvidenceSafety(index, record) };
	if (!(record.m_cAnimData.m_iFlags & FL_ONGROUND))
		required = std::max(required, 2);

	if (RageTuning::DefensiveBodyAim && IsUnreliable(index))
		required = std::max(required, 2);

	// Record validity and orientation confidence are independent. Defensive-user
	// status alone does not override the evidence required for a clean record.

	if (!bucket.m_bLocked && bucket.m_iMisses >= 1)
		required = std::max(required, 2);

	if (!bucket.m_bLocked && bucket.m_iMisses >= 2 && !isJitter)
		required = std::max(required, DESYNC_MATRIX_COUNT);

	if (isJitter && !bucket.m_bLocked && bucket.m_iMisses >= 2)
		required = std::max(required, DESYNC_MATRIX_COUNT);

	return required;
}

/*
 * RAGEBOT
 */

int CRageBot::ResolveRecordSide(PlayerEntry& entry, const LagRecord_t& record, int index) {
	if (!record.m_bMultiMatrix)
		return RESOLVER_SIDE_CENTER;

	// resolver off means off: script sides ( plist "Force body yaw" ) are ignored too
	if (!Config::Get<bool>(Vars.RagebotResolver))
		return RESOLVER_SIDE_CENTER;

	// lua player list: "Force body yaw" / "Correction active"
	// a script side that keeps missing gives way to our own resolver ( it learns from the misses )
	if (const int luaSide{ Lua::ResolverSideOverride(index) }; luaSide >= 0 && m_cBrain.Get(index).m_iMissStreak < 2)
		return luaSide;

	return m_cBrain.SideForRecord(index, record);
}

void CRageBot::Main(CUserCmd& cmd, bool shoot) {
	m_bShouldStop = false;

	if (m_vecSpreadSeeds.empty())
		PrecomputeSeeds();

	ctx.m_strDbgLogs.clear();

	if (!ctx.m_pLocal || !ctx.m_pWeapon || !ctx.m_pWeaponData)
		return;

	if (!Config::Get<bool>(Vars.RagebotEnable)
		|| (ctx.m_pWeaponData->nWeaponType >= WEAPONTYPE_C4 && ctx.m_pWeapon->m_iItemDefinitionIndex() != WEAPON_TASER)
		|| ctx.m_pLocal->m_MoveType() == MOVETYPE_LADDER)
		return;

	ParseCfgItems(ctx.m_pWeaponData->nWeaponType);

	if (ctx.m_pWeapon->IsKnife()) {
		if (Config::Get<bool>(Vars.RagebotKnifebot) && shoot)
			KnifeBot(cmd);
		return;
	}
	else if (ctx.m_pWeapon->m_iItemDefinitionIndex() == WEAPON_TASER && !Config::Get<bool>(Vars.RagebotZeusbot))
		return;

	if (m_vecHitboxes.empty())
		return;

	if (!m_pBestTarget)
		m_pBestTarget = std::make_unique< AimTarget_t >();
	if (!m_pScratchTarget)
		m_pScratchTarget = std::make_unique< AimTarget_t >();

	m_pBestTarget->Reset();

	const bool resolver{ Config::Get<bool>(Vars.RagebotResolver) };
	const int maxClients{ std::min< int >(Interfaces::Globals->nMaxClients, 64) };

	for (int i{ 1 }; i <= maxClients; ++i) {
		const auto player{ static_cast<CBasePlayer*>(Interfaces::ClientEntityList->GetClientEntity(i)) };
		if (!player
			|| player == ctx.m_pLocal
			|| player->IsDormant()
			|| player->IsDead()
			|| player->m_bGunGameImmunity()
			|| player->IsTeammate()
			|| Lua::Whitelisted(i))
			continue;

		auto& entry{ Features::AnimSys.m_arrEntries.at(i - 1) };
		if (entry.m_pPlayer != player
			|| entry.m_pRecords.empty()) {
			ctx.m_strDbgLogs.emplace_back(std::make_shared< std::string >("ZE << " + PlayerName(player)));
			continue;
		}

		// always track: defensive / shot history must stay continuous even while the target is out of fov
		m_cBrain.Update(entry, i);
		CRASH_MARK("Rage: lag compensation prediction begin");
		Features::LagCompPredictor.Update(i, entry);
		CRASH_MARK("Rage: lag compensation prediction end");

		const auto fov{ Math::GetFov(ctx.m_angOriginalViewangles, Math::CalcAngle(ctx.m_vecEyePos, player->GetAbsOrigin())) };
		if (fov > MenuVars.RagebotFOV)
			continue;

		// inside a defensive shift we still shoot the real records from between the shifts
		// GetBestLagRecord filters shifted records and can use the server-time fallback.
		if (resolver && m_cBrain.ShouldHoldFire(i))
			ctx.m_strDbgLogs.emplace_back(std::make_shared< std::string >("DF ( between ) << " + PlayerName(player)));

		auto& target{ *m_pScratchTarget };
		target.Reset();
		target.m_pPlayer = player;

		CRASH_MARK("Rage: GetBestLagRecord begin");
		target.GetBestLagRecord(entry);
		CRASH_MARK("Rage: GetBestLagRecord end");

		{
			auto& debug{ Debug(i) };
			debug = {};
			debug.m_iTick = Interfaces::Globals->iTickCount;

			if (target.m_pRecord) {
				debug.m_strStatus = "OK";
				debug.m_iRecordAge = Interfaces::Globals->iTickCount - target.m_pRecord->m_iReceiveTick;
				debug.m_iValidity = target.m_bExtrapolating ? 1 : target.m_pRecord->Validity();
				debug.m_bExtrapolated = target.m_bExtrapolating;
				debug.m_iExtrapTicks = target.m_iExtrapTicks;
				debug.m_iSpread = static_cast<int>(target.m_flExtrapSpread);
				debug.m_iDamage = static_cast<int>(target.m_iBestDamage);
			}
			else {
				debug.m_strStatus = target.m_pDbgLog && !target.m_pDbgLog->empty() ? *target.m_pDbgLog : std::string("NO");
				if (!entry.m_pRecords.empty() && entry.m_pRecords.back()) {
					debug.m_iRecordAge = Interfaces::Globals->iTickCount - entry.m_pRecords.back()->m_iReceiveTick;
					debug.m_iValidity = entry.m_pRecords.back()->Validity();
				}
			}
		}

		if (!target.m_pRecord) {
			if (target.m_pDbgLog)
				target.m_pDbgLog->append(" << " + PlayerName(player));

			continue;
		}

		if (!m_pBestTarget->m_pPlayer || target.IsBetterTarget(*m_pBestTarget))
			std::swap(m_pBestTarget, m_pScratchTarget);
	}

	auto& best{ *m_pBestTarget };
	if (!best.m_pPlayer || !best.m_pRecord)
		return;

	best.SetDbg(_("ST"));

	CRASH_MARK("Rage: ScanTarget begin");
	best.ScanTarget(m_vecHitboxes);
	CRASH_MARK("Rage: ScanTarget end");

	const bool couldFire{ ctx.m_bCanShoot };
	if (best.m_cPoint.m_bValid && shoot) {
		best.SetDbg(_("HC"));
		best.Fire(cmd);
	}

	{
		auto& debug{ Debug(best.m_pPlayer->Index()) };
		debug.m_bTarget = true;
		debug.m_bFired = shoot && best.m_cPoint.m_bValid && couldFire && (cmd.iButtons & IN_ATTACK);
		if (best.m_cPoint.m_bValid)
			debug.m_iDamage = best.m_cPoint.m_flDamage;
		debug.m_strStatus = debug.m_bFired ? std::string("FIRE")
			: !couldFire && best.m_cPoint.m_bValid ? std::string("WAIT ( weapon / dt )")
			: best.m_pDbgLog && !best.m_pDbgLog->empty() ? *best.m_pDbgLog : std::string("ST");
	}

	best.m_pDbgLog->append(" << " + PlayerName(best.m_pPlayer));
}

Vector2D CRageBot::CalcSpreadAngle(const int itemIndex, const float recoilIndex, float one, float twoPI) {
	SpreadSeed_t seed{};
	seed.m_flRand1 = one;
	seed.m_flRandPi1 = twoPI;
	seed.m_flRand2 = one;
	seed.m_flRandPi2 = twoPI;

	return CalcSpread(itemIndex, recoilIndex, seed);
}

Vector2D CRageBot::CalcSpread(const int itemIndex, const float recoilIndex, const SpreadSeed_t& seed) {
	float inaccuracyDensity{ seed.m_flRand1 };
	float spreadDensity{ seed.m_flRand2 };

	if (itemIndex == WEAPON_REVOLVER) {
		inaccuracyDensity = 1.f - inaccuracyDensity * inaccuracyDensity;
		spreadDensity = 1.f - spreadDensity * spreadDensity;
	}
	else if (itemIndex == WEAPON_NEGEV && recoilIndex < 3.f) {
		for (int j{ 3 }; j > recoilIndex; --j) {
			inaccuracyDensity *= inaccuracyDensity;
			spreadDensity *= spreadDensity;
		}

		inaccuracyDensity = 1.f - inaccuracyDensity;
		spreadDensity = 1.f - spreadDensity;
	}

	const float inaccuracy{ inaccuracyDensity * Features::EnginePrediction.Inaccuracy };
	const float spread{ spreadDensity * Features::EnginePrediction.Spread };

	return { std::cos(seed.m_flRandPi1) * inaccuracy + std::cos(seed.m_flRandPi2) * spread,
			 std::sin(seed.m_flRandPi1) * inaccuracy + std::sin(seed.m_flRandPi2) * spread };
}

/*
 * AIM TARGET
 */

void AimTarget_t::Reset() {
	m_flCBBMaxz = 0.f;
	m_flCBBTime = 0.f;
	m_flViewOffsetZ = 0.f;
	m_hGroundEntity = {};

	m_pPlayer = nullptr;

	m_iBestDamage = 0;
	m_iResolverSide = RESOLVER_SIDE_CENTER;
	m_iRequiredHeadSafety = 1;

	m_bAbuseRecord = false;
	m_bExtrapolating = false;
	m_vecExtrapOffsets.clear();
	m_iExtrapTicks = 0;
	m_flExtrapSpread = 0.f;
	m_bYawComputed = false;
	m_bCBBReady = false;
	m_bPreferSafe = false;
	m_bPreferSafeLethal = false;
	m_flYaw = -1.f;

	m_cPoint = {};

	m_pDbgLog = nullptr;
	m_pRecord = nullptr;
}

void AimTarget_t::SetDbg(const std::string& text) {
	if (!m_pDbgLog)
		m_pDbgLog = ctx.m_strDbgLogs.emplace_back(std::make_shared< std::string >(""));

	*m_pDbgLog = text;
}

int AimTarget_t::BoneCount() const {
	if (!m_pPlayer)
		return 0;

	return std::clamp(static_cast<int>(m_pPlayer->m_CachedBoneData().Size()), 0, 256);
}

int AimTarget_t::LatencyTicks(PlayerEntry& entry) const {
	int delay{};
	if (ctx.m_bFakeDucking)
		delay = 15 - Interfaces::ClientState->nChokedCommands;

	int latency{ ctx.m_iRealOutLatencyTicks + (Interfaces::Globals->iTickCount - entry.m_iLastRecievedTick) + delay };
	if (ctx.m_pLocal->Index() < m_pPlayer->Index())
		latency -= 1;

	return std::max(0, latency);
}

float AimTarget_t::PredictYaw(PlayerEntry& entry) {
	if (m_bYawComputed)
		return m_flYaw;

	m_bYawComputed = true;
	m_flYaw = entry.m_flPreviousYaws.size() >= 2 ? THIS.ExtrapolateYaw(entry.m_flPreviousYaws, LatencyTicks(entry)) : -1.f;
	return m_flYaw;
}

bool AimTarget_t::IsBetterTarget(const AimTarget_t& current) const {
	if (!current.m_pPlayer || !current.m_pRecord)
		return true;

	switch (Config::Get<int>(Vars.RagebotTargetSelection))
	{
	case 0: // highest damage
		return m_iBestDamage > current.m_iBestDamage;
	case 1: // fov
	{
		const auto mine{ Math::GetFov(ctx.m_angOriginalViewangles, Math::CalcAngle(ctx.m_vecEyePos, m_pPlayer->GetAbsOrigin())) };
		const auto theirs{ Math::GetFov(ctx.m_angOriginalViewangles, Math::CalcAngle(ctx.m_vecEyePos, current.m_pPlayer->GetAbsOrigin())) };
		return mine < theirs;
	}
	case 2: // lowest distance
	{
		const float mine{ m_pRecord->m_cAnimData.m_vecOrigin.DistTo(ctx.m_pLocal->m_vecOrigin()) };
		const float theirs{ current.m_pRecord->m_cAnimData.m_vecOrigin.DistTo(ctx.m_pLocal->m_vecOrigin()) };
		return mine < theirs;
	}
	case 3: // lowest health
		return m_pPlayer->m_iHealth() < current.m_pPlayer->m_iHealth();
	default:
		return m_iBestDamage > current.m_iBestDamage;
	}
}

int AimTarget_t::HitChance(const QAngle& ang, int hitchance) {
	const auto& seeds{ THIS.m_vecSpreadSeeds };
	const int total{ static_cast<int>(seeds.size()) };
	if (total <= 0)
		return 0;

	Vector fwd{}, right{}, up{};
	Math::AngleVectors(ang, &fwd, &right, &up);

	const int needed{ static_cast<int>(std::ceil(static_cast<float>(hitchance) / 100.f * total)) };
	const int itemIndex{ ctx.m_pWeapon->m_iItemDefinitionIndex() };
	const float recoilIndex{ ctx.m_pWeapon->m_flRecoilIndex() };

	// predicted lag-comp breaker: a seed only counts if it also hits wherever else they could be
	const auto hitboxSet{ !m_vecExtrapOffsets.empty() ? m_pPlayer->m_pStudioHdr()->pStudioHdr->GetHitboxSet(m_pPlayer->m_nHitboxSet()) : nullptr };

	int hits{};
	for (int i{}; i < total; ++i) {
		const auto spread{ THIS.CalcSpread(itemIndex, recoilIndex, seeds[i]) };
		const auto dir{ (fwd + right * spread.x + up * spread.y).Normalized() };
		const auto end{ ctx.m_vecEyePos + dir * ctx.m_pWeaponData->flRange };

		Trace_t tr{};
		Interfaces::EngineTrace->ClipRayToEntity({ ctx.m_vecEyePos, end }, MASK_SHOT_HULL | CONTENTS_HITBOX, m_pPlayer, &tr);

		bool hit{ tr.pHitEntity == m_pPlayer
			&& (!THIS.MenuVars.RagebotHitchanceThorough || tr.iHitGroup == m_cPoint.m_iHitgroup) };

		if (hit && hitboxSet) {
			const auto hitbox{ hitboxSet->GetHitbox(tr.iHitbox) };
			if (!hitbox)
				hit = false;
			else {
				const auto& bone{ m_pMatrices[m_iResolverSide][hitbox->iBone] };
				for (const auto& offset : m_vecExtrapOffsets) {
					if (!IntersectHitboxMoved(end, hitbox, bone, offset)) {
						hit = false;
						break;
					}
				}
			}
		}

		if (hit)
			++hits;

		// passed: stop tracing. a failing shot is traced to the end so the debug readout is the real chance
		// ( the old early exit always reported needed - 1, e.g. a flat "69 / 70", whatever the chance really was )
		if (hits >= needed)
			return std::max(hitchance, static_cast<int>(hits * 100.f / total));
	}

	return static_cast<int>(hits * 100.f / total);
}

void AimTarget_t::DrawHitMatrix() {
	if (!Config::Get<bool>(Vars.MiscHitMatrix))
		return;

	if (Config::Get<int>(Vars.MiscShotVisualizationType) == 0) {
		Features::Visuals.Chams.AddHitmatrix(m_pPlayer, m_pPlayer->m_CachedBoneData().Base());
		return;
	}

	const auto hdr{ Interfaces::ModelInfo->GetStudioModel(m_pPlayer->GetModel()) };
	if (!hdr)
		return;

	const auto hitboxSet{ hdr->GetHitboxSet(m_pPlayer->m_nHitboxSet()) };
	if (!hitboxSet)
		return;

	auto matrix{ m_pMatrices[m_iResolverSide] };
	const auto& color{ Config::Get<Color>(Vars.MiscHitMatrixCol) };
	const auto duration{ Config::Get<float>(Vars.MiscHitMatrixTime) };
	const auto xqz{ Config::Get<bool>(Vars.MiscHitMatrixXQZ) };

	for (int i{}; i < hitboxSet->nHitboxes; ++i) {
		const auto hitbox{ hitboxSet->GetHitbox(i) };
		if (!hitbox)
			continue;

		if (hitbox->flRadius <= 0.f) {
			matrix3x4_t transform{};
			transform.SetAngles(hitbox->angOffsetOrientation.y, hitbox->angOffsetOrientation.x, hitbox->angOffsetOrientation.z);

			const auto angles{ MatrixGetAngles(matrix[hitbox->iBone] * transform) };
			if (xqz)
				Interfaces::DebugOverlay->AddBoxOverlay2(matrix[hitbox->iBone].GetOrigin(), hitbox->vecBBMin, hitbox->vecBBMax, angles, { 0, 0, 0, 0 }, color, duration);
			else
				Interfaces::DebugOverlay->AddBoxOverlay(matrix[hitbox->iBone].GetOrigin(), hitbox->vecBBMin, hitbox->vecBBMax, angles, color[0], color[1], color[2], 0, duration);
			continue;
		}

		const auto min{ Math::VectorTransform(hitbox->vecBBMin, matrix[hitbox->iBone]) };
		const auto max{ Math::VectorTransform(hitbox->vecBBMax, matrix[hitbox->iBone]) };

		Interfaces::DebugOverlay->AddCapsuleOverlay(min, max, hitbox->flRadius, color[0], color[1], color[2], color[3], duration, 0, xqz);
	}
}

void AimTarget_t::Fire(CUserCmd& cmd) {
	if (!ctx.m_bCanShoot)
		return;

	const auto backup{ std::make_unique< LagBackup_t >(m_pPlayer) };
	const int index{ m_pPlayer->Index() };

	m_pRecord->Apply(m_pPlayer, m_iResolverSide);
	std::memcpy(m_pPlayer->m_CachedBoneData().Base(), m_pMatrices[m_iResolverSide], BoneCount() * sizeof(matrix3x4_t));

	QAngle angle{};
	Math::VectorAngles(m_cPoint.m_vecPoint - ctx.m_vecEyePos, angle);

	const auto state{ ctx.m_pLocal->m_pAnimState() };
	if (state && state->m_pPlayer && (state->m_bLanding || state->flDuckAmount || ctx.m_pLocal->m_hGroundEntity() == -1)) {
		ctx.m_vecEyePos = ctx.m_pLocal->GetEyePosition(angle.y, angle.x);
		Math::VectorAngles(m_cPoint.m_vecPoint - ctx.m_vecEyePos, angle);
	}

	const bool sniper{ ctx.m_pWeaponData->nWeaponType == WEAPONTYPE_SNIPER };
	const int hitchance{ ctx.m_pWeapon->m_iItemDefinitionIndex() == WEAPON_TASER ? 80 :
		(ctx.m_pWeapon->m_zoomLevel() || !sniper) ? THIS.MenuVars.RagebotHitchance : THIS.MenuVars.RagebotNoscopeHitchance };

	auto chance{ HitChance(angle, hitchance) };

	// the multipoint sits near the hitbox edge, so about half of the spread cone falls off it: standing scoped with
	// inaccuracy 3 still read 44-79 % ( combined_1792534999, median 53 ). before giving up, slide the point toward
	// the hitbox center and take the first one that passes ( or the best one ), as long as nothing blocks it
	if (chance < hitchance) {
		const auto set{ m_pPlayer->m_pStudioHdr() ? m_pPlayer->m_pStudioHdr()->pStudioHdr->GetHitboxSet(m_pPlayer->m_nHitboxSet()) : nullptr };
		mstudiobbox_t* box{};
		float bestDist{ FLT_MAX };
		for (int i{}; set && i < set->nHitboxes; ++i) {
			const auto hb{ set->GetHitbox(i) };
			if (!hb || hb->iGroup != m_cPoint.m_iHitgroup)
				continue;
			const auto c{ Math::VectorTransform((hb->vecBBMin + hb->vecBBMax) * 0.5f, m_pMatrices[m_iResolverSide][hb->iBone]) };
			const float dist{ (c - m_cPoint.m_vecPoint).Length() };
			if (dist < bestDist) {
				bestDist = dist;
				box = hb;
			}
		}

		if (box) {
			const auto center{ Math::VectorTransform((box->vecBBMin + box->vecBBMax) * 0.5f, m_pMatrices[m_iResolverSide][box->iBone]) };
			for (const float t : { 0.5f, 1.f }) {
				const Vector candidate{ m_cPoint.m_vecPoint + (center - m_cPoint.m_vecPoint) * t };

				// only a point we can see directly: a wall between us and it means a different damage path
				CTraceFilter filter{ ctx.m_pLocal };
				CGameTrace trace{};
				Interfaces::EngineTrace->TraceRay({ ctx.m_vecEyePos, candidate }, MASK_SHOT, &filter, &trace);
				if (trace.pHitEntity != m_pPlayer && trace.flFraction < 0.97f)
					continue;

				QAngle candidateAngle{};
				Math::VectorAngles(candidate - ctx.m_vecEyePos, candidateAngle);
				const int candidateChance{ HitChance(candidateAngle, hitchance) };
				if (candidateChance > chance) {
					chance = candidateChance;
					angle = candidateAngle;
					m_cPoint.m_vecPoint = candidate;
				}
				if (chance >= hitchance)
					break;
			}
		}
	}

	if (chance < hitchance) {
		// real chance + why: zoom level and current inaccuracy ( x1000 ). scoped standing scout is ~1-3, moving / unscoped is 20+
		SetDbg(_("HC[") + std::to_string(chance) + "/" + std::to_string(hitchance)
			+ " z" + std::to_string(ctx.m_pWeapon->m_zoomLevel())
			+ " inacc " + std::to_string(static_cast<int>((Features::EnginePrediction.Inaccuracy + Features::EnginePrediction.Spread) * 1000.f)) + "]");

		if (THIS.MenuVars.RagebotAutoScope && !ctx.m_pWeapon->m_zoomLevel() && sniper)
			cmd.iButtons |= IN_ATTACK2;

		backup->Apply(m_pPlayer);
		return;
	}

	if (THIS.MenuVars.RagebotAutoFire)
		cmd.iButtons |= IN_ATTACK;

	if (cmd.iButtons & IN_ATTACK) {
		cmd.iButtons &= ~IN_ATTACK2;
		cmd.iButtons &= ~IN_USE;

		cmd.viewAngles = angle - ctx.m_pLocal->m_aimPunchAngle() * Displacement::Cvars.weapon_recoil_scale->GetFloat();

		if (m_bAbuseRecord)
			cmd.iTickCount = 0;
		else if (m_bExtrapolating) {
			// the server ends up on their current position whatever we ask for ( it can't backtrack past the break ),
			// but if they slowed down and it can, ask for the newest time it still accepts
			const float correct{ std::clamp(ctx.m_flOutLatency + ctx.m_flLerpTime, 0.f, std::min(Displacement::Cvars.sv_maxunlag->GetFloat(), 0.2f)) };
			const float base{ ctx.m_flFixedCurtime - correct };
			const float target{ std::clamp(m_pRecord->m_cAnimData.m_flSimulationTime, base - 0.18f, base + 0.18f) };
			cmd.iTickCount = TIME_TO_TICKS(target + ctx.m_flLerpTime);
		}
		else
			cmd.iTickCount = TIME_TO_TICKS(m_pRecord->m_cAnimData.m_flSimulationTime + ctx.m_flLerpTime);

		if (!THIS.MenuVars.RagebotSilentAim)
			Interfaces::Engine->SetViewAngles(angle);

		auto& entry{ Features::AnimSys.m_arrEntries.at(index - 1) };
		const bool resolver{ Config::Get<bool>(Vars.RagebotResolver) };

		const bool unreliable{ m_bAbuseRecord || m_bExtrapolating || (resolver && THIS.m_cBrain.IsUnreliable(index)) };

		CBBShit_t cbb{};
		cbb.m_flCBBMaxz = m_flCBBMaxz;
		cbb.m_flCBBTime = m_flCBBTime;
		cbb.m_flViewOffsetZ = m_flViewOffsetZ;
		cbb.m_hGroundEntity = m_hGroundEntity;

		Features::Shots.AddShot(m_pPlayer, m_pRecord,
			m_cPoint.m_iHitgroup, m_cPoint.m_iDesyncIntersections >= DESYNC_MATRIX_COUNT, m_pMatrices[m_iResolverSide],
			m_iResolverSide, ctx.m_vecEyePos, m_cPoint.m_vecPoint, m_flYaw, m_bExtrapolating, cbb);

		{
			auto& added{ Features::Shots.m_vecShots.back() };
			added.m_bAfterShift = Features::Exploits.ShiftUnacknowledged();
			added.m_strTarget = PlayerName(m_pPlayer);
			added.m_iBacktrack = Interfaces::Globals->iTickCount - m_pRecord->m_iReceiveTick;
			added.m_iExtrapTicks = m_bExtrapolating ? m_iExtrapTicks : 0;
			added.m_iExtrapSpread = m_bExtrapolating ? static_cast<int>(m_flExtrapSpread) : 0;
			added.m_iPredDamage = m_cPoint.m_flDamage;
			added.m_iHitchance = chance;
		}

		if (resolver)
			THIS.m_cBrain.OnFire(Features::Shots.m_vecShots.back(), unreliable);

		Lua::OnAimFire(Features::Shots.m_vecShots.back(), index, chance, m_cPoint.m_iHitgroup, static_cast<int>(m_cPoint.m_flDamage),
			Interfaces::Globals->iTickCount - m_pRecord->m_iReceiveTick, m_bExtrapolating, m_cPoint.m_vecPoint);

		DrawHitMatrix();

		std::string message{
			std::string(_("shot ")) + PlayerName(m_pPlayer) +
			_(" | hitgroup: ") + THIS.HitgroupToString(m_cPoint.m_iHitgroup) +
			_(" | pred damage: ") + std::to_string(m_cPoint.m_flDamage) +
			_(" | hitchance: ") + std::to_string(chance) +
			_(" | backtrack: ") + std::to_string(Interfaces::Globals->iTickCount - m_pRecord->m_iReceiveTick) + _(" ticks") +
			_(" | diff: ") + std::to_string((Interfaces::ClientState->iServerTick + ctx.m_iRealOutLatencyTicks) - TIME_TO_TICKS(ctx.m_flFixedCurtime)) + _(" ticks") +
			_(" | intersections: ") + std::to_string(m_cPoint.m_iDesyncIntersections) +
			_(" | CBB safety: ") + std::to_string(m_bCBBReady ? static_cast<int>(static_cast<float>(m_cPoint.m_iCBBIntersections) / static_cast<float>(CBB_MATRIX_COUNT) * 100.f) : 0) +
			_(" | pred: ") + std::to_string(m_flYaw != -1.f) +
			_(" | side: ") + std::to_string(m_iResolverSide) };

		if (resolver && m_pRecord->m_bMultiMatrix) {
			const auto pattern{ THIS.m_cBrain.PatternFor(index, *m_pRecord) };
			const auto& bucket{ THIS.m_cBrain.BucketFor(index, *m_pRecord) };
			message += std::string(" | ") + CResolverBrain::PatternName(pattern) + "/" + CResolverBrain::PhaseName(bucket.m_ePhase) + "(" + std::to_string(m_iResolverSide) + ")";
		}

		if (m_bAbuseRecord)
			message += " | AB";
		if (m_bExtrapolating)
			message += " | EX +" + std::to_string(m_iExtrapTicks) + "t / " + std::to_string(static_cast<int>(m_flExtrapSpread)) + "u";
		if (unreliable)
			message += " | UNR";

		Features::Logger.Log(message, false);
	}

	backup->Apply(m_pPlayer);
}

void AimTarget_t::SetupMatrices(std::shared_ptr< LagRecord_t > record, float yaw, bool buildCBB) {
	record->Apply(m_pPlayer);

	const int bones{ BoneCount() };

	for (int i{}; i < RESOLVER_SIDE_COUNT; ++i) {
		// records without low delta data ( older / extrapolated ) fall back to their parent side
		const auto& side{ record->m_cAnimData.m_arrSides.at(i) };
		const auto& source{ side.m_bFilled || i < DESYNC_MATRIX_COUNT ? side : record->m_cAnimData.m_arrSides.at(ParentSide(i)) };
		std::memcpy(m_pMatrices[i], source.m_pMatrix, bones * sizeof(matrix3x4_t));
	}

	m_bCBBReady = false;
	if (!buildCBB)
		return;

	// yaw safety: rebuild the resolved skeleton rotated to every eye yaw the enemy could have when the shot lands
	const float recordYaw{ record->m_angEyeAngles.y };
	const float center{ yaw == -1.f ? recordYaw : yaw };
	const auto& origin{ record->m_cAnimData.m_vecOrigin };
	const int side{ std::clamp(static_cast<int>(m_iResolverSide), 0, RESOLVER_SIDE_COUNT - 1) };
	constexpr int half{ (CBB_MATRIX_COUNT - 1) / 2 };

	for (int i{}; i < CBB_MATRIX_COUNT; ++i) {
		const float candidate{ center + static_cast<float>(i - half) * RageTuning::CBBStep };
		const float delta{ Math::AngleDiff(recordYaw, candidate) };

		RotateBones(m_pMatrices[side], m_pMatrices[RESOLVER_SIDE_COUNT + i], bones, origin, delta);
	}

	m_bCBBReady = true;
}

int AimTarget_t::QuickScan(std::shared_ptr< LagRecord_t > record, const std::vector<int>& hitgroups, int side) {
	SetupMatrices(record, -1.f, false);

	side = std::clamp(side, 0, RESOLVER_SIDE_COUNT - 1);
	std::memcpy(m_pPlayer->m_CachedBoneData().Base(), m_pMatrices[side], BoneCount() * sizeof(matrix3x4_t));

	const auto hitboxSet{ m_pPlayer->m_pStudioHdr()->pStudioHdr->GetHitboxSet(m_pPlayer->m_nHitboxSet()) };
	if (!hitboxSet)
		return 0;

	int dmg{};

	for (const auto& hb : hitgroups) {
		const auto hitbox{ hitboxSet->GetHitbox(hb) };
		if (!hitbox)
			continue;

		const auto point{ Math::VectorTransform((hitbox->vecBBMin + hitbox->vecBBMax) / 2.f, m_pMatrices[side][hitbox->iBone]) };

		const auto data{ Features::Autowall.FireBullet(ctx.m_pLocal, m_pPlayer, ctx.m_pWeaponData,
			ctx.m_pWeapon->m_iItemDefinitionIndex() == WEAPON_TASER,
			ctx.m_vecEyePos, point, THIS.MenuVars.RagebotAutowall) };

		const int pointDamage{ static_cast<int>(data.dmg) };
		if (pointDamage < 1)
			continue;

		dmg = std::max(dmg, pointDamage);

		if (data.hitbox == HITBOX_HEAD)
			break;
	}

	return dmg;
}

void AimTarget_t::ScanTarget(std::vector<EHitboxIndex>& hitboxes) {
	const auto backup{ std::make_unique< LagBackup_t >(m_pPlayer) };

	const int index{ m_pPlayer->Index() };
	auto& entry{ Features::AnimSys.m_arrEntries.at(index - 1) };
	const int latency{ LatencyTicks(entry) };

	PredictYaw(entry);

	const auto backupViewOffsetZ{ entry.m_pPlayer->m_vecViewOffset().z };

	// simulates the new duck state / view offset before the record is applied
	if (entry.m_iLastNewCmds > 0)
		THIS.AdjustDuckingVars(entry, latency);

	m_flViewOffsetZ = entry.m_pPlayer->m_vecViewOffset().z;
	m_hGroundEntity = entry.m_pPlayer->m_hGroundEntity();

	SetupMatrices(m_pRecord, m_flYaw, true);

	entry.m_pPlayer->m_vecViewOffset().z = backupViewOffsetZ;

	std::memcpy(m_pPlayer->m_CachedBoneData().Base(), m_pMatrices[m_iResolverSide], BoneCount() * sizeof(matrix3x4_t));

	const bool resolver{ Config::Get<bool>(Vars.RagebotResolver) };

	// head safety off ( default ): the head point only has to be on the resolved side, nothing extra is demanded
	m_iRequiredHeadSafety = (resolver && Config::Get<bool>(Vars.RagebotHeadSafety)) ? THIS.m_cBrain.HeadSafetyRequired(index, *m_pRecord) : 0;

	// prefer safe point: always / target in air / after we already missed this player
	const bool targetInAir{ !(m_pRecord->m_cAnimData.m_iFlags & FL_ONGROUND) };
	const bool missedTarget{ entry.m_iMissedShots > 0 || (resolver && m_pRecord->m_bMultiMatrix && THIS.m_cBrain.BucketFor(index, *m_pRecord).m_iMisses > 0) };

	m_bPreferSafe = THIS.MenuVars.RagebotPreferSafe
		|| (THIS.MenuVars.RagebotPreferSafeInAir && targetInAir)
		|| (THIS.MenuVars.RagebotPreferSafeAfterMiss && missedTarget);
	m_bPreferSafeLethal = THIS.MenuVars.RagebotPreferSafeLethal;

	const auto forceBaim{ Config::Get<keybind_t>(Vars.RagebotForceBaimKey).enabled
		|| (THIS.MenuVars.RagebotForceBaimAfterX && entry.m_iMissedShots > THIS.MenuVars.RagebotForceBaimAfterXINT)
		|| (resolver && THIS.m_cBrain.ShouldForceBody(index, *m_pRecord)) };

	GetBestPoint(forceBaim, hitboxes);

	backup->Apply(m_pPlayer);
}

void AimTarget_t::GetBestPoint(bool forceBaim, std::vector<EHitboxIndex>& hitboxes) {
	const auto hitboxSet{ m_pPlayer->m_pStudioHdr()->pStudioHdr->GetHitboxSet(m_pPlayer->m_nHitboxSet()) };
	if (!hitboxSet)
		return;

	const int index{ m_pPlayer->Index() };
	const bool resolver{ Config::Get<bool>(Vars.RagebotResolver) };

	// lowest damage any point could be accepted with ( visible uses minimum, walls use penetration )
	int mindmg{ std::min(THIS.MenuVars.RagebotMinimumDamage, THIS.MenuVars.RagebotPenetrationDamage) };

	if (Config::Get<bool>(Vars.RagebotDamageOverride)
		&& Config::Get<keybind_t>(Vars.RagebotDamageOverrideKey).enabled)
		mindmg = THIS.MenuVars.RagebotOverrideDamage;
	else if (THIS.MenuVars.RagebotScaleDamage)
		mindmg = static_cast<int>(mindmg * (m_pPlayer->m_iHealth() / 100.f));

	// never ask for more than it takes to kill: min damage 101 on a 1 hp target only needs 1
	mindmg = std::min(mindmg, std::max(m_pPlayer->m_iHealth(), 1));

	const bool moving{ ctx.m_pLocal->m_vecVelocity().Length2D() > 5.f };

	for (const auto& hb : hitboxes) {
		const auto hitbox{ hitboxSet->GetHitbox(hb) };
		if (!hitbox)
			continue;

		const bool head{ hb == HITBOX_HEAD || hb == HITBOX_NECK };

		if (head && forceBaim)
			continue;

		// ignore limbs when moving
		if (THIS.MenuVars.RagebotIgnoreLimbs && moving
			&& (hitbox->iGroup == HITGROUP_LEFTLEG || hitbox->iGroup == HITGROUP_RIGHTLEG
				|| hitbox->iGroup == HITGROUP_LEFTARM || hitbox->iGroup == HITGROUP_RIGHTARM))
			continue;

		if (head && m_cPoint.m_bValid && m_cPoint.m_iHitgroup != HITGROUP_HEAD) {
			// a body shot already kills: no reason to risk the head
			if (m_cPoint.m_flDamage >= m_pPlayer->m_iHealth())
				break;

			// prefer baim
			if (THIS.MenuVars.RagebotPreferBaim)
				break;

			if (THIS.MenuVars.RagebotPreferBaimLethal && m_cPoint.m_flDamage >= m_pPlayer->m_iHealth())
				break;

			// prefer baim if dt
			const bool doubletap{ Config::Get<bool>(Vars.ExploitsDoubletap) && Config::Get<keybind_t>(Vars.ExploitsDoubletapKey).enabled };
			if (THIS.MenuVars.RagebotPreferBaimDoubletap && doubletap && ctx.m_iTicksAllowed)
				break;

			// resolver: defensive / lc / in air / repeated misses
			const int luaBody{ Lua::PreferBodyOverride(index) };
			if (luaBody > 0 || (luaBody == 0 && resolver && THIS.m_cBrain.ShouldPreferBody(index, *m_pRecord)))
				break;
		}

		auto damage{ static_cast<float>(ctx.m_pWeaponData->iDamage) };
		Features::Autowall.ScaleDamage(m_pPlayer, damage, ctx.m_pWeaponData->flArmorRatio, hitbox->iGroup, ctx.m_pWeaponData->flHeadShotMultiplier);

		// impossible
		if (damage < mindmg)
			continue;

		int scale{ hb == HITBOX_HEAD ? THIS.MenuVars.RagebotHeadScale : THIS.MenuVars.RagebotBodyScale };

		if (!THIS.MenuVars.RagebotStaticPointscale) {
			// dynamic multipoints: shrink with inaccuracy and with how sideways the target faces us
			scale = 100;
			scale -= static_cast<int>(std::floor(std::min(Features::EnginePrediction.Inaccuracy + Features::EnginePrediction.Spread, 0.15f) * 333.333f));
			scale -= std::min(90 - std::abs(90 - OffsetDelta(m_pPlayer, m_pRecord.get())), scale);
			scale = std::max(scale, RageTuning::MinDynamicScale);
		}

		const float scaleFloat{ std::clamp(scale, 0, 100) / 100.f };

		auto& matrix{ m_pMatrices[m_iResolverSide][hitbox->iBone] };
		const auto center{ Math::VectorTransform((hitbox->vecBBMax + hitbox->vecBBMin) * 0.5f, matrix) };

		Multipoint(hb, center, hitbox, scaleFloat, hitboxes);
	}
}

int AimTarget_t::SafePoint(Vector aimpoint, int index, bool cbb) {
	const auto hitboxSet{ m_pPlayer->m_pStudioHdr()->pStudioHdr->GetHitboxSet(m_pPlayer->m_nHitboxSet()) };
	if (!hitboxSet)
		return 0;

	const auto hitbox{ hitboxSet->GetHitbox(index) };
	if (!hitbox)
		return 0;

	if (cbb && !m_bCBBReady)
		return 0;

	const auto dir{ (aimpoint - ctx.m_vecEyePos).Normalized() };
	const auto end{ ctx.m_vecEyePos + dir * ctx.m_pWeaponData->flRange };
	const int bones{ BoneCount() };

	int hits{};
	const int start{ cbb ? RESOLVER_SIDE_COUNT : 0 };
	const int finish{ cbb ? MATRIX_COUNT : DESYNC_MATRIX_COUNT };

	for (int i{ start }; i < finish; ++i) {
		if (!cbb) {
			std::memcpy(m_pPlayer->m_CachedBoneData().Base(), m_pMatrices[i], bones * sizeof(matrix3x4_t));

			Trace_t tr{};
			Interfaces::EngineTrace->ClipRayToEntity({ ctx.m_vecEyePos, end }, MASK_SHOT_PLAYER, m_pPlayer, &tr);

			if (tr.pHitEntity == m_pPlayer && tr.iHitbox == index)
				++hits;
		}
		else if (IntersectHitbox(end, hitbox, m_pMatrices[i]))
			++hits;
	}

	std::memcpy(m_pPlayer->m_CachedBoneData().Base(), m_pMatrices[m_iResolverSide], bones * sizeof(matrix3x4_t));

	return hits;
}

bool AimTarget_t::IsBetterPoint(const AimPoint_t& candidate, const AimPoint_t& current) const {
	if (!current.m_bValid)
		return true;

	const int health{ std::max(m_pPlayer->m_iHealth(), 1) };
	const bool candidateLethal{ candidate.m_flDamage >= health };
	const bool currentLethal{ current.m_flDamage >= health };
	const bool candidateHead{ candidate.m_iHitgroup == HITGROUP_HEAD };
	const bool currentHead{ current.m_iHitgroup == HITGROUP_HEAD };

	// a kill beats no kill
	if (candidateLethal != currentLethal)
		return candidateLethal;

	if (candidateLethal && currentLethal) {
		// both kill: body is the bigger, safer target
		if (candidateHead != currentHead)
			return !candidateHead;

		if (m_bPreferSafe || m_bPreferSafeLethal) {
			if (candidate.m_iDesyncIntersections != current.m_iDesyncIntersections)
				return candidate.m_iDesyncIntersections > current.m_iDesyncIntersections;
		}
	}

	if (m_bPreferSafe) {
		// safety first, damage second
		if (candidate.m_iDesyncIntersections != current.m_iDesyncIntersections)
			return candidate.m_iDesyncIntersections > current.m_iDesyncIntersections;
	}
	else {
		// damage first ( with a tolerance so 1-2 damage doesn't throw away safety ), safety second
		const int tolerance{ std::max(5, current.m_flDamage / 10) };
		if (std::abs(candidate.m_flDamage - current.m_flDamage) > tolerance)
			return candidate.m_flDamage > current.m_flDamage;

		if (candidate.m_iDesyncIntersections != current.m_iDesyncIntersections)
			return candidate.m_iDesyncIntersections > current.m_iDesyncIntersections;
	}

	if (candidate.m_iCBBIntersections != current.m_iCBBIntersections)
		return candidate.m_iCBBIntersections > current.m_iCBBIntersections;

	return candidate.m_flDamage > current.m_flDamage;
}

void AimTarget_t::ScanPoint(AimPoint_t& point, std::vector<EHitboxIndex>& hitboxes) {
	const auto data{ Features::Autowall.FireBullet(ctx.m_pLocal, m_pPlayer, ctx.m_pWeaponData,
		ctx.m_pWeapon->m_iItemDefinitionIndex() == WEAPON_TASER,
		ctx.m_vecEyePos, point.m_vecPoint, THIS.MenuVars.RagebotAutowall) };

	SetDbg(_("ND"));

	if (static_cast<int>(data.dmg) < 1)
		return;

	// the wall's exit had to be guessed ( "breakable" with no exit found ): that damage is not something to bet a shot on
	if (data.guessedExit) {
		SetDbg(_("GUESS"));
		return;
	}

	point.m_flDamage = static_cast<int>(data.dmg);
	point.m_iHitgroup = data.hitgroup;

	int mindmg{ data.penetrationCount < 4 ? THIS.MenuVars.RagebotPenetrationDamage : THIS.MenuVars.RagebotMinimumDamage };

	if (Config::Get<bool>(Vars.RagebotDamageOverride)
		&& Config::Get<keybind_t>(Vars.RagebotDamageOverrideKey).enabled)
		mindmg = THIS.MenuVars.RagebotOverrideDamage;
	else if (THIS.MenuVars.RagebotScaleDamage)
		mindmg = static_cast<int>(mindmg * (m_pPlayer->m_iHealth() / 100.f));

	// lethal shots always pass the damage check
	mindmg = std::min(mindmg, m_pPlayer->m_iHealth());

	SetDbg(_("MD[") + std::to_string(point.m_flDamage) + "]");

	if (point.m_flDamage < mindmg)
		return;

	SetDbg(_("SA"));

	THIS.m_bShouldStop = true;

	if (std::find(hitboxes.begin(), hitboxes.end(), data.hitbox) == hitboxes.end())
		return;

	point.m_iDesyncIntersections = static_cast<uint8_t>(SafePoint(point.m_vecPoint, data.hitbox, false));
	point.m_iCBBIntersections = static_cast<uint8_t>(SafePoint(point.m_vecPoint, data.hitbox, true));

	if (Config::Get<keybind_t>(Vars.RagebotForceSafePointKey).enabled && point.m_iDesyncIntersections < DESYNC_MATRIX_COUNT)
		return;

	if (Config::Get<keybind_t>(Vars.RagebotForceYawSafetyKey).enabled && point.m_iCBBIntersections < CBB_MATRIX_COUNT)
		return;

	// resolver head safety: tighten as misses on this pattern stack up
	if (data.hitbox == HITBOX_HEAD && point.m_iDesyncIntersections < m_iRequiredHeadSafety) {
		SetDbg(_("HS[") + std::to_string(point.m_iDesyncIntersections) + "/" + std::to_string(m_iRequiredHeadSafety) + "]");
		return;
	}

	// predicted lag compensation breaker: the bullet must pass through this hitbox wherever else they could be
	if (!m_vecExtrapOffsets.empty()) {
		const auto hitboxSet{ m_pPlayer->m_pStudioHdr()->pStudioHdr->GetHitboxSet(m_pPlayer->m_nHitboxSet()) };
		const auto hitbox{ hitboxSet ? hitboxSet->GetHitbox(data.hitbox) : nullptr };
		if (!hitbox)
			return;

		const auto end{ ctx.m_vecEyePos + (point.m_vecPoint - ctx.m_vecEyePos).Normalized() * ctx.m_pWeaponData->flRange };
		const auto& bone{ m_pMatrices[m_iResolverSide][hitbox->iBone] };

		for (const auto& offset : m_vecExtrapOffsets) {
			if (!IntersectHitboxMoved(end, hitbox, bone, offset)) {
				SetDbg(_("EXU[") + std::to_string(static_cast<int>(m_flExtrapSpread)) + "]");
				return;
			}
		}
	}

	point.m_bValid = true;

	if (IsBetterPoint(point, m_cPoint))
		m_cPoint = point;
}

void AimTarget_t::Multipoint(EHitboxIndex index, Vector center, mstudiobbox_t* hitbox, float scale, std::vector<EHitboxIndex>& hitboxes) {
	auto& matrix{ m_pMatrices[m_iResolverSide][hitbox->iBone] };

	SetDbg(_("MP"));

	// box hitbox
	if (hitbox->flRadius == -1.f) {
		AimPoint_t point{ center, hitbox->iGroup };
		ScanPoint(point, hitboxes);

		if (THIS.IsMultiPointEnabled(index)) {
			const auto localCenter{ (hitbox->vecBBMax + hitbox->vecBBMin) * 0.5f };

			const auto d{ (hitbox->vecBBMin.x - localCenter.x) * scale };
			point = { Math::VectorTransform({ localCenter.x + d, localCenter.y, localCenter.z }, matrix), hitbox->iGroup };
			ScanPoint(point, hitboxes);

			const auto d0{ (hitbox->vecBBMax.x - localCenter.x) * scale };
			point = { Math::VectorTransform({ localCenter.x + d0, localCenter.y, localCenter.z }, matrix), hitbox->iGroup };
			ScanPoint(point, hitboxes);
		}

		return;
	}

	AimPoint_t point{ center, hitbox->iGroup };

	if (!THIS.IsMultiPointEnabled(index)) {
		ScanPoint(point, hitboxes);
		return;
	}

	const auto hitboxMin{ Math::VectorTransform(hitbox->vecBBMin, matrix) };
	const auto hitboxMax{ Math::VectorTransform(hitbox->vecBBMax, matrix) };

	const auto forwardDir{ (center - ctx.m_vecEyePos).Normalized() };
	const auto cylinderDir{ (hitboxMax - hitboxMin).Normalized() };

	Vector right{}, up{};

	if (index == HITBOX_HEAD) {
		const auto cross{ cylinderDir.CrossProduct(forwardDir) };
		QAngle crossAngle{};
		Math::VectorAngles(cross, crossAngle);

		Vector crossUp{};
		Math::AngleVectors(crossAngle, nullptr, nullptr, &crossUp);

		right = cross;
		up = crossUp;
	}
	else
		Math::VectorVectors(forwardDir, up, right);

	RayTracer::Hitbox box(hitboxMin, hitboxMax, hitbox->flRadius);
	RayTracer::Trace trace;

	const auto edgePoint = [&](const Vector& direction) {
		RayTracer::Ray ray(ctx.m_vecEyePos, center + direction * 1000.f);
		RayTracer::TraceFromCenter(ray, box, trace, RayTracer::Flags_RETURNEND);
		return ((trace.m_traceEnd - center) * scale) + center;
		};

	if (index == HITBOX_HEAD) {
		const Vector middle{ (right.Normalized() + up.Normalized()) * 0.5f };
		const Vector middle0{ (right.Normalized() - up.Normalized()) * 0.5f };

		// top first: most anti-aims hide the lower half of the head
		point = { edgePoint(up), hitbox->iGroup };
		ScanPoint(point, hitboxes);

		point = { center, hitbox->iGroup };
		ScanPoint(point, hitboxes);

		point = { edgePoint(up * -1.f), hitbox->iGroup };
		ScanPoint(point, hitboxes);

		point = { edgePoint(middle), hitbox->iGroup };
		ScanPoint(point, hitboxes);

		point = { edgePoint(middle0 * -1.f), hitbox->iGroup };
		ScanPoint(point, hitboxes);
	}
	else {
		point = { center, hitbox->iGroup };
		ScanPoint(point, hitboxes);

		point = { edgePoint(up * -1.f), hitbox->iGroup };
		ScanPoint(point, hitboxes);

		point = { edgePoint(up), hitbox->iGroup };
		ScanPoint(point, hitboxes);
	}
}

std::shared_ptr< LagRecord_t > AimTarget_t::GetAbuseRecord(PlayerEntry& entry) {
	if (entry.m_pRecords.empty())
		return nullptr;

	const auto curtime{ ctx.m_flFixedCurtime };
	const auto correct{ std::clamp(ctx.m_flOutLatency + ctx.m_flLerpTime, 0.f, std::min(Displacement::Cvars.sv_maxunlag->GetFloat(), 0.2f)) };
	const auto targetTime{ curtime - correct };

	std::shared_ptr< LagRecord_t > newer{};
	std::shared_ptr< LagRecord_t > record{};
	for (auto it{ entry.m_pRecords.rbegin() }; it != entry.m_pRecords.rend(); ++it) {
		newer = record;
		record = *it;

		if (record->m_cAnimData.m_flSimulationTime <= targetTime)
			break;
	}

	if (!record)
		return nullptr;

	auto ret{ std::make_shared< LagRecord_t >(*record) };

	if (!newer
		|| record->m_cAnimData.m_flSimulationTime >= targetTime
		|| record->m_cAnimData.m_flSimulationTime >= newer->m_cAnimData.m_flSimulationTime)
		return ret;

	// the exact time isn't stored but we have both neighbours: interpolate between them
	if (targetTime >= newer->m_cAnimData.m_flSimulationTime)
		return nullptr;

	const float frac{ (targetTime - record->m_cAnimData.m_flSimulationTime) /
		(newer->m_cAnimData.m_flSimulationTime - record->m_cAnimData.m_flSimulationTime) };

	if (frac > 1.f || frac < 0.f)
		return nullptr;

	auto player{ entry.m_pPlayer };

	const auto backupYaw{ player->GetAbsAngles().y };
	const auto backupPose{ player->m_flPoseParameter() };
	const auto backupState{ *player->m_pAnimState() };
	CAnimationLayer backupLayers[13]{};
	std::memcpy(backupLayers, player->m_AnimationLayers(), 13 * sizeof(CAnimationLayer));

	ret->m_cAnimData.m_vecOrigin = Math::Lerp(frac, record->m_cAnimData.m_vecOrigin, newer->m_cAnimData.m_vecOrigin);
	ret->m_cAnimData.m_vecMins = Math::Lerp(frac, record->m_cAnimData.m_vecMins, newer->m_cAnimData.m_vecMins);
	ret->m_cAnimData.m_vecMaxs = Math::Lerp(frac, record->m_cAnimData.m_vecMaxs, newer->m_cAnimData.m_vecMaxs);

	ret->Apply(player);

	const int sides{ record->m_bMultiMatrix ? RESOLVER_SIDE_COUNT : 1 };
	for (int i{}; i < sides; ++i) {
		if (!record->m_cAnimData.m_arrSides.at(i).m_bFilled || !newer->m_cAnimData.m_arrSides.at(i).m_bFilled)
			continue;

		const auto angle{ Math::Lerp(frac, record->m_cAnimData.m_arrSides.at(i).m_cAnimState.flAbsYaw, newer->m_cAnimData.m_arrSides.at(i).m_cAnimState.flAbsYaw) };

		auto& side{ ret->m_cAnimData.m_arrSides.at(i) };

		player->SetAbsAngles({ 0.f, angle, 0.f });
		player->m_flPoseParameter() = side.m_flPoseParameter;
		*player->m_pAnimState() = side.m_cAnimState;

		std::memcpy(player->m_AnimationLayers(), record->m_cAnimData.m_pLayers, 0x38 * 13);

		Features::AnimSys.SetupBonesRebuilt(player, side.m_pMatrix, BONE_USED_BY_SERVER, curtime, false);
	}

	if (!record->m_bMultiMatrix) {
		for (int i{ 1 }; i < RESOLVER_SIDE_COUNT; ++i)
			std::memcpy(ret->m_cAnimData.m_arrSides.at(i).m_pMatrix, ret->m_cAnimData.m_arrSides.at(0).m_pMatrix, player->m_CachedBoneData().Size() * sizeof(matrix3x4a_t));
	}

	player->SetAbsAngles({ 0.f, backupYaw, 0.f });
	player->m_flPoseParameter() = backupPose;
	*player->m_pAnimState() = backupState;
	std::memcpy(player->m_AnimationLayers(), backupLayers, 13 * sizeof(CAnimationLayer));

	return ret;
}

void AimTarget_t::Extrapolate(PlayerEntry& entry, bool useLagRecord) {
	static const std::vector<int> hitgroups{ HITBOX_HEAD, HITBOX_CHEST, HITBOX_STOMACH, HITBOX_PELVIS, HITBOX_RIGHT_UPPER_ARM, HITBOX_LEFT_UPPER_ARM, HITBOX_RIGHT_FOOT, HITBOX_LEFT_FOOT };

	m_pRecord = nullptr;

	if (entry.m_pRecords.empty() || entry.m_pRecords.back()->m_bFirst)
		return;

	const int index{ m_pPlayer->Index() };
	const int newCmds{ std::max(1, entry.m_iLastNewCmds) };
	const int latency{ LatencyTicks(entry) };

	const auto curtime{ ctx.m_flFixedCurtime };
	const auto correct{ std::clamp(ctx.m_flOutLatency + ctx.m_flLerpTime, 0.f, std::min(Displacement::Cvars.sv_maxunlag->GetFloat(), 0.2f)) };

	float simtime{ m_pPlayer->m_flSimulationTime() };

	// cap it out to the newest record and ignore the current pos
	int extrapolationCapped{ (latency / newCmds) * newCmds };

	if (useLagRecord) {
		bool newRecord{};
		bool found{};
		const int maxExtrapolation{ extrapolationCapped };

		for (extrapolationCapped = 0; extrapolationCapped <= maxExtrapolation; extrapolationCapped += newCmds) {
			const auto simulatedSimTime{ simtime + TICKS_TO_TIME(extrapolationCapped) };
			const auto delta{ correct - (curtime - simulatedSimTime) };

			if (simulatedSimTime > entry.m_flHighestSimulationTime) {
				newRecord = true;
				if (std::fabsf(delta) < 0.19f) {
					found = true;
					break;
				}
			}
		}

		// no new records will be added: shoot the record the server will interpolate to
		if (!newRecord) {
			auto record{ GetAbuseRecord(entry) };
			if (!record)
				return;

			const int side{ THIS.ResolveRecordSide(entry, *record, index) };
			const int dmg{ QuickScan(record, hitgroups, side) };
			if (dmg < 1)
				return;

			m_pRecord = record;
			m_iResolverSide = side;
			m_iBestDamage = dmg;
			m_bAbuseRecord = true;
			return;
		}

		if (!found)
			return;

		if (extrapolationCapped > latency)
			extrapolationCapped = 0;
		else
			simtime += TICKS_TO_TIME(extrapolationCapped);
	}

	const auto& newest{ entry.m_pRecords.back() };
	const int newestSide{ THIS.ResolveRecordSide(entry, *newest, index) };

	if (extrapolationCapped > 0) {
		ExtrapolationBackup_t backupExtrapolationData{ entry.m_pPlayer };

		if (!THIS.ExtrapolatePlayer(entry, -1.f, newestSide, extrapolationCapped, entry.m_vecPreviousVelocity)) {
			backupExtrapolationData.restore(entry.m_pPlayer);
			return;
		}

		const auto record{ std::make_shared< LagRecord_t >(entry.m_pPlayer) };
		record->m_iReceiveTick = Interfaces::Globals->iTickCount + extrapolationCapped;
		record->m_cAnimData.m_flSimulationTime = simtime;

		auto& side{ record->m_cAnimData.m_arrSides.at(0) };
		Features::AnimSys.SetupBonesRebuilt(entry.m_pPlayer, side.m_pMatrix, BONE_USED_BY_SERVER, simtime, false);

		for (int i{ 1 }; i < RESOLVER_SIDE_COUNT; ++i)
			std::memcpy(record->m_cAnimData.m_arrSides.at(i).m_pMatrix, side.m_pMatrix, entry.m_pPlayer->m_CachedBoneData().Size() * sizeof(matrix3x4a_t));

		backupExtrapolationData.restore(entry.m_pPlayer);

		m_pRecord = record;
		m_iResolverSide = RESOLVER_SIDE_CENTER;
		m_bExtrapolating = true;
	}
	else {
		m_pRecord = newest;
		m_iResolverSide = newestSide;
	}

	if (!m_pRecord->Validity() && Config::Get<bool>(Vars.RagebotLagcompensation)) {
		m_pRecord = nullptr;
		m_bExtrapolating = false;
		return;
	}

	const int dmg{ QuickScan(m_pRecord, hitgroups, m_iResolverSide) };
	if (dmg < 1) {
		m_pRecord = nullptr;
		m_bExtrapolating = false;
		return;
	}

	m_iBestDamage = dmg;
}

void AimTarget_t::PredictBreaker(PlayerEntry& entry, const LcPrediction_t& prediction) {
	static const std::vector<int> hitgroups{ HITBOX_HEAD, HITBOX_CHEST, HITBOX_STOMACH, HITBOX_PELVIS, HITBOX_RIGHT_UPPER_ARM, HITBOX_LEFT_UPPER_ARM, HITBOX_RIGHT_FOOT, HITBOX_LEFT_FOOT };

	m_pRecord = nullptr;

	if (entry.m_pRecords.empty())
		return;

	const auto& newest{ entry.m_pRecords.back() };
	if (!newest || newest->m_bFirst)
		return;

	const int index{ m_pPlayer->Index() };

	// a fake flick pose moved forward is still a fake flick pose
	if (THIS.m_cBrain.IsFlickRecord(index, *newest)) {
		SetDbg(_("FLICK"));
		return;
	}

	// the newest record, moved to where the server will have them ( animation stays: a few ticks of running or
	// flying barely change the pose, the position is what decides the hit )
	auto record{ std::make_shared< LagRecord_t >(*newest) };
	const Vector delta{ prediction.m_vecOrigin - newest->m_cAnimData.m_vecOrigin };

	record->m_cAnimData.m_vecOrigin = prediction.m_vecOrigin;
	record->m_cAnimData.m_flSimulationTime += TICKS_TO_TIME(prediction.m_iTicks);

	const int bones{ BoneCount() };
	for (int side{}; side < RESOLVER_SIDE_COUNT; ++side) {
		auto& data{ record->m_cAnimData.m_arrSides.at(side) };
		if (!data.m_bFilled && side >= DESYNC_MATRIX_COUNT)
			continue;

		for (int bone{}; bone < bones; ++bone) {
			data.m_pMatrix[bone][0][3] += delta.x;
			data.m_pMatrix[bone][1][3] += delta.y;
			data.m_pMatrix[bone][2][3] += delta.z;
		}
	}

	m_vecExtrapOffsets.clear();
	for (const auto& alternative : prediction.m_vecAlternatives)
		m_vecExtrapOffsets.push_back(alternative - prediction.m_vecOrigin);

	const int side{ THIS.ResolveRecordSide(entry, *newest, index) };
	const int dmg{ QuickScan(record, hitgroups, side) };
	if (dmg < 1) {
		m_vecExtrapOffsets.clear();
		return;
	}

	m_pRecord = record;
	m_iResolverSide = side;
	m_iBestDamage = dmg;
	m_bExtrapolating = true;
	m_iExtrapTicks = prediction.m_iTicks;
	m_flExtrapSpread = prediction.m_flSpread;
}

void AimTarget_t::GetBestLagRecord(PlayerEntry& entry) {
	const auto backup{ std::make_unique< LagBackup_t >(m_pPlayer) };

	const bool lagcomp{ Config::Get<bool>(Vars.RagebotLagcompensation) };
	const bool resolver{ Config::Get<bool>(Vars.RagebotResolver) };
	const int index{ m_pPlayer->Index() };

	// lag compensation broken ( teleported > 64 units, which a defensive shift ending while running always does ):
	// the server only refuses to backtrack PAST the teleport, its newest record is still exact. it stays the
	// server's newest record until the next real update, and the server stores nothing while a defensive shift
	// keeps their simulation time below it, so during a shift that record is shootable the whole time
	bool newestOnly{};
	if (lagcomp && entry.m_bBrokeLC) {
		const auto& newest{ entry.m_pRecords.back() };
		const auto& st{ THIS.m_cBrain.Get(index) };
		const int age{ Interfaces::Globals->iTickCount - newest->m_iReceiveTick + ctx.m_iRealOutLatencyTicks };
		// how long this record stays the server's newest: their choke, or for a defensive user the next shift
		// ( a clean update right after a shift measures ~1 tick of choke, which ping alone would already exceed )
		int headTicks{ std::max(st.m_iChokeTicks, 2) };
		if (st.m_bDefensiveUser)
			headTicks = std::max(headTicks, std::min(st.m_iDefensiveShift, DefTune::MaxShift()) + 2);
		newestOnly = newest && !newest->m_bFirst && (st.m_bInShift || age < headTicks);
	}

	// lag compensation breakers ( fakelag / speed, not defensive ): the server tests our shot against wherever they
	// are when it runs it, so predict that position instead of waiting for them
	if (lagcomp && Config::Get<bool>(Vars.RagebotExtrapolateBreakers)) {
		const auto& st{ THIS.m_cBrain.Get(index) };
		if (!st.m_bInShift && !st.m_bDefensiveUser && Features::LagCompPredictor.BreaksLagComp(index, entry)) {
			const auto prediction{ Features::LagCompPredictor.Predict(index, entry) };
			if (!prediction.m_bValid) {
				SetDbg(_("EX[") + std::string(prediction.m_szReason) + "]");
				backup->Apply(m_pPlayer);
				return;
			}

			if (prediction.m_iTicks > 0 || !prediction.m_vecAlternatives.empty()) {
				PredictBreaker(entry, prediction);
				backup->Apply(m_pPlayer);
				return;
			}

			// no packet of theirs reaches the server before our shot: their newest record is still the server's
			newestOnly = entry.m_bBrokeLC;
		}
	}

	if (!lagcomp || (entry.m_bBrokeLC && !newestOnly)) {
		// Preserve normal non-lagcomp aiming, but do not synthesize a shot
		// through a detected rollback or a broken positional history.
		if (!resolver || (!entry.m_bBrokeLC && !THIS.m_cBrain.InDefensiveShift(index)))
			Extrapolate(entry, lagcomp);
		backup->Apply(m_pPlayer);
		return;
	}

	const int robustValidity{ ctx.CalcCorrectionTicks() * 2 + 1 };

	// oldest -> newest, every record the server will still accept
	int lowMargin{};
	int defensive{};
	std::vector< std::shared_ptr< LagRecord_t > > candidates{};
	candidates.reserve(entry.m_pRecords.size());
	for (const auto& record : entry.m_pRecords) {
		if (!record || record->m_bFirst)
			continue;

		const auto validity{ record->Validity() };
		if (!validity)
			continue;

		// a record right at the edge of the server's 200 ms window: any tickbase difference ( our double tap charge,
		// a shifted command ) pushes it out and the server rewinds somewhere else. measured ( rage_watch_2 ): 6 of
		// the 9 misses were records with validity 2-4 ( <= 1 tick of margin ), hits were almost all 8+. a fresh
		// record gains margin within a few ticks, so this only waits, it does not give the record up
		const int margin{ std::clamp(Config::Get<int>(Vars.RagebotRecordMargin), 0, 7) };
		if (margin > 0 && validity < margin * 2 + 1) {
			++lowMargin;
			continue;
		}

		// Clock-correction robustness ranks valid records below; it must not discard
		// the only usable history while the target is withholding new records.

		// the pushed record of a defensive shift ( found by the tickbase shift detection, not the flick filter ) carries
		// the defensive angles: never shoot it. shots_163853 + shots_165512: 26 misses on it, all head. the player is
		// still shot on their clean records before / after the shift; while only the pushed one is valid, wait ( "DEF" )
		if (resolver && THIS.m_cBrain.IsDefensiveRecord(index, *record)) {
			++defensive;
			continue;
		}

		candidates.push_back(record);
	}

	if (candidates.empty()) {
		SetDbg(defensive > 0 ? _("DEF") : lowMargin > 0 ? _("MARGIN") : _("NO"));

		// With the resolver enabled, absence of valid history is uncertainty: wait.
		// Do not fabricate a target from a defensive or extrapolated pose.
		if (!resolver)
			Extrapolate(entry, lagcomp);

		backup->Apply(m_pPlayer);
		return;
	}

	// backtrack off ( or lag compensation broken ): only the newest valid record
	if ((newestOnly || !Config::Get<bool>(Vars.RagebotBacktrack)) && candidates.size() > 1)
		candidates.erase(candidates.begin(), candidates.end() - 1);

	// flick records ( confirmed, or the newest record of a known flicker that just jumped ) are never shot:
	// the head is only there for one tick and comes straight back. no fallback: no steady record = no shot
	candidates.erase(
		std::remove_if(candidates.begin(), candidates.end(),
			[&](const std::shared_ptr< LagRecord_t >& record) { return THIS.m_cBrain.IsFlickRecord(index, *record); }),
		candidates.end());

	if (candidates.empty()) {
		SetDbg(_("FLICK"));
		backup->Apply(m_pPlayer);
		return;
	}

	const auto hitboxSet{ m_pPlayer->m_pStudioHdr()->pStudioHdr->GetHitboxSet(m_pPlayer->m_nHitboxSet()) };
	if (!hitboxSet) {
		backup->Apply(m_pPlayer);
		return;
	}

	const auto head{ hitboxSet->GetHitbox(HITBOX_HEAD) };
	const int weaponIndex{ ctx.m_pWeapon->m_iItemDefinitionIndex() };

	static const std::vector<int> fullScan{ HITBOX_HEAD, HITBOX_CHEST, HITBOX_STOMACH, HITBOX_PELVIS, HITBOX_RIGHT_UPPER_ARM, HITBOX_LEFT_UPPER_ARM, HITBOX_RIGHT_FOOT, HITBOX_LEFT_FOOT };
	const std::vector<int> quickScan{ (weaponIndex == WEAPON_SCAR20 || weaponIndex == WEAPON_G3SG1 || weaponIndex == WEAPON_AWP) ? HITBOX_STOMACH : HITBOX_HEAD };

	// newest, oldest, then evenly sampled records in between
	const int count{ static_cast<int>(candidates.size()) };
	std::vector< int > order{ count - 1 };
	if (count > 1)
		order.push_back(0);

	const int inner{ std::max(0, count - 2) };
	if (inner > 0) {
		const int step{ std::max(1, (inner + RageTuning::MaxIntermediateRecords - 1) / RageTuning::MaxIntermediateRecords) };
		for (int i{ count - 2 }; i >= 1; i -= step)
			order.push_back(i);
	}

	struct Ranked_t {
		std::shared_ptr< LagRecord_t > m_pRecord{};
		int m_iSide{};
		int m_iDamage{};
		int m_iResolverPriority{};
		int m_iHeadSafety{};
		int m_iCBB{ -1 };
		bool m_bRobust{};
	};

	const auto headPoint = [&](int side) {
		return Math::VectorTransform((head->vecBBMin + head->vecBBMax) / 2.f, m_pMatrices[side][head->iBone]);
		};

	const auto cbbSafety = [&](Ranked_t& ranked) {
		if (ranked.m_iCBB >= 0 || !head)
			return std::max(ranked.m_iCBB, 0);

		const auto backupSide{ m_iResolverSide };
		m_iResolverSide = ranked.m_iSide;
		SetupMatrices(ranked.m_pRecord, PredictYaw(entry), true);
		ranked.m_iCBB = SafePoint(headPoint(ranked.m_iSide), HITBOX_HEAD, true);
		m_iResolverSide = backupSide;
		return ranked.m_iCBB;
		};

	Ranked_t best{};

	for (size_t i{}; i < order.size(); ++i) {
		const auto& record{ candidates[order[i]] };

		Ranked_t ranked{};
		ranked.m_pRecord = record;
		ranked.m_iSide = THIS.ResolveRecordSide(entry, *record, index);
		ranked.m_bRobust = record->Validity() >= robustValidity;
		ranked.m_iResolverPriority = resolver ? RecordResolverPriority(index, *record) : 0;
		ranked.m_iDamage = QuickScan(record, i < 2 ? fullScan : quickScan, ranked.m_iSide);

		if (ranked.m_iDamage < 1)
			continue;

		if (head) {
			const auto backupSide{ m_iResolverSide };
			m_iResolverSide = ranked.m_iSide;
			ranked.m_iHeadSafety = SafePoint(headPoint(ranked.m_iSide), HITBOX_HEAD, false);
			m_iResolverSide = backupSide;
		}

		bool better{};

		if (!best.m_pRecord || ranked.m_iDamage > best.m_iDamage + RageTuning::RecordDamageTolerance)
			better = true;
		else if (ranked.m_iDamage + RageTuning::RecordDamageTolerance >= best.m_iDamage) {
			if (ranked.m_bRobust != best.m_bRobust)
				better = ranked.m_bRobust;
			else if (ranked.m_iResolverPriority != best.m_iResolverPriority)
				better = ranked.m_iResolverPriority > best.m_iResolverPriority;
			else if (ranked.m_iHeadSafety != best.m_iHeadSafety)
				better = ranked.m_iHeadSafety > best.m_iHeadSafety;
			else {
				const int candidateCBB{ cbbSafety(ranked) };
				const int bestCBB{ cbbSafety(best) };
				better = candidateCBB != bestCBB ? candidateCBB > bestCBB : ranked.m_iDamage > best.m_iDamage;
			}
		}

		if (better)
			best = ranked;
	}

	if (best.m_pRecord) {
		m_pRecord = best.m_pRecord;
		m_iResolverSide = best.m_iSide;
		m_iBestDamage = best.m_iDamage;
	}

	backup->Apply(m_pPlayer);
}
