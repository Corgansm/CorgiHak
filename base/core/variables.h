#pragma once
#include "config.h"
#include "input_manager.h"
#include <array>

#define ADD_PLAYER_VAR(type, name, def) C_ADD_VARIABLE( type, name##Local, def );C_ADD_VARIABLE( type, name##Team, def );C_ADD_VARIABLE( type, name##Enemy, def );
// custom ( momentum ) anti-aim: one variable per player state
#define ADD_AA_STATE_VAR(type, name, def) C_ADD_VARIABLE( type, name##Default, def );C_ADD_VARIABLE( type, name##Stand, def );C_ADD_VARIABLE( type, name##Move, def );C_ADD_VARIABLE( type, name##Slow, def );C_ADD_VARIABLE( type, name##Duck, def );C_ADD_VARIABLE( type, name##Air, def );C_ADD_VARIABLE( type, name##AirDuck, def );C_ADD_VARIABLE( type, name##Fakelag, def );C_ADD_VARIABLE( type, name##Defensive, def );
#define ADD_RAGE_VAR(type, name, def) C_ADD_VARIABLE( type, name##Pistol, def );C_ADD_VARIABLE( type, name##HeavyPistol, def );C_ADD_VARIABLE( type, name##SMG, def );C_ADD_VARIABLE( type, name##Rifle, def );C_ADD_VARIABLE( type, name##Shotgun, def );C_ADD_VARIABLE( type, name##AWP, def );C_ADD_VARIABLE( type, name##Scout, def );C_ADD_VARIABLE( type, name##Auto, def );C_ADD_VARIABLE( type, name##Machine, def );

struct Variables_t {
	/* VISUALS */
	// player esp
	ADD_PLAYER_VAR( bool, VisEnable, false );
	ADD_PLAYER_VAR( bool, VisName, false );
	ADD_PLAYER_VAR( Color, VisNameCol, Color(255,255,255) );
	ADD_PLAYER_VAR( bool, VisBox, false );
	ADD_PLAYER_VAR( Color, VisBoxCol, Color( 255, 255, 255 ) );
	ADD_PLAYER_VAR( bool, VisHealth, false );
	ADD_PLAYER_VAR( bool, VisHealthOverride, false );
	ADD_PLAYER_VAR( Color, VisHealthCol, Color( 255, 255, 255 ) );
	ADD_PLAYER_VAR( bool, VisAmmo, false );
	ADD_PLAYER_VAR( Color, VisAmmoCol, Color( 255, 107, 107 ) );	
	ADD_PLAYER_VAR( bool, VisSkeleton, false );
	ADD_PLAYER_VAR( Color, VisSkeletonCol, Color( 255, 255, 255 ) );	
	ADD_PLAYER_VAR( bool, VisGlow, false );
	ADD_PLAYER_VAR( Color, VisGlowCol, Color( 255, 255, 255 ) );
	ADD_PLAYER_VAR( bool, VisOOF, false );
	ADD_PLAYER_VAR( Color, VisOOFCol, Color( 255, 255, 255 ) );

	ADD_PLAYER_VAR( bool, VisWeapIcon, false );
	ADD_PLAYER_VAR( bool, VisWeapText, false );
	ADD_PLAYER_VAR( Color, VisWeapCol, Color( 255, 255, 255 ) );

	ADD_PLAYER_VAR( bool, VisFlagExploit, false );
	ADD_PLAYER_VAR( bool, VisFlagBLC, false );
	ADD_PLAYER_VAR( bool, VisFlagC4, false );
	ADD_PLAYER_VAR( bool, VisFlagArmor, false );
	ADD_PLAYER_VAR( bool, VisFlagFlash, false );
	ADD_PLAYER_VAR( bool, VisFlagReload, false );
	ADD_PLAYER_VAR( bool, VisFlagScoped, false );
	ADD_PLAYER_VAR( bool, VisFlagDefusing, false );

