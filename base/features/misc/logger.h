#pragma once
#include "../../context.h"
#include "../../utils/math.h"
#include "../../core/menu rework/menu.h"

struct OnsCreen_t {
	OnsCreen_t( std::string str ) {
		m_strText = str;
	}

	std::string m_strText;
	float m_flTimeOnScreen{ 5 };
	float m_fAlpha{ };
	Anim::Fade_t m_cFade{ };
	float m_flY{ -1.f };       // drawn row position, glides to its slot when rows above leave
};

class CLogger {
private:
	std::vector<OnsCreen_t> m_cOnscreenText;
public:
	void PrintToScreen( );
	FORCEINLINE void Log( std::string text, bool show_on_screen ) {
		text.append( "\n" );
		Interfaces::ConVar->ConsoleColorPrintf( ACCENT, _( "[ CorgiHak ] " ) );
		// "%s": a '%' in a player name was read as a format specifier
		Interfaces::ConVar->ConsolePrintf( _( "%s" ), text.c_str( ) );

		if ( show_on_screen )
			m_cOnscreenText.push_back( text );
	}
};

namespace Features { inline CLogger Logger; };