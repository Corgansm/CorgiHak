#include "../core/hooks.h"
#include "../utils/crash_log.h"
#include "../context.h"
#include "../features/rage/autowall.h"
#include "../features/rage/antiaim.h"
#include "../features/rage/ragebot.h"
#include "../features/rage/exploits.h"
#include "../features/misc/engine_prediction.h"
#include "../features/misc/misc.h"
#include "../features/visuals/visuals.h"
#include "../core/lua/lua_engine.h"

FORCEINLINE void KeepCommunication() {
	const auto& netChannel{ Interfaces::ClientState->pNetChannel };

	if (netChannel) {
		const auto backupChokedPackets{ netChannel->iChokedPackets };
		netChannel->iChokedPackets = 0;

		netChannel->SendDatagram(0);

		--netChannel->iOutSequenceNr;
		netChannel->iChokedPackets = backupChokedPackets;
	}
}

FORCEINLINE void ShouldShift(CUserCmd& cmd) {
	if (!ctx.m_pWeapon || !ctx.m_pWeaponData)
		return;

	if (!Interfaces::ClientState->pNetChannel)
		return;

	if (Features::Exploits.m_iRechargeCmd == Interfaces::ClientState->iLastOutgoingCommand)
		return;

	const auto fakeducking{ Config::Get<bool>(Vars.MiscFakeDuck) && Config::Get<keybind_t>(Vars.MiscFakeDuckKey).enabled };

	if (ctx.m_iTicksAllowed) {
		if (((cmd.iButtons & IN_ATTACK || (cmd.iButtons & IN_ATTACK2 && ctx.m_pWeaponData->nWeaponType == WEAPONTYPE_KNIFE)) && ctx.m_bCanShoot /* && ctx.m_iTicksAllowed >= 14*/
			&& ctx.m_pWeaponData->nWeaponType >= WEAPONTYPE_KNIFE && ctx.m_pWeaponData->nWeaponType < WEAPONTYPE_C4)
			|| (!ctx.m_bExploitsEnabled || fakeducking)) {
			const bool isDTEnabled{ (Config::Get<bool>(Vars.ExploitsDoubletap) && Config::Get<keybind_t>(Vars.ExploitsDoubletapKey).enabled) };

			Features::Exploits.m_bRealCmds = (!ctx.m_bExploitsEnabled || isDTEnabled || fakeducking);
			Features::Exploits.m_iShiftAmount = ctx.m_iTicksAllowed;// Features::Exploits.m_bRealCmds ? ctx.m_iTicksAllowed : 9;

			//**( int** )Displacement::Sigs.numticks += ctx.m_iTicksAllowed; 1 line dt xD
		}
	}
}