	// chams
	ADD_PLAYER_VAR( bool, ChamVis, false );
	ADD_PLAYER_VAR( Color, ChamVisCol, Color( 255, 107, 107 ) );
	ADD_PLAYER_VAR( bool, ChamHid, false );
	ADD_PLAYER_VAR( Color, ChamHidCol, Color( 255, 107, 107 ) );
	ADD_PLAYER_VAR( bool, ChamDouble, false );
	ADD_PLAYER_VAR( Color, ChamDoubleCol, Color( 255, 107, 107 ) );
	ADD_PLAYER_VAR( int, ChamDoubleMat, 0 );
	ADD_PLAYER_VAR( bool, ChamDoubleZ, false );
	ADD_PLAYER_VAR( int, ChamMatVisible, 0 );
	ADD_PLAYER_VAR( int, ChamMatInvisible, 0 );
	ADD_PLAYER_VAR( int, ChamGlowStrength, 0 );

	C_ADD_VARIABLE( bool, ChamBacktrack, false );
	C_ADD_VARIABLE( Color, ChamBacktrackCol, Color( 255, 107, 107 ) );
	C_ADD_VARIABLE( int, ChamBacktrackMat, 0 );

	C_ADD_VARIABLE( bool, MiscHitMatrix, false );
	C_ADD_VARIABLE( int, MiscShotVisualizationType, 0 );
	C_ADD_VARIABLE( bool, MiscHitMatrixXQZ, false );
	C_ADD_VARIABLE( Color, MiscHitMatrixCol, Color( 255, 255, 255 ) );
	C_ADD_VARIABLE( int, MiscHitMatrixMat, 0 );
	C_ADD_VARIABLE( float, MiscHitMatrixTime, 0 );

	C_ADD_VARIABLE( bool, ChamDesync, false );
	C_ADD_VARIABLE( Color, ChamDesyncCol, Color( 255, 107, 107 ) );
	C_ADD_VARIABLE( int, ChamDesyncMat, 0 );
	C_ADD_VARIABLE( int, ChamLocalTransparencyInScope, 100 );

	C_ADD_VARIABLE( bool, ChamHand, false );
	C_ADD_VARIABLE( Color, ChamHandCol, Color( 255, 107, 107 ) );
	C_ADD_VARIABLE( int, ChamHandMat, 0 );
	C_ADD_VARIABLE( bool, ChamHandOverlay, false );
	C_ADD_VARIABLE( Color, ChamHandOverlayCol, Color( 255, 107, 107 ) );
	C_ADD_VARIABLE( int, ChamHandOverlayMat, 0 );
	C_ADD_VARIABLE( int, ChamHandGlow, 0 );
	C_ADD_VARIABLE( int, ChamWeaponGlow, 0 );

	C_ADD_VARIABLE( bool, ChamWeapon, false );
	C_ADD_VARIABLE( Color, ChamWeaponCol, Color( 255, 107, 107 ) );
	C_ADD_VARIABLE( int, ChamWeaponMat, 0 );
	C_ADD_VARIABLE( bool, ChamWeaponOverlay, false );
	C_ADD_VARIABLE( Color, ChamWeaponOverlayCol, Color( 255, 107, 107 ) );
	C_ADD_VARIABLE( int, ChamWeaponOverlayMat, 0 );

	// other
	C_ADD_VARIABLE( bool, RemovalScope, false );
	C_ADD_VARIABLE( bool, RemovalFlash, false );
	C_ADD_VARIABLE( bool, RemovalPunch, false );
	C_ADD_VARIABLE( bool, RemovalPostProcess, false );
	C_ADD_VARIABLE( bool, RemovalZoom, false );
	C_ADD_VARIABLE( bool, RemovalSmoke, false );

	C_ADD_VARIABLE( int, SecondZoomAmt, 0 );

