//things
#include "main_mod.h"
#include "stdafx.h"
#include "pch.h"
#include <iostream>

//Microsoft Detours
#include <detours.h>

//Discord RPC stuff
#include "discord-rpc/discord.h"
#pragma comment(lib, "discord_game_sdk.dll.lib")

//TagLib
#pragma comment(lib, "taglib.lib")
#include <taglib.h>
#include <fileref.h>

#pragma region Detours things
static LRESULT(WINAPI* TrueSendMessage)(
	HWND hWnd,
	UINT   Msg,
	WPARAM wParam,
	LPARAM lParam) = SendMessageA;

static TagLib::Tag* (__thiscall* TrueFileRefGetTag)(TagLib::FileRef* self) = nullptr;
#pragma endregion

TagLib::Tag* tagPtr;
std::string artistTag = std::string("Unknown Artist");
std::string titleTag = std::string("Unknown Title");
std::string fullTags = std::string("Unknown Artist - Unknown Title");

std::string score;
std::string fullScore;
std::string songFinishText;

discord::Core* core{};
discord::User currentUser;

void UpdatePresence(const char* state, const char* details) {
	discord::Activity activity{};
	activity.SetState(state);
	activity.SetDetails(details);
	activity.GetAssets().SetLargeImage("audiosurficon4x");
	activity.GetAssets().SetLargeText("Audiosurf");
	core->ActivityManager().UpdateActivity(activity, [](discord::Result result) {

	});
}

LRESULT WINAPI MessageInterceptor(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	COPYDATASTRUCT* cds = (COPYDATASTRUCT*)lParam;
	std::cout << "Outgoing message caught: " << (char*)cds->lpData << "\n";

	if (strstr((char*)cds->lpData, (char*)"oncharacterscreen")) {
		std::cout << "Entered character select screen" << "\n";
		UpdatePresence("Selecting character/song", "");
	}

	if (strstr((char*)cds->lpData, (char*)"nowplayingartistname")) {
		std::cout << "Playing song: " << artistTag << " - " << titleTag << "\n";
		UpdatePresence(fullTags.c_str(), "Riding song");
	}

	if (strstr((char*)cds->lpData, (char*)"songcomplete")) {
		score = (char*)cds->lpData;
		//Make only the score number remain in the stuff the game sent back
		score.erase(0, 22);
		fullScore = score + " points";
		songFinishText = "Song finished: " + fullTags;
		std::cout << "Song complete: " << artistTag << " - " << titleTag << " with score " << score << "\n";
		UpdatePresence(fullScore.c_str(), songFinishText.c_str());
	}

	return TrueSendMessage(hWnd, Msg, wParam, lParam);
}

static TagLib::Tag* __fastcall FileRefGetTagInterceptor(TagLib::FileRef* self, DWORD edx) {
	tagPtr = TrueFileRefGetTag(self);
	artistTag = tagPtr->artist().toCString(true);
	titleTag = tagPtr->title().toCString(true);
	if (artistTag.empty()) {
		artistTag = "Unknown Artist";
	}
	if (titleTag.empty()) {
		titleTag = "Unknown Title";
	}
	fullTags = artistTag + std::string(" - ") + titleTag;

	std::cout << "TagLib::FileRef::tag() caught! " << fullTags.c_str() << "\n";
	return tagPtr;
}

DWORD WINAPI ModThread(HMODULE hModule)
{
	//LONG error;
	
	//Create Console
	AllocConsole();
	FILE* f;
	freopen_s(&f, "CONOUT$", "w", stdout);

	std::cout << "DLL attached!\n";

	//init discord stuff
	auto result = discord::Core::Create(698844368953671750, DiscordCreateFlags_Default, &core);
	core->ActivityManager().RegisterSteam(12900);
	core->UserManager().OnCurrentUserUpdate.Connect([]() {
		core->UserManager().GetCurrentUser(&currentUser);
		std::cout << currentUser.GetUsername() << "#" << currentUser.GetDiscriminator() << " has logged in!" << "\n";
	});
	UpdatePresence("Loading", "");

	DetourRestoreAfterWith();

	TrueFileRefGetTag =
		(TagLib::Tag*(__thiscall*)(TagLib::FileRef*))
		DetourFindFunction("taglib.dll", "?tag@FileRef@TagLib@@QBEPAVTag@2@XZ");

	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());
	DetourAttach(&(PVOID&)TrueSendMessage, MessageInterceptor);
	DetourAttach(&(PVOID&)TrueFileRefGetTag, FileRefGetTagInterceptor);
	DetourTransactionCommit();

	if (TrueFileRefGetTag == NULL) {
		std::cout << "THIS IS HORRIBLE! COULDN'T HOOK TagLib::FileRef::tag()!\n";
	}
	
	//get Audiosurf's window handle
	HWND hwndTargetWin = FindWindow(NULL, L"Audiosurf");

	//create the command message and data struct
	char* str = (char*)"ascommand quickstartregisterwindow Audiosurf";
	COPYDATASTRUCT cds;
	cds.cbData = strlen(str) + 1;
	cds.lpData = (void*)str;

	SendMessage(hwndTargetWin, WM_COPYDATA, 0, (LPARAM)&cds);

	while (true) {
		//Run Discord callbacks
		core->RunCallbacks();
		//i don't remember why i'm using exactly 16ms for this but who even cares
		Sleep(16);
	}
	std::cout << "Shutting down! \n";

	fclose(f);
	FreeConsole();

	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());
	//Detach the hooks when the DLL is ejected
	DetourDetach(&(PVOID&)TrueSendMessage, MessageInterceptor);
	DetourDetach(&(PVOID&)TrueFileRefGetTag, FileRefGetTagInterceptor);
	DetourTransactionCommit();

	FreeLibraryAndExitThread(hModule, 0);
	return 0;
}