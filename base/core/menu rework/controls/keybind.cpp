#include "../menu.h"
#include "../../../sdk/datatypes/color.h"

const char* keys[ ] = { "[-]", "[M1]", "[M2]", "[BRK]", "[M3]", "[M4]", "[M5]",
"[_]", "[BSPC]", "[TAB]", "[_]", "[_]", "[_]", "[ENTER]", "[_]", "[_]", "[SHFT]",
"[CTRL]", "[ALT]", "[PAUSE]", "[CAPS]", "[_]", "[_]", "[_]", "[_]", "[_]", "[_]",
"[ESC]", "[_]", "[_]", "[_]", "[_]", "[SPACE]", "[PGUP]", "[PGDOWN]", "[END]", "[HOME]", "[LEFT]",
"[UP]", "[RIGHT]", "[DOWN]", "[_]", "[PRNT]", "[_]", "[PRTSCR]", "[INS]", "[DEL]", "[_]", "[0]", "[1]",
"[2]", "[3]", "[4]", "[5]", "[6]", "[7]", "[8]", "[9]", "[_]", "[_]", "[_]", "[_]", "[_]", "[_]",
"[_]", "[A]", "[B]", "[C]", "[D]", "[E]", "[F]", "[G]", "[H]", "[I]", "[J]", "[K]", "[L]", "[M]", "[N]", "[O]", "[P]", "[Q]", "[R]", "[S]", "[T]", "[U]",
"[V]", "[W]", "[X]", "[Y]", "[Z]", "[LFTWIN]", "[RGHTWIN]", "[_]", "[_]", "[_]", "[NUM0]", "[NUM1]",
"[NUM2]", "[NUM3]", "[NUM4]", "[NUM5]", "[NUM6]", "[NUM7]", "[NUM8]", "[NUM9]", "[*]", "[+]", "[_]", "[-]", "[.]", "[/]", "[F1]", "[F2]", "[F3]",
"[F4]", "[F5]", "[F6]", "[F7]", "[F8]", "[F9]", "[F10]", "[F11]", "[F12]", "[F13]", "[F14]", "[F15]", "[F16]", "[F17]", "[F18]", "[F19]", "[F20]", "[F21]",
"[F22]", "[F23]", "[F24]", "[_]", "[_]", "[_]", "[_]", "[_]", "[_]", "[_]", "[_]",
"[NUMLCK]", "[SCRLLCK]", "[_]", "[_]", "[_]", "[_]", "[_]", "[_]", "[_]",
"[_]", "[_]", "[_]", "[_]", "[_]", "[_]", "[_]", "[LSHFT]", "[RSHFT]", "[LCTRL]",
"[RCTRL]", "[LALT]", "[RALT]", "[_]", "[_]", "[_]", "[_]", "[_]", "[_]", "[_]",
"[_]", "[_]", "[_]", "[NTRK]", "[PTRK]", "[STOP]", "[PLAY]", "[_]", "[_]",
"[_]", "[_]", "[_]", "[_]", "[;]", "[+]", "[,]", "[-]", "[.]", "[?]", "[~]", "[_]", "[_]",
"[_]", "[_]", "[_]", "[_]", "[_]", "[_]", "[_]", "[_]", "[_]",
"[_]", "[_]", "[_]", "[_]", "[_]", "[_]", "[_]", "[_]", "[_]",
"[_]", "[_]", "[_]", "[_]", "[_]", "[_]", "[_]", "[_]", "[_]", "[_]", "[_]",
"[_]", "[_]", "[_]", "[_]", "[_]", "[_]", "[_]", "[_]", "[_]",
"[_]", "[_]", "[_]", "[_]", "[_]", "[_]", "[_]", "[_]", "[_]",
"[_]", "[_]", "[_]", "[_]", "[_]", "[_]", "[_]", "[_]", "[_]",
"[_]", "[_]" };

ComboElement_t items[ 4 ]{ _( "Always on" ),_( "Toggle" ), _( "Hold" ), _( "Off hotkey" ) };

namespace {
	// "[M1]" -> "M1", "[-]" -> "none"
	std::string KeyDisplayName( int key ) {
		if ( key <= 0 || key >= static_cast< int >( sizeof( keys ) / sizeof( keys[ 0 ] ) ) )
			return _( "none" );

		std::string name{ keys[ key ] };
		if ( name.size( ) > 2 && name.front( ) == '[' && name.back( ) == ']' )
			name = name.substr( 1, name.size( ) - 2 );

		if ( name == "-" || name == "_" )
			return _( "none" );

		return name;
	}
}

std::string Menu::KeyName( int key ) {
	return KeyDisplayName( key );
}

