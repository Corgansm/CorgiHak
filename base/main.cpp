#include <thread>
#include "havoc.h"
#include "context.h"
#include "utils/math.h"
#include "utils/render.h"
#include "utils/crash_dumper.h"
#include "utils/crash_log.h"
#include "core/prop_manager.h"
#include "core/config.h"
#include "core/hooks.h"
#include "core/menu rework/menu.h"
#include "core/displacement.h"
#include "core/event_listener.h"
#include "core/lua/lua_engine.h"
#include "features/misc/logger.h"
#include "features/misc/skinchanger/kitparser.h"
#include <tchar.h>

FILE* pStream;

// our own module, freed after unloading so a new build can be injected into the same game
HMODULE g_hModule{ };

namespace {
	// END while the game window has focus unloads ( same as the menu button )
	bool UnloadKeyPressed( ) {
		if ( !( GetAsyncKeyState( VK_END ) & 0x8000 ) )
			return false;

		DWORD process{ };
		GetWindowThreadProcessId( GetForegroundWindow( ), &process );
		return process == GetCurrentProcessId( );
	}

	// true only for a module the windows loader knows ( LoadLibrary injection ), never for a manually mapped image
	bool LoadedByWindows( HMODULE module ) {
		wchar_t path[ MAX_PATH ]{ };
		return module && GetModuleFileNameW( module, path, MAX_PATH ) != 0;
	}
}

void Entry( void* netvars )
{
	while ( !MEM::GetModuleBaseHandle( SERVERBROWSER_DLL ) )
		std::this_thread::sleep_for( 200ms );

	CrashLog::Info( "waiting done, setting up interfaces" );
	if (!Interfaces::Setup()) {
		CrashLog::Info( "FAILED: Interfaces::Setup" );
		return;
	}

	CrashLog::Info( "interfaces ok, registering menu" );
	Menu::Register( );
	CrashLog::Info( "menu ok, config setup" );
	Config::Setup( );
	Menu::LoadState( );

	Interfaces::GameConsole->Clear( );

	CrashLog::Info( "config ok, prop manager" );
	if ( !PropManager::Get( ).Create( ) ) {
		CrashLog::Info( "FAILED: PropManager" );
		return;
	}

	Displacement::Init( netvars );
	CrashLog::Info( "displacement ok" );

	// start with the config that was active last time ( survives a reinject )
	const auto lastConfig{ Config::LoadLast( ) };
	if ( lastConfig.empty( ) )
		CrashLog::Info( "no last config, defaults" );
	else
		CrashLog::Info( "loaded last config %s", lastConfig.c_str( ) );

	if (!Math::Setup()) {
		CrashLog::Info( "FAILED: Math::Setup" );
		return;
	}
	
	Render::CreateFonts( );
	EventListener.Setup( { _( "bullet_impact" ), _( "round_start" ), _( "player_hurt" ),_( "weapon_fire" ), _( "player_death" ) } );

	CrashLog::Info( "fonts + events ok, hooking" );
	if (!Hooks::Setup()) {
		CrashLog::Info( "FAILED: Hooks::Setup" );
		return;
	}
	CrashLog::Info( "hooks ok, running" );

	Features::Logger.Log( _( "Deployed CorgiHak." ), true );

	// what this injection is running: tells a fresh build apart from the old one after a reinject
	Features::Logger.Log( std::string( "build " __DATE__ " " __TIME__ ) + " | config: "
		+ ( lastConfig.empty( ) ? std::string( "defaults" ) : lastConfig ), true );
	{
		const auto scripts{ Lua::AutoloadSummary( ) };
		Features::Logger.Log( "auto-loading scripts: " + ( scripts.empty( ) ? std::string( "none" ) : scripts ), true );
	}

	while ( !ctx.m_bUnload ) {
		if ( UnloadKeyPressed( ) )
			ctx.m_bUnload = true;
		std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
	}

	CrashLog::Info( "unloading" );
	Hooks::Restore( );
	EventListener.Destroy( );

	// hooks are gone, give the game thread a moment to leave any lua callback before the states close
	std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
	Lua::Shutdown( );

#ifndef _RELEASE
	delete Displacement::Netvars;
#endif

	if ( netvars )
		delete netvars;

	CrashLog::Shutdown( );

	// let every game thread leave our code ( a hook that was already running when we restored them ),
	// then free the dll: the file is unlocked for the next build and a fresh injection runs DllMain again
	std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
	if ( LoadedByWindows( g_hModule ) )
		FreeLibraryAndExitThread( g_hModule, 0 );
}

BOOL APIENTRY DllMain( HMODULE hModule, DWORD dwReason, LPVOID lpReserved )
{
	if ( dwReason == DLL_PROCESS_ATTACH ) {
		DisableThreadLibraryCalls( hModule );
		g_hModule = hModule;
		CrashLog::Init( hModule );

		// welcome to artiehack/timhack/ETHEREAL/havoc AKA BEST HVH CHEAT
		if ( const HANDLE hThread = ::CreateThread( nullptr, 0U, LPTHREAD_START_ROUTINE( Entry ), lpReserved, 0UL, nullptr ); hThread != nullptr )
			::CloseHandle( hThread );

		return TRUE;
	}

	return FALSE;
}