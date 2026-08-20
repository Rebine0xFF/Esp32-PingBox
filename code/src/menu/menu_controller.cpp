#include "menu/menu_controller.h"
#include "input/encoder.h"
#include "input/buttons.h"
#include "utils/logger.h"
#include <Arduino.h>

static MenuState _state = MenuState::OFF;
static int _selectedIndex = 0;


static const uint32_t LONG_PRESS_MS = 600;
static bool     _encSwWasDown      = false;
static uint32_t _encSwDownSinceMs  = 0;
static bool     _longPressConsumed = false;

enum class EncClickEvent { NONE, SHORT_CLICK, LONG_PRESS };

static EncClickEvent _pollEncoderClick() {
    bool down = encoderSwitchIsDown();
    uint32_t now = millis();
    EncClickEvent event = EncClickEvent::NONE;

    if (down && !_encSwWasDown) {
        // Press started.
        _encSwDownSinceMs = now;
        _longPressConsumed = false;
    } else if (down && _encSwWasDown && !_longPressConsumed) {
        if (now - _encSwDownSinceMs >= LONG_PRESS_MS) {
            _longPressConsumed = true;
            event = EncClickEvent::LONG_PRESS;
        }
    } else if (!down && _encSwWasDown) {
        // Released: only counts as a short click if the long-press
        // threshold was never reached during this press.
        if (!_longPressConsumed && (now - _encSwDownSinceMs < LONG_PRESS_MS)) {
            event = EncClickEvent::SHORT_CLICK;
        }
    }

    _encSwWasDown = down;
    return event;
}

static MenuState _stateForCategory(int index) {
    switch ((MenuCategory)index) {
        case MenuCategory::DISPLAY_SETTINGS: return MenuState::DISPLAY_SETTINGS;
        case MenuCategory::STATISTICS:       return MenuState::STATISTICS;
        case MenuCategory::SERVO_CONTROL:    return MenuState::SERVO_CONTROL;
        case MenuCategory::SYSTEM_INFO:      return MenuState::SYSTEM_INFO;
        case MenuCategory::NETWORK_DIAG:     return MenuState::NETWORK_DIAG;
        case MenuCategory::RESET_STATS:      return MenuState::RESET_CONFIRM;
        default:                             return MenuState::LIST;
    }
}

// ------------------------------------------------------------

void menuControllerInit() {
    _state = MenuState::OFF;
    _selectedIndex = 0;
}

void menuControllerUpdate(bool entryBlocked, bool emergencyActive) {
    // Emergency force-close the menu from any state.
    if (_state != MenuState::OFF && emergencyActive) {
        LOG_WARN("MENU", "Emergency triggered, force-closing menu");
        _state = MenuState::OFF;
        return;
    }

    bool menuSwitchPressed = buttonMenuPressed();

    if (_state == MenuState::OFF) {
        if (menuSwitchPressed && !entryBlocked) {
            // Resync so unrelated rotation that happened while the menu was
            // closed doesn't produce a spurious cursor jump on entry.
            encoderResyncMenuDelta();
            _state = MenuState::LIST;
            LOG_INFO("MENU", "Menu opened, cursor at index %d", _selectedIndex);
        }
        return;
    }

    // Menu is open (any state other than OFF) from here on.
    if (menuSwitchPressed) {
        LOG_INFO("MENU", "Menu closed via switch");
        _state = MenuState::OFF;
        return;
    }

    EncClickEvent click = _pollEncoderClick();

    if (_state == MenuState::LIST) {
        int delta = encoderConsumeDelta();
        if (delta != 0) {
            int count = (int)MenuCategory::COUNT;
            _selectedIndex = (_selectedIndex + (delta > 0 ? 1 : -1) + count) % count;
        }
        if (click == EncClickEvent::SHORT_CLICK) {
            _state = _stateForCategory(_selectedIndex);
            LOG_INFO("MENU", "Entered category %d", _selectedIndex);
        }
        // A long press on the list itself has no effect
    } else {
        // Inside a category: rotation/click handling specific to that
        // category is delegated to its own module (TODO).
        // Only the shared "return to list" gesture is handled here.
        if (click == EncClickEvent::LONG_PRESS) {
            _state = MenuState::LIST;
            LOG_INFO("MENU", "Returned to menu list at index %d", _selectedIndex);
        }
    }
}

bool menuIsActive()        { return _state != MenuState::OFF; }
bool menuIsListFocused()   { return _state == MenuState::LIST; }
MenuState menuGetState()   { return _state; }
int menuGetSelectedIndex() { return _selectedIndex; }