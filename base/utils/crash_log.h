#pragma once
// CorgiHak crash + debug logging.
//
// Writes to Desktop\CorgiHak_logs\:
//   session_<time>.log  - startup steps and anything passed to CrashLog::Info
//   crash_<time>.log    - exception code, registers, call stack, our-module return addresses, last breadcrumbs
//   crash_<time>.dmp    - minidump for the first fatal exception ( open in Visual Studio / WinDbg )
//
// Breadcrumbs: CRASH_MARK( "FSN:NET_UPDATE_END" ) records the last places each thread passed through,
// so the crash log shows what the cheat was doing right before it died. Marks only store a pointer to a
// string literal, so they are cheap enough to leave in hot paths.
#include <Windows.h>
#include <dbghelp.h>
#include <shlobj.h>
#include <atomic>
#include <cstdio>
#include <cstdarg>
#include <ctime>

#pragma comment( lib, "dbghelp.lib" )

namespace CrashLog {
	struct Mark_t {
		const char* m_szTag{ };
		DWORD m_dwThread{ };
		DWORD m_dwTick{ };
	};

	constexpr int MARK_COUNT{ 128 };
	constexpr int MAX_CRASH_REPORTS{ 8 };

	inline Mark_t g_Marks[ MARK_COUNT ]{ };
	inline std::atomic< unsigned > g_MarkIndex{ };

	inline uintptr_t g_ModuleBase{ };
	inline uintptr_t g_ModuleSize{ };
	inline char g_LogDir[ MAX_PATH ]{ };
	inline char g_SessionPath[ MAX_PATH ]{ };
	inline char g_Stamp[ 32 ]{ };
	inline std::atomic< int > g_Reports{ };
	inline std::atomic< bool > g_InHandler{ };
	inline bool g_DumpWritten{ };
	inline PVOID g_Handler{ };
	inline CRITICAL_SECTION g_FileLock{ };
	inline bool g_Ready{ };

	inline void Mark( const char* tag ) {
		const auto i{ g_MarkIndex.fetch_add( 1, std::memory_order_relaxed ) % MARK_COUNT };
		g_Marks[ i ].m_szTag = tag;
		g_Marks[ i ].m_dwThread = GetCurrentThreadId( );
		g_Marks[ i ].m_dwTick = GetTickCount( );
	}

	/* raw file helpers ( no heap, safe inside the exception handler ) */
	inline void AppendRaw( const char* path, const char* text, int length ) {
		const HANDLE file{ CreateFileA( path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr ) };
		if ( file == INVALID_HANDLE_VALUE )
			return;

		DWORD written{ };
		WriteFile( file, text, static_cast< DWORD >( length ), &written, nullptr );
		CloseHandle( file );
	}

	inline void AppendF( const char* path, const char* fmt, ... ) {
		char buffer[ 1024 ];
		va_list args;
		va_start( args, fmt );
		const int length{ _vsnprintf_s( buffer, sizeof( buffer ), _TRUNCATE, fmt, args ) };
		va_end( args );

		AppendRaw( path, buffer, length < 0 ? static_cast< int >( strlen( buffer ) ) : length );
	}

