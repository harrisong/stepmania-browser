#include "global.h"
#include "LowLevelWindow_SDL.h"
#include "RageLog.h"
#include "RageDisplay.h"
#include "DisplaySpec.h"

#include <SDL2/SDL.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

static SDL_Window *g_pWindow = nullptr;
static SDL_GLContext g_GLContext = nullptr;

LowLevelWindow_SDL::LowLevelWindow_SDL()
{
	LOG->Info("LowLevelWindow_SDL: Initializing SDL window");

#ifndef __EMSCRIPTEN__
	// Initialize SDL video subsystem if not already done
	// Note: For Emscripten, SDL is auto-initialized by the runtime
	if (!SDL_WasInit(SDL_INIT_VIDEO))
	{
		LOG->Info("LowLevelWindow_SDL: Initializing SDL VIDEO subsystem");
		if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0)
		{
			LOG->Warn("SDL_InitSubSystem(SDL_INIT_VIDEO) failed: %s", SDL_GetError());
		}
		else
		{
			LOG->Info("SDL VIDEO subsystem initialized successfully");
		}
	}
	else
	{
		LOG->Info("SDL VIDEO subsystem already initialized");
	}
#else
	LOG->Info("LowLevelWindow_SDL: Emscripten build - SDL auto-initialized by runtime");
#endif
}

LowLevelWindow_SDL::~LowLevelWindow_SDL()
{
	if (g_GLContext)
	{
		SDL_GL_DeleteContext(g_GLContext);
		g_GLContext = nullptr;
	}
	if (g_pWindow)
	{
		SDL_DestroyWindow(g_pWindow);
		g_pWindow = nullptr;
	}
}

void *LowLevelWindow_SDL::GetProcAddress( RString s )
{
	return SDL_GL_GetProcAddress(s.c_str());
}

RString LowLevelWindow_SDL::TryVideoMode( const VideoModeParams &p, bool &bNewDeviceOut )
{
	// Set OpenGL attributes for GLES2/WebGL
	#ifndef __EMSCRIPTEN__
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	#else
	// For Emscripten/WebGL, don't set ANY GL attributes
	// Let Emscripten/SDL handle everything automatically
	LOG->Info("LowLevelWindow_SDL: Emscripten build - using default WebGL context settings");
	#endif

	// Note: Removed EM_ASM block that was interfering with SDL context creation

	if (!g_pWindow)
	{
		Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;
		if (p.windowed)
			flags |= SDL_WINDOW_RESIZABLE;

		// For Emscripten, force canvas size from HTML (1280x720)
		// Otherwise SDL uses its own size calculation
		int windowWidth = 1280;
		int windowHeight = 720;

		LOG->Info("LowLevelWindow_SDL: Creating window at %dx%d (requested: %dx%d)",
			windowWidth, windowHeight, p.width, p.height);

		#ifdef __EMSCRIPTEN__
		EM_ASM({
			console.log('[SDL WINDOW] Creating at ' + $0 + 'x' + $1 + ' (requested: ' + $2 + 'x' + $3 + ')');
		}, windowWidth, windowHeight, p.width, p.height);
		#endif

		LOG->Info("LowLevelWindow_SDL: About to call SDL_CreateWindow...");
		g_pWindow = SDL_CreateWindow(
			"StepMania",
			SDL_WINDOWPOS_UNDEFINED,
			SDL_WINDOWPOS_UNDEFINED,
			windowWidth, windowHeight,
			flags
		);

		if (!g_pWindow)
		{
			LOG->Warn("SDL_CreateWindow FAILED: %s", SDL_GetError());
			return ssprintf("SDL_CreateWindow failed: %s", SDL_GetError());
		}

		LOG->Info("LowLevelWindow_SDL: SDL_CreateWindow succeeded, got window=%p", g_pWindow);

		#ifdef __EMSCRIPTEN__
		// For Emscripten, we still need to create and make current the GL context
		LOG->Info("LowLevelWindow_SDL: About to call SDL_GL_CreateContext (Emscripten)...");
		g_GLContext = SDL_GL_CreateContext(g_pWindow);
		if (!g_GLContext)
		{
			LOG->Warn("SDL_GL_CreateContext FAILED: %s", SDL_GetError());
			return ssprintf("SDL_GL_CreateContext failed: %s", SDL_GetError());
		}
		LOG->Info("LowLevelWindow_SDL: SDL_GL_CreateContext succeeded, got context=%p", g_GLContext);

		LOG->Info("LowLevelWindow_SDL: About to call SDL_GL_MakeCurrent (Emscripten)...");
		if (SDL_GL_MakeCurrent(g_pWindow, g_GLContext) < 0)
		{
			LOG->Warn("SDL_GL_MakeCurrent FAILED: %s", SDL_GetError());
			return ssprintf("SDL_GL_MakeCurrent failed: %s", SDL_GetError());
		}
		LOG->Info("LowLevelWindow_SDL: SDL_GL_MakeCurrent completed - WebGL context is now active");

		// SDL_CreateWindow on Emscripten may have shrunk the canvas; force it
		// back to the intended drawing-buffer size (this drives WebGL's actual
		// framebuffer dimensions).
		EM_ASM({
			if (Module.canvas) {
				Module.canvas.width = $0;
				Module.canvas.height = $1;
				console.log('[LowLevelWindow_SDL] Forced canvas size to ' + $0 + 'x' + $1 +
					' (was ' + Module.canvas.width + 'x' + Module.canvas.height + ' after SDL)');
			}
		}, windowWidth, windowHeight);
		#else
		LOG->Info("LowLevelWindow_SDL: About to call SDL_GL_CreateContext...");
		g_GLContext = SDL_GL_CreateContext(g_pWindow);
		if (!g_GLContext)
		{
			LOG->Warn("SDL_GL_CreateContext FAILED: %s", SDL_GetError());
			return ssprintf("SDL_GL_CreateContext failed: %s", SDL_GetError());
		}

		LOG->Info("LowLevelWindow_SDL: SDL_GL_CreateContext succeeded, got context=%p", g_GLContext);

		LOG->Info("LowLevelWindow_SDL: About to call SDL_GL_MakeCurrent...");
		SDL_GL_MakeCurrent(g_pWindow, g_GLContext);
		LOG->Info("LowLevelWindow_SDL: SDL_GL_MakeCurrent completed");
		#endif
		bNewDeviceOut = true;
	}
	else
	{
		SDL_SetWindowSize(g_pWindow, p.width, p.height);
		bNewDeviceOut = false;
	}

	LOG->Info("LowLevelWindow_SDL: Video mode set to %dx%d", p.width, p.height);
	return "";
}

