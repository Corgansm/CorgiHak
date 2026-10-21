#include "animation.h"
#include "replay_rules.h"
#include "../../utils/crash_log.h"
#include "../rage/ragebot.h"
#include "../../utils/performance_monitor.h"
#include "../../utils/threading/thread_pool.hpp"

// https://www.youtube.com/watch?v=a3Z7zEc7AXQ

void CAnimationSys::OnLevelChange()
{
	for (auto& entry : m_arrEntries)
	{
		entry.OutOfDormancy();

		entry.m_pPlayer = nullptr;
		entry.m_flSpawnTime = 0.f;
		entry.m_iResolverSide = 0;
		entry.m_iMissedShots = 0;
		entry.m_iLastRecordTick = 0;
		entry.m_iLastRecievedTick = 0;
		entry.m_vecLastMergeOrigin = { 0, 0, 0 };
	}

	Features::Ragebot.m_cBrain.OnLevelChange();
}

void CAnimationSys::RunAnimationSystem()
{
	// Do not touch any engine state until the core interfaces exist.
	if (!Interfaces::ClientState ||
		!Interfaces::ClientEntityList ||
		!Interfaces::Globals)
	{
		return;
	}

	/*
	 * A server tick going significantly backwards normally means that
	 * the old entity/animation state belongs to another level/server.
	 */
	{
		static int lastServerTick{};

		const int serverTick = Interfaces::ClientState->iServerTick;

		if (lastServerTick > 0 &&
			serverTick + 64 < lastServerTick)
		{
			OnLevelChange();
		}

		lastServerTick = serverTick;
	}

	float maxUnlag = 0.2f;

	if (Displacement::Cvars.sv_maxunlag)
	{
		maxUnlag = std::min(
			Displacement::Cvars.sv_maxunlag->GetFloat(),
			0.2f
		);
	}

	for (int i = 1; i < 64; ++i)
	{
		auto* player = static_cast<CBasePlayer*>(
			Interfaces::ClientEntityList->GetClientEntity(i)
			);

		auto& entry = m_arrEntries.at(i - 1);

		if (!player ||
			!player->IsPlayer() ||
			player == ctx.m_pLocal)
		{
			entry.Clear();
			continue;
		}

		if (entry.m_pPlayer != player)
			entry.OnPlayerChange(player);

		if (player->IsDead())
			continue;

		if (player->IsDormant())
		{
			entry.OutOfDormancy();
			continue;
		}

		auto* state = player->m_pAnimState();
		auto* layers = player->m_AnimationLayers();

		/*
		 * Both pointers belong to the entity and may disappear while
		 * connecting, disconnecting, respawning, changing maps, etc.
		 */
		if (!state || !layers)
		{
			entry.OutOfDormancy();
			continue;
		}

		const float serverCurtime = TICKS_TO_TIME(
			Interfaces::ClientState->iServerTick +
			ctx.m_iRealOutLatencyTicks
		);

		const float deadTime = serverCurtime - maxUnlag;

		/*
		 * Data far ahead of the current entity is almost certainly
		 * stale data from the previous level/server/entity instance.
		 */
		if (entry.m_optPreviousData.has_value() &&
			entry.m_optPreviousData->m_flSimulationTime >
			player->m_flSimulationTime() + 1.f)
		{
			entry.OutOfDormancy();
		}

		entry.m_pRecords.erase(
			std::remove_if(
				entry.m_pRecords.begin(),
				entry.m_pRecords.end(),
				[&](const std::shared_ptr<LagRecord_t>& lagRecord)
				{
					if (!lagRecord)
						return true;

					return
						lagRecord->m_cAnimData.m_flSimulationTime < deadTime ||
						lagRecord->m_cAnimData.m_flSimulationTime >
						serverCurtime + 1.f;
				}
			),
			entry.m_pRecords.end()
		);

		/*
		 * A new life must be handled before the duplicate-animation
		 * early out.
		 */
		if (player->m_flSpawnTime() != entry.m_flSpawnTime)
		{
			state->Reset();
			entry.OnNewRound();

			// Re-fetch after Reset/round initialization.
			state = player->m_pAnimState();
			layers = player->m_AnimationLayers();

			if (!state || !layers)
				continue;
		}

		if (entry.m_optPreviousData.has_value() &&
			entry.m_optPreviousData->m_pLayers[11].flPlaybackRate ==
			layers[11].flPlaybackRate &&
			entry.m_optPreviousData->m_pLayers[11].flCycle ==
			layers[11].flCycle)
		{
			player->m_flSimulationTime() =
				entry.m_optPreviousData->m_flSimulationTime;

			player->m_flOldSimulationTime() =
				entry.m_optPreviousData->m_flOldSimulationTime;

			continue;
		}

		/*
		 * Equal/regressed simulation time doesn't represent a new
		 * server record.
		 */
		if (entry.m_flHighestSimulationTime > 0.f &&
			player->m_flSimulationTime() <=
			entry.m_flHighestSimulationTime)
		{
			entry.m_bRecordAdded = false;
			entry.m_iLastRecievedTick =
				Interfaces::Globals->iTickCount;

			continue;
		}

		auto current = std::make_shared<LagRecord_t>(player);

		if (!current)
			continue;

		current->FinalAdjustments(
			player,
			entry.m_optPreviousData
		);

		const bool outOfDormancy =
			entry.m_vecUpdatedOrigin.IsZero();

		AnimatePlayer(current.get(), entry);

		/*
		 * Side zero is the baseline animation. If it was not produced,
		 * animation failed validation somewhere and this record cannot
		 * safely be used.
		 */
		if (!current->m_cAnimData.m_arrSides.at(0).m_bFilled)
		{
			entry.m_bRecordAdded = false;
			continue;
		}

		entry.m_vecPreviousVelocity =
			entry.m_optPreviousData.has_value()
			? entry.m_optPreviousData->m_vecVelocity
			: Vector{ 0, 0, 0 };

		entry.m_optPreviousData = current->m_cAnimData;

		const float oldSimTime =
			!entry.m_pRecords.empty() &&
			entry.m_pRecords.back()
			? entry.m_pRecords.back()
			->m_cAnimData.m_flSimulationTime
			: player->m_flOldSimulationTime();

		entry.m_bRecordAdded =
			(
				!Config::Get<bool>(Vars.RagebotLagcompensation) ||
				player->m_flSimulationTime() > oldSimTime
				) &&
			!player->IsTeammate();

		if (player->m_flSimulationTime() >
			entry.m_flHighestSimulationTime)
		{
			entry.m_flHighestSimulationTime =
				player->m_flSimulationTime();
		}

		if (entry.m_bRecordAdded)
		{
			entry.m_bBrokeLC =
				!entry.m_pRecords.empty() &&
				entry.m_pRecords.back() &&
				(
					player->m_vecOrigin() -
					entry.m_pRecords.back()
					->m_cAnimData.m_vecOrigin
					).Length2DSqr() > 4096.f;

			if (entry.m_bBrokeLC)
				entry.m_pRecords.clear();

			current->m_bFirst = outOfDormancy;

			entry.m_iLastRecordInterval =
				Interfaces::Globals->iTickCount -
				entry.m_iLastRecordTick;

			entry.m_iLastRecordTick =
				Interfaces::Globals->iTickCount;

			entry.m_pRecords.push_back(current);
		}

		entry.m_iLastRecievedTick =
			Interfaces::Globals->iTickCount;

		/*
		 * Validate the entity again before restoring generated animation
		 * data. An entity can change during level/round transitions.
		 */
		if (!entry.m_pPlayer)
			continue;

		auto* entryState = entry.m_pPlayer->m_pAnimState();
		auto* entryLayers = entry.m_pPlayer->m_AnimationLayers();

		if (!entryState || !entryLayers)
			continue;

		auto copyToCachedBones =
			[&](const matrix3x4a_t* source)
			{
				if (!source)
					return;

				auto* destination =
					entry.m_pPlayer->m_CachedBoneData().Base();

				const int count =
					entry.m_pPlayer->m_CachedBoneData().Size();

				if (!destination || count <= 0)
					return;

				std::memcpy(
					destination,
					source,
					static_cast<std::size_t>(count) *
					sizeof(matrix3x4a_t)
				);
			};

		if (!entry.m_bRecordAdded)
		{
			if (!entry.m_optPreviousData.has_value())
				continue;

			const int resolverSide =
				std::clamp(entry.m_iResolverSide, 0, 4);

			const auto& side =
				entry.m_optPreviousData
				->m_arrSides.at(resolverSide);

			const auto& fallback =
				entry.m_optPreviousData
				->m_arrSides.at(0);

			const auto& selectedSide =
				side.m_bFilled ? side : fallback;

			if (!selectedSide.m_bFilled)
				continue;

			entry.m_pPlayer->SetAbsAngles(
				{
					0.f,
					selectedSide.m_cAnimState.flAbsYaw,
					0.f
				}
			);

			entry.m_pPlayer->m_flPoseParameter() =
				selectedSide.m_flPoseParameter;

			*entryState = selectedSide.m_cAnimState;

			std::memcpy(
				entryLayers,
				selectedSide.m_pLayers,
				sizeof(CAnimationLayer) * 13
			);

			if (SetupBonesRebuilt(
				entry.m_pPlayer,
				entry.m_matMatrix,
				BONE_USED_BY_SERVER,
				entry.m_optPreviousData
				->m_flSimulationTime,
				true))
			{
				copyToCachedBones(entry.m_matMatrix);
			}
		}
		else
		{
			if (entry.m_pRecords.empty())
				continue;

			const auto& record =
				entry.m_pRecords.back();

			if (!record)
				continue;

			if (record->m_bMultiMatrix)
			{
				for (int j = 0; j < 5; ++j)
				{
					auto& side =
						record->m_cAnimData
						.m_arrSides.at(j);

					if (!side.m_bFilled)
						continue;

					entry.m_pPlayer->SetAbsAngles(
						{
							0.f,
							side.m_cAnimState.flAbsYaw,
							0.f
						}
					);

					entry.m_pPlayer->m_flPoseParameter() =
						side.m_flPoseParameter;

					*entryState = side.m_cAnimState;

					std::memcpy(
						entryLayers,
						side.m_pLayers,
						sizeof(CAnimationLayer) * 13
					);

					if (SetupBonesRebuilt(
						entry.m_pPlayer,
						side.m_pMatrix,
						BONE_USED_BY_SERVER,
						record->m_cAnimData
						.m_flSimulationTime,
						false))
					{
						/*
						 * Original code built side.m_pMatrix but copied
						 * entry.m_matMatrix here. That was inconsistent.
						 */
						copyToCachedBones(
							side.m_pMatrix
						);
					}
				}
			}
			else
			{
				auto& side =
					record->m_cAnimData
					.m_arrSides.at(0);

				if (!side.m_bFilled)
					continue;

				entry.m_pPlayer->SetAbsAngles(
					{
						0.f,
						side.m_cAnimState.flAbsYaw,
						0.f
					}
				);

				entry.m_pPlayer->m_flPoseParameter() =
					side.m_flPoseParameter;

				*entryState = side.m_cAnimState;

				std::memcpy(
					entryLayers,
					side.m_pLayers,
					sizeof(CAnimationLayer) * 13
				);

				if (!SetupBonesRebuilt(
					entry.m_pPlayer,
					side.m_pMatrix,
					BONE_USED_BY_SERVER,
					record->m_cAnimData
					.m_flSimulationTime,
					false))
				{
					continue;
				}

				const int boneCount =
					entry.m_pPlayer
					->m_CachedBoneData()
					.Size();

				if (boneCount > 0)
				{
					for (int j = 1; j < 5; ++j)
					{
						std::memcpy(
							record->m_cAnimData
							.m_arrSides.at(j)
							.m_pMatrix,
							side.m_pMatrix,
							static_cast<std::size_t>(
								boneCount
								) *
							sizeof(matrix3x4a_t)
						);
					}
				}
			}

			int resolverSide =
				std::clamp(
					entry.m_iResolverSide,
					0,
					4
				);

			if (!record->m_cAnimData
				.m_arrSides.at(resolverSide)
				.m_bFilled)
			{
				resolverSide = 0;
			}

			const auto& selectedSide =
				record->m_cAnimData
				.m_arrSides.at(resolverSide);

			if (!selectedSide.m_bFilled)
				continue;

			const int boneCount =
				entry.m_pPlayer
				->m_CachedBoneData()
				.Size();

			if (boneCount > 0)
			{
				std::memcpy(
					entry.m_matMatrix,
					selectedSide.m_pMatrix,
					static_cast<std::size_t>(
						boneCount
						) *
					sizeof(matrix3x4a_t)
				);

				copyToCachedBones(
					entry.m_matMatrix
				);
			}
		}
	}
}

