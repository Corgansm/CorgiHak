#pragma once
// used: winapi, directx, fmt includes
#include "../havoc.h"
// used: hook setup/destroy
#include "../utils/detourhook.h"
// used: recvprop hook setup/destroy, recvproxydata
#include "prop_manager.h"
// used: baseclasses
#include "interfaces.h"

#define FASTCALL __fastcall
#define STDCALL __stdcall

/*
* VTABLE INDEXES
* functions indexes in their virtual tables
*/
namespace VTABLE
{
	enum
	{
		/* directx table */
		RESET = 16,
		PRESENT = 17,
		ENDSCENE = 42,
		RESETEX = 132,

		/* keyvaluessystem table */
		ALLOCKEYVALUESMEMORY = 2,

		/* client table */
		CREATEMOVE = 22,
		FRAMESTAGENOTIFY = 37,
		WRITEUSERCMDDELTATOBUFFER = 24,

		/* panel table */
		PAINTTRAVERSE = 41,

		/* clientmode table */
		OVERRIDEVIEW = 18,
		OVERRIDEMOUSEINPUT = 23,
		GETVIEWMODELFOV = 35,
		DOPOSTSCREENEFFECTS = 44,

		/* modelrender table */
		DRAWMODELEXECUTE = 21,

		/* studiorender table */
		DRAWMODEL = 29,

		/* enginevgui table*/
		VGUI_PAINT = 14,

		/* viewrender table */
		RENDERSMOKEOVERLAY = 41,

		/* engine table */
		ISCONNECTED = 27,
		ISPAUSED = 90,
		ISHLTV = 93,
		GETSCREENASPECTRATIO = 101,

		/* bsp query table */
		LISTLEAVESINBOX = 6,

		/* prediction table */
		RUNCOMMAND = 19,
		INPREDICTION = 14,

		/* steamgamecoordinator table */
		SENDMESSAGE = 0,
		RETRIEVEMESSAGE = 2,

		/* sound table */
		EMITSOUND = 5,

		/* materialsystem table */
		OVERRIDECONFIG = 21,

		/* renderview table */
		SCENEEND = 9,

		/* surface table */
		LOCKCURSOR = 67,
		PLAYSOUND = 82,

		/* gameevent table */
		FIREEVENT = 9,

		/* convar table */
		GETBOOL = 13,
		GETINT = 12,

		/* netchannel table */
		SENDNETMSG = 40,
		SENDDATAGRAM = 46,

		/* gamemovement table*/
		PROCESSMOVEMENT = 1
	};
}

/*
 * DETOURS
 * detour hook managers
 */
