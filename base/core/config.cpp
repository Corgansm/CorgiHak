// used: shgetknownfolderpath
#include <shlobj.h>
#include "../dependencies/json/json.hpp"
#include "config.h"
#include "../core/input_manager.h"
#include "../features/rage/antiaim.h"
#include "lua/lua_engine.h"

#include <fstream>

namespace {
	// remembered so the next injection starts with the same settings
	void RememberLast( const std::filesystem::path& file )
	{
		std::ofstream out( Config::fsPath / _( "last.txt" ), std::ios::out | std::ios::trunc );
		if ( out.good( ) )
			out << file.filename( ).string( );
	}
}

std::string Config::LoadLast( )
{
	std::ifstream in( fsPath / _( "last.txt" ), std::ios::in );
	if ( !in.good( ) )
		return { };

	std::string name{ };
	std::getline( in, name );
	while ( !name.empty( ) && ( name.back( ) == '\r' || name.back( ) == ' ' ) )
		name.pop_back( );

	if ( name.empty( ) || !std::filesystem::exists( fsPath / name ) )
		return { };

	return Load( name ) ? name : std::string( );
}

bool Config::Setup( )
{
	// create directory "settings" in "%userprofile%\documents\.cfg" if it incorrect or doesnt exists
	if (!std::filesystem::is_directory(fsPath))
	{
		std::filesystem::remove(fsPath);
		if (!std::filesystem::create_directories(fsPath))
			return false;
	}

	return true;
}

bool Config::Save(std::string_view szFileName)
{
	// check for extension if it is not our replace it
	std::filesystem::path fsFilePath(szFileName);
	if (fsFilePath.extension() != _(".cfg"))
		fsFilePath.replace_extension(_(".cfg"));

	// get utf-8 full path to config
	const auto szFile = std::filesystem::path(fsPath / fsFilePath);
	nlohmann::json config = { };

	try
	{
		for (auto& variable : vecVariables)
		{
			nlohmann::json entry = { };

			// save hashes to compare it later
			entry[_("name-id")] = variable.uNameHash;
			entry[_("type-id")] = variable.uTypeHash;

			// get current variable
			switch (variable.uTypeHash)
			{
			case FNV1A::HashConst("int"):
			{
				entry[_("value")] = variable.Get<int>();
				break;
			}
			case FNV1A::HashConst("float"):
			{
				entry[_("value")] = variable.Get<float>();
				break;
			}
			case FNV1A::HashConst("bool"):
			{
				entry[_("value")] = variable.Get<bool>();
				break;
			}
			case FNV1A::HashConst("std::string"):
			{
				entry[_("value")] = variable.Get<std::string>();
				break;
			}
			case FNV1A::HashConst("Color"):
			{
				const auto& colVariable = variable.Get<Color>();

				// store RGBA as sub-node
				nlohmann::json sub = { };

				// fill node with all color values
				sub.push_back(colVariable.Get<COLOR_R>());
				sub.push_back(colVariable.Get<COLOR_G>());
				sub.push_back(colVariable.Get<COLOR_B>());
				sub.push_back(colVariable.Get<COLOR_A>());

				entry[_("value")] = sub.dump();
				break;
			}
			case FNV1A::HashConst( "keybind_t" ):
			{
				const auto& keyVariable = variable.Get<keybind_t>( );

				// store RGBA as sub-node
				nlohmann::json sub = { };

				// fill node with all color values
				sub.push_back( keyVariable.key );
				sub.push_back( keyVariable.mode );

				entry[ _( "value" ) ] = sub.dump( );
				break;
			}
			case FNV1A::HashConst("std::vector<bool>"):
			{
				const auto& vecBools = variable.Get<std::vector<bool>>();

				// store vector values as sub-node
				nlohmann::json sub = { };

				// fill node with all vector values
				for (const auto&& bValue : vecBools)
					sub.push_back(static_cast<bool>(bValue));

				entry[_("value")] = sub.dump();
				break;
			}
			case FNV1A::HashConst("std::vector<int>"):
			{
				const auto& vecInts = variable.Get<std::vector<int>>();

				// store vector values as sub-node
				nlohmann::json sub = { };

				// fill node with all vector values
				for (const auto& iValue : vecInts)
					sub.push_back(iValue);

				entry[_("value")] = sub.dump();
				break;
			}
			case FNV1A::HashConst("std::vector<float>"):
			{
				const auto& vecFloats = variable.Get<std::vector<float>>();

				// store vector values as sub-node
				nlohmann::json sub = { };

				// fill node with all vector values
				for (const auto& flValue : vecFloats)
					sub.push_back(flValue);

				entry[_("value")] = sub.dump();
				break;
			}
			default:
				break;
			}

			// add current variable to config
			config.push_back(entry);
		}

		// lua script item values ( unknown name hash: older builds skip the entry )
		nlohmann::json lua = { };
		lua[_("name-id")] = FNV1A::HashConst("__corgi_lua");
		lua[_("type-id")] = 0;
		lua[_("corgi-lua")] = nlohmann::json::parse(Lua::ExportConfig(), nullptr, false);
		if (!lua[_("corgi-lua")].is_discarded())
			config.push_back(lua);
	}
	catch (const nlohmann::detail::exception& ex)
	{
		return false;
	}

	// open output config file
	std::ofstream ofsOutFile( szFile, std::ios::out | std::ios::trunc );

	if (!ofsOutFile.good())
		return false;

	try
	{
		// write stored variables
		ofsOutFile << config.dump(4);
		ofsOutFile.close();
	}
	catch (std::ofstream::failure& ex)
	{
		return false;
	}

	strActive = szFile.filename().string();
	RememberLast(szFile);

	return true;
}

