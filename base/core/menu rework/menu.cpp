#include "menu.h"
#include <fstream>
#include <sstream>

void Menu::Render( ) {
	if ( !m_bOpened )
		m_pFocusItem.m_pItem = nullptr;

	// linear progress, drawn through an ease ( paint.cpp ) so opening / closing glides instead of ramping
	m_flAlpha = Anim::Step( m_flAlpha, m_bOpened, 0.2f );

	if ( m_flAlpha <= 0 ) {
		m_bCapturingKeys = false;
		m_bListeningThisFrame = false;
		return;
	}

	HandleControls( );

	m_bRendering = true;

	const auto& pos{ m_vecPos };
	const auto& size{ m_vecSize };

	// soft shadow + frame
	Render::FilledRoundedBox( pos - Vector2D( 4, 4 ), size + Vector2D( 8, 8 ), 6, WINDOW_RADIUS + 4, Color( 0, 0, 0, 45 ) );
	Render::FilledRoundedBox( pos - Vector2D( 2, 2 ), size + Vector2D( 4, 4 ), 6, WINDOW_RADIUS + 2, Color( 0, 0, 0, 70 ) );
	Render::FilledRoundedBox( pos - Vector2D( 1, 1 ), size + Vector2D( 2, 2 ), 6, WINDOW_RADIUS, OUTLINE_DARK );
	Render::FilledRoundedBox( pos, size, 6, WINDOW_RADIUS, BACKGROUND );

	// sidebar ( rounded on the left, square where it meets the content )
	Render::FilledRoundedBox( pos, Vector2D( BAR_SIZE, size.y ), 6, WINDOW_RADIUS, MENU_SIDEBAR );
	Render::FilledRectangle( pos + Vector2D( BAR_SIZE - WINDOW_RADIUS, 0 ), Vector2D( WINDOW_RADIUS, size.y ), MENU_SIDEBAR );
	Render::FilledRectangle( pos + Vector2D( BAR_SIZE, 0 ), Vector2D( 1, size.y ), OUTLINE_LIGHT );

	// content header separator
	Render::FilledRectangle( pos + Vector2D( BAR_SIZE + 1, HEADER_SIZE ), Vector2D( size.x - BAR_SIZE - 1, 1 ), OUTLINE_LIGHT );

	// accent line across the top
	Render::Gradient( pos.x + WINDOW_RADIUS, pos.y, size.x - WINDOW_RADIUS * 2, 2, ACCENT, ACCENT2, true );

	Render::RoundedBox( pos, size, 6, WINDOW_RADIUS, OUTLINE_LIGHT );

	// logo + name
	MenuDraw::Logo( pos + Vector2D( 30.f, LOGO_HEIGHT / 2.f + 3.f ), 30.f, ACCENT, ACCENT2, OUTLINE_DARK );

	const auto firstWidth{ Render::GetTextSize( MENU_NAME_FIRST, Fonts::MenuTitle ).x };
	const auto nameY{ pos.y + LOGO_HEIGHT / 2.f - 6.f };
	Render::Text( Fonts::MenuTitle, Vector2D( pos.x + 52.f, nameY ), MENU_TEXT, FONT_LEFT, MENU_NAME_FIRST );
	Render::Text( Fonts::MenuTitle, Vector2D( pos.x + 52.f + firstWidth, nameY ), ACCENT, FONT_LEFT, MENU_NAME_SECOND );

	Render::FilledRectangle( pos + Vector2D( 14, LOGO_HEIGHT + 4 ), Vector2D( BAR_SIZE - 28, 1 ), OUTLINE_LIGHT );

	// footer
	Render::FilledRectangle( pos + Vector2D( 14.f, size.y - 36.f ), Vector2D( BAR_SIZE - 28, 1 ), OUTLINE_LIGHT );
	Render::Text( Fonts::Logs, pos + Vector2D( 16.f, size.y - 26.f ), MENU_TEXT_DIM, FONT_LEFT, MENU_NAME_FIRST MENU_NAME_SECOND " " MENU_VERSION );

	// resize grip
	const auto corner{ pos + size };
	for ( int i{ }; i < 3; ++i )
		Render::Line( corner - Vector2D( 5.f + i * 4.f, 3.f ), corner - Vector2D( 3.f, 5.f + i * 4.f ), m_bDraggingSize ? ACCENT : DIM_ELEMENT );

	RenderElements( );

	// typing anywhere in the menu keeps the keys from the game ( window proc reads this )
	const bool typing{ m_pFocusItem.m_pItem && !m_pFocusItem.m_bFadeOut && m_pFocusItem.m_pItem->m_eItemType == TEXT_INPUT };
	m_bCapturingKeys = m_bOpened && ( m_bSearchActive || typing || m_bListeningThisFrame );
	m_bListeningThisFrame = false;

	SaveStateIfChanged( );

	m_bRendering = false;
	m_pFocusItem.m_bFrameAfterFocus = false;
}

