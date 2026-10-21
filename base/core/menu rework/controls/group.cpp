#include "../menu.h"

#define GET_HEIGHT( percent ) std::ceil( static_cast<float>( Menu::m_vecSize.y - HEADER_SIZE - MARGIN * 2 - ( rowBelow ? MARGIN : 0 ) ) * percent )

void CMenuGroup::Render( int groupNum, std::vector < CMenuGroup >& groups ) {
	const auto& groupCount{ groups.size( ) };

	// TODO: properly handle this in the case that we use 3 groups
	const auto rowBelow{ groupNum == 1 ? groupCount > m_iParentMaxGroups && this->m_flPercent < 1.f
		: groupNum == 2 ? ( groupCount > m_iParentMaxGroups && groups.front( ).m_flPercent >= 1.f ) || groupCount >= 4 : groupNum > m_iParentMaxGroups };

	auto size{ Vector2D{ ( Menu::m_vecSize.x - BAR_SIZE - MARGIN * ( m_iParentMaxGroups + 1 ) ) / m_iParentMaxGroups, GET_HEIGHT( this->m_flPercent ) } };

	// new line
	if ( groupNum > 2 ) {
		if ( groups.front( ).m_flPercent + groups.front( ).m_flPercent > 0.99f ) {
			Menu::m_vecDrawPos.x += size.x + MARGIN;
			Menu::m_vecDrawPos.y += GET_HEIGHT( groups.at( 1 ).m_flPercent ) + MARGIN;
		}
		else {
			Menu::m_vecDrawPos.y += GET_HEIGHT( groups.front( ).m_flPercent ) + MARGIN;
		}
	}

	size = Vector2D{ ( Menu::m_vecSize.x - BAR_SIZE - MARGIN * ( m_iParentMaxGroups + 1 ) ) / m_iParentMaxGroups, GET_HEIGHT( this->m_flPercent ) };
	const auto hovered{ Inputsys::hovered( Menu::m_vecDrawPos, size ) };

	const auto groupPos{ Menu::m_vecDrawPos };

	// frame
	Render::FilledRoundedBox( groupPos - Vector2D( 1, 1 ), size + Vector2D( 2, 2 ), 5, 5, OUTLINE_DARK );
	Render::FilledRoundedBox( groupPos, size, 5, 5, OUTLINE_LIGHT );
	Render::FilledRoundedBox( groupPos + Vector2D( 1, 1 ), size - Vector2D( 2, 2 ), 5, 5, GROUP );

	// title bar
	Render::FilledRectangle( groupPos + Vector2D( 12.f, GROUP_TITLE / 2.f - 5.f ), Vector2D( 3.f, 10.f ), ACCENT );
	Render::Text( Fonts::MenuBold, groupPos + Vector2D( 21.f, GROUP_TITLE / 2.f - 7.f ), MENU_TEXT, FONT_LEFT, m_szName );
	Render::FilledRectangle( groupPos + Vector2D( 1.f, static_cast< float >( GROUP_TITLE ) ), Vector2D( size.x - 2.f, 1.f ), OUTLINE_LIGHT );

	const float contentTop{ static_cast< float >( GROUP_TITLE + 1 ) };
	const float contentHeight{ size.y - contentTop - 2.f };

	Interfaces::Surface->SetClipRect( groupPos.x + 2, groupPos.y + contentTop, size.x - 4, contentHeight );

	Menu::m_vecDrawPos.x += PADDING * 2;
	Menu::m_vecDrawPos.y += contentTop + PADDING * 2;
	Menu::m_vecDrawPos.y -= m_flScroll;

	if ( this->m_vecItems.size( ) ) {
		for ( auto& element : this->m_vecItems )
			element.Render( );
	}

	Interfaces::Surface->SetClipRect( 0, 0, ctx.m_ve2ScreenSize.x, ctx.m_ve2ScreenSize.y );

	// search jumped to an item in this group: scroll it into view once
	if ( Menu::m_bHighlightScroll && Owns( Menu::m_pHighlightItem ) ) {
		Menu::m_bHighlightScroll = false;

		const float top{ groupPos.y + contentTop + PADDING };
		const float bottom{ groupPos.y + size.y - PADDING * 4.f };
		if ( Menu::m_flHighlightY < top || Menu::m_flHighlightY > bottom )
			m_flScrollTarget = m_flScroll + Menu::m_flHighlightY - top - PADDING;
	}

	const float outside{ std::max( 0.f, Menu::m_vecDrawPos.y - groupPos.y - size.y + m_flScroll ) };
	if ( hovered && !Menu::m_pFocusItem.m_pItem )
		m_flScrollTarget -= Inputsys::scroll * 28;

	// smooth scrolling: the content glides to where the wheel sent it
	m_flScrollTarget = std::clamp( m_flScrollTarget, 0.f, outside );
	m_flScroll = Anim::Approach( m_flScroll, m_flScrollTarget, 18.f );
	m_flScroll = std::clamp( m_flScroll, 0.f, outside );

	Menu::m_vecDrawPos = groupPos;

	// fade content in / out at the clip edges
	Render::Gradient( groupPos.x + 2, groupPos.y + contentTop, size.x - 4, PADDING, GROUP, GROUP.Set<COLOR_A>( 0.f ), false );
	Render::Gradient( groupPos.x + 2, groupPos.y + size.y - PADDING - 2, size.x - 4, PADDING, GROUP.Set<COLOR_A>( 0.f ), GROUP, false );

	// scrollbar
	if ( outside > 0.f ) {
		const float trackTop{ contentTop + PADDING };
		const float trackHeight{ contentHeight - PADDING * 2.f };
		const float percent{ m_flScroll / outside };
		const float barLength{ std::max( 18.f, trackHeight * ( 1.f - std::min( outside / size.y, 0.85f ) ) ) };

		Render::FilledRoundedBox( groupPos + Vector2D( size.x - 6.f, trackTop ), Vector2D( 3.f, trackHeight ), 3, 1, MENU_CONTROL );
		Render::FilledRoundedBox( groupPos + Vector2D( size.x - 6.f, trackTop + percent * ( trackHeight - barLength ) ), Vector2D( 3.f, barLength ), 3, 1, ACCENT );
	}

	// new line
	if ( groupNum == 2 && groupCount > 2 )
		Menu::m_vecDrawPos.x = Menu::m_vecPos.x + BAR_SIZE + MARGIN;
	else
		Menu::m_vecDrawPos.x += size.x + MARGIN;
}