const ActualVideoModeParams LowLevelWindow_SDL::GetActualVideoModeParams() const
{
	ActualVideoModeParams params;
#ifdef __EMSCRIPTEN__
	// On Emscripten the canvas drives the GL drawing buffer size; SDL's window
	// size can be wrong (it has been observed to return 5x5). Read the canvas
	// directly so the viewport matches the actual rendering surface.
	int w = 0, h = 0;
	w = EM_ASM_INT({ return Module.canvas ? Module.canvas.width : 0; });
	h = EM_ASM_INT({ return Module.canvas ? Module.canvas.height : 0; });
	if (w <= 0 || h <= 0) {
		// Fall back to HTML-defined size
		w = 1280;
		h = 720;
	}
	params.width = w;
	params.height = h;
	params.windowWidth = w;
	params.windowHeight = h;
	params.rate = 60;
	params.windowed = true;
	params.bpp = 32;
	params.fDisplayAspectRatio = (float)w / (float)h;
#else
	if (g_pWindow)
	{
		int w, h;
		SDL_GetWindowSize(g_pWindow, &w, &h);
		params.width = w;
		params.height = h;
		params.windowWidth = w;
		params.windowHeight = h;
		params.rate = 60;
		params.windowed = true;
		params.bpp = 32;
		params.fDisplayAspectRatio = (float)w / (float)h;
	}
#endif
	return params;
}

void LowLevelWindow_SDL::GetDisplaySpecs( DisplaySpecs &out ) const
{
	SDL_DisplayMode mode;
	if (SDL_GetCurrentDisplayMode(0, &mode) == 0)
	{
		DisplayMode dm;
		dm.width = mode.w;
		dm.height = mode.h;
		dm.refreshRate = mode.refresh_rate;

		DisplaySpec spec("0", "Browser Display", dm);
		out.insert(spec);
	}
}

bool LowLevelWindow_SDL::IsSoftwareRenderer( RString &sError )
{
	// WebGL is hardware accelerated
	return false;
}

void LowLevelWindow_SDL::SwapBuffers()
{
	static int swapCount = 0;
	swapCount++;

	if (swapCount % 60 == 0) {
		LOG->Info("[SwapBuffers] #%d - About to call SDL_GL_SwapWindow", swapCount);
	}

	if (g_pWindow)
		SDL_GL_SwapWindow(g_pWindow);

	if (swapCount % 60 == 0) {
		LOG->Info("[SwapBuffers] #%d - Completed SDL_GL_SwapWindow", swapCount);
	}
}

/*
 * (c) 2025 Browser port contributors
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
