#include <U8g2lib.h>
#include <Wire.h>
#include <math.h>
#include "display/screen_main.h"
#include "display/hourglass_anim.h"
#include "config.h"


static U8G2_SH1106_128X64_NONAME_F_HW_I2C display(
    U8G2_R0,
    /* reset= */ U8X8_PIN_NONE
);



// ─── Wheel geometry ──────────────────────────────────────
static const int CENTER_X       = 64;   // horizontal center (screen middle)
static const int CENTER_Y       = 120;  // vertical center (off-screen) -> arc effect
static const int RADIUS_PIXEL   = 92;   // radius for small ticks (pixels)
static const int RADIUS_LINE    = 87;   // inner radius for major tick lines
static const int RADIUS_TEXT    = 75;   // radius for numeric labels

// ─── Precomputed sin/cos lookup tables ──────────────────────────────
//  Avoid sin/cos calls in the loop -> significant performance gain
static uint8_t lut_x[180];
static uint8_t lut_y[180];

// ─── Rendering buffers ─────────────────────────────────────────
//  Store drawing primitives before rendering
//  (required by u8g1; kept for clarity and performance)
static int pixel_buf[50][2];
static int line_buf[10][4];
static int text_buf[10][3];
static int pixel_count, line_count, text_count;

static char str_buf[16];   // generic buffer for itoa / sprintf



static void _drawPauseIcon(U8G2& display) {
    display.drawBox(5, 3, 5, 18);
    display.drawBox(14, 3, 5, 18);
}

static void _drawEmergencyScreen(U8G2& display) {
    uint32_t now = millis();

    display.clearBuffer();

    bool blinkOn = ((now / EMERGENCY_BLINK_INTERVAL_MS) % 2) == 0;
    if (blinkOn) {
        display.setFont(u8g2_font_7x14B_tr);
        display.drawStr(4, 26, "SVP RESET");
        display.drawStr(4, 44, "BOUTON URGENCE");
    }

    uint32_t phase = now % EMERGENCY_ARROW_SWEEP_MS;
    float progress = (float)phase / (float)EMERGENCY_ARROW_SWEEP_MS;
    float angleRad = radians(progress * EMERGENCY_ARROW_SWEEP_DEG);

    const int cx = 108, cy = 50, len = 14;
    int x1 = cx - (int)(cos(angleRad) * len);
    int y1 = cy - (int)(sin(angleRad) * len);
    int x2 = cx + (int)(cos(angleRad) * len);
    int y2 = cy + (int)(sin(angleRad) * len);
    display.drawLine(x1, y1, x2, y2);
    display.drawDisc(x2, y2, 2);

    display.sendBuffer();
}



// ------------------------------------------------------------



void screenMainInit() {
    Wire.begin(PIN_SCREEN_MAIN_SDA, PIN_SCREEN_MAIN_SCL);
    display.begin();
    display.clearDisplay();
    display.clearBuffer();
    display.sendBuffer();

    // Precompute angular positions (done once at startup)
    for (int i = 0; i < 180; i++) {
        lut_x[i] = (uint8_t)(sin(radians(i - 90)) * RADIUS_PIXEL + CENTER_X);
        lut_y[i] = (uint8_t)(-cos(radians(i - 90)) * RADIUS_PIXEL + CENTER_Y);
    }
}

void screenMainSleep() {
    display.clearDisplay();
    display.setPowerSave(1);
}



// ------------------------------------------------------------