void CMenuGroup::Reset( ) {
	if ( this->m_vecItems.size( ) ) {
		for ( auto& element : this->m_vecItems ) {
			if ( element.m_eItemType == SLIDER_FLOAT || element.m_eItemType == SLIDER_INT )
				reinterpret_cast< SliderArgs_t* >( element.m_pArgs )->m_flValue = 0;

			//element.m_cColor = BACKGROUND;
		}
	}
}

void CMenuItem::RenderFocus( ) {
	switch ( this->m_eItemType ) {
	case SLIDER_FLOAT:
		SliderFloatFocus( );
		break;
	case SLIDER_INT:
		SliderIntFocus( );
		break;
	case COMBO:
		ComboFocus( );
		break;
	case MULTI_COMBO:
		MultiComboFocus( );
		break;
	case KEY_BIND:
		KeybindFocus( );
		break;
	case COLOR_PICKER:
		ColorPickerFocus( );
		break;
	case TEXT_INPUT:
		TextInputFocus( );
		break;
	default:
		break;
	}
}

namespace {
	// the clickable part of a control ( glow goes around it ) and the area that counts as hovering it
	bool ControlRect( const CMenuItem& item, const Vector2D& start, Vector2D& glowPos, Vector2D& glowSize, Vector2D& hoverSize ) {
		switch ( item.m_eItemType ) {
		case CHECKBOX:
			glowPos = start;
			glowSize = Vector2D( ITEM_HEIGHT, ITEM_HEIGHT );
			hoverSize = Vector2D( Menu::ControlWidth( 2 ), ITEM_HEIGHT );
			return true;
		case BUTTON:
			glowPos = start;
			glowSize = Vector2D( Menu::ControlWidth( reinterpret_cast< ButtonArgs_t* >( item.m_pArgs )->m_iMaxGroups ), ITEM_HEIGHT * 2.f );
			hoverSize = glowSize;
			return true;
		case TEXT_INPUT:
			glowPos = start;
			glowSize = Vector2D( Menu::ControlWidth( *reinterpret_cast< int* >( item.m_pArgs ) ), ITEM_HEIGHT * 2.f );
			hoverSize = glowSize;
			return true;
		case COMBO:
		case MULTI_COMBO:
			glowPos = start + Vector2D( 0.f, 16.f );
			glowSize = Vector2D( Menu::ControlWidth( 2 ), ITEM_HEIGHT * 1.5f );
			hoverSize = glowSize;
			return true;
		case SLIDER_INT:
		case SLIDER_FLOAT:
			glowPos = start + Vector2D( 0.f, 16.f );
			glowSize = Vector2D( Menu::ControlWidth( reinterpret_cast< SliderArgs_t* >( item.m_pArgs )->m_iMaxGroups ), ITEM_HEIGHT );
			hoverSize = glowSize;
			return true;
		default:
			return false;
		}
	}
}