void CAnimationSys::AnimatePlayer(
	LagRecord_t* current,
	PlayerEntry& entry)
{
	if (!current ||
		!entry.m_pPlayer ||
		!Interfaces::Globals)
	{
		return;
	}

	auto* player = entry.m_pPlayer;
	auto* state = player->m_pAnimState();

	if (!state)
		return;

	current->m_bMultiMatrix = false;

	entry.m_bCanExtrapolate =
		entry.m_iLastNewCmds ==
		current->m_iNewCmds;

	current->m_iNewCmds =
		std::clamp(
			current->m_iNewCmds,
			1,
			64
		);

	entry.m_iLastNewCmds =
		std::min(
			Interfaces::Globals->iTickCount -
			entry.m_iLastRecievedTick,
			current->m_iNewCmds
		);

	const CCSGOPlayerAnimState backupState =
		*state;

	player->m_iEFlags() |=
		EFL_DIRTY_ABSTRANSFORM;

	player->SetAbsOrigin(
		current->m_cAnimData.m_vecOrigin
	);

	const float lastYaw =
		!entry.m_flPreviousYaws.empty()
		? entry.m_flPreviousYaws.back()
		.m_flYaw
		: player->m_angEyeAngles().y;

	entry.m_flPreviousYaws.push_back(
		{
			Interfaces::Globals->iTickCount,
			player->m_angEyeAngles().y,
			Math::AngleDiff(
				lastYaw,
				player->m_angEyeAngles().y
			)
		}
	);

	entry.m_flPreviousYaws.erase(
		std::remove_if(
			entry.m_flPreviousYaws.begin(),
			entry.m_flPreviousYaws.end(),
			[&](const PreviousYaw_t& yaw)
			{
				return
					Interfaces::Globals->iTickCount -
					yaw.m_iTickCount >
					128;
			}
		),
		entry.m_flPreviousYaws.end()
	);

	const float backupFlash =
		player->m_flNextAttack();

	if (current->m_bFixJumpFall)
		player->m_flNextAttack() =
		current->m_flLeftGroundTime;
	else
		player->m_flNextAttack() = 0.f;

	const bool localAlive =
		ctx.m_pLocal &&
		!ctx.m_pLocal->IsDead();

	if (!player->IsTeammate() &&
		!entry.m_bBot &&
		entry.m_optPreviousData.has_value() &&
		localAlive)
	{
		bool allSidesValid = true;

		for (int side = 1; side < 5; ++side)
		{
			*state = backupState;

			UpdateSide(
				entry,
				current,
				side
			);

			if (!current->m_cAnimData
				.m_arrSides.at(side)
				.m_bFilled)
			{
				allSidesValid = false;
			}
		}

		*state = backupState;

		current->m_bMultiMatrix =
			allSidesValid;
	}

	/*
	 * Side zero is always required, regardless of whether resolver
	 * alternatives were generated.
	 */
	*state = backupState;

	UpdateSide(
		entry,
		current,
		0
	);

	player->m_flNextAttack() =
		backupFlash;

	if (!current->m_cAnimData
		.m_arrSides.at(0)
		.m_bFilled)
	{
		current->m_bMultiMatrix = false;
		return;
	}

	if (!current->m_bMultiMatrix)
	{
		for (int i = 1; i < 5; ++i)
		{
			current->m_cAnimData
				.m_arrSides.at(i) =
				current->m_cAnimData
				.m_arrSides.at(0);

			current->m_cAnimData
				.m_arrSides.at(i)
				.m_bFilled = false;
		}
	}

	if (Config::Get<bool>(
		Vars.RagebotResolver) &&
		current->m_bMultiMatrix &&
		localAlive)
	{
		entry.Rezik(current);

		if (current->m_iResolverSide)
		{
			entry.m_iResolverSide =
				std::clamp(
					current->m_iResolverSide,
					0,
					4
				);
		}
	}
	else
	{
		if (!Config::Get<bool>(
			Vars.RagebotResolver) ||
			!localAlive)
		{
			entry.m_iResolverSide = 0;
		}
	}

	entry.m_vecUpdatedOrigin =
		player->GetAbsOrigin();
}