	C_ADD_VARIABLE( int, VisPlayerInterpolation, 1 );
	C_ADD_VARIABLE( bool, VisRecordAnims, false );
	C_ADD_VARIABLE( bool, VisPenetrationCrosshair, false );
	C_ADD_VARIABLE( bool, VisGrenadeWarning, false );
	C_ADD_VARIABLE( bool, VisGrenadePrediction, false );
	C_ADD_VARIABLE( Color, VisGrenadePredictionCol, Color( 255, 255, 255 ) );
	C_ADD_VARIABLE( bool, VisThirdPerson, false );
	C_ADD_VARIABLE( keybind_t, VisThirdPersonKey, { } );
	C_ADD_VARIABLE( int, VisThirdPersonDistance, 130 );
	C_ADD_VARIABLE( bool, VisDroppedWeapon, false );
	C_ADD_VARIABLE( Color, VisDroppedWeaponCol, Color( 255, 255, 255 ) );
	C_ADD_VARIABLE( bool, VisGrenadesEnemy, false );
	C_ADD_VARIABLE( Color, VisGrenadesEnemyCol, Color( 255, 107, 107 ) );
	C_ADD_VARIABLE( bool, VisGrenadesTeam, false );
	C_ADD_VARIABLE( Color, VisGrenadesTeamCol, Color( 255, 107, 107 ) );
	C_ADD_VARIABLE( bool, VisBomb, false );
	C_ADD_VARIABLE( bool, VisLocalBulletImpacts, false );
	C_ADD_VARIABLE( Color, VisLocalBulletImpactsCol, Color( 255, 107, 107 ) );
	C_ADD_VARIABLE( bool, VisClientBulletImpacts, false );
	C_ADD_VARIABLE( Color, VisClientBulletImpactsCol, Color( 255, 107, 107 ) );
	C_ADD_VARIABLE( bool, VisServerBulletImpacts, false );
	C_ADD_VARIABLE( Color, VisServerBulletImpactsCol, Color( 255, 107, 107 ) );

	C_ADD_VARIABLE( bool, VisLocalBulletTracers, false );
	C_ADD_VARIABLE( Color, VisLocalBulletTracersCol, Color( 255, 107, 107 ) );	
	
	C_ADD_VARIABLE( bool, VisOtherBulletTracers, false );
	C_ADD_VARIABLE( Color, VisOtherBulletTracersCol, Color( 255, 107, 107 ) );

	C_ADD_VARIABLE( int, VisBulletTracersType, 0 );


	C_ADD_VARIABLE( bool, WorldModulation, false );
	C_ADD_VARIABLE( Color, WorldModulationCol, Color( 255, 107, 107 ) );

	C_ADD_VARIABLE( bool, VisWorldSkyboxMod, false );
	C_ADD_VARIABLE( Color, WorldSkyboxCol, Color( 255, 107, 107 ) );

	C_ADD_VARIABLE( bool, VisWorldPropMod, false );
	C_ADD_VARIABLE( Color, VisWorldPropCol, Color( 255, 107, 107 ) );

	C_ADD_VARIABLE( bool, WorldFullbright, false );

	C_ADD_VARIABLE( bool, WorldAmbientLighting, false );
	C_ADD_VARIABLE( Color, WorldAmbientLightingCol, Color( 255, 255, 255 ) );

	C_ADD_VARIABLE( int, VisWorldSkybox, 0 );
	C_ADD_VARIABLE( std::string, VisWorldSkyboxCustom, "" );
	C_ADD_VARIABLE( bool, VisWorldBloom, false );
	C_ADD_VARIABLE( int, VisWorldBloomScale, 0 );
	C_ADD_VARIABLE( int, VisWorldBloomAmbience, 0 );
	C_ADD_VARIABLE( int, VisWorldBloomExposure, 0 );

	C_ADD_VARIABLE( int, VisWorldWeather, 0 );

	C_ADD_VARIABLE( bool, VisWorldFog, false );
	C_ADD_VARIABLE( Color, VisWorldFogCol, Color( 255, 255, 255 ) );
	C_ADD_VARIABLE( int, VisWorldFogDistance, 0 );
	C_ADD_VARIABLE( int, VisWorldFogDensity, 0 );
	C_ADD_VARIABLE( int, VisWorldFogHDR, 0 );


