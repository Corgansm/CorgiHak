#include "../core/hooks.h"
#include "../utils/render.h"
#include "../core/menu rework/menu.h"
#include "../core/framework/framework.h"
#include "../context.h"
#include <intrin.h>

LRESULT CALLBACK Hooks::hkWndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	// bit 30: key was already down ( auto repeat ), holding insert no longer flickers the menu
	if ( uMsg == WM_KEYDOWN && wParam == VK_INSERT && !( lParam & ( 1 << 30 ) ) )
	{
		Menu::m_bOpened = !Menu::m_bOpened;
		MenuFramework::m_bOpen = !MenuFramework::m_bOpen;
	}

	

	//if ( Menu::Opened && ( uMsg == WM_LBUTTONDOWN || uMsg == WM_LBUTTONUP || uMsg == WM_MOUSEMOVE ) )
	//	return false;

	if ( Menu::m_bOpened ) {
		// typing into the menu: the game never sees the key presses ( no walking, binds or pause menu ).
		// key releases still go through so nothing held before stays stuck
		if ( Menu::m_bCapturingKeys && wParam != VK_INSERT ) {
			switch ( uMsg ) {
			case WM_KEYDOWN:
			case WM_SYSKEYDOWN:
			case WM_CHAR:
			case WM_SYSCHAR:
				return 0;
			default:
				break;
			}
		}

		if ( uMsg == WM_MOUSEWHEEL || uMsg == WM_MOUSEHWHEEL )
			Inputsys::scroll += ( float )GET_WHEEL_DELTA_WPARAM( wParam ) / ( float )WHEEL_DELTA;

		switch ( uMsg ) {
		case WM_MOUSEMOVE:
		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONUP:
		case WM_MBUTTONDOWN:
		case WM_MBUTTONUP:
		case WM_MOUSEWHEEL:
		case WM_XBUTTONDOWN:
		case WM_XBUTTONUP:
			return 0;

		default:
			break;
		}
	}

	// return input controls to the game
	return CallWindowProc( pOldWndProc, hWnd, uMsg, wParam, lParam );
}