namespace DTR
{
	inline CDetourHook Reset;
	inline CDetourHook EndScene;
	inline CDetourHook Present;
	inline CDetourHook AllocKeyValuesMemory;
	inline CDetourHook CreateMoveProxy;
	inline CDetourHook FrameStageNotify;
	inline CDetourHook GetScreenAspectRatio;
	inline CDetourHook IsPaused;
	inline CDetourHook IsHLTV;
	inline CDetourHook OverrideView;
	inline CDetourHook ShouldDrawViewModel;
	inline CDetourHook OverrideConfig;
	inline CDetourHook SendNetMsg;
	inline CDetourHook SendDatagram;
	inline CDetourHook GetViewModelFOV;
	inline CDetourHook DoPostScreenEffects;
	inline CDetourHook IsConnected;
	inline CDetourHook RenderSmokeOverlay;
	inline CDetourHook ListLeavesInBox;
	inline CDetourHook PaintTraverse;
	inline CDetourHook DrawModel;
	inline CDetourHook RunCommand;
	inline CDetourHook SendMessageGC;
	inline CDetourHook RetrieveMessage;
	inline CDetourHook LockCursor;
	inline CDetourHook PlaySoundSurface;
	inline CDetourHook SvCheatsGetBool;
	inline CDetourHook ClDoResetLatchedGetBool;
	inline CDetourHook ClPredictGetInt;
	inline CDetourHook PacketEnd;
	inline CDetourHook PacketStart;
	inline CDetourHook Paint;
	inline CDetourHook EmitSound;
	inline CDetourHook ProcessMovement;
	inline CDetourHook InterpolatedVarArrayBase_Reset;
	inline CDetourHook DoExtraBonesProcessing;
	inline CDetourHook StandardBlendingRules;
	inline CDetourHook UpdateClientsideAnimation;
	inline CDetourHook GetEyeAngles;
	inline CDetourHook AccumulateLayers;
	inline CDetourHook PhysicsSimulate;
	inline CDetourHook ModifyEyePosition;
	inline CDetourHook CalcView;
	inline CDetourHook CalcViewmodelBob;
	inline CDetourHook ShouldSkipAnimFrame;
	inline CDetourHook Setupbones;
	inline CDetourHook CL_SendMove;
	inline CDetourHook CMCreateMove;
	inline CDetourHook WriteUserCmdDeltaToBuffer;
	inline CDetourHook ShouldInterpolate;
	inline CDetourHook AddBoxOverlay;
	inline CDetourHook GlowEffectSpectator;
	inline CDetourHook GetColorModulation;
	inline CDetourHook GetAlphaModulation;
	inline CDetourHook OnLatchInterpolatedVariables;
	inline CDetourHook ClampBonesInBBox;
	inline CDetourHook OnNewCollisionBounds;
	inline CDetourHook CHudScopePaint;
	inline CDetourHook BuildTransformations;
	inline CDetourHook UpdatePostProcessingEffects;
	inline CDetourHook GetWeaponType;
	inline CDetourHook FinishTrackPredictionErrors;
	inline CDetourHook GetUserCmd;
	inline CDetourHook SelectItem;
	inline CDetourHook PreThink;
	inline CDetourHook ItemPostFrame;
	inline CDetourHook ClipRayToCollideable;
	inline CDetourHook MoveParentPhysicsSimulate;
	inline CDetourHook IsBoneAvailable;
	inline CDetourHook PostDataUpdate;
	inline CDetourHook SceneEnd;
	inline CDetourHook CL_Move;
	inline CDetourHook CL_ReadPackets;
	inline CDetourHook UpdatePrediction;
	inline CDetourHook CL_PreprocessEntities;
	inline CDetourHook SVCMsg_PacketEntities;
	inline CDetourHook SetUpMovement;
	inline CDetourHook isBoneAvailableForRead;
	inline CDetourHook GetAbsOrigin;
	inline CDetourHook _Host_RunFrame_Client;
	inline CDetourHook _Host_RunFrame_Input;
	inline CDetourHook C_BaseViewModel__Interpolate;
	inline CDetourHook GetExposureRange;
	inline CDetourHook AnimStateUpdate;
	inline CDetourHook ResetLatched;
	inline CDetourHook PRECACHE_REGISTER_BEGIN__PrecachePrecipitation;

#ifdef SERVER_DBGING
	inline CDetourHook ServerSetupBones;
#endif
}

/*
 * HOOKS
 * swap functions with given pointers
 */
namespace Hooks
{
	inline WNDPROC pOldWndProc = nullptr;
	inline HWND hWindow = nullptr;
	inline RecvVarProxyFn m_bClientSideAnimation;
	inline RecvVarProxyFn m_flSimulationTime;
	inline RecvVarProxyFn m_flAbsYaw;
	inline RecvVarProxyFn m_hWeapon;
	inline RecvVarProxyFn m_flCycle;
	inline RecvVarProxyFn m_flAnimTime;
	inline RecvVarProxyFn m_nSequence;

	// Get
	bool	Setup();
	void	Restore();

