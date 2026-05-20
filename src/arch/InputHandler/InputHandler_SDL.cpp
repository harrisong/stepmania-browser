#include "global.h"
#include "InputHandler_SDL.h"
#include "RageLog.h"
#include "RageInputDevice.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#include <string.h>
#endif

REGISTER_INPUT_HANDLER_CLASS( SDL );

#ifdef __EMSCRIPTEN__

static InputHandler_SDL *g_pInstance = nullptr;

// Map a DOM KeyboardEvent.code string to a StepMania DeviceButton.
// Reference: https://developer.mozilla.org/en-US/docs/Web/API/UI_Events/Keyboard_event_code_values
static DeviceButton DomCodeToDeviceButton( const char *code )
{
	if (!code || !*code)
		return DeviceButton_Invalid;

	// Letter keys: "KeyA".."KeyZ" -> KEY_Ca..KEY_Cz (lowercase variant; the
	// game's mapper treats letter keys case-insensitively).
	if (strncmp(code, "Key", 3) == 0 && code[3] >= 'A' && code[3] <= 'Z' && code[4] == '\0') {
		return (DeviceButton)(KEY_Ca + (code[3] - 'A'));
	}

	// Digit keys: "Digit0".."Digit9" -> KEY_C0..KEY_C9
	if (strncmp(code, "Digit", 5) == 0 && code[5] >= '0' && code[5] <= '9' && code[6] == '\0') {
		return (DeviceButton)(KEY_C0 + (code[5] - '0'));
	}

	// Function keys: "F1".."F16"
	if (code[0] == 'F' && (code[1] >= '1' && code[1] <= '9')) {
		int n = atoi(code + 1);
		if (n >= 1 && n <= 16) return (DeviceButton)(KEY_F1 + (n - 1));
	}

	// Numpad
	if (strncmp(code, "Numpad", 6) == 0) {
		const char *rest = code + 6;
		if (rest[0] >= '0' && rest[0] <= '9' && rest[1] == '\0')
			return (DeviceButton)(KEY_KP_C0 + (rest[0] - '0'));
		if (strcmp(rest, "Divide") == 0)   return KEY_KP_SLASH;
		if (strcmp(rest, "Multiply") == 0) return KEY_KP_ASTERISK;
		if (strcmp(rest, "Subtract") == 0) return KEY_KP_HYPHEN;
		if (strcmp(rest, "Add") == 0)      return KEY_KP_PLUS;
		if (strcmp(rest, "Decimal") == 0)  return KEY_KP_PERIOD;
		if (strcmp(rest, "Equal") == 0)    return KEY_KP_EQUAL;
		if (strcmp(rest, "Enter") == 0)    return KEY_KP_ENTER;
	}

	// Arrows
	if (strcmp(code, "ArrowUp") == 0)    return KEY_UP;
	if (strcmp(code, "ArrowDown") == 0)  return KEY_DOWN;
	if (strcmp(code, "ArrowLeft") == 0)  return KEY_LEFT;
	if (strcmp(code, "ArrowRight") == 0) return KEY_RIGHT;

	// Editing/navigation
	if (strcmp(code, "Enter") == 0)      return KEY_ENTER;
	if (strcmp(code, "Escape") == 0)     return KEY_ESC;
	if (strcmp(code, "Backspace") == 0)  return KEY_BACK;
	if (strcmp(code, "Tab") == 0)        return KEY_TAB;
	if (strcmp(code, "Space") == 0)      return KEY_SPACE;
	if (strcmp(code, "Insert") == 0)     return KEY_INSERT;
	if (strcmp(code, "Delete") == 0)     return KEY_DEL;
	if (strcmp(code, "Home") == 0)       return KEY_HOME;
	if (strcmp(code, "End") == 0)        return KEY_END;
	if (strcmp(code, "PageUp") == 0)     return KEY_PGUP;
	if (strcmp(code, "PageDown") == 0)   return KEY_PGDN;
	if (strcmp(code, "Pause") == 0)      return KEY_PAUSE;
	if (strcmp(code, "PrintScreen") == 0) return KEY_PRTSC;
	if (strcmp(code, "ScrollLock") == 0) return KEY_SCRLLOCK;
	if (strcmp(code, "NumLock") == 0)    return KEY_NUMLOCK;
	if (strcmp(code, "CapsLock") == 0)   return KEY_CAPSLOCK;
	if (strcmp(code, "ContextMenu") == 0) return KEY_MENU;

	// Modifiers
	if (strcmp(code, "ShiftLeft") == 0)   return KEY_LSHIFT;
	if (strcmp(code, "ShiftRight") == 0)  return KEY_RSHIFT;
	if (strcmp(code, "ControlLeft") == 0) return KEY_LCTRL;
	if (strcmp(code, "ControlRight") == 0) return KEY_RCTRL;
	if (strcmp(code, "AltLeft") == 0)     return KEY_LALT;
	if (strcmp(code, "AltRight") == 0)    return KEY_RALT;
	if (strcmp(code, "MetaLeft") == 0 || strcmp(code, "OSLeft") == 0)   return KEY_LMETA;
	if (strcmp(code, "MetaRight") == 0 || strcmp(code, "OSRight") == 0) return KEY_RMETA;

	// Punctuation (US layout)
	if (strcmp(code, "Minus") == 0)         return KEY_HYPHEN;
	if (strcmp(code, "Equal") == 0)         return KEY_EQUAL;
	if (strcmp(code, "BracketLeft") == 0)   return KEY_LBRACKET;
	if (strcmp(code, "BracketRight") == 0)  return KEY_RBRACKET;
	if (strcmp(code, "Backslash") == 0)     return KEY_BACKSLASH;
	if (strcmp(code, "Semicolon") == 0)     return KEY_SEMICOLON;
	if (strcmp(code, "Quote") == 0)         return KEY_SQUOTE;
	if (strcmp(code, "Backquote") == 0)     return KEY_ACCENT;
	if (strcmp(code, "Comma") == 0)         return KEY_COMMA;
	if (strcmp(code, "Period") == 0)        return KEY_PERIOD;
	if (strcmp(code, "Slash") == 0)         return KEY_SLASH;
	if (strcmp(code, "IntlBackslash") == 0) return KEY_BACKSLASH;

	return DeviceButton_Invalid;
}