void CAnimationSys::UpdateSide(
	PlayerEntry& entry,
	LagRecord_t* current,
	int side)
{
	if (!current ||
		!entry.m_pPlayer ||
		!Interfaces::Globals ||
		side < 0 ||
		side >= 5)
	{
		return;
	}

	auto* player = entry.m_pPlayer;

	auto* state =
		player->m_pAnimState();

	auto* layers =
		player->m_AnimationLayers();

	if (!state || !layers)
		return;

	const float backupCurtime =
		Interfaces::Globals->flCurTime;

	const float backupFrametime =
		Interfaces::Globals->flFrameTime;

	const float backupYaw =
		player->m_angEyeAngles().y;

	const auto backupPose =
		player->m_flPoseParameter();

	const auto backupFlags =
		player->m_fFlags();

	const auto backupEFlags =
		player->m_iEFlags();

	const auto backupDuck =
		player->m_flDuckAmount();

	const auto backupVelocity =
		player->m_vecAbsVelocity();

	const bool backupClientAnimation =
		player->m_bClientSideAnimation();

	const bool backupUpdating =
		ctx.m_bUpdatingAnimations;

	CAnimationLayer backupLayers[13];

	std::memcpy(
		backupLayers,
		layers,
		sizeof(backupLayers)
	);

	Interfaces::Globals->flFrameTime =
		Interfaces::Globals->flIntervalPerTick;

	/*
	 * Eye yaw is the observed network input.
	 * Resolver hypotheses modify feet yaw instead.
	 */
	InterpolateFromLastData(
		player,
		current,
		entry.m_optPreviousData,
		side
	);

	/*
	 * InterpolateFromLastData is expected to keep this object valid,
	 * but do not call m_pAnimState() again unnecessarily. We already
	 * validated and cached it above.
	 */
	player->m_angEyeAngles().y =
		backupYaw;

	auto& sideData =
		current->m_cAnimData
		.m_arrSides.at(side);

	sideData.m_cAnimState =
		*state;

	sideData.m_flPoseParameter =
		player->m_flPoseParameter();

	std::memcpy(
		sideData.m_pLayers,
		layers,
		sizeof(CAnimationLayer) * 13
	);

	sideData.m_bFilled = true;

	/*
	 * Restore absolutely everything we temporarily altered.
	 */
	Interfaces::Globals->flCurTime =
		backupCurtime;

	Interfaces::Globals->flFrameTime =
		backupFrametime;

	player->m_angEyeAngles().y =
		backupYaw;

	player->m_flPoseParameter() =
		backupPose;

	player->m_fFlags() =
		backupFlags;

	player->m_iEFlags() =
		backupEFlags;

	player->m_flDuckAmount() =
		backupDuck;

	player->m_vecAbsVelocity() =
		backupVelocity;

	player->m_bClientSideAnimation() =
		backupClientAnimation;

	ctx.m_bUpdatingAnimations =
		backupUpdating;

	std::memcpy(
		layers,
		backupLayers,
		sizeof(backupLayers)
	);
}