	/* MISC */
	// movement
	C_ADD_VARIABLE( bool, MiscBunnyhop, false );
	C_ADD_VARIABLE( bool, MiscAutostrafe, false );
	C_ADD_VARIABLE( int, MiscAutostrafeSpeed, 30 );
	C_ADD_VARIABLE( int, MiscLocalAnimSway, 20 );
	//C_ADD_VARIABLE( bool, MiscCrouchInAir, false );
	//C_ADD_VARIABLE( bool, MiscAccurateWalk, false );
	C_ADD_VARIABLE( bool, MiscInfiniteStamina, false );
	C_ADD_VARIABLE( bool, MiscQuickStop, false );
	C_ADD_VARIABLE( bool, MiscSlideWalk, false );
	C_ADD_VARIABLE( bool, MiscSlowWalk, false );
	C_ADD_VARIABLE( keybind_t, MiscSlowWalkKey, { } );
	C_ADD_VARIABLE( bool, MiscFakeDuck, false );
	C_ADD_VARIABLE( keybind_t, MiscFakeDuckKey, { } );
	C_ADD_VARIABLE( bool, MiscAutoPeek, false );
	C_ADD_VARIABLE( bool, MiscAutoPeekOnRelease, false );
	C_ADD_VARIABLE( keybind_t, MiscAutoPeekKey, { } );
	C_ADD_VARIABLE( Color, MiscAutoPeekCol, Color( 255, 107, 107 ) );

	// general
	C_ADD_VARIABLE( bool, MiscWatermark, true );
	C_ADD_VARIABLE( bool, MiscForceCrosshair, false );
	C_ADD_VARIABLE( bool, MiscClantag, false );
	C_ADD_VARIABLE( bool, MiscKeybindList, false );
	C_ADD_VARIABLE( bool, MiscCustomModelChanger, false );
	C_ADD_VARIABLE( std::string, MiscCustomModelChangerString, "" );
	C_ADD_VARIABLE( int, MiscKeybindPosX, 0 );
	C_ADD_VARIABLE( int, MiscKeybindPosY, 0 );
	C_ADD_VARIABLE( bool, MiscAspectRatio, false );
	C_ADD_VARIABLE( float, MiscAspectRatioAmt, 1.4f );
	C_ADD_VARIABLE( int, MiscFOV, 0 );
	C_ADD_VARIABLE( int, MiscPlayerModelT, 0 );
	C_ADD_VARIABLE( int, MiscPlayerModelCT, 0 );
	C_ADD_VARIABLE( bool, MiscPreserveKillfeed, false );
	C_ADD_VARIABLE( bool, MiscFakePing, false );
	C_ADD_VARIABLE( int, MiscWeaponVolume, 100 );
	C_ADD_VARIABLE( bool, MiscWorldHitmarker, 0 );
	C_ADD_VARIABLE( bool, MiscScreenHitmarker, 0 );
	C_ADD_VARIABLE( int, MiscScreenHitmarkerSize, 0 );
	C_ADD_VARIABLE( int, MiscScreenHitmarkerGap, 0 );	
	C_ADD_VARIABLE( int, MiscWorldHitmarkerSize, 0 );
	C_ADD_VARIABLE( int, MiscWorldHitmarkerGap, 0 );
	C_ADD_VARIABLE( bool, MiscDamageMarker, false );
	C_ADD_VARIABLE( Color, MiscDamageMarkerCol, Color( 255, 255, 255 ) );
	C_ADD_VARIABLE( int, MiscHitSound, 0 );
	C_ADD_VARIABLE( int, MiscKillSound, 0 );
	C_ADD_VARIABLE( bool, VisKillEffect, true );
	C_ADD_VARIABLE( Color, VisKillEffectCol, Color( 255, 149, 46 ) );
	C_ADD_VARIABLE( bool, VisKillFlash, true );
	C_ADD_VARIABLE( int, MiscClantagMode, 1 );          // 0 static, 1 animated ( synced to server time )
	C_ADD_VARIABLE( bool, MiscSpectatorList, false );
	C_ADD_VARIABLE( int, MiscSpectatorPosX, 20 );
	C_ADD_VARIABLE( int, MiscSpectatorPosY, 420 );
	C_ADD_VARIABLE( bool, ChamDesyncPulse, true );      // fake chams breathe and fade with the desync amount
	C_ADD_VARIABLE( std::string, MiscCustomHitSound, "" );

