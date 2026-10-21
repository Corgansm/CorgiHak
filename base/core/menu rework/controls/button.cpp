#include "../menu.h"
#include "../../../sdk/datatypes/color.h"

void CMenuItem::Button( ) {
	if ( !m_fnShow( ) )
		return;

	auto& args{ *reinterpret_cast< ButtonArgs_t* >( m_pArgs ) };

	const auto size{ Vector2D( Menu::ControlWidth( args.m_iMaxGroups ), ITEM_HEIGHT * 2.f ) };
	const auto hovered{ Inputsys::hovered( Menu::m_vecDrawPos, size ) };

	Menu::LerpToCol( this->m_cColor, hovered ? Inputsys::down( VK_LBUTTON ) ? ACCENT_DARK : MENU_CONTROL_HOVER : MENU_CONTROL );

	if ( hovered && Inputsys::pressed( VK_LBUTTON ) && !Menu::m_pFocusItem.m_bFrameAfterFocus && !Menu::m_pFocusItem.m_pItem )
		args.m_bDown = true;

	if ( args.m_bDown && Inputsys::released( VK_LBUTTON ) ) {
		args.m_bDown = false;

		// first click arms it, second click inside 3 seconds runs it ( drag off to cancel )
		if ( hovered && args.m_fnCallback ) {
			args.m_fnCallback( );
		}
		else if ( hovered ) {
			if ( args.m_pCallback && args.m_flTimer ) {
				args.m_flTimer = 0.f;
				reinterpret_cast< void( __cdecl* )( ) >( args.m_pCallback )( );
			}
			else if ( !args.m_flTimer )
				args.m_flTimer = 3.f;
		}
	}

	if ( args.m_flTimer )
		args.m_flTimer -= Interfaces::Globals->flFrameTime;

	args.m_flTimer = std::max( args.m_flTimer, 0.f );

	Render::FilledRoundedBox( Menu::m_vecDrawPos - Vector2D( 1, 1 ), size + 2, 4, 4, args.m_flTimer ? ACCENT.Set<COLOR_A>( 160 ) : OUTLINE_DARK );
	Render::FilledRoundedBox( Menu::m_vecDrawPos, size, 4, 4, this->m_cColor );

	if ( args.m_flTimer )
		Render::FilledRectangle( Menu::m_vecDrawPos + Vector2D{ 4.f, size.y - 3.f }, Vector2D{ ( size.x - 8.f ) * ( args.m_flTimer / 3.f ), 2.f }, ACCENT );

	Render::Text( Fonts::MenuBold, Menu::m_vecDrawPos + Vector2D{ size.x / 2.f, size.y / 2.f - 7.f }, args.m_flTimer ? ACCENT2 : MENU_TEXT, FONT_CENTER, args.m_flTimer ? _( "Click again to confirm" ) : m_szName );

	Menu::m_vecDrawPos.y += ITEM_HEIGHT * 2.f + PADDING;
}
