#pragma once
#include "../../context.h"

struct CompressionVars_t
{
	QAngle	m_aimPunchAngle{ };
	Vector	m_aimPunchAngleVel{ };
	float	m_vecViewOffsetZ{ };
	int		m_iCommandNumber{ -1 };
};

struct ViewmodelData_t
{
	float m_flCycle{ };
	float m_flAnimationTime{ };
};

class CEnginePrediction
{
public:
	// call right before IPrediction::Update: forces a full re-predict when the server changed a value the client can't predict
	void PreUpdate( );
	// call at FRAME_NET_UPDATE_END: checks the freshly networked velocity modifier against what we predicted for that command
	void OnNetUpdate( );

	void RunCommand( CUserCmd& cmd );
	void Finish( );

	// both take the full command number, not the % 150 slot
	void RestoreNetvars( int commandNumber );
	void StoreNetvars( int commandNumber );

	void Reset( );

	float Spread{ };
	float Inaccuracy{ };

	ViewmodelData_t						m_ViewmodelData{ };
	std::array< CompressionVars_t, 150> m_cCompressionVars{ };
private:
	float		m_flCurtime{ };
	float		m_flFrametime{ };
	int			m_TickCount{ };

	float		m_flVelocityModifier{ 1.f };

	// velocity modifier we predicted, per command, so a networked value can be checked against the command it belongs to
	struct PredictedModifier_t {
		int		m_iCommandNumber{ -1 };
		float	m_flValue{ 1.f };
	};
	std::array< PredictedModifier_t, 150 > m_cPredictedModifier{ };
	bool		m_bForceRepredict{ };

	bool		m_bActive{ };
	bool		m_bBackupInPrediction{ };
	bool		m_bBackupFirstTimePredicted{ };

	CMoveData	MoveData{ };
};

namespace Features { inline CEnginePrediction EnginePrediction; };