	// hooks
	void	FASTCALL	hkLockCursor( ISurface* thisptr, int edx );
	LRESULT	CALLBACK	hkWndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam );
	void	FASTCALL	hkPaintTraverse( ISurface* thisptr, int edx, unsigned int uPanel, bool bForceRepaint, bool bForce );
	void	FASTCALL	HkPaint( const std::uintptr_t ecx, const std::uintptr_t edx, const int mode );
	void	FASTCALL	hkCreateMoveProxy( uint8_t* ecx, uint8_t*, int sequence_number, float input_sample_frametime, bool active );
	void	FASTCALL	hkPacketEnd( void* cl_state, void* EDX );
	void	FASTCALL	hkPacketStart( void* ecx, void* edx, int in_seq, int out_acked );
	void	FASTCALL	hkDrawModel( IStudioRender* thisptr, int edx, DrawModelResults_t* pResults, const DrawModelInfo_t& info, matrix3x4_t* pBoneToWorld, float* flFlexWeights, float* flFlexDelayedWeights, const Vector& vecModelOrigin, int nFlags );
	void	FASTCALL	hkFrameStageNotify( IBaseClientDll* thisptr, int edx, EClientFrameStage stage );
	void	FASTCALL	hkOverrideView( IClientModeShared* thisptr, int edx, CViewSetup* pSetup );
	bool	FASTCALL	hkOverrideConfig( IMaterialSystem* ecx, void* edx, MaterialSystemConfig_t& config, bool bForceUpdate );
	float	FASTCALL	hkGetScreenAspectRatio( void* ECX, void* EDX, int32_t iWidth, int32_t iHeight );
	bool	FASTCALL	hkIsPaused( void* ecx, void* edx );
	bool	FASTCALL	hkIsHltv( void* ecx, void* EDX );
	int		FASTCALL	hkEmitSound( void* _this, int edx, IRecipientFilter& filter, int iEntIndex, int iChannel, const char* pSoundEntry, unsigned int nSoundEntryHash, const char* pSample, float flVolume, int nSeed, float flAttenuation, int iFlags, int iPitch, const Vector* pOrigin, const Vector* pDirection, void* pUtlVecOrigins, bool bUpdatePositions, float soundtime, int speakerentity, int unk );
	bool	FASTCALL	hkSendNetMsg( INetChannel* pNetChan, void* edx, INetMessage& msg, bool bForceReliable, bool bVoice );
	int		FASTCALL	hkSendDatagram( INetChannel* thisptr, int edx, bf_write* pDatagram );
	void	FASTCALL	hkProcessMovement( void* ecx, DWORD edx, CBasePlayer* basePlayer, CMoveData* moveData );
	void**	STDCALL		hkFinishTrackPredictionErrors( CBasePlayer* basePlayer );
	bool	FASTCALL	hkSvCheatsGetBool( CConVar* thisptr, int edx );
	bool	FASTCALL	hkClDoResetLatchedGetBool( CConVar* thisptr, int edx );
	int		FASTCALL	hkClPredictGetInt( CConVar* thisptr, int edx );
	void	FASTCALL	hkDoExtraBonesProcessing( void* ecx, uint32_t ye, CStudioHdr* hdr, Vector* pos, Quaternion* q, const matrix3x4_t& matrix, uint8_t* bone_computed, void* context );
	void	FASTCALL	hkStandardBlendingRules( CBasePlayer* const ent, const std::uintptr_t edx, CStudioHdr* const mdl_data, Vector* a1, Quaternion* a2, float a3, int mask );
	void	FASTCALL	hkUpdateClientsideAnimation( CBasePlayer* ecx, void* edx );
	void __vectorcall	HkAnimStateUpdate( void*, void*, float, float, float, void* );
	bool	FASTCALL	hkShouldSkipAnimFrame( void* ecx, uint32_t ebx );
	void	FASTCALL	hkClampBonesInBBox( CBasePlayer* ecx, uint32_t ebx, matrix3x4_t* bones, int boneMask );
	void	FASTCALL	hkAccumulateLayers( CBasePlayer* const ecx, const std::uintptr_t edx, int a0, int a1, float a2, int a3 );
	bool	FASTCALL	hkIsBoneAvailable( void* ecx, uint32_t, int a1 );
	void	FASTCALL	hkPhysicsSimulate( CBasePlayer* player, int time );
	CUserCmd* FASTCALL	hkGetUserCmd( uint8_t* ecx, uint8_t* edx, int slot, int seqnr );
	void	FASTCALL	hkModifyEyePosition( CCSGOPlayerAnimState* ecx, void* edx, Vector& pos );
	void	FASTCALL	hkCalcView( CBasePlayer* pPlayer, void* edx, Vector& vecEyeOrigin, QAngle& angEyeAngles, float& flZNear, float& flZFar, float& flFov );
	float	FASTCALL	hkCalcViewmodelBob( CWeaponCSBase* pWeapon, void* EDX );
	bool	FASTCALL	hkSetupbones( const std::uintptr_t ecx, const std::uintptr_t edx, matrix3x4_t* bones, int max_bones, int mask, float time );
	bool	STDCALL		hkCMCreateMove( float input_sample_frametime, CUserCmd* cmd );
	bool	FASTCALL	hkShouldDrawViewModel( void* );
	bool	FASTCALL	hkWriteUserCmdDeltaToBuffer( void* ecx, void* edx, int slot, bf_write* buf, int from, int to, bool is_new_command );
	bool	FASTCALL	hkShouldInterpolate( CBasePlayer* ecx, const std::uintptr_t edx );
	bool	CDECL		hkGlowEffectSpectator( CBasePlayer* const player, CBasePlayer* const local, int& style, Vector& clr, float& alpha_from, float& alpha_to, float& time_from, float& time_to, bool& animate );
	void	FASTCALL	hkGetColorModulation( IMaterial* const ecx, const std::uintptr_t edx, float* const r, float* const g, float* const b );
	float	FASTCALL	hkGetAlphaModulation( IMaterial* ecx, uint32_t ebx );
	void**	FASTCALL	hkCHudScopePaint( void* ecx, int edx );
	void	FASTCALL	hkOnNewCollisionBounds( CBasePlayer* ecx, uint32_t edx, Vector* oldMins, Vector* newMins, Vector* oldMaxs, Vector* new_Maxs );
	void	FASTCALL	hkUpdatePostProcessingEffects( void* ecx, int edx );
	int		FASTCALL	hkGetWeaponType( void* ecx, int edx );
	Vector*	FASTCALL	hkGetAbsOrigin( void* ecx, int edx );
	void	FASTCALL	hkClipRayToCollideable( void* ecx, void* edx, const Ray_t& ray, uint32_t fMask, ICollideable* pCollide, CGameTrace* pTrace );
	void	FASTCALL	hkPostDataUpdate( void* ecx, int edx, int update );
	void	FASTCALL	hkSetUpMovement( CCSGOPlayerAnimState* ecx, int edx );
	void	FASTCALL	hkSceneEnd( void* ecx, int edx );
	bool	FASTCALL	hkisBoneAvailableForRead( void* ecx, int edx, int a2 );
	void	FASTCALL	hkGetExposureRange( float* pflAutoExposureMin, float* pflAutoExposureMax );
	void	FASTCALL	hkInterpolatedVarArrayBase_Reset( void* ecx, int edx, float a2 );

	void	CDECL		m_bClientSideAnimationHook( const CRecvProxyData* data, void* entity, void* output );
	void	CDECL		m_flSimulationTimeHook( const CRecvProxyData* data, void* entity, void* output );
	void	CDECL		m_flAbsYawHook( const CRecvProxyData* data, void* entity, void* output );
	void	CDECL		m_hWeaponHook( const CRecvProxyData* data, void* entity, void* output );
	void	CDECL		m_flCycle_Recv( const CRecvProxyData* data, void* entity, void* output );
	void	CDECL		m_flAnimTime_Recv( const CRecvProxyData* data, void* entity, void* output );


	void	FASTCALL	hk_Host_RunFrame_Client( bool framefinished );
	void __vectorcall hkCL_Move( float accumulated_extra_samples, bool bFinalTick );
	void __vectorcall hk_Host_RunFrame_Input( float accumulated_extra_samples, bool bFinalTick );
	void	FASTCALL	hkCL_ReadPackets( bool bFinalTick );
	bool	FASTCALL	hkSVCMsg_PacketEntities( void* ecx );
	void hkCL_PreprocessEntities( );


#ifdef SERVER_DBGING
	void FASTCALL hkServerSetupBones( CBaseAnimating* ecx, int edx, matrix3x4a_t* pBoneToWorld, int boneMask );
#endif
}