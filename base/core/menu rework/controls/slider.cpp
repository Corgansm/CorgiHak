#include "../menu.h"
#include "../../../sdk/datatypes/color.h"
#include <cstdio>

namespace {
	std::string FormatSliderValue( float value, bool isFloat ) {
		char buffer[ 32 ]{ };
		if ( isFloat )
			std::snprintf( buffer, sizeof( buffer ), "%.2f", value );
		else
			std::snprintf( buffer, sizeof( buffer ), "%d", static_cast< int >( value ) );
		return buffer;
	}

	// hovered slider: left / right arrows step ( held = repeat ), shift + wheel steps, ctrl = 10x, right click = default
	float SliderInput( SliderArgs_t& args, float value, bool isFloat ) {
		const float range{ args.m_flMax - args.m_flMin };
		float step{ isFloat ? std::max( range / 100.f, 0.01f ) : 1.f };
		if ( Inputsys::down( VK_CONTROL ) )
			step *= 10.f;

		if ( Inputsys::pressed( VK_RBUTTON ) )
			value = args.m_flDefault;

		int direction{ };
		if ( Inputsys::pressed( VK_LEFT ) || Inputsys::pressed( VK_RIGHT ) ) {
			direction = Inputsys::pressed( VK_RIGHT ) ? 1 : -1;
			args.m_flRepeat = 0.35f;
		}
		else if ( Inputsys::down( VK_LEFT ) || Inputsys::down( VK_RIGHT ) ) {
			args.m_flRepeat -= Anim::Dt( );
			if ( args.m_flRepeat <= 0.f ) {
				direction = Inputsys::down( VK_RIGHT ) ? 1 : -1;
				args.m_flRepeat = 0.045f;
			}
		}

		value += step * direction;

		// the wheel is taken here so the group does not scroll at the same time
		if ( Inputsys::down( VK_SHIFT ) && Inputsys::scroll != 0 ) {
			value += step * static_cast< float >( Inputsys::scroll );
			Inputsys::scroll = 0;
		}

		return std::clamp( value, args.m_flMin, args.m_flMax );
	}

	// label + value on one line, thin track underneath with a knob
	void DrawSlider( const Vector2D& barPos, const Vector2D& size, const char* name, const std::string& valueText, float ratio, Color fill, bool active, float defaultRatio ) {
		Render::Text( Fonts::Menu, barPos - Vector2D( 0.f, 16.f ), MENU_TEXT, FONT_LEFT, name );
		Render::Text( Fonts::Menu, barPos + Vector2D( size.x, -16.f ), active ? ACCENT2 : MENU_TEXT_DIM, FONT_RIGHT, valueText.c_str( ) );

		ratio = std::clamp( ratio, 0.f, 1.f );

		const float trackY{ barPos.y + size.y / 2.f - 2.f };
		const float fillWidth{ std::max( std::ceil( size.x * ratio ), 4.f ) };

		Render::FilledRoundedBox( Vector2D( barPos.x - 1.f, trackY - 1.f ), Vector2D( size.x + 2.f, 6.f ), 3, 3, OUTLINE_DARK );
		Render::FilledRoundedBox( Vector2D( barPos.x, trackY ), Vector2D( size.x, 4.f ), 3, 2, MENU_CONTROL );
		Render::FilledRoundedBox( Vector2D( barPos.x, trackY ), Vector2D( fillWidth, 4.f ), 3, 2, fill );

		// small tick where the default sits ( right click returns there )
		defaultRatio = std::clamp( defaultRatio, 0.f, 1.f );
		if ( std::abs( defaultRatio - ratio ) > 0.01f ) {
			const float tickX{ std::round( barPos.x + std::clamp( size.x * defaultRatio, 1.f, size.x - 1.f ) ) };
			Render::FilledRectangle( Vector2D( tickX, trackY - 2.f ), Vector2D( 1.f, 8.f ), MENU_TEXT_DIM.Set<COLOR_A>( 150 ) );
		}

		const Vector2D knob{ barPos.x + std::clamp( size.x * ratio, 4.f, size.x - 4.f ), trackY + 2.f };
		MenuDraw::Circle( knob, active ? 5.5f : 4.5f, OUTLINE_DARK, 16 );
		MenuDraw::Circle( knob, active ? 4.5f : 3.5f, active ? ACCENT2 : MENU_TEXT, 16 );
	}
}

void CMenuItem::SliderFloatFocus( ) {
	auto& value{ *reinterpret_cast< float* >( m_pValue ) };
	auto& args{ *reinterpret_cast< SliderArgs_t* >( m_pArgs ) };

	const auto size{ Vector2D( Menu::ControlWidth( args.m_iMaxGroups ), ITEM_HEIGHT ) };
	const auto pos{ Menu::m_pFocusItem.m_vecDrawPos - Vector2D{ 0.f, 4.f * Menu::m_pFocusItem.m_flFocusAnim } };

	const auto ratio{ Menu::mapNumber( args.m_flValue, args.m_flMin, args.m_flMax, 0.f, 1.f ) };
	DrawSlider( pos, size, m_szName, FormatSliderValue( value, true ), ratio, this->m_cColor, true, Menu::mapNumber( args.m_flDefault, args.m_flMin, args.m_flMax, 0.f, 1.f ) );
}

