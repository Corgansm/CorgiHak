#include "../menu.h"
#include "../../../sdk/datatypes/color.h"

void CMenuItem::Checkbox( ) {
	if ( !m_fnShow( ) )
		return;

	auto& value{ *reinterpret_cast< bool* >( m_pValue ) };
	const auto size{ Vector2D{ ITEM_HEIGHT, ITEM_HEIGHT } };
	const auto hovered{ Inputsys::hovered( Menu::m_vecDrawPos, size + Vector2D{ Render::GetTextSize( m_szName, Fonts::Menu ).x + ITEM_HEIGHT * 2, 0 } ) };

	if ( hovered && Inputsys::pressed( VK_LBUTTON ) && !Menu::m_pFocusItem.m_pItem && !Menu::m_pFocusItem.m_bFrameAfterFocus )
		value = !value;

	Menu::LerpToCol( this->m_cColor, value ? ACCENT : hovered ? MENU_CONTROL_HOVER : MENU_CONTROL );

	Render::FilledRoundedBox( Menu::m_vecDrawPos - Vector2D( 1, 1 ), size + 2, 4, 3, OUTLINE_DARK );
	Render::FilledRoundedBox( Menu::m_vecDrawPos, size, 4, 3, this->m_cColor );

	// the mark fades in / out with the box colour instead of popping
	m_flToggle = Anim::Approach( m_flToggle, value ? 1.f : 0.f, 18.f );
	if ( m_flToggle > 0.01f )
		MenuDraw::CheckMark( Menu::m_vecDrawPos, static_cast< float >( ITEM_HEIGHT ), OUTLINE_DARK.Set<COLOR_A>( 255.f * m_flToggle ) );

	Render::Text( Fonts::Menu, Menu::m_vecDrawPos + Vector2D( ITEM_HEIGHT + 8, -1 ), value || hovered ? MENU_TEXT : SELECTED_ELEMENT, FONT_LEFT, m_szName );

	Menu::m_vecDrawPos.y += ITEM_HEIGHT + PADDING;
}
