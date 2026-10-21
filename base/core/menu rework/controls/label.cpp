#include "../menu.h"
#include "../../../sdk/datatypes/color.h"

void CMenuItem::Label( ) {
	if ( !m_fnShow( ) )
		return;

	const auto size{ Vector2D{ Menu::ControlWidth( 2 ) + PADDING * 2, 16.f } };
	const auto hovered{ Inputsys::hovered( Menu::m_vecDrawPos, size ) };

	Menu::LerpToCol( this->m_cColor, hovered ? MENU_TEXT : SELECTED_ELEMENT );

	Render::Text( Fonts::Menu, Menu::m_vecDrawPos + Vector2D( 0, -1 ), this->m_cColor, FONT_LEFT, m_szName );

	Menu::m_vecDrawPos.y += ITEM_HEIGHT + PADDING;
}