void CMenuItem::SliderFloat( ) {
	auto& args{ *reinterpret_cast< SliderArgs_t* >( m_pArgs ) };

	if ( !m_fnShow( ) ) {
		args.m_flValue = 0.f;
		return;
	}

	auto& value{ *reinterpret_cast< float* >( m_pValue ) };

	const auto size{ Vector2D( Menu::ControlWidth( args.m_iMaxGroups ), ITEM_HEIGHT ) };

	value = std::clamp( value, args.m_flMin, args.m_flMax );

	Menu::m_vecDrawPos.y += 16;
	const auto hovered{ Inputsys::hovered( Menu::m_vecDrawPos, size ) };

	args.m_flValue = Math::Interpolate( args.m_flValue, value, ANIMATION_SPEED );

	const bool focused{ Menu::m_pFocusItem.m_pItem == this && !Menu::m_pFocusItem.m_bFadeOut };
	Menu::LerpToCol( this->m_cColor, focused ? ACCENT2 : hovered ? ACCENT : ACCENT_DARK );

	if ( Menu::m_pFocusItem.m_pItem == this ) {
		if ( !Menu::m_pFocusItem.m_bFadeOut ) {
			const auto offset{ std::clamp( Vector2D( Inputsys::MousePos - Menu::m_vecDrawPos ).x, 0.f, size.x ) };
			value = Menu::mapNumber( offset, 0, size.x, args.m_flMin, args.m_flMax );
		}

		if ( Inputsys::released( VK_LBUTTON ) ) {
			Menu::m_pFocusItem.m_bFrameAfterFocus = true;
			Menu::m_pFocusItem.m_bFadeOut = true;
		}
	}
	else {
		if ( !Menu::m_pFocusItem.m_pItem && hovered && !Menu::m_pFocusItem.m_bFrameAfterFocus ) {
			if ( Inputsys::pressed( VK_LBUTTON ) )
				Menu::m_pFocusItem.Update( this );
			else
				value = SliderInput( args, value, true );
		}

		const auto ratio{ Menu::mapNumber( args.m_flValue, args.m_flMin, args.m_flMax, 0.f, 1.f ) };
		DrawSlider( Menu::m_vecDrawPos, size, m_szName, FormatSliderValue( value, true ), ratio, this->m_cColor, hovered, Menu::mapNumber( args.m_flDefault, args.m_flMin, args.m_flMax, 0.f, 1.f ) );
	}

	Menu::m_vecDrawPos.y += ITEM_HEIGHT + PADDING;
}

void CMenuItem::SliderIntFocus( ) {
	auto& value{ *reinterpret_cast< int* >( m_pValue ) };
	auto& args{ *reinterpret_cast< SliderArgs_t* >( m_pArgs ) };

	const auto size{ Vector2D( Menu::ControlWidth( args.m_iMaxGroups ), ITEM_HEIGHT ) };
	const auto pos{ Menu::m_pFocusItem.m_vecDrawPos - Vector2D{ 0.f, 4.f * Menu::m_pFocusItem.m_flFocusAnim } };

	const auto ratio{ Menu::mapNumber( args.m_flValue, args.m_flMin, args.m_flMax, 0.f, 1.f ) };
	DrawSlider( pos, size, m_szName, FormatSliderValue( static_cast< float >( value ), false ), ratio, this->m_cColor, true, Menu::mapNumber( args.m_flDefault, args.m_flMin, args.m_flMax, 0.f, 1.f ) );
}

void CMenuItem::SliderInt( ) {
	auto& args{ *reinterpret_cast< SliderArgs_t* >( m_pArgs ) };

	if ( !m_fnShow( ) ) {
		args.m_flValue = 0.f;
		return;
	}

	auto& value{ *reinterpret_cast< int* >( m_pValue ) };

	const auto size{ Vector2D( Menu::ControlWidth( args.m_iMaxGroups ), ITEM_HEIGHT ) };

	value = std::clamp< int >( value, args.m_flMin, args.m_flMax );

	Menu::m_vecDrawPos.y += 16;
	const auto hovered{ Inputsys::hovered( Menu::m_vecDrawPos, size ) };

	args.m_flValue = Math::Interpolate( args.m_flValue, value, ANIMATION_SPEED );

	const bool focused{ Menu::m_pFocusItem.m_pItem == this && !Menu::m_pFocusItem.m_bFadeOut };
	Menu::LerpToCol( this->m_cColor, focused ? ACCENT2 : hovered ? ACCENT : ACCENT_DARK );

	if ( Menu::m_pFocusItem.m_pItem == this ) {
		if ( !Menu::m_pFocusItem.m_bFadeOut ) {
			const auto offset{ std::clamp( Vector2D( Inputsys::MousePos - Menu::m_vecDrawPos ).x, 0.f, size.x ) };
			value = static_cast< int >( std::round( Menu::mapNumber( offset, 0, size.x, args.m_flMin, args.m_flMax ) ) );
		}

		if ( Inputsys::released( VK_LBUTTON ) ) {
			Menu::m_pFocusItem.m_bFrameAfterFocus = true;
			Menu::m_pFocusItem.m_bFadeOut = true;
		}
	}
	else {
		if ( !Menu::m_pFocusItem.m_pItem && hovered && !Menu::m_pFocusItem.m_bFrameAfterFocus ) {
			if ( Inputsys::pressed( VK_LBUTTON ) )
				Menu::m_pFocusItem.Update( this );
			else
				value = static_cast< int >( std::round( SliderInput( args, static_cast< float >( value ), false ) ) );
		}

		const auto ratio{ Menu::mapNumber( args.m_flValue, args.m_flMin, args.m_flMax, 0.f, 1.f ) };
		DrawSlider( Menu::m_vecDrawPos, size, m_szName, FormatSliderValue( static_cast< float >( value ), false ), ratio, this->m_cColor, hovered, Menu::mapNumber( args.m_flDefault, args.m_flMin, args.m_flMax, 0.f, 1.f ) );
	}

	Menu::m_vecDrawPos.y += ITEM_HEIGHT + PADDING;
}