void CAnimationSys::InterpolateFromLastData(
	CBasePlayer* player,
	LagRecord_t* current,
	std::optional<AnimData_t>& from,
	int side)
{
	if (!player ||
		!current ||
		!Interfaces::Globals ||
		side < 0 ||
		side >= 5)
	{
		return;
	}

	auto* state =
		player->m_pAnimState();

	auto* layers =
		player->m_AnimationLayers();

	if (!state || !layers)
		return;

	/*
	 * Never allow zero/negative/outlandish new command counts into the
	 * interpolation loop, even if this function is called somewhere
	 * other than AnimatePlayer.
	 */
	current->m_iNewCmds =
		std::clamp(
			current->m_iNewCmds,
			1,
			64
		);

	const auto prepareUpdate =
		[&]()
		{
			player->m_angEyeAngles().y =
				current->m_angEyeAngles.y;

			AnimationReplay::PrepareClock(
				Interfaces::Globals->flCurTime,
				Interfaces::Globals
				->flIntervalPerTick,
				Interfaces::Globals
				->iFrameCount,
				state->flLastUpdateTime,
				state->iLastUpdateFrame
			);

			if (side > 0)
			{
				const auto limits =
					AnimationReplay::Limits(
						state
						->flSpeedAsPortionOfWalkTopSpeed,
						state
						->flWalkToRunTransition,
						state->flDuckAmount,
						state->flDuckingSpeed,
						state->flMinBodyYaw,
						state->flMaxBodyYaw
					);

				state->flAbsYaw =
					AnimationReplay::FeetYaw(
						current
						->m_angEyeAngles.y,
						side,
						limits
					);
			}
		};

	if (!from.has_value())
	{
		std::memcpy(
			layers,
			current->m_cAnimData.m_pLayers,
			sizeof(CAnimationLayer) * 13
		);

		state->m_bLanding = false;
		state->flDurationInAir = 0.f;

		state->flLastUpdateTime =
			current->m_cAnimData
			.m_flSimulationTime -
			Interfaces::Globals
			->flIntervalPerTick;

		const auto& layer6 =
			current->m_cAnimData
			.m_pLayers[6];

		const auto& layer7 =
			current->m_cAnimData
			.m_pLayers[7];

		const auto& layer12 =
			current->m_cAnimData
			.m_pLayers[12];

		state->flFeetCycle =
			layer6.flCycle;

		if ((current->m_cAnimData.m_iFlags &
			FL_ONGROUND) &&
			!current->m_cAnimData
			.m_pLayers[5]
			.flWeight &&
			layer6.flWeight > 0.f &&
			layer6.flWeight < 1.f)
		{
			state->flMoveWeight =
				layer6.flWeight;
		}

		state->iStrafeSequence =
			layer7.nSequence;

		state->flStrafeWeight =
			layer7.flWeight;

		state->flStrafeCycle =
			layer7.flCycle;

		state->flAccelerationWeight =
			layer12.flWeight;

		Interfaces::Globals->flCurTime =
			current->m_cAnimData
			.m_flSimulationTime;

		if (current->m_cAnimData
			.m_pLayers[6]
			.flPlaybackRate != 0.f &&
			current->m_cAnimData
			.m_vecVelocity.Length() <
			1.f)
		{
			current->m_cAnimData
				.m_vecVelocity =
			{ 1.1f, 0.f, 0.f };
		}

		player->m_vecAbsVelocity() =
			current->m_cAnimData
			.m_vecVelocity;

		player->m_iEFlags() &=
			~EFL_DIRTY_ABSVELOCITY;

		if (state->iLastUpdateFrame ==
			Interfaces::Globals
			->iFrameCount)
		{
			state->iLastUpdateFrame =
				Interfaces::Globals
				->iFrameCount -
				1;
		}

		prepareUpdate();

		player->m_bClientSideAnimation() =
			true;

		ctx.m_bUpdatingAnimations =
			true;

		CRASH_MARK(
			"Anim: first-record "
			"UpdateClientsideAnimations begin"
		);

		player->UpdateClientsideAnimations();

		CRASH_MARK(
			"Anim: first-record "
			"UpdateClientsideAnimations end"
		);

		ctx.m_bUpdatingAnimations =
			false;

		player->m_bClientSideAnimation() =
			false;

		return;
	}

	auto& previous =
		from.value();

	auto& previousSide =
		previous.m_arrSides.at(side);

	if (previousSide.m_bFilled)
	{
		*state =
			previousSide.m_cAnimState;

		player->m_flPoseParameter() =
			previousSide.m_flPoseParameter;
	}

	const auto& layer6 =
		previous.m_pLayers[6];

	const auto& layer7 =
		previous.m_pLayers[7];

	const auto& layer12 =
		previous.m_pLayers[12];

	state->flFeetCycle =
		layer6.flCycle;

	if ((previous.m_iFlags & FL_ONGROUND) &&
		(current->m_cAnimData.m_iFlags &
			FL_ONGROUND) &&
		!current->m_cAnimData
		.m_pLayers[5]
		.flWeight &&
		!previous.m_pLayers[5].flWeight &&
		layer6.flWeight > 0.f &&
		layer6.flWeight < 1.f)
	{
		state->flMoveWeight =
			layer6.flWeight;
	}

	state->iStrafeSequence =
		layer7.nSequence;

	state->flStrafeWeight =
		layer7.flWeight;

	state->flStrafeCycle =
		layer7.flCycle;

	state->flAccelerationWeight =
		layer12.flWeight;

	std::memcpy(
		layers,
		previous.m_pLayers,
		sizeof(CAnimationLayer) * 13
	);

	const auto& to =
		current->m_cAnimData;

	const bool manualStandVel =
		current->m_cAnimData
		.m_pLayers[6]
		.flPlaybackRate != 0.f &&
		current->m_cAnimData
		.m_vecVelocity.Length() <=
		1.f;

	const float duckAmountDelta =
		to.m_flDuckAmount -
		previous.m_flDuckAmount;

	const Vector velocityDelta =
		to.m_vecVelocity -
		previous.m_vecVelocity;

	bool landed = false;

	Interfaces::Globals->flCurTime =
		to.m_flSimulationTime -
		TICKS_TO_TIME(
			current->m_iNewCmds
		);

	for (int i = 1;
		i <= current->m_iNewCmds;
		++i)
	{
		Interfaces::Globals->flCurTime +=
			Interfaces::Globals
			->flIntervalPerTick;

		const float lerp =
			static_cast<float>(i) /
			static_cast<float>(
				current->m_iNewCmds
				);

		player->m_flDuckAmount() =
			previous.m_flDuckAmount +
			duckAmountDelta * lerp;

		if (manualStandVel)
		{
			player->m_vecAbsVelocity() =
			{
				(i & 1) ? 1.1f : -1.1f,
				0.f,
				0.f
			};
		}
		else
		{
			player->m_vecAbsVelocity() =
				previous.m_vecVelocity +
				velocityDelta * lerp;
		}

		player->m_iEFlags() &=
			~EFL_DIRTY_ABSVELOCITY;

		if (current->m_bFixJumpFall)
		{
			player->m_fFlags() &=
				~FL_ONGROUND;

			if (!landed &&
				TIME_TO_TICKS(
					Interfaces::Globals
					->flCurTime
				) >=
				TIME_TO_TICKS(
					current
					->m_flLeftGroundTime
				))
			{
				player->m_fFlags() |=
					FL_ONGROUND;

				landed = true;
			}
		}

		if (state->iLastUpdateFrame ==
			Interfaces::Globals
			->iFrameCount)
		{
			state->iLastUpdateFrame =
				Interfaces::Globals
				->iFrameCount -
				1;
		}

		prepareUpdate();

		player->m_bClientSideAnimation() =
			true;

		ctx.m_bUpdatingAnimations =
			true;

		CRASH_MARK(
			"Anim: replay "
			"UpdateClientsideAnimations begin"
		);

		player->UpdateClientsideAnimations();

		CRASH_MARK(
			"Anim: replay "
			"UpdateClientsideAnimations end"
		);

		ctx.m_bUpdatingAnimations =
			false;

		player->m_bClientSideAnimation() =
			false;
	}
}

