#include "../menu.h"
#include "../../../sdk/datatypes/color.h"

namespace {
	std::string JoinSelected( const MultiComboArgs_t& args, float width ) {
		std::string joined{ };
		for ( int i{ }; i < args.m_iSize; ++i ) {
			if ( *args.m_pArguments[ i ].m_pValue ) {
				if ( !joined.empty( ) )
					joined += ", ";
				joined += args.m_pArguments[ i ].m_szName;
			}
		}

		if ( joined.empty( ) )
			return _( "None" );

		// keep room for the chevron
		const float maxWidth{ width - PADDING * 2 - 16.f };
		if ( Render::GetTextSize( joined, Fonts::Menu ).x > maxWidth ) {
			while ( joined.size( ) > 1 && Render::GetTextSize( joined + "...", Fonts::Menu ).x > maxWidth )
				joined.pop_back( );
			joined += "...";
		}

		return joined;
	}

	void DrawMultiField( const Vector2D& pos, const Vector2D& size, const std::string& text, bool hovered, bool open ) {
		Render::FilledRoundedBox( pos - Vector2D( 1, 1 ), size + Vector2D( 2, 2 ), 4, 4, open ? ACCENT.Set<COLOR_A>( 160 ) : OUTLINE_DARK );
		Render::FilledRoundedBox( pos, size, 4, 4, hovered || open ? MENU_CONTROL_HOVER : MENU_CONTROL );
		Render::Text( Fonts::Menu, pos + Vector2D{ PADDING, 2 }, MENU_TEXT, FONT_LEFT, text.c_str( ) );
		MenuDraw::Chevron( pos + Vector2D( size.x - 11.f, size.y / 2.f ), open, open ? ACCENT : MENU_TEXT_DIM );
	}
}

void CMenuItem::MultiComboFocus( ) {
	const auto& args{ *reinterpret_cast< MultiComboArgs_t* >( m_pArgs ) };

	const auto width{ Menu::ControlWidth( 2 ) };
	const auto size{ Vector2D( width, ( 15 * args.m_iSize + PADDING * 2 ) * Menu::m_pFocusItem.m_flFocusAnim ) };
	const auto pos{ Menu::m_pFocusItem.m_vecDrawPos + Vector2D{ 0, ITEM_HEIGHT * 1.5f + PADDING } };
	const auto sizeOG{ Vector2D( width, ITEM_HEIGHT * 1.5f ) };

	Render::Text( Fonts::Menu, Menu::m_pFocusItem.m_vecDrawPos - Vector2D{ 0, 16 }, MENU_TEXT, FONT_LEFT, m_szName );
	DrawMultiField( Menu::m_pFocusItem.m_vecDrawPos, sizeOG, JoinSelected( args, width ), true, true );

	Interfaces::Surface->SetClipRect( pos.x - 1, pos.y - 1, size.x + 2, size.y + 2 );

	Render::FilledRoundedBox( pos - Vector2D( 1, 1 ), size + Vector2D( 2, 2 ), 4, 4, Menu::FocusAlpha( OUTLINE_DARK ) );
	Render::FilledRoundedBox( pos, size, 4, 4, Menu::FocusAlpha( MENU_POPUP ) );

	for ( int i{ }; i < args.m_iSize; ++i ) {
		auto& element{ args.m_pArguments[ i ] };
		const bool selected{ *element.m_pValue };

		const auto rowPos{ pos + Vector2D( PADDING, PADDING + 15 * i ) };
		const auto hovered{ Inputsys::hovered( rowPos, Vector2D{ size.x, 15 } ) };

		Menu::LerpToCol( element.m_cColor, selected ? ACCENT : hovered ? MENU_TEXT : MENU_TEXT_DIM );

		if ( hovered )
			Render::FilledRoundedBox( Vector2D( pos.x + 3.f, rowPos.y - 1.f ), Vector2D( size.x - 6.f, 15.f ), 3, 3, Menu::FocusAlpha( MENU_CONTROL ) );

		Render::Text( Fonts::Menu, rowPos + Vector2D( 2, 0 ), Menu::FocusAlpha( element.m_cColor ), FONT_LEFT, element.m_szName );

		if ( selected )
			MenuDraw::CheckMark( Vector2D( pos.x + size.x - 20.f, rowPos.y + 1.f ), 12.f, Menu::FocusAlpha( ACCENT ) );

		if ( hovered && Inputsys::pressed( VK_LBUTTON ) && !Menu::m_pFocusItem.m_bFrameAfterFocus && !Menu::m_pFocusItem.m_bFadeOut )
			*element.m_pValue = !*element.m_pValue;
	}

	Interfaces::Surface->SetClipRect( 0, 0, ctx.m_ve2ScreenSize.x, ctx.m_ve2ScreenSize.y );

	const auto hovered{ Inputsys::hovered( pos, size ) };

	if ( Inputsys::pressed( VK_LBUTTON ) && !hovered && !Menu::m_pFocusItem.m_bFrameAfterFocus ) {
		Menu::m_pFocusItem.m_bFrameAfterFocus = true;
		Menu::m_pFocusItem.m_bFadeOut = true;
	}
}

void CMenuItem::MultiCombo( ) {
	if ( !m_fnShow( ) )
		return;

	const auto& args{ *reinterpret_cast< MultiComboArgs_t* >( m_pArgs ) };

	const auto size{ Vector2D( Menu::ControlWidth( 2 ), ITEM_HEIGHT * 1.5f ) };

	if ( Menu::m_pFocusItem.m_pItem != this )
		Render::Text( Fonts::Menu, Menu::m_vecDrawPos, MENU_TEXT, FONT_LEFT, m_szName );

	Menu::m_vecDrawPos.y += 16;
	const auto hovered{ Inputsys::hovered( Menu::m_vecDrawPos, size ) };

	if ( Menu::m_pFocusItem.m_pItem != this )
		DrawMultiField( Menu::m_vecDrawPos, size, JoinSelected( args, size.x ), hovered, false );

	if ( hovered && Inputsys::pressed( VK_LBUTTON ) && !Menu::m_pFocusItem.m_bFrameAfterFocus && !Menu::m_pFocusItem.m_pItem )
		Menu::m_pFocusItem.Update( this );

	Menu::m_vecDrawPos.y += ITEM_HEIGHT * 1.5f + PADDING;
}