void CMenuItem::Render( ) {
	const Vector2D start{ Menu::m_vecDrawPos };

	// hover glow from last frame's rectangle, under the control
	if ( m_flHover > 0.01f && m_vecGlowSize.x > 0.f )
		MenuDraw::Glow( m_vecGlowPos, m_vecGlowSize, m_flHover, ACCENT );

	RenderControl( );

	const float height{ Menu::m_vecDrawPos.y - start.y };

	// hidden ( show callback false ): nothing drawn, nothing to glow
	if ( height <= 0.5f ) {
		m_flHover = 0.f;
		m_vecGlowSize = { };
		return;
	}

	Vector2D hoverSize{ };
	if ( ControlRect( *this, start, m_vecGlowPos, m_vecGlowSize, hoverSize ) ) {
		const bool focused{ Menu::m_pFocusItem.m_pItem == this && !Menu::m_pFocusItem.m_bFadeOut };
		const bool hovered{ !Menu::m_pFocusItem.m_pItem && !Menu::m_bDraggingMenu && Inputsys::hovered( m_vecGlowPos, hoverSize ) };
		m_flHover = Anim::Approach( m_flHover, focused ? 1.f : hovered ? 0.75f : 0.f, 16.f );
	}
	else {
		m_flHover = 0.f;
		m_vecGlowSize = { };
	}

	// search result: pulse around the item for a couple of seconds
	if ( this == Menu::m_pHighlightItem ) {
		Menu::m_flHighlightY = start.y;

		const float age{ Interfaces::Globals->flRealTime - Menu::m_flHighlightTime };
		if ( age >= 0.f && age < 2.5f ) {
			const float pulse{ ( 0.5f + 0.5f * std::sin( age * 9.f ) ) * ( 1.f - age / 2.5f ) };
			const Vector2D boxPos{ start - Vector2D( 5.f, 4.f ) };
			const Vector2D boxSize{ Menu::ControlWidth( 2 ) + 10.f, height - PADDING + 8.f };
			Render::FilledRoundedBox( boxPos, boxSize, 5, 5, ACCENT.Set<COLOR_A>( 28.f * pulse ) );
			Render::RoundedBox( boxPos, boxSize, 5, 5, ACCENT.Set<COLOR_A>( 255.f * pulse ) );
		}
	}
}

void CMenuItem::RenderControl( ) {
	switch ( this->m_eItemType ) {
	case CHECKBOX:
		Checkbox( );
		break;
	case SLIDER_FLOAT:
		SliderFloat( );
		break;
	case SLIDER_INT:
		SliderInt( );
		break;
	case COMBO:
		Combo( );
		break;
	case MULTI_COMBO:
		MultiCombo( );
		break;
	case KEY_BIND:
		Keybind( );
		break;
	case COLOR_PICKER:
		ColorPicker( );
		break;	
	case LABEL:
		Label( );
		break;
	case BUTTON:
		Button( );
		break;
	case LIST_BOX:
		Listbox( );
		break;
	case TEXT_INPUT:
		TextInput( );
		break;
	default:
		break;
	}
}