	// timestamped line in the session log
	inline void Info( const char* fmt, ... ) {
		if ( !g_Ready )
			return;

		char message[ 900 ];
		va_list args;
		va_start( args, fmt );
		_vsnprintf_s( message, sizeof( message ), _TRUNCATE, fmt, args );
		va_end( args );

		SYSTEMTIME st;
		GetLocalTime( &st );

		EnterCriticalSection( &g_FileLock );
		AppendF( g_SessionPath, "[%02d:%02d:%02d.%03d][tid %lu] %s\r\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, GetCurrentThreadId( ), message );
		LeaveCriticalSection( &g_FileLock );
	}

	inline bool InOurModule( uintptr_t address ) {
		return g_ModuleBase && address >= g_ModuleBase && address < g_ModuleBase + g_ModuleSize;
	}

	// "client.dll+0x1234", "corgihak+0x1234" or the raw address
	inline void DescribeAddress( uintptr_t address, char* out, size_t outSize ) {
		if ( InOurModule( address ) ) {
			_snprintf_s( out, outSize, _TRUNCATE, "corgihak+0x%X", static_cast< unsigned >( address - g_ModuleBase ) );
			return;
		}

		HMODULE module{ };
		if ( GetModuleHandleExA( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast< LPCSTR >( address ), &module ) && module ) {
			char path[ MAX_PATH ]{ };
			GetModuleFileNameA( module, path, MAX_PATH );
			const char* name{ strrchr( path, '\\' ) };
			name = name ? name + 1 : path;
			_snprintf_s( out, outSize, _TRUNCATE, "%s+0x%X", name, static_cast< unsigned >( address - reinterpret_cast< uintptr_t >( module ) ) );
			return;
		}

		_snprintf_s( out, outSize, _TRUNCATE, "0x%08X", static_cast< unsigned >( address ) );
	}

	inline const char* CodeName( DWORD code ) {
		switch ( code ) {
		case EXCEPTION_ACCESS_VIOLATION: return "ACCESS_VIOLATION";
		case EXCEPTION_STACK_OVERFLOW: return "STACK_OVERFLOW";
		case EXCEPTION_ILLEGAL_INSTRUCTION: return "ILLEGAL_INSTRUCTION";
		case EXCEPTION_PRIV_INSTRUCTION: return "PRIV_INSTRUCTION";
		case EXCEPTION_INT_DIVIDE_BY_ZERO: return "INT_DIVIDE_BY_ZERO";
		case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "FLT_DIVIDE_BY_ZERO";
		case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "ARRAY_BOUNDS_EXCEEDED";
		case EXCEPTION_IN_PAGE_ERROR: return "IN_PAGE_ERROR";
		case 0xC0000374: return "HEAP_CORRUPTION";
		case 0xE06D7363: return "C++ EXCEPTION ( throw )";
		default: return nullptr;
		}
	}

	inline void WriteDump( EXCEPTION_POINTERS* info, const char* path ) {
		const HANDLE file{ CreateFileA( path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr ) };
		if ( file == INVALID_HANDLE_VALUE )
			return;

		MINIDUMP_EXCEPTION_INFORMATION exceptionInfo{ GetCurrentThreadId( ), info, FALSE };
		MiniDumpWriteDump( GetCurrentProcess( ), GetCurrentProcessId( ), file,
			static_cast< MINIDUMP_TYPE >( MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory | MiniDumpWithThreadInfo ),
			&exceptionInfo, nullptr, nullptr );
		CloseHandle( file );
	}

	inline void WriteReport( EXCEPTION_POINTERS* info, const char* codeName ) {
		const auto record{ info->ExceptionRecord };
		const auto context{ info->ContextRecord };
		const auto address{ reinterpret_cast< uintptr_t >( record->ExceptionAddress ) };

		SYSTEMTIME st;
		GetLocalTime( &st );

		char path[ MAX_PATH ];
		_snprintf_s( path, sizeof( path ), _TRUNCATE, "%s\\crash_%s_%d.log", g_LogDir, g_Stamp, g_Reports.load( ) );

		char where[ 128 ];
		DescribeAddress( address, where, sizeof( where ) );

		AppendF( path, "CorgiHak crash report  %04d-%02d-%02d %02d:%02d:%02d\r\n", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond );
		AppendF( path, "exception   : %s ( 0x%08X )\r\n", codeName, record->ExceptionCode );
		AppendF( path, "address     : %s\r\n", where );
		AppendF( path, "in cheat    : %s\r\n", InOurModule( address ) ? "YES" : "no ( game / other module, check the stack below )" );
		AppendF( path, "thread      : %lu\r\n", GetCurrentThreadId( ) );
		AppendF( path, "cheat base  : 0x%08X size 0x%X\r\n", static_cast< unsigned >( g_ModuleBase ), static_cast< unsigned >( g_ModuleSize ) );

		if ( record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2 ) {
			const auto op{ record->ExceptionInformation[ 0 ] };
			AppendF( path, "access      : %s 0x%08X%s\r\n",
				op == 0 ? "READ of" : op == 1 ? "WRITE to" : "EXECUTE at",
				static_cast< unsigned >( record->ExceptionInformation[ 1 ] ),
				record->ExceptionInformation[ 1 ] < 0x10000 ? "  <- null / near-null pointer" : "" );
		}

#ifdef _M_IX86
		AppendF( path, "\r\nregisters\r\n  EAX %08X  EBX %08X  ECX %08X  EDX %08X\r\n  ESI %08X  EDI %08X  EBP %08X  ESP %08X  EIP %08X\r\n",
			context->Eax, context->Ebx, context->Ecx, context->Edx, context->Esi, context->Edi, context->Ebp, context->Esp, context->Eip );
#endif

		/* call stack via dbghelp ( works for game modules, frame pointer chains in ours ) */
		AppendF( path, "\r\ncall stack\r\n" );
		// DbgHelp can exhaust the remaining stack before raw addresses and breadcrumbs
		// are written. The overflow report must use only the lightweight path below.
		if (record->ExceptionCode != EXCEPTION_STACK_OVERFLOW) {
			const auto process{ GetCurrentProcess( ) };
			static bool symInit{ };
			if ( !symInit ) {
				SymSetOptions( SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS );
				symInit = SymInitialize( process, nullptr, TRUE ) != FALSE;
			}

			CONTEXT ctx{ *context };
			STACKFRAME64 frame{ };
#ifdef _M_IX86
			const DWORD machine{ IMAGE_FILE_MACHINE_I386 };
			frame.AddrPC.Offset = ctx.Eip;
			frame.AddrFrame.Offset = ctx.Ebp;
			frame.AddrStack.Offset = ctx.Esp;
#else
			const DWORD machine{ IMAGE_FILE_MACHINE_AMD64 };
			frame.AddrPC.Offset = ctx.Rip;
			frame.AddrFrame.Offset = ctx.Rbp;
			frame.AddrStack.Offset = ctx.Rsp;
#endif
			frame.AddrPC.Mode = AddrModeFlat;
			frame.AddrFrame.Mode = AddrModeFlat;
			frame.AddrStack.Mode = AddrModeFlat;

			for ( int i{ }; i < 32; ++i ) {
				if ( !StackWalk64( machine, process, GetCurrentThread( ), &frame, &ctx, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr ) )
					break;
				if ( !frame.AddrPC.Offset )
					break;

				char desc[ 128 ];
				DescribeAddress( static_cast< uintptr_t >( frame.AddrPC.Offset ), desc, sizeof( desc ) );

				char symbolBuffer[ sizeof( SYMBOL_INFO ) + 256 ]{ };
				auto symbol{ reinterpret_cast< SYMBOL_INFO* >( symbolBuffer ) };
				symbol->SizeOfStruct = sizeof( SYMBOL_INFO );
				symbol->MaxNameLen = 255;
				DWORD64 displacement{ };

				if ( symInit && SymFromAddr( process, frame.AddrPC.Offset, &displacement, symbol ) )
					AppendF( path, "  #%02d %-32s %s+0x%llX\r\n", i, desc, symbol->Name, displacement );
				else
					AppendF( path, "  #%02d %s\r\n", i, desc );
			}
		}

		/* raw stack scan for return addresses inside the cheat ( catches frames the walker loses ) */
#ifdef _M_IX86
		AppendF( path, "\r\ncheat addresses found on the stack ( newest first )\r\n" );
		{
			auto stack{ reinterpret_cast< const uintptr_t* >( context->Esp ) };
			const auto stackEnd{ reinterpret_cast< const uintptr_t* >( reinterpret_cast< NT_TIB* >( NtCurrentTeb( ) )->StackBase ) };
			const auto stackLimit{ reinterpret_cast< NT_TIB* >(NtCurrentTeb())->StackLimit };
			AppendF(path, "  stack limit=%p base=%p fault ESP=%08X\r\n", stackLimit, stackEnd, context->Esp);
			int found{ };
			for ( int i{ }; i < 2048 && found < 24 && stack + i < stackEnd; ++i ) {
				const auto value{ stack[ i ] };
				if ( InOurModule( value ) ) {
					AppendF( path, "  [esp+0x%04X] corgihak+0x%X\r\n", i * 4, static_cast< unsigned >( value - g_ModuleBase ) );
					++found;
				}
			}
			if ( !found )
				AppendF( path, "  none\r\n" );
		}
#endif

		/* breadcrumbs, oldest to newest */
		AppendF( path, "\r\nlast breadcrumbs ( oldest -> newest, * = crashing thread )\r\n" );
		{
			const auto end{ g_MarkIndex.load( ) };
			const auto start{ end > MARK_COUNT ? end - MARK_COUNT : 0u };
			const auto now{ GetTickCount( ) };
			const auto thread{ GetCurrentThreadId( ) };

			for ( auto i{ start }; i < end; ++i ) {
				const auto& mark{ g_Marks[ i % MARK_COUNT ] };
				if ( !mark.m_szTag )
					continue;

				AppendF( path, "  %c tid %-6lu -%5lums  %s\r\n", mark.m_dwThread == thread ? '*' : ' ', mark.m_dwThread, now - mark.m_dwTick, mark.m_szTag );
			}
		}

		if (record->ExceptionCode == EXCEPTION_STACK_OVERFLOW)
			AppendF(path, "\r\nsymbol walk and minidump skipped on exhausted thread stack\r\n");
		if ( !g_DumpWritten && record->ExceptionCode != 0xE06D7363
			&& record->ExceptionCode != EXCEPTION_STACK_OVERFLOW ) {
			char dumpPath[ MAX_PATH ];
			_snprintf_s( dumpPath, sizeof( dumpPath ), _TRUNCATE, "%s\\crash_%s.dmp", g_LogDir, g_Stamp );
			WriteDump( info, dumpPath );
			g_DumpWritten = true;
			AppendF( path, "\r\nminidump    : %s\r\n", dumpPath );
		}

		Info( "!!! %s at %s - report written to %s", codeName, where, path );
	}

	inline LONG CALLBACK VectoredHandler( EXCEPTION_POINTERS* info ) {
		const auto code{ info->ExceptionRecord->ExceptionCode };
		const auto name{ CodeName( code ) };
		if ( !name )
			return EXCEPTION_CONTINUE_SEARCH;

		// C++ throws are only interesting when they come from our code ( e.g. vector::at out of range )
		if ( code == 0xE06D7363 ) {
			bool ours{ };
#ifdef _M_IX86
			auto stack{ reinterpret_cast< const uintptr_t* >( info->ContextRecord->Esp ) };
			const auto stackEnd{ reinterpret_cast< const uintptr_t* >( reinterpret_cast< NT_TIB* >( NtCurrentTeb( ) )->StackBase ) };
			for ( int i{ }; i < 256 && !ours && stack + i < stackEnd; ++i )
				ours = InOurModule( stack[ i ] );
#endif
			if ( !ours )
				return EXCEPTION_CONTINUE_SEARCH;
		}

		if ( g_Reports.load( ) >= MAX_CRASH_REPORTS || g_InHandler.exchange( true ) )
			return EXCEPTION_CONTINUE_SEARCH;

		WriteReport( info, name );
		++g_Reports;
		g_InHandler = false;

		// never swallow it: the game's own handlers / crash reporter still run
		return EXCEPTION_CONTINUE_SEARCH;
	}

	// module: the cheat's base address ( hModule from DllMain, works for manual maps too )
	inline void Init( void* module ) {
		if ( g_Ready )
			return;

		InitializeCriticalSection( &g_FileLock );

		g_ModuleBase = reinterpret_cast< uintptr_t >( module );
		g_ModuleSize = 0x4000000; // fallback when the PE headers were wiped by the loader

		__try {
			const auto dos{ reinterpret_cast< const IMAGE_DOS_HEADER* >( g_ModuleBase ) };
			if ( dos && dos->e_magic == IMAGE_DOS_SIGNATURE ) {
				const auto nt{ reinterpret_cast< const IMAGE_NT_HEADERS* >( g_ModuleBase + dos->e_lfanew ) };
				if ( nt->Signature == IMAGE_NT_SIGNATURE )
					g_ModuleSize = nt->OptionalHeader.SizeOfImage;
			}
		}
		__except ( EXCEPTION_EXECUTE_HANDLER ) {
		}

		char desktop[ MAX_PATH ]{ };
		if ( FAILED( SHGetFolderPathA( nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, SHGFP_TYPE_CURRENT, desktop ) ) )
			GetTempPathA( MAX_PATH, desktop );

		_snprintf_s( g_LogDir, sizeof( g_LogDir ), _TRUNCATE, "%s\\CorgiHak_logs", desktop );
		CreateDirectoryA( g_LogDir, nullptr );

		const auto now{ std::time( nullptr ) };
		std::tm local{ };
		localtime_s( &local, &now );
		std::strftime( g_Stamp, sizeof( g_Stamp ), "%Y%m%d_%H%M%S", &local );

		_snprintf_s( g_SessionPath, sizeof( g_SessionPath ), _TRUNCATE, "%s\\session_%s.log", g_LogDir, g_Stamp );

		g_Ready = true;
		g_Handler = AddVectoredExceptionHandler( 1, VectoredHandler );

		Info( "CorgiHak loaded. base 0x%08X size 0x%X", static_cast< unsigned >( g_ModuleBase ), static_cast< unsigned >( g_ModuleSize ) );
	}

	inline void Shutdown( ) {
		if ( g_Handler ) {
			RemoveVectoredExceptionHandler( g_Handler );
			g_Handler = nullptr;
		}
		Info( "unloaded cleanly" );
	}
}

#define CRASH_MARK( tag ) ::CrashLog::Mark( tag )