void PlayerEntry::Rezik(
	LagRecord_t* current)
{
	if (!current || !m_pPlayer)
		return;

	const auto& from =
		m_optPreviousData;

	if (!from.has_value())
		return;

	if ((current->m_cAnimData.m_iFlags &
		FL_ONGROUND) &&
		(from->m_iFlags & FL_ONGROUND))
	{
		const float speed =
			current->m_cAnimData
			.m_vecVelocity.Length2D();

		if (m_pPlayer
			->m_flLowerBodyYawTarget() !=
			m_pPlayer
			->m_angEyeAngles().y &&
			!current->m_cAnimData
			.m_pLayers[6]
			.flPlaybackRate)
		{
			if (Math::AngleDiff(
				m_pPlayer
				->m_flLowerBodyYawTarget(),
				m_pPlayer
				->m_angEyeAngles().y
			) > 0.f)
			{
				current->m_iResolverSide = 1;
			}
			else
			{
				current->m_iResolverSide = 2;
			}

			m_bLBYResolved = true;

			if (m_bInvertResolverOrientation)
			{
				current->m_iResolverSide =
					SWAP_RESIK_SIDE(
						current
						->m_iResolverSide
					);
			}
		}

		if (speed > 1.f)
		{
			/*
			 * Resolver comparison requires the generated baseline,
			 * left, and right hypotheses.
			 */
			if (!current->m_cAnimData
				.m_arrSides[0]
				.m_bFilled ||
				!current->m_cAnimData
				.m_arrSides[1]
				.m_bFilled ||
				!current->m_cAnimData
				.m_arrSides[2]
				.m_bFilled)
			{
				return;
			}

			const bool accelerating =
				static_cast<int>(
					current->m_cAnimData
					.m_pLayers[6]
					.flWeight *
					1000.f
					) ==
				static_cast<int>(
					from->m_pLayers[6]
					.flWeight *
					1000.f
					);

			if ((!accelerating &&
				!current->m_bAccurateVelocity) ||
				static_cast<int>(
					current->m_cAnimData
					.m_pLayers[12]
					.flWeight *
					1000.f
					))
			{
				return;
			}

			const int leftDelta =
				static_cast<int>(
					std::abs(
						current->m_cAnimData
						.m_pLayers[6]
						.flPlaybackRate -
						current->m_cAnimData
						.m_arrSides[1]
						.m_pLayers[6]
						.flPlaybackRate
					) *
					1000.f
					);

			const int rightDelta =
				static_cast<int>(
					std::abs(
						current->m_cAnimData
						.m_pLayers[6]
						.flPlaybackRate -
						current->m_cAnimData
						.m_arrSides[2]
						.m_pLayers[6]
						.flPlaybackRate
					) *
					1000.f
					);

			const int middleDelta =
				static_cast<int>(
					std::abs(
						current->m_cAnimData
						.m_pLayers[6]
						.flPlaybackRate -
						current->m_cAnimData
						.m_arrSides[0]
						.m_pLayers[6]
						.flPlaybackRate
					) *
					1000.f
					);

			if (middleDelta < leftDelta ||
				rightDelta <= leftDelta ||
				leftDelta)
			{
				if (middleDelta >= rightDelta &&
					leftDelta > rightDelta &&
					!rightDelta)
				{
					current->m_iResolverSide = 2;
					m_bLBYResolved = false;
				}
			}
			else
			{
				current->m_iResolverSide = 1;
				m_bLBYResolved = false;
			}
		}
	}
	else if (current->m_iNewCmds <= 1)
	{
		current->m_iResolverSide = 0;
	}

	current->m_iResolverSide =
		std::clamp(
			current->m_iResolverSide,
			0,
			4
		);
}