void CreateMove(const int nSequenceNumber, const float flInputSampleFrametime, const bool bIsActive)
{
	typedef void(__thiscall* Fn)(void*, const int, const float, const bool);
	static auto oCreateMove = DTR::CreateMoveProxy.GetOriginal<Fn>();
	oCreateMove(Interfaces::Client, nSequenceNumber, flInputSampleFrametime, bIsActive);

	auto& cmd{ Interfaces::Input->pCommands[nSequenceNumber % 150] };
	auto& verifiedCmd{ Interfaces::Input->pVerifiedCommands[nSequenceNumber % 150] };

	const auto sameFrameCMD{ **(int**)Displacement::Sigs.numticks - **(int**)Displacement::Sigs.host_currentframetick > 0 };

	// Gamesense cmd.force_defensive is a trigger, not merely a one-packet visual flag.
	//
	// IMPORTANT: count the lifetime in normal CreateMove ticks, NOT command numbers.
	// DT can generate several sameFrameCMD commands in one real tick. A command-number
	// deadline lets those generated commands burn through a 14-tick defensive window
	// almost instantly, which is why Lua sometimes observed only "-13 -12".
	static int s_iLuaDefensiveTicksLeft{ 0 };

	/*if ( **( int** )Displacement::Sigs.host_currentframetick == 1 ) {
		Interfaces::ClientState->iServerTick += **( int** )Displacement::Sigs.numticks - 1;
		Interfaces::ClientState->iClientTick += **( int** )Displacement::Sigs.numticks - 1;
		Interfaces::Globals->iTickCount += **( int** )Displacement::Sigs.numticks - 1;
	}*/

	ctx.GetLocal();
	CRASH_MARK("CM: start");

	if (!Config::Get<bool>(Vars.RagebotLagcompensation))
	{
		if (Displacement::Cvars.cl_predict->GetBool())
			Displacement::Cvars.cl_predict->SetValue(0);
	}

	static bool did{};

	if (!ctx.m_pLocal)
	{
		ctx.m_iLastPeekCmdNum = 0;
		s_iLuaDefensiveTicksLeft = 0;
		did = false;

		if (Displacement::Cvars.cl_interp->GetFloat() != 0.015625f)
			Displacement::Cvars.cl_interp->SetValue(0.015625f);

		if (Displacement::Cvars.cl_interp_ratio->GetInt() != 2)
			Displacement::Cvars.cl_interp_ratio->SetValue(2);

		CRASH_MARK("CM:90 Features::Misc.ClearIncomingSequences( );");
		Features::Misc.ClearIncomingSequences();
		ctx.m_iSentCmds.clear();
		return;
	}

	const auto nci{ Interfaces::Engine->GetNetChannelInfo() };
	if (!nci)
		return;

	if (Menu::m_bOpened)
		cmd.iButtons &= ~(IN_ATTACK | IN_ATTACK2);

	cmd.iButtons &= ~(IN_SPEED | IN_WALK);

	// same lerp the server computes for us ( CGameClient::SetUpdateRate + interp ratio clamping )
	{
		const auto updateRate{ std::clamp(Displacement::Cvars.cl_updaterate->GetFloat(),
			Displacement::Cvars.sv_minupdaterate->GetFloat(), Displacement::Cvars.sv_maxupdaterate->GetFloat()) };

		auto lerpRatio{ Displacement::Cvars.cl_interp_ratio->GetFloat() };
		if (lerpRatio == 0.f)
			lerpRatio = 1.f;

		const auto minRatio{ Displacement::Cvars.sv_client_min_interp_ratio->GetFloat() };
		const auto maxRatio{ Displacement::Cvars.sv_client_max_interp_ratio->GetFloat() };
		if (minRatio != -1.f && maxRatio >= minRatio)
			lerpRatio = std::clamp(lerpRatio, minRatio, maxRatio);

		ctx.m_flLerpTime = std::max(Displacement::Cvars.cl_interp->GetFloat(), lerpRatio / std::max(updateRate, 1.f));
	}

	ctx.m_iRealOutLatencyTicks = std::max((ctx.m_iLast4Deltas[0] + ctx.m_iLast4Deltas[1] + ctx.m_iLast4Deltas[2] + ctx.m_iLast4Deltas[3]) / 4, 0);
	ctx.m_flOutLatency = nci->GetLatency(FLOW_INCOMING);
	ctx.m_flInLatency = nci->GetLatency(FLOW_INCOMING);

	ctx.m_angOriginalViewangles = cmd.viewAngles;

	ctx.m_pWeapon = ctx.m_pLocal->GetWeapon();
	ctx.m_pWeaponData = ctx.m_pWeapon ? ctx.m_pWeapon->GetCSWeaponData() : nullptr;

	static bool prev{};
	auto updateClantag = [](const char* tag) -> void {
		using Fn = int(__fastcall*)(const char*, const char*);
		static auto fn = reinterpret_cast<Fn>(Displacement::Sigs.ClanTag);

		fn(tag, tag);
		};

	// clan tag: static, or typed out / held / erased with frames picked from the server tick,
	// so everyone running it shows the same frame at the same time
	static std::string lastTag{ "\x01" };
	if (Config::Get<bool>(Vars.MiscClantag)) {
		static const char* frames[]{
			"", "C", "Co", "Cor", "Corg", "Corgi", "CorgiH", "CorgiHa", "CorgiHak",
			"CorgiHak", "CorgiHak", "CorgiHak", "CorgiHak", "CorgiHak",
			"orgiHak", "rgiHak", "giHak", "iHak", "Hak", "ak", "k", "" };
		constexpr int frameCount{ sizeof(frames) / sizeof(frames[0]) };

		std::string tag{ "CorgiHak" };
		if (Config::Get<int>(Vars.MiscClantagMode) == 1) {
			const float serverTime{ TICKS_TO_TIME(Interfaces::ClientState->iServerTick) };
			tag = frames[static_cast<int>(serverTime / 0.35f) % frameCount];
		}

		if (tag != lastTag) {
			lastTag = tag;
			updateClantag(lastTag.c_str());
		}
	}
	else if (lastTag != "\x01") {
		lastTag = "\x01";
		updateClantag(_(""));
	}

	if (ctx.m_pLocal->IsDead()) {
		Interfaces::Input->bCameraInThirdPerson = false;
		ctx.m_iLastPeekCmdNum = 0;
		s_iLuaDefensiveTicksLeft = 0;
		Features::Exploits.m_iShiftAmount = 0;
		ctx.m_iTicksAllowed = 0;
		CRASH_MARK("CM:155 Features::Misc.ClearIncomingSequences( );");
		Features::Misc.ClearIncomingSequences();
		ctx.m_iSentCmds.clear();
		return;
	}

	if (Config::Get<bool>(Vars.MiscFakePing))
	{
		CRASH_MARK("CM: Features::Misc.UpdateIncomingSequences( Interfaces::ClientState->pNetC"); Features::Misc.UpdateIncomingSequences(Interfaces::ClientState->pNetChannel);
	}
	else
		Features::Misc.ClearIncomingSequences();

	if (cmd.iButtons & IN_ATTACK)
		ctx.m_iLastStopTime = Interfaces::Globals->flRealTime;

	//if ( !did && ctx.m_pWeapon )
		//did = Features::EnginePrediction.ModifyDatamap( );

	static float prevSpawnTime = ctx.m_pLocal->m_flSpawnTime();

	if (prevSpawnTime != ctx.m_pLocal->m_flSpawnTime()) {
		//Interfaces::Engine->ClientCmdUnrestricted( _( "cl_fullupdate" ) );
		//ctx.m_pLocal->m_pAnimState( )->Reset( );

		lastTag = "\x02"; // respawn: re-send the clan tag next tick
		ctx.m_cLocalData = {};
		ctx.m_iTicksAllowed = 0;
		s_iLuaDefensiveTicksLeft = 0;

		ctx.m_iHighestTickbase = 0;
		ctx.m_iLastShotNumber = 0;
		ctx.m_iSentCmds.clear();

		for (int i{}; i < 13; ++i) {
			if (i < 4)
				ctx.m_iLast4Deltas[i] = 0;

			auto& layer{ ctx.m_pAnimationLayers[i] };
			auto& layer2{ ctx.m_pLocal->m_AnimationLayers()[i] };
			layer.flCycle = layer.flWeight = layer.flPlaybackRate = layer.nSequence =
				layer2.flCycle = layer2.flWeight = layer2.flPlaybackRate = layer2.nSequence = 0;
		}

		//ctx.m_cLocalAnimData = { };

		CRASH_MARK("CM:197 Features::EnginePrediction.Reset( );");
		Features::EnginePrediction.Reset();

		prevSpawnTime = ctx.m_pLocal->m_flSpawnTime();

		QAngle angles{};
		Interfaces::Engine->GetViewAngles(angles);
		angles.z = 0;

		Interfaces::Engine->SetViewAngles(angles);
	}

	ctx.m_bSendPacket = false;

	if (Features::Exploits.ShouldRecharge()) {
		cmd.iTickCount = INT_MAX;
		verifiedCmd.userCmd = cmd;
		verifiedCmd.uHashCRC = cmd.GetChecksum();
		KeepCommunication();
		return;
	}

	ctx.m_bInCreatemove = true;

	if ((ctx.m_iTicksAllowed && Config::Get<bool>(Vars.ExploitsDoubletapDefensive) && ((Config::Get<bool>(Vars.ExploitsDoubletap) && Config::Get<keybind_t>(Vars.ExploitsDoubletapKey).enabled) || !Config::Get<bool>(Vars.ExploitsDefensiveBreakAnimations))) || (Config::Get<bool>(Vars.AntiaimFakeLagInPeek) && !ctx.m_iTicksAllowed))
	{
		ctx.m_bInPeek = Features::Misc.InPeek(Interfaces::Input->pCommands[Interfaces::ClientState->iLastOutgoingCommand % 150]);

		if (ctx.m_bInPeek)
			ctx.m_iLastPeekCmdNum = cmd.iCommandNumber;
	}
	else
		ctx.m_bInPeek = false;

	ctx.m_bCanPenetrate = Features::Autowall.CanPenetrate();

	auto& localData{ ctx.m_cLocalData.at(cmd.iCommandNumber % 150) };
	localData.Save(ctx.m_pLocal, cmd, ctx.m_pWeapon);

	// Lua must run BEFORE we choose the prediction tickbase. In the old ordering,
	// cmd.force_defensive was not observed until after EnginePrediction::RunCommand,
	// so the request could only affect a later packet and never the current prediction.
	CRASH_MARK("CM: Lua::OnSetupCommand( cmd );");
	Lua::OnSetupCommand(cmd);

	const bool luaDefensiveRequest{
		ctx.m_iTicksAllowed > 0
		&& !sameFrameCMD
		&& Lua::ForceDefensive(cmd.iCommandNumber)
	};

	if (luaDefensiveRequest) {
		// Snapshot the native charge at trigger time. A full CorgiHak charge is normally
		// 14. Retriggering can only extend/refresh the remaining real-tick budget.
		s_iLuaDefensiveTicksLeft = std::max(
			s_iLuaDefensiveTicksLeft,
			std::max(ctx.m_iTicksAllowed, 1)
		);
	}

	const bool luaDefensiveActive{
		ctx.m_iTicksAllowed > 0
		&& s_iLuaDefensiveTicksLeft > 0
	};

	// Set this before prediction as well. Several exploit/prediction paths key off this
	// state, and setting it after RunCommand was too late.
	if (luaDefensiveActive)
		Features::Exploits.m_bWasDefensiveTick = true;

	// get some sleep and tidy this shit up later
	const auto extraTicks{ **(int**)Displacement::Sigs.numticks - **(int**)Displacement::Sigs.host_currentframetick };

	static auto hadExtraTicks{ extraTicks > 0 };

	const auto runningDefensive{ Config::Get<bool>(Vars.ExploitsDoubletapDefensive) && ctx.m_iTicksAllowed };

	const auto newCmdsWithDefensive{ std::min(Interfaces::ClientState->nChokedCommands + 1 + extraTicks + ctx.m_iTicksAllowed, 16) };
	const auto newCmdsReal{ std::min(Interfaces::ClientState->nChokedCommands + 1 + extraTicks, 16) };

	const auto tbDefensive{ Features::Exploits.AdjustTickbase(newCmdsWithDefensive) + Interfaces::ClientState->nChokedCommands };
	const auto tbReal{ Features::Exploits.AdjustTickbase(newCmdsReal) + Interfaces::ClientState->nChokedCommands };

	const auto actualShiftTicks{ (ctx.m_pWeaponData && ctx.m_pWeaponData->nWeaponType >= WEAPONTYPE_KNIFE && ctx.m_pWeaponData->nWeaponType < WEAPONTYPE_C4) ? ctx.m_iTicksAllowed : 0 };

	const auto newCmds{ std::min(Interfaces::ClientState->nChokedCommands + 1 + extraTicks + actualShiftTicks, 16) };
	//const auto realTickbase{ Features::Exploits.AdjustTickbase( newCmds ) + Interfaces::ClientState->nChokedCommands };
	// Lua force_defensive must select the same defensive tickbase used by the native
	// defensive system. Previously m_bWasDefensiveTick was set only after prediction and
	// realTickbase ignored it entirely, which reduced Lua defensive to 1-2 incidental samples.
	const bool useDefensiveTickbase{
		luaDefensiveActive
		|| ((ctx.m_bSafeFromDefensive && ctx.m_bInPeek) && runningDefensive)
	};
	const auto realTickbase{
		useDefensiveTickbase
			? tbDefensive
			: (Features::Exploits.AdjustTickbase(newCmds) + Interfaces::ClientState->nChokedCommands)
	};

	ctx.m_flFixedCurtime = TICKS_TO_TIME(realTickbase);

	CRASH_MARK("CM:256 Features::EnginePrediction.RestoreNetvars( cmd.iCommandNumber - 1 );");
	Features::EnginePrediction.RestoreNetvars(cmd.iCommandNumber - 1);

	// server changed something we can't predict ( velocity modifier ): re-predict every command
	CRASH_MARK("CM:259 Features::EnginePrediction.PreUpdate( );");
	Features::EnginePrediction.PreUpdate();

	// pred gets fucked bc of restoring entity, which happens when we call cl_readpackets
	Interfaces::Prediction->Update(Interfaces::ClientState->iDeltaTick,
		Interfaces::ClientState->iDeltaTick > 0,
		Interfaces::ClientState->iLastCommandAck,
		Interfaces::ClientState->iLastOutgoingCommand + Interfaces::ClientState->nChokedCommands);

	ctx.m_bCanShoot = ctx.m_pLocal->CanShoot();

	// double tap follow-up on a server that drops shifted commands: wait until the shift is acknowledged
	if (ctx.m_bCanShoot && Features::Exploits.HoldFollowup())
		ctx.m_bCanShoot = false;

	CRASH_MARK("CM:269 Features::Antiaim.FakeLag( cmd.iCommandNumber );");
	Features::Antiaim.FakeLag(cmd.iCommandNumber);
	CRASH_MARK("CM:270 Features::Misc.Movement( cmd );");
	Features::Misc.Movement(cmd);

	// gamesense order: run_command fires as the command starts running, predict_command once it was predicted
	// ( momentum's defensive module pairs them by command_number and reads the predicted m_nTickBase )
	CRASH_MARK("CM: Lua::OnRunCommand( cmd );");
	Lua::OnRunCommand(cmd);

	CRASH_MARK("CM:272 Features::EnginePrediction.RunCommand( cmd );");
	Features::EnginePrediction.RunCommand(cmd);
	CRASH_MARK("CM: Lua::OnPredictCommand( cmd );");
	Lua::OnPredictCommand(cmd);
	{
		ctx.m_vecEyePos = ctx.m_pLocal->GetEyePosition(ctx.m_angOriginalViewangles.y, ctx.m_angOriginalViewangles.x);

		if (!sameFrameCMD)
		{
			CRASH_MARK("CM: Features::Ragebot.Main( cmd, true );"); Features::Ragebot.Main(cmd, true);
		}

		if (!Features::Ragebot.m_bShouldStop)
		{
			cmd.flForwardMove = Features::Misc.m_ve2SubAutostopMovement.x;
			cmd.flSideMove = Features::Misc.m_ve2SubAutostopMovement.y;
		}
		else
		{
			Features::Ragebot.m_bShouldStop = false;
			ctx.m_iLastStopTime = Interfaces::Globals->flRealTime;
		}

		CRASH_MARK("CM:290 Features::Misc.AutoPeek( cmd );");
		Features::Misc.AutoPeek(cmd);

		localData.m_bCanAA = !Features::Antiaim.Condition(cmd, true);
	}
	CRASH_MARK("CM:294 Features::EnginePrediction.Finish( );");
	Features::EnginePrediction.Finish();

	if (sameFrameCMD && ctx.m_pWeapon && !ctx.m_pWeapon->IsGrenade())
		cmd.iButtons &= ~IN_ATTACK;

	if (Interfaces::ClientState->nChokedCommands >= 15 - ctx.m_iTicksAllowed)
		ctx.m_bSendPacket = true;

	if (!ctx.m_bFakeDucking && cmd.iButtons & IN_ATTACK && ctx.m_pWeapon && !ctx.m_pWeapon->IsGrenade() && ctx.m_bCanShoot)
		ctx.m_bSendPacket = true;

	if ((cmd.iButtons & IN_ATTACK || (cmd.iButtons & IN_ATTACK2 && ctx.m_pWeaponData->nWeaponType == WEAPONTYPE_KNIFE)) && ctx.m_bCanShoot)
	{
		ctx.m_iLastShotNumber = cmd.iCommandNumber;
		ctx.m_iLastStopTime = Interfaces::Globals->flRealTime;
	}

	ShouldShift(cmd);

	// lua allow_send_packet / no_choke ( never past the 15 tick choke cap )
	if (Lua::SendPacketOverride() == 1)
		ctx.m_bSendPacket = true;
	else if (Lua::SendPacketOverride() == 0 && Interfaces::ClientState->nChokedCommands < 14 - ctx.m_iTicksAllowed)
		ctx.m_bSendPacket = false;

	if (Features::Antiaim.m_bFlickNow)
		ctx.m_bSendPacket = true;

	if (sameFrameCMD)
	{
		hadExtraTicks = true;
		ctx.m_bSendPacket = false;
	}

	if (ctx.m_bSendPacket)
	{
		bool safeFromDefensive{};
		if (tbDefensive > ctx.m_iHighestTickbase)
			safeFromDefensive = false;
		else
			safeFromDefensive = true;

		if (Features::Exploits.m_iRechargeCmd == Interfaces::ClientState->iLastOutgoingCommand)
			Features::Exploits.m_bWasDefensiveTick = false;
		else
		{
			// Lua defensive owns m_bWasDefensiveTick for the entire native-charge window.
			// Consume only the trigger request once the first real packet goes out; the
			// separate active-window state continues to drive prediction afterwards.
			if (luaDefensiveActive)
			{
				Features::Exploits.m_bWasDefensiveTick = true;
				if (luaDefensiveRequest)
					Lua::ConsumeDefensive();
			}
			else if (ctx.m_iTicksAllowed && Config::Get<bool>(Vars.ExploitsDoubletapDefensive))
			{
				if (!safeFromDefensive && ((Config::Get<bool>(Vars.ExploitsDefensiveInAir) && Features::Misc.m_bWasJumping) || ctx.m_bInPeek) && Features::Exploits.m_bWasDefensiveTick)
					Features::Exploits.m_bWasDefensiveTick = false;
				else
					Features::Exploits.m_bWasDefensiveTick = Config::Get<bool>(Vars.ExploitsDefensiveBreakAnimations) ? !Features::Exploits.m_bWasDefensiveTick : true;
			}
			else
				Features::Exploits.m_bWasDefensiveTick = false;
		}

		ctx.m_iSentCmds.push_back(cmd.iCommandNumber);
	}
	else
		KeepCommunication();

	const auto tb{ Features::Exploits.m_bWasDefensiveTick || Features::Exploits.m_iShiftAmount ? tbDefensive : tbReal };

	if (ctx.m_bSendPacket)
		hadExtraTicks = false;

	ctx.m_bRevolverCanShoot = false;

	if (ctx.m_pWeapon && ctx.m_pWeapon->m_iItemDefinitionIndex() == WEAPON_REVOLVER)
	{
		static float m_flPostPoneFireReadyTime{};

		if (ctx.m_bRevolverCanCock && !(cmd.iButtons & IN_RELOAD))
		{
			if (TICKS_TO_TIME(tb) < m_flPostPoneFireReadyTime)
				cmd.iButtons |= IN_ATTACK;
			else if (TICKS_TO_TIME(tb) < ctx.m_pWeapon->m_flNextSecondaryAttack())
				cmd.iButtons |= IN_ATTACK2;
			else
				m_flPostPoneFireReadyTime = TICKS_TO_TIME(tb) + 0.234375f;

			ctx.m_bRevolverCanShoot = TICKS_TO_TIME(tb) > m_flPostPoneFireReadyTime;
		}
		else
		{
			cmd.iButtons &= ~IN_ATTACK;
			m_flPostPoneFireReadyTime = Interfaces::Globals->flCurTime + 0.234375f;
		}
	}

	CRASH_MARK("CM:379 Features::Misc.MoveMINTFix( cmd, ctx.m_angOriginalViewangles, ctx.m_pL");
	Features::Misc.MoveMINTFix(cmd, ctx.m_angOriginalViewangles, ctx.m_pLocal->m_fFlags(), ctx.m_pLocal->m_MoveType());
	CRASH_MARK("CM:380 Features::Misc.NormalizeMovement( cmd );");
	Features::Misc.NormalizeMovement(cmd);

	verifiedCmd.userCmd = cmd;
	verifiedCmd.uHashCRC = cmd.GetChecksum();

	// Consume one defensive step only on a normal CreateMove command. Extra commands
	// generated in the same engine frame remain defensive, but do not shorten the window.
	if (luaDefensiveActive && !sameFrameCMD && s_iLuaDefensiveTicksLeft > 0)
		--s_iLuaDefensiveTicksLeft;

	ctx.m_bInCreatemove = false;
}

void __declspec(naked) __fastcall Hooks::hkCreateMoveProxy(uint8_t* ecx, uint8_t*, int sequenceNumber, float inputSampleFrametime, bool active)
{
	__asm
	{
		push ebp
		mov ebp, esp

		push eax
		lea eax, [ecx]
		pop eax

		mov ctx.m_bSendPacket, bl

		push ebx
		push esi
		push edi
	}

	CreateMove(sequenceNumber, inputSampleFrametime, active);

	__asm {
		pop edi
		pop esi
		pop ebx

		mov bl, ctx.m_bSendPacket

		mov esp, ebp
		pop ebp

		ret 0xC
	}
}
