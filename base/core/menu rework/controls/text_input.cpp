#include "../menu.h"
#include "../../../sdk/datatypes/color.h"

void CMenuItem::TextInputFocus( ) {
	auto& maxGroups{ *reinterpret_cast< int* > ( m_pArgs ) };
	auto& value{ *reinterpret_cast< std::string* > ( m_pValue ) };

	const auto size{ Vector2D( Menu::ControlWidth( maxGroups ), ITEM_HEIGHT * 2.f ) };
	const auto hovered{ Inputsys::hovered( Menu::m_pFocusItem.m_vecDrawPos, size ) };

	Render::FilledRoundedBox( Menu::m_pFocusItem.m_vecDrawPos - Vector2D( 1, 1 ), size + 2, 4, 4, ACCENT );
	Render::FilledRoundedBox( Menu::m_pFocusItem.m_vecDrawPos, size, 4, 4, MENU_CONTROL_HOVER );

	Render::Text( Fonts::Menu, Menu::m_pFocusItem.m_vecDrawPos + Vector2D{ PADDING, 5 }, MENU_TEXT, FONT_LEFT, value.c_str( ) );

	const auto textSize{ Render::GetTextSize( value, Fonts::Menu ) };

	const auto second{ static_cast< int >( Interfaces::Globals->flRealTime ) };
	this->m_cColor = Color( 1.f, 1.f, 1.f, second & 1 ? Interfaces::Globals->flRealTime - second :  1.f - ( Interfaces::Globals->flRealTime - second ) );
	Render::Line( Menu::m_pFocusItem.m_vecDrawPos + Vector2D{ PADDING + textSize.x + 1.f, 5.f }, Menu::m_pFocusItem.m_vecDrawPos + Vector2D{ PADDING + textSize.x + 1.f, size.y - 5.f }, ACCENT.Set<COLOR_A>( this->m_cColor.Get<COLOR_A>( ) ) );


	if ( Inputsys::pressed( VK_ESCAPE ) || Inputsys::pressed( VK_RETURN ) ) {
		Menu::m_pFocusItem.m_pItem = nullptr;
		Menu::m_pFocusItem.m_bFrameAfterFocus = true;
	}
	else if ( Inputsys::pressed( VK_BACK ) )
		value = value.substr( 0, value.size( ) - 1 );
	else if ( value.size( ) < 110 ) {
		for ( auto i{ 0 }; i < 255; i++ ) {
			if ( isalpha( i ) ) {
				if ( Inputsys::pressed( i ) ) {
					auto final{ i };
					if ( !GetKeyState( VK_CAPITAL ) && !Inputsys::down( VK_SHIFT ) )
						final += 32;

					value += final;
				}
			}
		}

		if ( Inputsys::pressed( VK_SPACE ) )
			value += ' ';
	}

	if ( Inputsys::pressed( VK_LBUTTON ) && !Menu::m_pFocusItem.m_bFrameAfterFocus && !hovered ) {
		Menu::m_pFocusItem.m_pItem = nullptr;
		Menu::m_pFocusItem.m_bFrameAfterFocus = true;
	}

	Menu::m_vecDrawPos.y += size.y + PADDING;
}

void CMenuItem::TextInput( ) {
	if ( !m_fnShow( ) )
		return;

	auto& maxGroups{ *reinterpret_cast< int* > ( m_pArgs ) };
	auto& value{ *reinterpret_cast< std::string* > ( m_pValue ) };

	const auto size{ Vector2D( Menu::ControlWidth( maxGroups ), ITEM_HEIGHT * 2.f ) };
	const auto hovered{ Inputsys::hovered( Menu::m_vecDrawPos, size ) };

	if ( hovered && Inputsys::pressed( VK_LBUTTON ) && !Menu::m_pFocusItem.m_bFrameAfterFocus && !Menu::m_pFocusItem.m_pItem )
		Menu::m_pFocusItem.Update( this );

	Render::FilledRoundedBox( Menu::m_vecDrawPos - Vector2D( 1, 1 ), size + 2, 4, 4, OUTLINE_DARK );
	Render::FilledRoundedBox( Menu::m_vecDrawPos, size, 4, 4, hovered ? MENU_CONTROL_HOVER : MENU_CONTROL );

	if ( value.empty( ) )
		Render::Text( Fonts::Menu, Menu::m_vecDrawPos + Vector2D{ PADDING, 5 }, MENU_TEXT_DIM, FONT_LEFT, _( "click to type..." ) );
	else
		Render::Text( Fonts::Menu, Menu::m_vecDrawPos + Vector2D{ PADDING, 5 }, MENU_TEXT, FONT_LEFT, value.c_str( ) );

	Menu::m_vecDrawPos.y += ITEM_HEIGHT * 2.f + PADDING;
}