bool CAnimationSys::SetupBonesRebuilt(
	CBasePlayer* const player,
	matrix3x4a_t* bones,
	const int boneMask,
	const float time,
	const bool clampbonesinbbox)
{
	/*
	 * All of these are required before touching any state.
	 */
	if (!player ||
		!bones ||
		!Interfaces::Globals)
	{
		return false;
	}

	/*
	 * IMPORTANT:
	 *
	 * Check the studio header BEFORE modifying curtime, frametime,
	 * EFlags, or IK state. The old function changed engine state and
	 * then returned immediately when hdr was null.
	 */
	auto* hdr =
		player->m_pStudioHdr();

	if (!hdr)
		return false;

	const float backupCurTime =
		Interfaces::Globals->flCurTime;

	const float backupFrameTime =
		Interfaces::Globals->flFrameTime;

	const auto backupEFlags =
		player->m_iEFlags();

	auto* backupIk =
		player->m_pIk();

	Interfaces::Globals->flCurTime =
		time;

	Interfaces::Globals->flFrameTime =
		Interfaces::Globals
		->flIntervalPerTick;

	player->m_iEFlags() =
		backupEFlags |
		EFL_SETTING_UP_BONES;

	alignas(16) Vector pos[256]{};
	alignas(16) Quaternion q[256]{};

	uint8_t boneComputed[256]{};

	auto* ik =
		new CIKContext();

	if (!ik)
	{
		Interfaces::Globals->flCurTime =
			backupCurTime;

		Interfaces::Globals->flFrameTime =
			backupFrameTime;

		player->m_iEFlags() =
			backupEFlags;

		return false;
	}

	player->m_pIk() =
		ik;

	bool success = false;

	/*
	 * Keep all temporary engine modifications inside this single
	 * section so there is one cleanup path below.
	 */
	ik->Init(
		hdr,
		player->GetAbsAngles(),
		player->GetAbsOrigin(),
		Interfaces::Globals->flCurTime,
		Interfaces::Globals->iTickCount,
		boneMask
	);

	CRASH_MARK(
		"Bones: GetSkeleton begin"
	);

	GetSkeleton(
		player,
		hdr,
		pos,
		q,
		boneMask,
		ik
	);

	CRASH_MARK(
		"Bones: GetSkeleton end; IK begin"
	);

	ik->UpdateTargets(
		pos,
		q,
		bones,
		boneComputed
	);

	player->CalculateIKLocks(
		Interfaces::Globals->flCurTime
	);

	ik->SolveDependencies(
		pos,
		q,
		bones,
		boneComputed
	);

	CRASH_MARK(
		"Bones: IK end; BuildMatrices begin"
	);

	BuildMatrices(
		player,
		hdr,
		pos,
		q,
		bones,
		boneMask,
		boneComputed
	);

	CRASH_MARK(
		"Bones: BuildMatrices end"
	);

	if (clampbonesinbbox)
	{
		/*
		 * Enable this only if your ClampBonesInBBOX implementation /
		 * signature is verified for this build.
		 *
		 * player->ClampBonesInBBOX(
		 *     bones,
		 *     boneMask
		 * );
		 */
	}

	success = true;

	/*
	 * Restore the exact previous values rather than just clearing a bit.
	 */
	player->m_pIk() =
		backupIk;

	delete ik;

	player->m_iEFlags() =
		backupEFlags;

	Interfaces::Globals->flCurTime =
		backupCurTime;

	Interfaces::Globals->flFrameTime =
		backupFrameTime;

	return success;
}