void screenMainUpdate(int duration_minutes, int current_hour, int current_minute, CallState callState) {
    
    if (callState == CallState::EMERGENCY) {
        _drawEmergencyScreen(display);
        return;
    }

    // ── Internal scaling ──────────────────────────────
    //  Work in "tenths of a minute" (×10) to preserve the same integer math as the original
    //  (original used ×10 for percentages)
    //  Range: 0–90 min -> 0–900 internally
    int internal = duration_minutes * 10;

    // ── End time calculation ──────────────────────────────
    int total_end_minutes = current_hour * 60 + current_minute + duration_minutes;
    int end_hour   = (total_end_minutes / 60) % 24;
    int end_minute =  total_end_minutes % 60;

    // ── Tick calculations ────────────────────────────────
    pixel_count = line_count = text_count = 0;

    for (int i = -48; i <= 48; i += 3) {

        // Actual angle of this tick on the wheel
        int angle = i + (internal * 3 / 10) % 3;

        // Numeric value (minutes)
        int tick_val = (int)round(internal / 10.0 + angle / 3.0);

        // Position on the arc (read from LUT)
        int px = lut_x[angle + 90];
        int py = lut_y[angle + 90];

        // Off-screen -> ignore
        if (px < 0 || px >= 128 || py < 0 || py >= 64) continue;
        // Out of range 0–90 min -> ignore
        if (tick_val < 0 || tick_val > 90) continue;

        if (tick_val % 5 == 0) {
            // ── Major tick: line + label ──────────────
            float angle_rad = radians(angle);
            int lx = (int)(sin(angle_rad) * RADIUS_LINE + CENTER_X);
            int ly = (int)(-cos(angle_rad) * RADIUS_LINE + CENTER_Y);

            line_buf[line_count][0] = lx;
            line_buf[line_count][1] = ly;
            line_buf[line_count][2] = px;
            line_buf[line_count][3] = py;
            line_count++;

            float tx = sin(angle_rad) * RADIUS_TEXT + CENTER_X;
            float ty = -cos(angle_rad) * RADIUS_TEXT + CENTER_Y;
            text_buf[text_count][0] = (int)tx;
            text_buf[text_count][1] = (int)ty;
            text_buf[text_count][2] = tick_val;
            text_count++;

        } else {
            // ── Minor tick: pixel ──────────────────────
            pixel_buf[pixel_count][0] = px;
            pixel_buf[pixel_count][1] = py;
            pixel_count++;
        }
    }

    // ── Rendering ─────────────────────────────────────────────────
    display.clearBuffer();

    // --- Minor ticks (pixels) ---
    for (int i = 0; i < pixel_count; i++) {
        display.drawPixel(pixel_buf[i][0], pixel_buf[i][1]);
    }

    // --- Major ticks (lines) ---
    for (int i = 0; i < line_count; i++) {
        display.drawLine(line_buf[i][0], line_buf[i][1],
                         line_buf[i][2], line_buf[i][3]);
    }

    // --- Numeric labels ---
    display.setFont(u8g2_font_6x10_tr);
    for (int i = 0; i < text_count; i++) {
        itoa(text_buf[i][2], str_buf, 10);
        int sw = display.getStrWidth(str_buf);
        display.drawStr(text_buf[i][0] - sw / 2,
                        text_buf[i][1],
                        str_buf);
    }

    // --- Center value: selected duration ---
    display.setFont(u8g2_font_profont17_tr);
    snprintf(str_buf, sizeof(str_buf), "%d min", duration_minutes);
    int sw = display.getStrWidth(str_buf);

    display.setDrawColor(1);
    display.drawRBox(CENTER_X - (sw + 6) / 2, 0, sw + 6, 13, 2);   // rounded background
    display.drawTriangle(CENTER_X - 4, 13,
                         CENTER_X + 4, 13,
                         CENTER_X,     17);                          // small arrow pointing to the wheel

    display.setDrawColor(0);   // text black on white background
    display.drawStr(CENTER_X - sw / 2, 12, str_buf);
    display.setDrawColor(1);

    // --- End time ---
    display.setFont(u8g2_font_profont12_tr);
    snprintf(str_buf, sizeof(str_buf), "-> %dh%02d", end_hour, end_minute);
    sw = display.getStrWidth(str_buf);
    display.drawStr(78, 23, str_buf);

    
    if (callState == CallState::PAUSED) {
        hourglassAnimDraw(display, false); // stop/reset the hourglass anim
        _drawPauseIcon(display);
    } else {
        hourglassAnimDraw(display, callState == CallState::RUNNING);
    }

    display.sendBuffer();
}