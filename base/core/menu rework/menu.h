#pragma once
#include "../../utils/render.h"
#include "../../utils/anim.h"
#include "../input_manager.h"
#include "../config.h"
#include "../variables.h"
#include "controls/containers.h"
#include "icons.h"
#include <memory>
#include <optional>

// branding
#define MENU_NAME_FIRST "Corgi"
#define MENU_NAME_SECOND "Hak"
#define MENU_VERSION "v1.0"

// colors ( corgi orange + cream )
#define ACCENT Color( 255, 149, 46 )
#define ACCENT2 Color( 255, 214, 153 )
#define ACCENT_DARK Color( 196, 108, 30 )

#define BACKGROUND Color( 17, 17, 20 )
#define MENU_SIDEBAR Color( 12, 12, 15 )
#define GROUP Color( 23, 23, 27 )
#define MENU_POPUP Color( 28, 28, 33 )
#define MENU_CONTROL Color( 34, 34, 40 )
#define MENU_CONTROL_HOVER Color( 46, 46, 54 )

#define OUTLINE_LIGHT Color( 44, 44, 51 )
#define OUTLINE_DARK Color( 8, 8, 10 )

#define DIM_ELEMENT Color( 52, 52, 60 )
#define HOVERED_ELEMENT Color( 150, 150, 160 )
#define SELECTED_ELEMENT Color( 225, 225, 232 )

#define MENU_TEXT Color( 228, 228, 234 )
#define MENU_TEXT_DIM Color( 128, 128, 140 )

// layout
#define BAR_SIZE 150
#define HEADER_SIZE 46
#define LOGO_HEIGHT 64
#define TAB_HEIGHT 36
#define GROUP_TITLE 26
#define MARGIN 16
#define PADDING 8
#define ITEM_HEIGHT 12
#define WINDOW_RADIUS 6

#define MENU_DEFAULT_WIDTH 860
#define MENU_DEFAULT_HEIGHT 640
#define MENU_MIN_WIDTH 700
#define MENU_MIN_HEIGHT 460
#define MENU_MAX_WIDTH 1150
#define MENU_MAX_HEIGHT 820

// fraction of the way to cover this frame ( frame rate independent, see utils/anim.h )
#define ANIMATION_SPEED Anim::Factor( 10.f )

struct FocusItem_t {
	CMenuItem* m_pItem{ };
	Vector2D m_vecDrawPos{ };
	float m_flFocusAnim{ };
	bool m_bFrameAfterFocus{ };
	bool m_bFadeOut{ };

	void Update( CMenuItem* item );
};

namespace Menu {
	void Render( );
	void RenderElements( );
	void Register( );

	void HandleControls( );
	void SetCursor( bool resize );
	void LerpToCol( Color& col, Color to );

	inline float mapNumber( float input, float inputMin, float inputMax, float outputMin, float outputMax ) {
		return ( input - inputMin ) / ( inputMax - inputMin ) * ( outputMax - outputMin ) + outputMin;
	}

	/* main */
	inline bool m_bOpened{ true };
	inline bool m_bRendering{ };
	inline float m_flAlpha{ };
	inline FocusItem_t m_pFocusItem{ };

	inline Vector2D m_vecDrawPos{ };

	inline void* m_pPopup{ };
	inline uint8_t m_iSelectedTab{ };

	inline CMenuTab m_cTabs[ 5 ]{ };

	inline Vector2D m_vecSize{ MENU_DEFAULT_WIDTH, MENU_DEFAULT_HEIGHT };
	inline Vector2D m_vecPos{ 500, 200 };

	/* control stuff */
	inline bool m_bDraggingMenu{ };
	inline bool m_bDraggingSize{ };

	/* search ( header box, ctrl + f ) */
	inline bool m_bSearchActive{ };          // the search box has keyboard focus
	inline CMenuItem* m_pHighlightItem{ };   // item a search result jumped to ( compared only, never dereferenced )
	inline float m_flHighlightTime{ -100.f };
	inline float m_flHighlightY{ };          // where it was drawn this frame
	inline bool m_bHighlightScroll{ };       // its group scrolls it into view once

	void RenderSearch( bool& blockClicks );

	/* keyboard capture: while typing ( search, text box, key bind listening ) keys are kept from the game */
	inline bool m_bCapturingKeys{ };         // read by the window proc
	inline bool m_bListeningThisFrame{ };    // a keybind is waiting for a key ( set while drawing )

	// display name of a virtual key ( "M4", "SHFT", "none" )
	std::string KeyName( int key );

	/* window state ( position, size, tab, subtabs ) kept across reinjects */
	void LoadState( );
	void SaveStateIfChanged( );

	// width every control in a group gets ( group width minus inner padding )
	inline float ControlWidth( int groupsPerRow ) {
		groupsPerRow = std::max( groupsPerRow, 1 );
		return ( m_vecSize.x - BAR_SIZE - MARGIN * ( groupsPerRow + 1 ) ) / groupsPerRow - PADDING * 4;
	}

	// focused popups ( dropdowns, pickers ) fade with this
	inline Color FocusAlpha( Color col ) {
		return col.Set<COLOR_A>( m_pFocusItem.m_flFocusAnim * col.Get<COLOR_A>( ) );
	}
}