void Menu::HandleControls( ) {
	// drag from the logo area or the thin strip along the top edge
	const auto dragHovered{ Inputsys::hovered( m_vecPos, { BAR_SIZE, LOGO_HEIGHT } )
		|| Inputsys::hovered( m_vecPos, { m_vecSize.x, 8.f } ) };

	if ( !m_bDraggingMenu && !m_bDraggingSize && Inputsys::pressed( VK_LBUTTON ) && dragHovered && !m_pFocusItem.m_pItem )
		m_bDraggingMenu = true;
	else if ( m_bDraggingMenu && Inputsys::down( VK_LBUTTON ) )
		m_vecPos -= Inputsys::MouseDelta;
	else if ( m_bDraggingMenu && !Inputsys::down( VK_LBUTTON ) )
		m_bDraggingMenu = false;

	const auto bottomCornerHovered{ Inputsys::hovered( m_vecPos + m_vecSize - Vector2D( 16, 16 ), Vector2D( 16, 16 ) ) };

	if ( ( bottomCornerHovered && !m_pFocusItem.m_pItem ) || m_bDraggingSize ) {
		SetCursor( true );

		if ( Inputsys::down( VK_LBUTTON ) && !m_bDraggingMenu ) {
			m_bDraggingSize = true;
			m_vecSize += Vector2D( Inputsys::MousePos - ( m_vecPos + m_vecSize ) );
		}
		else
			m_bDraggingSize = false;
	}
	else
		SetCursor( false );

	m_vecSize.x = std::clamp( static_cast< int >( m_vecSize.x ), MENU_MIN_WIDTH, MENU_MAX_WIDTH );
	m_vecSize.y = std::clamp( static_cast< int >( m_vecSize.y ), MENU_MIN_HEIGHT, MENU_MAX_HEIGHT );

	// keep the window reachable after a resolution change
	if ( ctx.m_ve2ScreenSize.x > 0.f && ctx.m_ve2ScreenSize.y > 0.f ) {
		m_vecPos.x = std::clamp( m_vecPos.x, 60.f - m_vecSize.x, ctx.m_ve2ScreenSize.x - 60.f );
		m_vecPos.y = std::clamp( m_vecPos.y, 0.f, ctx.m_ve2ScreenSize.y - 40.f );
	}
}

void Menu::SetCursor( bool resize ) {
	// the game draws its own cursor while the menu is open; nothing to swap
	( void )resize;
}

void FocusItem_t::Update( CMenuItem* item ) {
	this->m_pItem = item;
	this->m_vecDrawPos = Menu::m_vecDrawPos;
	this->m_bFadeOut = false;
	this->m_bFrameAfterFocus = true;
}

/* window state across reinjects: "x y w h tab sub0 sub1 sub2 sub3 sub4" in the config folder */

namespace {
	std::string StateString( ) {
		std::ostringstream out{ };
		out << static_cast< int >( Menu::m_vecPos.x ) << ' ' << static_cast< int >( Menu::m_vecPos.y ) << ' '
			<< static_cast< int >( Menu::m_vecSize.x ) << ' ' << static_cast< int >( Menu::m_vecSize.y ) << ' '
			<< static_cast< int >( Menu::m_iSelectedTab );

		for ( const auto& tab : Menu::m_cTabs ) {
			int index{ };
			if ( tab.m_pSelectedSubtab && !tab.m_vecSubtabs.empty( ) )
				index = static_cast< int >( tab.m_pSelectedSubtab - tab.m_vecSubtabs.data( ) );
			out << ' ' << index;
		}

		return out.str( );
	}

	std::string g_strSavedState{ };
}

void Menu::LoadState( ) {
	std::ifstream in( Config::fsPath / _( "menu.txt" ), std::ios::in );
	if ( !in.good( ) )
		return;

	std::string line{ };
	std::getline( in, line );
	std::istringstream values( line );

	int x{ }, y{ }, w{ }, h{ }, tab{ };
	if ( !( values >> x >> y >> w >> h >> tab ) )
		return;

	m_vecPos = Vector2D( static_cast< float >( x ), static_cast< float >( y ) );
	m_vecSize = Vector2D( static_cast< float >( std::clamp( w, MENU_MIN_WIDTH, MENU_MAX_WIDTH ) ), static_cast< float >( std::clamp( h, MENU_MIN_HEIGHT, MENU_MAX_HEIGHT ) ) );

	if ( tab >= 0 && tab < 5 && !m_cTabs[ tab ].m_vecSubtabs.empty( ) )
		m_iSelectedTab = static_cast< uint8_t >( tab );

	for ( auto& entry : m_cTabs ) {
		int index{ };
		if ( !( values >> index ) )
			break;
		if ( index >= 0 && index < static_cast< int >( entry.m_vecSubtabs.size( ) ) )
			entry.m_pSelectedSubtab = &entry.m_vecSubtabs.at( index );
	}

	g_strSavedState = StateString( );
}

void Menu::SaveStateIfChanged( ) {
	// not mid drag, and at most once a second
	if ( m_bDraggingMenu || m_bDraggingSize )
		return;

	static float lastSave{ };
	const float now{ Interfaces::Globals->flRealTime };
	if ( std::abs( now - lastSave ) < 1.f )
		return;

	const auto state{ StateString( ) };
	if ( state == g_strSavedState )
		return;

	lastSave = now;
	g_strSavedState = state;

	std::ofstream out( Config::fsPath / _( "menu.txt" ), std::ios::out | std::ios::trunc );
	if ( out.good( ) )
		out << state << '\n';
}