bool Config::Load(std::string_view szFileName)
{
	// get utf-8 full path to config
	const auto szFile = std::filesystem::path( fsPath / szFileName );
	nlohmann::json config = { };

	// open input config file
	std::ifstream ifsInputFile(szFile, std::ios::in);

	if (!ifsInputFile.good())
		return false;

	try
	{
		// parse saved variables
		config = nlohmann::json::parse(ifsInputFile, nullptr, false);

		// check is json parse failed
		if (config.is_discarded())
			return false;

		ifsInputFile.close();
	}
	catch (std::ifstream::failure& ex)
	{
		return false;
	}

	bool hadLua{ };

	try
	{
		for (const auto& variable : config)
		{
			// lua script item values
			if (variable.is_object() && variable.find(_("corgi-lua")) != variable.end())
			{
				hadLua = true;
				Lua::ImportConfig(variable[_("corgi-lua")].dump());
				continue;
			}

			const std::size_t nIndex = GetVariableIndex(variable[_("name-id")].get<FNV1A_t>());

			// check is variable exist
			if (nIndex == C_INVALID_VARIABLE)
				continue;

			// get variable
			auto& entry = vecVariables.at(nIndex);

			// a variable whose type changed between builds: the saved value would replace the variable with the
			// wrong type, and the next Get<> returns null ( crash ). keep the default instead
			const auto typeId = variable[_("type-id")].get<FNV1A_t>();
			if (typeId != entry.uTypeHash)
				continue;

			// one malformed entry no longer stops the rest of the config from loading
			try
			{
			switch (typeId)
			{
			case FNV1A::HashConst("bool"):
			{
				entry.Set<bool>(variable[_("value")].get<bool>());
				break;
			}
			case FNV1A::HashConst("float"):
			{
				entry.Set<float>(variable[_("value")].get<float>());
				break;
			}
			case FNV1A::HashConst("int"):
			{
				entry.Set<int>(variable[_("value")].get<int>());
				break;
			}
			case FNV1A::HashConst("std::string"):
			{
				entry.Set<std::string>(variable[_("value")].get<std::string>());
				break;
			}
			case FNV1A::HashConst("Color"):
			{
				const nlohmann::json vector = nlohmann::json::parse(variable[_("value")].get<std::string>());

				entry.Set<Color>(Color(
					vector.at(0).get<std::uint8_t>(),
					vector.at(1).get<std::uint8_t>(),
					vector.at(2).get<std::uint8_t>(),
					vector.at(3).get<std::uint8_t>()
				));

				break;
			}
			case FNV1A::HashConst( "keybind_t" ):
			{
				const nlohmann::json vector = nlohmann::json::parse( variable[ _( "value" ) ].get<std::string>( ) );

				entry.Set<keybind_t>( keybind_t(
					vector.at( 0 ).get<std::int32_t>( ),
					0,
					0,
					vector.at( 1 ).get<int>( )
				) );

				break;
			}
			case FNV1A::HashConst("std::vector<bool>"):
			{
				const nlohmann::json vector = nlohmann::json::parse(variable[_("value")].get<std::string>());
				auto& vecBools = entry.Get<std::vector<bool>>();

				for (std::size_t i = 0U; i < vector.size(); i++)
				{
					// check is item out of bounds
					if (i < vecBools.size())
						vecBools.at(i) = vector.at(i).get<bool>();
				}

				break;
			}
			case FNV1A::HashConst("std::vector<int>"):
			{
				const nlohmann::json vector = nlohmann::json::parse(variable[_("value")].get<std::string>());
				auto& vecInts = entry.Get<std::vector<int>>();

				for (std::size_t i = 0U; i < vector.size(); i++)
				{
					// check is item out of bounds
					if (i < vecInts.size())
						vecInts.at(i) = vector.at(i).get<int>();
				}

				break;
			}
			case FNV1A::HashConst("std::vector<float>"):
			{
				const nlohmann::json vector = nlohmann::json::parse(variable[_("value")].get<std::string>());
				auto& vecFloats = entry.Get<std::vector<float>>();

				for (std::size_t i = 0U; i < vector.size(); i++)
				{
					// check is item out of bounds
					if (i < vecFloats.size())
						vecFloats.at(i) = vector.at(i).get<float>();
				}

				break;
			}
			default:
				break;
			}
			}
			catch (const nlohmann::detail::exception&)
			{
				continue;
			}
		}
	}
	catch (const nlohmann::detail::exception& ex)
	{
		return false;
	}

	// a config saved without scripts carries no script values: forget the previous config's ones
	if (!hadLua)
		Lua::ImportConfig("{}");

	Features::Antiaim.ManualSide = 0;
	Config::Get<bool>( Vars.DBGLC1 ) = false;

	strActive = szFile.filename().string();
	RememberLast(szFile);

	return true;
}

