#include "../menu.h"
#include "../../../sdk/datatypes/color.h"

namespace {
	// closed dropdown field: label above, value + chevron inside
	void DrawField( const Vector2D& pos, const Vector2D& size, const char* text, bool hovered, bool open ) {
		Render::FilledRoundedBox( pos - Vector2D( 1, 1 ), size + Vector2D( 2, 2 ), 4, 4, open ? ACCENT.Set<COLOR_A>( 160 ) : OUTLINE_DARK );
		Render::FilledRoundedBox( pos, size, 4, 4, hovered || open ? MENU_CONTROL_HOVER : MENU_CONTROL );
		Render::Text( Fonts::Menu, pos + Vector2D{ PADDING, 2 }, MENU_TEXT, FONT_LEFT, text );
		MenuDraw::Chevron( pos + Vector2D( size.x - 11.f, size.y / 2.f ), open, open ? ACCENT : MENU_TEXT_DIM );
	}
}

void CMenuItem::ComboFocus( ) {
	auto& value{ *reinterpret_cast< int* >( m_pValue ) };
	const auto& args{ *reinterpret_cast< ComboArgs_t* >( m_pArgs ) };

	const auto width{ Menu::ControlWidth( 2 ) };
	const auto size{ Vector2D( width, ( 15 * args.m_iSize + PADDING * 2 ) * Menu::m_pFocusItem.m_flFocusAnim ) };
	const auto pos{ Menu::m_pFocusItem.m_vecDrawPos + Vector2D{ 0, ITEM_HEIGHT * 1.5f + PADDING } };
	const auto sizeOG{ Vector2D( width, ITEM_HEIGHT * 1.5f ) };

	Render::Text( Fonts::Menu, Menu::m_pFocusItem.m_vecDrawPos - Vector2D{ 0, 16 }, MENU_TEXT, FONT_LEFT, m_szName );
	DrawField( Menu::m_pFocusItem.m_vecDrawPos, sizeOG, args.m_pArguments[ value ].m_szName, true, true );

	Interfaces::Surface->SetClipRect( pos.x - 1, pos.y - 1, size.x + 2, size.y + 2 );

	Render::FilledRoundedBox( pos - Vector2D( 1, 1 ), size + Vector2D( 2, 2 ), 4, 4, Menu::FocusAlpha( OUTLINE_DARK ) );
	Render::FilledRoundedBox( pos, size, 4, 4, Menu::FocusAlpha( MENU_POPUP ) );

	for ( int i{ }; i < args.m_iSize; ++i ) {
		auto& element{ args.m_pArguments[ i ] };

		const auto rowPos{ pos + Vector2D( PADDING, PADDING + 15 * i ) };
		const auto hovered{ Inputsys::hovered( rowPos, Vector2D{ size.x, 15 } ) };

		Menu::LerpToCol( element.m_cColor, value == i ? ACCENT : hovered ? MENU_TEXT : MENU_TEXT_DIM );

		if ( hovered )
			Render::FilledRoundedBox( Vector2D( pos.x + 3.f, rowPos.y - 1.f ), Vector2D( size.x - 6.f, 15.f ), 3, 3, Menu::FocusAlpha( MENU_CONTROL ) );

		if ( value == i )
			Render::FilledRectangle( Vector2D( pos.x + 3.f, rowPos.y + 2.f ), Vector2D( 2.f, 10.f ), Menu::FocusAlpha( ACCENT ) );

		Render::Text( Fonts::Menu, rowPos + Vector2D( 2, 0 ), Menu::FocusAlpha( element.m_cColor ), FONT_LEFT, element.m_szName );

		if ( hovered && Inputsys::pressed( VK_LBUTTON ) && !Menu::m_pFocusItem.m_bFrameAfterFocus && !Menu::m_pFocusItem.m_bFadeOut )
			value = i;
	}

	Interfaces::Surface->SetClipRect( 0, 0, ctx.m_ve2ScreenSize.x, ctx.m_ve2ScreenSize.y );

	if ( Inputsys::pressed( VK_LBUTTON ) && !Menu::m_pFocusItem.m_bFrameAfterFocus ) {
		Menu::m_pFocusItem.m_bFrameAfterFocus = true;
		Menu::m_pFocusItem.m_bFadeOut = true;
	}
}

void CMenuItem::Combo( ) {
	if ( !m_fnShow( ) )
		return;

	const auto& value{ *reinterpret_cast< int* >( m_pValue ) };
	const auto& args{ *reinterpret_cast< ComboArgs_t* >( m_pArgs ) };

	const auto size{ Vector2D( Menu::ControlWidth( 2 ), ITEM_HEIGHT * 1.5f ) };

	if ( Menu::m_pFocusItem.m_pItem != this )
		Render::Text( Fonts::Menu, Menu::m_vecDrawPos, MENU_TEXT, FONT_LEFT, m_szName );

	Menu::m_vecDrawPos.y += 16;
	const auto hovered{ Inputsys::hovered( Menu::m_vecDrawPos, size ) };

	if ( Menu::m_pFocusItem.m_pItem != this )
		DrawField( Menu::m_vecDrawPos, size, args.m_pArguments[ value ].m_szName, hovered, false );

	if ( hovered && Inputsys::pressed( VK_LBUTTON ) && !Menu::m_pFocusItem.m_pItem )
		Menu::m_pFocusItem.Update( this );

	Menu::m_vecDrawPos.y += ITEM_HEIGHT * 1.5f + PADDING;
}
