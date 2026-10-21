#include "../menu.h"
#include "../../../sdk/datatypes/color.h"
#include "../../config.h"

// STATIC IMPLEMENTATION!
// TODO: (only if using it multiple times)- dynamically do it. for now idc :)
void CMenuItem::Listbox( ) {
	auto& value{ *reinterpret_cast< int* >( m_pValue ) };
	auto& scroll{ *reinterpret_cast< float* >( m_pArgs ) };


	//Render::Text( Fonts::Menu, Menu::m_vecDrawPos, Color( 255, 255, 255 ), FONT_LEFT, m_szName );
	//Menu::m_vecDrawPos.y += 16;

	static std::vector< ComboElement_t > configs{ };
	if ( !configs.size( ) )
		Config::m_bDueRefresh = true;

	bool refreshed{ };
	if ( Config::m_bDueRefresh ) {
		Config::Refresh( );

		configs.clear( );
		for ( auto& config : Config::vecFileNames )
			configs.push_back( config.c_str( ) );

		refreshed = true;
		Config::m_bDueRefresh = false;

		// first fill after inject: select the config that was loaded automatically
		static bool selectedActive{ };
		if ( !selectedActive && !Config::strActive.empty( ) ) {
			selectedActive = true;
			for ( int i{ }; i < static_cast< int >( Config::vecFileNames.size( ) ); ++i ) {
				if ( Config::vecFileNames.at( i ) == Config::strActive ) {
					value = i;
					break;
				}
			}
		}
	}

	// whole group width; height leaves room for the text box + 5 buttons under it
	const auto size{ Vector2D( Menu::ControlWidth( 1 ),
		std::max( 60.f, Menu::m_vecSize.y - HEADER_SIZE - GROUP_TITLE - MARGIN * 2 - PADDING * 4 - PADDING * 6 - ITEM_HEIGHT * 2 * 6 ) ) };

	Render::FilledRoundedBox( Menu::m_vecDrawPos - Vector2D( 1, 1 ), size + Vector2D( 2, 2 ), 4, 4, OUTLINE_DARK );
	Render::FilledRoundedBox( Menu::m_vecDrawPos, size, 4, 4, MENU_CONTROL );

	Interfaces::Surface->SetClipRect( Menu::m_vecDrawPos.x + 2, Menu::m_vecDrawPos.y + 2, size.x - 4, size.y - 4 );

	const auto wholeHovered{ Inputsys::hovered( Menu::m_vecDrawPos, size ) };

	for ( int i{ }; i < configs.size( ); ++i ) {
		auto& element{ configs.at( i ) };

		const auto pos{ Menu::m_vecDrawPos + Vector2D( PADDING, 4 + 18 * i - scroll ) };

		const auto hovered{ Inputsys::hovered( pos, Vector2D{ size.x, 18 } ) };
		const Color target{ value == i ? ACCENT : hovered ? MENU_TEXT : MENU_TEXT_DIM };

		if ( !refreshed )
			Menu::LerpToCol( element.m_cColor, target );
		else
			element.m_cColor = target;

		if ( value == i ) {
			Render::FilledRoundedBox( Vector2D( Menu::m_vecDrawPos.x + 3.f, pos.y - 1.f ), Vector2D( size.x - 6.f, 17.f ), 3, 3, MENU_CONTROL_HOVER );
			Render::FilledRectangle( Vector2D( Menu::m_vecDrawPos.x + 3.f, pos.y + 2.f ), Vector2D( 2.f, 11.f ), ACCENT );
		}
		else if ( hovered )
			Render::FilledRoundedBox( Vector2D( Menu::m_vecDrawPos.x + 3.f, pos.y - 1.f ), Vector2D( size.x - 6.f, 17.f ), 3, 3, MENU_CONTROL_HOVER.Set<COLOR_A>( 120 ) );

		Render::Text( Fonts::Menu, pos + Vector2D( 4, 1 ), element.m_cColor, FONT_LEFT, element.m_szName );

		if ( wholeHovered && hovered && Inputsys::pressed( VK_LBUTTON ) && !Menu::m_pFocusItem.m_pItem && !Menu::m_pFocusItem.m_bFrameAfterFocus )
			value = i;
	}

	Interfaces::Surface->SetClipRect( 0, 0, ctx.m_ve2ScreenSize.x, ctx.m_ve2ScreenSize.y );

	const float outside{ std::max( 0.f, PADDING + 18.f * configs.size( ) - size.y ) };
	if ( outside > 0.f ) {
		const float percent{ scroll / outside };
		const float barLength{ std::max( 18.f, size.y * ( 1.f - std::min( outside / size.y, 0.85f ) ) ) };
		Render::FilledRoundedBox( Menu::m_vecDrawPos + Vector2D( size.x - 5.f, 2.f + percent * ( size.y - barLength - 4.f ) ),
			Vector2D( 3.f, barLength ), 3, 1, ACCENT );
	}

	if ( configs.empty( ) )
		Render::Text( Fonts::Menu, Menu::m_vecDrawPos + Vector2D( PADDING + 4, 6 ), MENU_TEXT_DIM, FONT_LEFT, _( "no configs yet - type a name below and press Create" ) );

	if ( wholeHovered ) {
		scroll -= Inputsys::scroll * 18;
		scroll = std::min( outside, scroll );
		scroll = std::max( 0.f, scroll );
	}

	Menu::m_vecDrawPos.y += size.y + PADDING;
}