#pragma once

// ============================================================
// Menu controller state machine.
// Navigation: encoder rotates the list cursor; short click enters a
// category; long press returns to the list. Menu switch opens/closes
// the menu from any state.
// ============================================================

enum class MenuState {
    OFF,               // menu closed, normal device operation
    LIST,              // browsing the category list
    DISPLAY_SETTINGS,
    STATISTICS,
    SERVO_CONTROL,
    SYSTEM_INFO,
    NETWORK_DIAG,
    RESET_CONFIRM
};

// Fixed display order of the category list.
enum class MenuCategory {
    DISPLAY_SETTINGS = 0,
    STATISTICS,
    SERVO_CONTROL,
    SYSTEM_INFO,
    NETWORK_DIAG,
    RESET_STATS,
    COUNT
};

void menuControllerInit();

// Call once per loop(). entryBlocked blocks menu opening; emergencyActive
// force-closes menu immediately (emergency takes priority)
void menuControllerUpdate(bool entryBlocked, bool emergencyActive);

bool menuIsActive();                  // true for any state other than OFF
bool menuIsListFocused();             // true only in LIST (screen_main brightness)
MenuState menuGetState();
int menuGetSelectedIndex();           // cursor position in the category list (0..COUNT-1)