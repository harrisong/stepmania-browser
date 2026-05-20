#ifndef LOW_LEVEL_WINDOW_SDL_H
#define LOW_LEVEL_WINDOW_SDL_H

#include "LowLevelWindow.h"

// SDL-based window implementation for Emscripten
class LowLevelWindow_SDL: public LowLevelWindow
{
public:
	LowLevelWindow_SDL();
	~LowLevelWindow_SDL();

	void *GetProcAddress( RString s );
	RString TryVideoMode( const VideoModeParams &p, bool &bNewDeviceOut );
	const ActualVideoModeParams GetActualVideoModeParams() const;
	void GetDisplaySpecs( DisplaySpecs &out ) const;
	bool IsSoftwareRenderer( RString &sError );
	void SwapBuffers();
};

#ifdef ARCH_LOW_LEVEL_WINDOW
#error "More than one LowLevelWindow selected!"
#endif
#define ARCH_LOW_LEVEL_WINDOW LowLevelWindow_SDL

#endif