void CMenuItem::KeybindFocus( ) {
	auto& value{ *reinterpret_cast< keybind_t* >( m_pValue ) };

	const auto size{ Vector2D( Render::GetTextSize( items[ 3 ].m_szName, Fonts::Menu ).x + PADDING * 2 + 8.f, 15.f * 4.f + PADDING * 2 ) };
	const auto& pos{ Menu::m_pFocusItem.m_vecDrawPos };

	Render::FilledRoundedBox( pos - Vector2D( 1, 1 ), size + Vector2D( 2, 2 ), 4, 4, Menu::FocusAlpha( OUTLINE_DARK ) );
	Render::FilledRoundedBox( pos, size, 4, 4, Menu::FocusAlpha( MENU_POPUP ) );

	for ( int i{ }; i < 4; ++i ) {
		auto& element{ items[ i ] };

		const auto rowPos{ pos + Vector2D( PADDING, PADDING + 15 * i ) };
		const auto hovered{ Inputsys::hovered( rowPos, Vector2D{ size.x, 15 } ) };

		Menu::LerpToCol( element.m_cColor, value.mode == i ? ACCENT : hovered ? MENU_TEXT : MENU_TEXT_DIM );

		if ( hovered )
			Render::FilledRoundedBox( Vector2D( pos.x + 3.f, rowPos.y - 1.f ), Vector2D( size.x - 6.f, 15.f ), 3, 3, Menu::FocusAlpha( MENU_CONTROL ) );

		Render::Text( Fonts::Menu, rowPos + Vector2D( 2, 0 ), Menu::FocusAlpha( element.m_cColor ), FONT_LEFT, element.m_szName );

		if ( hovered && Inputsys::pressed( VK_LBUTTON ) && !Menu::m_pFocusItem.m_bFrameAfterFocus && !Menu::m_pFocusItem.m_bFadeOut )
			value.mode = i;
	}

	if ( Inputsys::pressed( VK_LBUTTON ) && !Menu::m_pFocusItem.m_bFrameAfterFocus ) {
		Menu::m_pFocusItem.m_bFrameAfterFocus = true;
		Menu::m_pFocusItem.m_bFadeOut = true;
	}
}

void CMenuItem::Keybind( ) {
	if ( !m_fnShow( ) )
		return;

	auto& value{ *reinterpret_cast< keybind_t* >( m_pValue ) };
	const bool listening{ m_pArgs != nullptr };
	const std::string display{ listening ? std::string( "..." ) : KeyDisplayName( value.key ) };

	const auto textWidth{ Render::GetTextSize( display, Fonts::Menu ).x };
	const auto pillSize{ Vector2D( textWidth + 12.f, 14.f ) };

	// right edge of the row above ( keybinds sit next to the checkbox / label they belong to )
	const auto rowRight{ Menu::m_vecDrawPos + Vector2D{ Menu::ControlWidth( 2 ), -ITEM_HEIGHT - PADDING } };
	const auto pillPos{ rowRight - Vector2D( pillSize.x, 1.f ) };

	const auto hovered{ Inputsys::hovered( pillPos, pillSize ) };
	if ( hovered && Inputsys::pressed( VK_RBUTTON ) && !Menu::m_pFocusItem.m_pItem && !Menu::m_pFocusItem.m_bFrameAfterFocus ) {
		const auto backup{ Menu::m_vecDrawPos };
		Menu::m_vecDrawPos = pillPos + Vector2D( 0.f, pillSize.y + 4.f );
		Menu::m_pFocusItem.Update( this );
		Menu::m_vecDrawPos = backup;
	}

	if ( hovered && Inputsys::pressed( VK_LBUTTON ) && !Menu::m_pFocusItem.m_pItem && !Menu::m_pFocusItem.m_bFrameAfterFocus )
		m_pArgs = this;
	else if ( !hovered && Inputsys::pressed( VK_LBUTTON ) )
		m_pArgs = nullptr;

	// delete / backspace on a hovered key clears it
	if ( hovered && !m_pArgs && !Menu::m_pFocusItem.m_pItem && ( Inputsys::pressed( VK_DELETE ) || Inputsys::pressed( VK_BACK ) ) )
		value.key = 0;

	if ( m_pArgs ) {
		Menu::m_bListeningThisFrame = true;

		for ( int i{ 2 }; i < 255; i++ ) {
			if ( Inputsys::pressed( i ) ) {
				if ( i == VK_ESCAPE ) {
					value.key = 0;
					m_pArgs = nullptr;
					break;
				}

				value.key = i;
				m_pArgs = nullptr;
				break;
			}
		}
	}

	Render::FilledRoundedBox( pillPos, pillSize, 4, 4, listening ? ACCENT_DARK : hovered ? MENU_CONTROL_HOVER : MENU_CONTROL );
	Render::Text( Fonts::Logs, pillPos + Vector2D( pillSize.x / 2.f, 1.f ), listening ? MENU_TEXT : value.key ? ACCENT2 : MENU_TEXT_DIM, FONT_CENTER, display.c_str( ) );
}
