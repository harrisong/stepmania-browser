// GameLoop_Emscripten.cpp - Emscripten-specific game loop wrapper
// Browsers require a non-blocking main loop using requestAnimationFrame

#include "global.h"

#ifdef EMSCRIPTEN

#include "GameLoop.h"
#include "RageLog.h"
#include "RageDisplay.h"
#include "ScreenManager.h"
#include "InputFilter.h"
#include "InputMapper.h"
#include "InputMan.h"
#include "GameState.h"
#include "arch/ArchHooks/ArchHooks.h"
#include <emscripten.h>
#include <emscripten/html5.h>

// Emscripten main loop callback
static void EmscriptenGameLoopIteration(void* arg)
{
	// Check if user wants to quit
	if (ArchHooks::UserQuit())
	{
		LOG->Info("User quit detected, cancelling main loop");
		emscripten_cancel_main_loop();

		// Final cleanup
		GAMESTATE->SaveLocalData();
		return;
	}

	// Handle theme/game changes
	extern RString g_NewTheme;
	extern RString g_NewGame;

	// Note: These functions are in GameLoop.cpp namespace block
	// We'll need to expose them or duplicate the logic
	if (!g_NewGame.empty())
	{
		// DoChangeGame(); // This is in anonymous namespace, need to handle differently
		LOG->Warn("Game change requested but not implemented in Emscripten build yet");
		g_NewGame = "";
	}

	if (!g_NewTheme.empty())
	{
		// DoChangeTheme(); // This is in anonymous namespace, need to handle differently
		LOG->Warn("Theme change requested but not implemented in Emscripten build yet");
		g_NewTheme = "";
	}

	// Update game state
	GameLoop::UpdateAllButDraw(false);

	// Check for input device changes
	if (INPUTMAN->DevicesChanged())
	{
		INPUTFILTER->Reset();
		INPUTMAN->LoadDrivers();
		RString sMessage;
		if (INPUTMAPPER->CheckForChangedInputDevicesAndRemap(sMessage))
			SCREENMAN->SystemMessage(sMessage);
	}

	// Render frame
	SCREENMAN->Draw();
}

// Emscripten-specific game loop entry point
void GameLoop::RunGameLoop_Emscripten()
{
	LOG->Info("Starting Emscripten game loop");

	// Set up the main loop to run at browser's refresh rate (typically 60fps)
	// 0 = use requestAnimationFrame (optimal)
	// 1 = simulate infinite loop (gives control back to browser between iterations)
	emscripten_set_main_loop_arg(EmscriptenGameLoopIteration, nullptr, 0, 1);

	// Note: Code after emscripten_set_main_loop_arg() may not execute
	// The function essentially "never returns" in traditional sense
	LOG->Info("Main loop registered with Emscripten");
}

// Callback for handling browser visibility changes
static EM_BOOL emscripten_visibility_callback(int eventType, const EmscriptenVisibilityChangeEvent *e, void *userData)
{
	if (e->hidden)
	{
		LOG->Info("Browser tab hidden - pausing audio");
		// Could pause audio here if desired
	}
	else
	{
		LOG->Info("Browser tab visible - resuming");
	}

	return EM_TRUE;
}

// Callback for handling fullscreen changes
static EM_BOOL emscripten_fullscreen_callback(int eventType, const EmscriptenFullscreenChangeEvent *e, void *userData)
{
	LOG->Info("Fullscreen change: %s", e->isFullscreen ? "entered" : "exited");
	return EM_TRUE;
}

// Initialize Emscripten-specific event handlers
void GameLoop::InitEmscriptenCallbacks()
{
	LOG->Info("Initializing Emscripten callbacks");

	// Register visibility change handler
	emscripten_set_visibilitychange_callback(nullptr, nullptr, EM_TRUE, emscripten_visibility_callback);

	// Register fullscreen change handler
	emscripten_set_fullscreenchange_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, emscripten_fullscreen_callback);

	LOG->Info("Emscripten callbacks registered");
}

#endif // EMSCRIPTEN

/*
 * Copyright (c) 2026 StepMania Browser Port Contributors
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to permit
 * persons to whom the Software is furnished to do so, subject to the
 * following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
 * NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
