#ifdef _WIN64
#pragma comment(lib, "lua53-64.lib")
#else
#pragma comment(lib, "lua53-32.lib")
#endif

#include <windows.h>
#include "cepluginsdk.h"
#include "lua.hpp"
#include "MapFile.h"

int SelfID;
ExportedFunctions Exported;
int ImportSymbolsPluginID = -1;
int RefToAddSymbol = -1;

void AddSymbol(const char* module, const char* name, const unsigned address, const unsigned length)
{
	if (RefToAddSymbol == -1)
	{
		return;
	}

	int stringLength = static_cast<int>(strnlen_s(name, 100));
	if (stringLength <= 0)
	{
		return;
	}

	lua_State *L = static_cast<lua_State*>(Exported.GetLuaState());
	if (L == nullptr)
	{
		return;
	}

	char buffer[103]; // 3 extra chars for duplicates #0 to #99
	strncpy_s(buffer, 100, name, _TRUNCATE);
	char* adjustedName = buffer;
	while (adjustedName[0] == '_')
	{
		adjustedName++;
		stringLength -= 1;

		if (stringLength <= 0)
		{
			return;
		}
	}

	for (int dupe = 0; dupe <= 99; dupe++)
	{
		int i = lua_gettop(L);
		int success = 0;

		if (dupe > 0)
		{
			adjustedName[stringLength] = '#';
			_itoa(dupe, &(adjustedName[stringLength +1]), 10);
		}

		lua_rawgeti(L, LUA_REGISTRYINDEX, RefToAddSymbol);
		lua_pushstring(L, module);
		lua_pushstring(L, const_cast<const char*>(adjustedName));
		lua_pushinteger(L, address);
		lua_pushinteger(L, length);
		lua_pcall(L, 4, 1, 0);
		success = lua_toboolean(L, -1);
		lua_pop(L, 1);

		lua_settop(L, i);

		if (success || strcmp(adjustedName, "`string'") == 0)
		{
			break;
		}
	}
}

BOOL __stdcall ImportMapfileSymbols(ULONG *disassembleraddress, ULONG *selected_disassembler_address, ULONG *hexviewaddress)
{
	OPENFILENAME ofn;
	char directoryName[MAX_PATH];
	char fileName[MAX_PATH];
	HWND hWnd = GetActiveWindow(); // static_cast<HWND>(Exported.GetMainWindowHandle());
	if (GetCurrentDirectory(sizeof(directoryName), directoryName) == FALSE)
	{
		directoryName[0] = '\0';
	}

	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hWnd;
	ofn.lpstrFile = fileName;
	ofn.lpstrFile[0] = '\0';
	ofn.nMaxFile = sizeof(fileName);
	ofn.lpstrFilter = "Linker Address Map\0*.map\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = directoryName;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

	if (GetOpenFileName(&ofn) == TRUE)
	{
		if (RefToAddSymbol == -1)
		{
			lua_State *L = static_cast<lua_State*>(Exported.GetLuaState());
			int i = lua_gettop(L);

			char *addSymbol =
				"function addSymbol(module, name, address, size)\n"
				"if getAddress(name) ~= 0 then\n"
				"return false\n"
				"end\n"
				"registerSymbol(name, address)\n"
				"return true\n"
				"end\n"
				"errorOnLookupFailure(false)\n";
			luaL_dostring(L, addSymbol);

			lua_getglobal(L, "addSymbol");
			RefToAddSymbol = luaL_ref(L, LUA_REGISTRYINDEX);

			lua_settop(L, i);
		}

		MapFile* mapFile = new MapFile(0x1000);
		if (mapFile)
		{
			unsigned numEntries = mapFile->read(ofn.lpstrFile);
			// CE abandons the plugin call if lua gets an error such as a duplicate entry.
			for (unsigned i = 0; i < numEntries; i++)
			{
				const MapFile::_map_entry_t* entry = mapFile->getEntryByIndex(i);
				if (entry == NULL)
				{
					continue;
				}

				AddSymbol(entry->module, entry->name, entry->address, 1);
			}
			delete mapFile;
		}
	}

	return TRUE;
}

BOOL WINAPI DllMain(__in  HINSTANCE hinstDLL, __in DWORD fdwReason, __in  LPVOID lpvReserved)
{
	switch (fdwReason)
	{
	case DLL_PROCESS_ATTACH:
		break;
	case DLL_PROCESS_DETACH:
		break;
	case DLL_THREAD_ATTACH:
		break;
	case DLL_THREAD_DETACH:
		break;
	}

	return TRUE;
}

#ifdef _WIN64
#pragma comment(linker, "/EXPORT:CEPlugin_GetVersion=?CEPlugin_GetVersion@@YAHPEAU_PluginVersion@@H@Z")
#else
#pragma comment(linker, "/EXPORT:CEPlugin_GetVersion=?CEPlugin_GetVersion@@YGHPAU_PluginVersion@@H@Z")
#endif
__declspec(dllexport) BOOL __stdcall CEPlugin_GetVersion(PPluginVersion pv, int sizeofpluginversion)
//BOOL __stdcall CEPlugin_GetVersion(PPluginVersion pv , int sizeofpluginversion)
{
	pv->version = CESDK_VERSION;
	pv->pluginname = "Import Mapfile Symbols (SDK version 4: 6.0+)";

	return TRUE;
}

#ifdef _WIN64
#pragma comment(linker, "/EXPORT:CEPlugin_InitializePlugin=?CEPlugin_InitializePlugin@@YAHPEAU_ExportedFunctions@@H@Z")
#else
#pragma comment(linker, "/EXPORT:CEPlugin_InitializePlugin=?CEPlugin_InitializePlugin@@YGHPAU_ExportedFunctions@@H@Z")
#endif
__declspec(dllexport) BOOL __stdcall CEPlugin_InitializePlugin(PExportedFunctions ef, int pluginid)
//BOOL __stdcall CEPlugin_InitializePlugin(PExportedFunctions ef , int pluginid)
{
	SelfID = pluginid;

	Exported = *ef;
	if (Exported.sizeofExportedFunctions != sizeof(Exported))
	{
		return FALSE;
	}

	MEMORYVIEWPLUGIN_INIT importMapfileSymbolsInit;
	importMapfileSymbolsInit.name = "Import Symbols From Mapfile";
	importMapfileSymbolsInit.callbackroutine = ImportMapfileSymbols;
	importMapfileSymbolsInit.shortcut = "";

	ImportSymbolsPluginID = Exported.RegisterFunction(SelfID, ptMemoryView, &importMapfileSymbolsInit);
	if (ImportSymbolsPluginID == -1)
	{
		Exported.ShowMessage("Failure to register the Import Mapfile Symbols plugin");
		return FALSE;
	}

	return TRUE;
}

#ifdef _WIN64
#pragma comment(linker, "/EXPORT:CEPlugin_DisablePlugin=?CEPlugin_DisablePlugin@@YAHXZ")
#else
#pragma comment(linker, "/EXPORT:CEPlugin_DisablePlugin=?CEPlugin_DisablePlugin@@YGHXZ")
#endif
__declspec(dllexport) BOOL __stdcall CEPlugin_DisablePlugin()
//BOOL __stdcall CEPlugin_DisablePlugin(void)
{
	Exported.UnregisterFunction(SelfID, ImportSymbolsPluginID);
	return TRUE;
}