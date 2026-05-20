#ifndef INPUT_HANDLER_SDL_H
#define INPUT_HANDLER_SDL_H

#include "InputHandler.h"

class InputHandler_SDL: public InputHandler
{
public:
	InputHandler_SDL();
	~InputHandler_SDL();

	void GetDevicesAndDescriptions( vector<InputDeviceInfo>& vDevicesOut );
	void Update();

	// Public trampoline so HTML5 keyboard callbacks can deliver events into
	// the protected ButtonPressed() helper.
	void ButtonPressedTrampoline( DeviceInput di );
};

#endif
