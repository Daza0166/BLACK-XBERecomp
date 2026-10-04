#pragma once
#include <cstddef>
#include <string>

#include "core/input.h"

// the keyboard and mouse as the controller on port 0
namespace input {

void StartMouse(void* hwnd);     // window thread, the game window is up
void OnRawInput(void* handle);   // window thread, WM_INPUT
void ReleaseCursor();            // the game lost the input: the clip goes, held keys wait for release
void Keyboard(Pad& pad);         // held keys onto port 0's pad
// raw mouse counts since the last call, scaled by the settings; false when there are none
bool TakeMouse(float& dx, float& dy);

// actions the keyboard and mouse can be bound to, key names comma separated
struct Action {
    const char* id;
    const char* label;
    const char* defaults;
};
const Action* Actions(size_t* count);
std::string Binding(size_t action);
void Bind(size_t action, int vk);
void ResetBindings();
void LoadBindings();     // after the settings load
int PressedKey();        // a bindable key or mouse button held now, 0 for none
bool PanelKey(int vk);   // the settings panel is bound to this key and no key capture wants it

// the game's own menus
void SeenPad(const Pad& pad);  // a controller's report before the keys join it: its input hides the cursor
void SetMenu(bool menu);       // game thread, each tick: a menu page is up
bool KeyboardActive();         // keyboard or mouse used last, not a controller
bool CursorShown();            // in a menu with the keyboard or mouse used last
// the cursor in the shown frame, 0 to 1 across and down, and the left button; false while it is not shown
bool Cursor(float& x, float& y, bool& down);
int TakeWheel();               // wheel notches in the menus since the last call, up positive

}  // namespace input
