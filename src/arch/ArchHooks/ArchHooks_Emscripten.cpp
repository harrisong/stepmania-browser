#include "global.h"
#include "ArchHooks_Emscripten.h"
#include "RageLog.h"
#include "RageThreads.h"
#include "RageUtil.h"
#include "RageFileManager.h"
#include "RageFileDriverDirect.h"
#include "archutils/Unix/GetSysInfo.h"

#include <emscripten.h>
#include <emscripten/html5.h>

void ArchHooks_Emscripten::Init()
{
	// Don't log here - RageLog's mutex infrastructure may not be fully initialized yet
	// LOG->Info("ArchHooks_Emscripten: Initializing WebAssembly/Browser hooks");
}

RString ArchHooks_Emscripten::GetArchName() const
{
	return "WebAssembly";
}

void ArchHooks_Emscripten::DumpDebugInfo()
{
	LOG->Info("=== Browser/WebAssembly Environment ===");
	LOG->Info("Running in browser via Emscripten");
	LOG->Info("User Agent: %s", emscripten_run_script_string("navigator.userAgent"));

	// Get screen resolution
	int width = 0, height = 0;
	emscripten_get_screen_size(&width, &height);
	LOG->Info("Screen Size: %dx%d", width, height);
}

void ArchHooks_Emscripten::SetTime( tm /* newtime */ )
{
	// Cannot set system time in browser
	LOG->Warn("SetTime: Cannot set system time in browser environment");
}

void ArchHooks::MountInitialFilesystems( const RString &sDirOfExecutable )
{
	// In Emscripten, the filesystem is virtual (MEMFS or IDBFS)
	// Mount the root directory where preloaded assets are
	LOG->Info("MountInitialFilesystems: Using Emscripten virtual filesystem");

#ifdef __EMSCRIPTEN__
	#include <sys/stat.h>
	#include <errno.h>
	#include <unistd.h>

	// Check current working directory
	char cwd[1024];
	if (getcwd(cwd, sizeof(cwd)) != NULL) {
		LOG->Info("Current working directory: %s", cwd);
	}

	// Create directories that StepMania expects using proper C API
	mkdir("/Logs", 0777);
	mkdir("/Save", 0777);

	// Change to root directory where assets are mounted
	if (chdir("/") == 0) {
		LOG->Info("Changed working directory to /");
	} else {
		LOG->Warn("Failed to change directory to /");
	}

	// Mount the root directory so RageFileManager can access preloaded assets
	LOG->Info("Mounting root directory '/' for file access");
	FILEMAN->Mount( new RageFileDriverDirect("/"), "/", false );
	LOG->Info("Filesystem mounted successfully");

	// Debug: List filesystem contents via emscripten
	EM_ASM({
		console.log('[FS Debug] Root directory contents:', FS.readdir('/'));
		console.log('[FS Debug] Current working directory:', FS.cwd());
	});
#endif

	// The working directory is now /
	// Emscripten preloaded files are at /NoteSkins, /Data, /Themes
}

void ArchHooks::MountUserFilesystems( const RString &sDirOfExecutable )
{
	// User writable directory in browser (IndexedDB backed)
	LOG->Info("MountUserFilesystems: User data will use IndexedDB storage");

	// Mount user directories - these will use IDBFS for persistence
	// For initial build, just create the directories
}

float ArchHooks_Emscripten::GetDisplayAspectRatio()
{
	// Get the canvas/window aspect ratio
	double width = 0, height = 0;
	emscripten_get_element_css_size("#canvas", &width, &height);

	if (height > 0)
		return (float)(width / height);

	return 16.0f / 9.0f; // Default widescreen
}

bool ArchHooks_Emscripten::GoToURL( RString sUrl )
{
	// Open URL in new browser tab
	RString sCommand = ssprintf("window.open('%s', '_blank');", sUrl.c_str());
	emscripten_run_script(sCommand.c_str());
	return true;
}

RString ArchHooks_Emscripten::GetClipboard()
{
	// Browser clipboard access requires async API and user permission
	// For now, return empty string
	LOG->Warn("GetClipboard: Browser clipboard access not yet implemented");
	return "";
}

// Implement required static functions from ArchHooks base class
int64_t ArchHooks::GetMicrosecondsSinceStart( bool /* bAccurate */ )
{
	// Use emscripten_get_now() which returns milliseconds since page load
	double ms = emscripten_get_now();
	return (int64_t)(ms * 1000.0); // Convert to microseconds
}

RString ArchHooks::GetPreferredLanguage()
{
	// Get browser language
	const char *lang = emscripten_run_script_string("navigator.language.substring(0, 2).toLowerCase()");
	return RString(lang);
}

/*
 * Copyright (c) 2025 Browser port contributors
 * Based on ArchHooks_Unix (c) 2003-2004 Glenn Maynard
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