void Config::Remove(const std::size_t nIndex)
{
	const std::string& szFileName = vecFileNames.at(nIndex);

	// get utf-8 full path to config
	const auto szFile = std::filesystem::path(fsPath / szFileName);

	std::filesystem::remove( szFile );
}

void Config::Refresh()
{
	vecFileNames.clear();

	for (const auto& it : std::filesystem::directory_iterator(fsPath))
    {
		if (it.path().filename().extension() == _(".cfg"))
		{
			vecFileNames.push_back(it.path().filename().string());
		}
    }
}

std::size_t Config::GetVariableIndex(const FNV1A_t uNameHash)
{
	for (std::size_t i = 0U; i < vecVariables.size(); i++)
	{
		if (vecVariables.at(i).uNameHash == uNameHash)
			return i;
	}

	return C_INVALID_VARIABLE;
}

std::filesystem::path Config::GetWorkingPath()
{
	std::filesystem::path fsWorkingPath;

	// get path to user documents
	if (PWSTR pszPathToDocuments; SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0UL, nullptr, &pszPathToDocuments)))
	{
		fsWorkingPath.assign(pszPathToDocuments);
		fsWorkingPath.append(_("Havoc"));
		CoTaskMemFree(pszPathToDocuments);
	}
	
	return fsWorkingPath;
}