class InputHandler_SDL_Trampoline
{
public:
	static void SendButton( DeviceButton db, bool bPressed )
	{
		if (g_pInstance == nullptr || db == DeviceButton_Invalid)
			return;
		DeviceInput di( DEVICE_KEYBOARD, db, bPressed ? 1.0f : 0.0f );
		g_pInstance->ButtonPressedTrampoline( di );
	}
};

void InputHandler_SDL::ButtonPressedTrampoline( DeviceInput di )
{
	ButtonPressed( di );
}

static EM_BOOL OnKeyDown( int eventType, const EmscriptenKeyboardEvent *e, void *userData )
{
	if (e->repeat)
		return EM_TRUE; // already pressed; don't re-fire

	DeviceButton db = DomCodeToDeviceButton( e->code );
	if (db != DeviceButton_Invalid)
		InputHandler_SDL_Trampoline::SendButton( db, true );

	// Swallow keys we recognize so the browser doesn't act on them
	// (e.g. arrow keys scrolling the page, Tab moving focus).
	return db != DeviceButton_Invalid ? EM_TRUE : EM_FALSE;
}

static EM_BOOL OnKeyUp( int eventType, const EmscriptenKeyboardEvent *e, void *userData )
{
	DeviceButton db = DomCodeToDeviceButton( e->code );
	if (db != DeviceButton_Invalid)
		InputHandler_SDL_Trampoline::SendButton( db, false );

	return db != DeviceButton_Invalid ? EM_TRUE : EM_FALSE;
}

#endif // __EMSCRIPTEN__

InputHandler_SDL::InputHandler_SDL()
{
	LOG->Info( "InputHandler_SDL: registering HTML5 keyboard callbacks" );

#ifdef __EMSCRIPTEN__
	g_pInstance = this;
	emscripten_set_keydown_callback( EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, OnKeyDown );
	emscripten_set_keyup_callback( EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, OnKeyUp );
#endif
}

InputHandler_SDL::~InputHandler_SDL()
{
#ifdef __EMSCRIPTEN__
	emscripten_set_keydown_callback( EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, nullptr );
	emscripten_set_keyup_callback( EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, nullptr );
	if (g_pInstance == this)
		g_pInstance = nullptr;
#endif
}

void InputHandler_SDL::GetDevicesAndDescriptions( vector<InputDeviceInfo>& vDevicesOut )
{
	vDevicesOut.push_back( InputDeviceInfo(DEVICE_KEYBOARD, "Browser Keyboard") );
}

void InputHandler_SDL::Update()
{
	// Events are pushed asynchronously from JS callbacks; nothing to poll.
	InputHandler::UpdateTimer();
}

/*
 * (c) 2026 Browser port contributors
 * All rights reserved.
 */