	C_ADD_VARIABLE( bool, MiscBuyBot, false );
	C_ADD_VARIABLE( int, MiscBuyBotPrimary, 0 );
	C_ADD_VARIABLE( int, MiscBuyBotSecondary, 0 );
	C_ADD_VARIABLE( bool, MiscBuyBotOtherTaser, false );
	C_ADD_VARIABLE( bool, MiscBuyBotOtherArmor, false );
	C_ADD_VARIABLE( bool, MiscBuyBotOtherKit, false );
	C_ADD_VARIABLE( bool, MiscBuyBotOtherSmoke, false );
	C_ADD_VARIABLE( bool, MiscBuyBotOtherNade, false );
	C_ADD_VARIABLE( bool, MiscBuyBotOtherFlashbang, false );
	C_ADD_VARIABLE( bool, MiscBuyBotOtherMolotov, false );

	/* rage */
	// ragebot
	C_ADD_VARIABLE( bool, RagebotEnable, false );
	C_ADD_VARIABLE( bool, RagebotResolver, true );
	// defensive anti-aim detection ( menu: Rage > General > Defensive detection )
	C_ADD_VARIABLE( bool, RagebotHeadSafety, false );      // demand head points safe on extra desync sides as misses stack up
	C_ADD_VARIABLE( int, RagebotRecordMargin, 3 );         // ticks of tickbase error a record must survive before it is shot ( 0 = off )
	C_ADD_VARIABLE( bool, RagebotDefDetect, true );
	C_ADD_VARIABLE( int, RagebotDefMinShift, 3 );          // ticks: smaller simulation time drops are network jitter
	C_ADD_VARIABLE( int, RagebotDefMaxShift, 22 );         // ticks: bigger drops are not a defensive window
	C_ADD_VARIABLE( int, RagebotDefConfirmShifts, 3 );     // shifts needed inside the window to flag a player
	C_ADD_VARIABLE( int, RagebotDefConfirmTime, 3 );       // seconds a shift counts toward that
	C_ADD_VARIABLE( bool, RagebotDefNeedAngles, false );   // only shifts that flick pitch / yaw count
	C_ADD_VARIABLE( bool, RagebotDefAngleConfirm, true );  // one shift with defensive angles flags immediately
	C_ADD_VARIABLE( bool, RagebotDefPushConfirm, true );   // a drop right after a forward jump of the same size ( the tickbase push ) flags immediately
	C_ADD_VARIABLE( int, RagebotDefPitchJump, 45 );        // degrees of pitch change that count as a defensive flick
	C_ADD_VARIABLE( int, RagebotDefYawJump, 100 );         // degrees of yaw change ( beyond their jitter ) that count
	C_ADD_VARIABLE( int, RagebotDefNetTolerance, 5 );      // % of our own incoming loss / choke above which shifts are blamed on our network
	C_ADD_VARIABLE( bool, RagebotDefFilterSimultaneous, true ); // two players shifting on the same tick = network hiccup
	C_ADD_VARIABLE( bool, RagebotDefDebug, false );        // esp line: what the detector saw per player
	// fake flick filter ( menu: Rage > General )
	C_ADD_VARIABLE( bool, RagebotFlickHoldNew, true );     // a newest record on a yaw none of the recent records had is not shot until the next record confirms it
	C_ADD_VARIABLE( int, RagebotFlickJump, 60 );           // degrees from every recent yaw that make a record a flick candidate
	C_ADD_VARIABLE( int, RagebotFlickAgree, 40 );          // degrees: the record after it is "back" when this close to a recent yaw
	C_ADD_VARIABLE( bool, RagebotLagcompensation, true );
	C_ADD_VARIABLE( bool, RagebotBacktrack, true ); // aim at older lag records ( off = newest record only )
	C_ADD_VARIABLE( bool, RagebotExtrapolateBreakers, true ); // predict where lag compensation breakers are on the server
	C_ADD_VARIABLE( bool, RagebotDebugOverlay, false ); // esp text: what the ragebot is doing with each enemy
	C_ADD_VARIABLE( int, RagebotExtrapMaxTicks, 24 );   // never predict a lag-comp breaker further ahead than this
	C_ADD_VARIABLE( int, RagebotExtrapMaxSpread, 40 );  // possible positions further apart than this ( units ): no shot
	//C_ADD_VARIABLE( bool, RagebotForceSafeClampbones, false );
	C_ADD_VARIABLE( int, RagebotWeaponGroup, 0 );
	ADD_RAGE_VAR( int, RagebotFOV, 0 );
	ADD_RAGE_VAR( bool, RagebotAutoFire, false );
	ADD_RAGE_VAR( bool, RagebotSilentAim, false );
	ADD_RAGE_VAR( bool, RagebotAutoScope, false );
	ADD_RAGE_VAR( int, RagebotHitchance, 0 );
	ADD_RAGE_VAR( int, RagebotNoscopeHitchance, 0 );
	ADD_RAGE_VAR( bool, RagebotHitchanceThorough, false );
	ADD_RAGE_VAR( int, RagebotMinimumDamage, 0 );
	ADD_RAGE_VAR( bool, RagebotAutowall, false );
	ADD_RAGE_VAR( int, RagebotPenetrationDamage, 0 );
	ADD_RAGE_VAR( bool, RagebotScaleDamage, false );
	C_ADD_VARIABLE( bool, RagebotDamageOverride, false );
	C_ADD_VARIABLE( keybind_t, RagebotDamageOverrideKey, { } );
	ADD_RAGE_VAR( int, RagebotOverrideDamage, 0 );
	ADD_RAGE_VAR( bool, RagebotAutoStop, false );
	ADD_RAGE_VAR( bool, RagebotBetweenShots, false );
	ADD_RAGE_VAR( bool, RagebotAutostopInAir, false );
	C_ADD_VARIABLE( bool, RagebotZeusbot, false );
	C_ADD_VARIABLE( bool, RagebotKnifebot, false );

	C_ADD_VARIABLE( bool, ExploitsDoubletap, false );
	C_ADD_VARIABLE( bool, ExploitsDoubletapDefensive, false );
	C_ADD_VARIABLE( bool, ExploitsDoubletapExtended, false );
	C_ADD_VARIABLE( bool, ExploitsDefensiveInAir, false );
	C_ADD_VARIABLE( bool, ExploitsDefensiveBreakAnimations, false );
	C_ADD_VARIABLE( keybind_t, ExploitsDoubletapKey, { } );	
	C_ADD_VARIABLE( bool, ExploitsBreakCBB, false );
	C_ADD_VARIABLE( bool, ExploitsHideshots, false );
	C_ADD_VARIABLE( keybind_t, ExploitsHideshotsKey, { } );

	C_ADD_VARIABLE( int, RagebotTargetSelection, 0 );

	ADD_RAGE_VAR( bool, RagebotHBHead, false );
	ADD_RAGE_VAR( bool, RagebotHBUpperChest, false );
	ADD_RAGE_VAR( bool, RagebotHBChest, false );
	ADD_RAGE_VAR( bool, RagebotHBLowerChest, false );
	ADD_RAGE_VAR( bool, RagebotHBStomach, false );
	ADD_RAGE_VAR( bool, RagebotHBPelvis, false );
	ADD_RAGE_VAR( bool, RagebotHBArms, false );
	ADD_RAGE_VAR( bool, RagebotHBLegs, false );
	ADD_RAGE_VAR( bool, RagebotHBFeet, false );

	ADD_RAGE_VAR( bool, RagebotMPHead, false );
	ADD_RAGE_VAR( bool, RagebotMPUpperChest, false );
	ADD_RAGE_VAR( bool, RagebotMPChest, false );
	ADD_RAGE_VAR( bool, RagebotMPLowerChest, false );
	ADD_RAGE_VAR( bool, RagebotMPStomach, false );
	ADD_RAGE_VAR( bool, RagebotMPPelvis, false );
	ADD_RAGE_VAR( bool, RagebotMPArms, false );
	ADD_RAGE_VAR( bool, RagebotMPLegs, false );
	ADD_RAGE_VAR( bool, RagebotMPFeet, false );

	ADD_RAGE_VAR( bool, RagebotStaticPointscale, false );
	ADD_RAGE_VAR( int, RagebotHeadScale, 0 );
	ADD_RAGE_VAR( int, RagebotBodyScale, 0 );
	ADD_RAGE_VAR( bool, RagebotIgnoreLimbs, false );
	ADD_RAGE_VAR( bool, RagebotForceBaimAfterX, false );
	C_ADD_VARIABLE( bool, RagebotForceSafeClampbones, false );
	ADD_RAGE_VAR( int, RagebotForceBaimAfterXINT, 0 );
	ADD_RAGE_VAR( bool, RagebotPreferBaim, false );
	ADD_RAGE_VAR( bool, RagebotPreferBaimDoubletap, false );
	ADD_RAGE_VAR( bool, RagebotPreferBaimLethal, false );
	// "Always" defaults on so configs saved before this option keep the old safety-first point choice
	ADD_RAGE_VAR( bool, RagebotPreferSafe, true );
	ADD_RAGE_VAR( bool, RagebotPreferSafeLethal, false );
	ADD_RAGE_VAR( bool, RagebotPreferSafeInAir, false );
	ADD_RAGE_VAR( bool, RagebotPreferSafeAfterMiss, false );
	C_ADD_VARIABLE( keybind_t, RagebotForceBaimKey, { } );
	C_ADD_VARIABLE( keybind_t, RagebotForceSafePointKey, { } );
	C_ADD_VARIABLE( keybind_t, RagebotForceYawSafetyKey, { } );

	// anti aim
	C_ADD_VARIABLE( bool, AntiaimEnable, false );
	C_ADD_VARIABLE( bool, AntiaimDesync, false );
	C_ADD_VARIABLE( bool, AntiaimTrickLBY, false );
	C_ADD_VARIABLE( bool, AntiAimManualDir, false );
	C_ADD_VARIABLE( bool, AntiAimManualDirInd, false );
	C_ADD_VARIABLE( int, AntiaimAtTargets, 0 );
	C_ADD_VARIABLE( int, AntiaimPitch, 0 );
	C_ADD_VARIABLE( int, AntiaimYaw, 0 );
	C_ADD_VARIABLE( int, AntiaimSafePitch, 0 );
	C_ADD_VARIABLE( int, AntiaimSafeYawRandomisation, 0 );
	C_ADD_VARIABLE( int, AntiaimYawAdd, 0 );
	C_ADD_VARIABLE( int, AntiaimYawRange, 0 );
	C_ADD_VARIABLE( int, AntiaimYawSpeed, 0 );
	C_ADD_VARIABLE( int, AntiaimFreestand, 0 );
	C_ADD_VARIABLE( bool, AntiaimAntiBackStab, false );
	C_ADD_VARIABLE( int, AntiaimFreestanding, 0 );
	C_ADD_VARIABLE( keybind_t, AntiaimFreestandingKey, { } );
	C_ADD_VARIABLE( keybind_t, AntiaimInvert, { } );
	C_ADD_VARIABLE( bool, AntiaimConstantInvert, { } );
	C_ADD_VARIABLE( keybind_t, AntiaimRight, { } );
	C_ADD_VARIABLE( keybind_t, AntiaimLeft, { } );
	C_ADD_VARIABLE( Color, AntiaimManualCol, Color( 255, 255, 255 ) );
	C_ADD_VARIABLE( int, AntiaimNormalYawAdd, 0 );
	C_ADD_VARIABLE( int, AntiaimInvertedYawAdd, 0 );

	C_ADD_VARIABLE( bool, AntiaimFlickHead, false );
	C_ADD_VARIABLE( bool, AntiaimConstantInvertFlick, false );
	C_ADD_VARIABLE( int, AntiaimFlickAdd, 0 );
	C_ADD_VARIABLE( int, AntiaimFlickSpeed, 0 );
	C_ADD_VARIABLE( keybind_t, AntiaimFlickInvert, { } );

	C_ADD_VARIABLE( int, AntiaimFakeLagLimit, 0 );
	C_ADD_VARIABLE( int, AntiaimFakeLagVariance, 0 );
	C_ADD_VARIABLE( bool, AntiaimFakeLagBreakLC, false );
	C_ADD_VARIABLE( bool, AntiaimFakeLagInPeek, false );

	C_ADD_VARIABLE( bool, DBGLC1, false );
	C_ADD_VARIABLE( bool, DBGExtrap, false );
	C_ADD_VARIABLE( bool, DBGOldPing, false );
	C_ADD_VARIABLE( bool, DBGNoPingReducer, false );
	C_ADD_VARIABLE( keybind_t, DBGKeybind, { } );

	C_ADD_VARIABLE( int, SkinKnifeChanger, 0 );
	C_ADD_VARIABLE( int, SkinGloveChanger, 0 );

	/* CUSTOM ANTI-AIM ( momentum builder ) */
	C_ADD_VARIABLE( int, AntiaimMode, 0 ); // 0 classic, 1 custom

	ADD_AA_STATE_VAR( bool, AACEnable, false );
	ADD_AA_STATE_VAR( int, AACPitch, 1 );        // disabled, down, up, random
	ADD_AA_STATE_VAR( int, AACYawMode, 0 );      // default, 3 way, extrapolated, extrapolated skitter, synced body, synced random, delayed jitter
	ADD_AA_STATE_VAR( int, AACYawCenter, 0 );
	ADD_AA_STATE_VAR( int, AACYawLeft, 10 );
	ADD_AA_STATE_VAR( int, AACYawRight, -10 );
	ADD_AA_STATE_VAR( int, AACWay1, -30 );
	ADD_AA_STATE_VAR( int, AACWay2, 0 );
	ADD_AA_STATE_VAR( int, AACWay3, 30 );
	ADD_AA_STATE_VAR( int, AACRandMinLeft, 0 );
	ADD_AA_STATE_VAR( int, AACRandMaxLeft, 20 );
	ADD_AA_STATE_VAR( int, AACRandMinRight, -20 );
	ADD_AA_STATE_VAR( int, AACRandMaxRight, 0 );
	ADD_AA_STATE_VAR( int, AACJitterDelay, 2 );
	ADD_AA_STATE_VAR( int, AACJitterRandom, 0 );
	ADD_AA_STATE_VAR( int, AACBody, 1 );         // static, jitter, opposite, off
	ADD_AA_STATE_VAR( int, AACBodySide, 0 );     // left, right
	ADD_AA_STATE_VAR( int, AACBodySpeed, 1 );    // send cycles per flip, 0 = random
	ADD_AA_STATE_VAR( bool, AACNoFreestand, false );

	C_ADD_VARIABLE( int, AACDefYaw, 0 );         // none, forward, spin, sideways, jitter 90, opposite, opposite jitter, 3-way, 5-way, random
	C_ADD_VARIABLE( int, AACDefPitch, 0 );       // none, zero, up, down, random, semi-up, semi-down
	C_ADD_VARIABLE( bool, AACDefHeightPitch, false );
	C_ADD_VARIABLE( int, AACDefPitchAbove, 3 );  // 0 = use defensive pitch, then same list as AACDefPitch
	C_ADD_VARIABLE( int, AACDefPitchBelow, 2 );
	C_ADD_VARIABLE( int, AACDefHeightThreshold, 8 );
	C_ADD_VARIABLE( bool, AACOnUse, false );
	C_ADD_VARIABLE( bool, AACSafeHead, false );

	/* HUD ( momentum visuals ) */
	C_ADD_VARIABLE( bool, VisIndicators, false );
	C_ADD_VARIABLE( Color, VisIndicatorsCol, Color( 255, 149, 46 ) );
	C_ADD_VARIABLE( bool, VisMinDmgIndicator, false );
	C_ADD_VARIABLE( bool, VisCustomScope, false );
	C_ADD_VARIABLE( Color, VisCustomScopeCol, Color( 255, 149, 46 ) );
	C_ADD_VARIABLE( int, VisCustomScopeGap, 10 );
	C_ADD_VARIABLE( int, VisCustomScopeSize, 60 );
};

inline Variables_t Vars;

// all 9 per-state ids of a custom anti-aim variable, indexed by CAntiAim state
#define AA_STATE_IDS( name ) std::array< std::uint32_t, 9 >{ Vars.name##Default, Vars.name##Stand, Vars.name##Move, Vars.name##Slow, Vars.name##Duck, Vars.name##Air, Vars.name##AirDuck, Vars.name##Fakelag, Vars.name##Defensive }