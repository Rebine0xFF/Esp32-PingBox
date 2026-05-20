#include "display/screen_main.h"
#include "config.h"
#include <U8g2lib.h>
#include <Wire.h>
#include <math.h>


static U8G2_SH1106_128X64_NONAME_F_HW_I2C display(
    U8G2_R0,
    /* reset= */ U8X8_PIN_NONE
);



// ─── Géométrie ────────────────────────────────────────────────
//  Centre hors écran à droite → l'arc est visible sur la droite
//  La partie gauche (0–80px) est libre pour le texte
static const int CENTER_X     = 155;  // hors écran à droite
static const int CENTER_Y     = 32;   // milieu vertical
static const int RADIUS_PIXEL = 70;   // graduations fines
static const int RADIUS_LINE  = 65;   // graduations majeures (trait)
static const int RADIUS_TEXT  = 54;   // labels numériques

// ─── LUT sin/cos ──────────────────────────────────────────────
static uint8_t lut_x[180];
static uint8_t lut_y[180];

// ─── Buffers rendu ────────────────────────────────────────────
static int pixel_buf[50][2];
static int line_buf[10][4];
static int text_buf[10][3];
static int pixel_count, line_count, text_count;
static char str_buf[16];



// ------------------------------------------------------------



void screenMainInit() {
    Wire.begin(PIN_SCREEN_MAIN_SDA, PIN_SCREEN_MAIN_SCL);
    display.begin();
    display.clearDisplay();
    display.clearBuffer();
    display.sendBuffer();

    // Précalcul des positions angulaires (fait une seule fois au démarrage)
    for (int i = 0; i < 180; i++) {
        float rad = radians(i - 90);
        lut_x[i] = (uint8_t)(-cos(rad) * RADIUS_PIXEL + CENTER_X);
        lut_y[i] = (uint8_t)( sin(rad) * RADIUS_PIXEL + CENTER_Y);
    }
}



// ------------------------------------------------------------



void screenMainUpdate(int duration_minutes, int current_hour, int current_minute) {

    int internal = duration_minutes * 10;  // 0–900, même arithmétique que l'original

    // Heure de fin
    int total     = current_hour * 60 + current_minute + duration_minutes;
    int end_hour  = (total / 60) % 24;
    int end_min   = total % 60;

    // ── Calcul des graduations ────────────────────────────────
    pixel_count = line_count = text_count = 0;

    for (int i = -48; i <= 48; i += 3) {
        int angle    = i + (internal * 3 / 10) % 3;
        int tick_val = (int)round(internal / 10.0 + angle / 3.0);

        int px = lut_x[angle + 90];
        int py = lut_y[angle + 90];

        if (px < 0 || px >= 128 || py < 0 || py >= 64) continue;
        if (tick_val < 0 || tick_val > 90)              continue;

        if (tick_val % 5 == 0) {
            // Grande graduation
            float rad = radians(angle);
            int lx = (int)(-cos(rad) * RADIUS_LINE + CENTER_X);
            int ly = (int)( sin(rad) * RADIUS_LINE + CENTER_Y);

            line_buf[line_count][0] = px;  line_buf[line_count][1] = py;
            line_buf[line_count][2] = lx;  line_buf[line_count][3] = ly;
            line_count++;

            text_buf[text_count][0] = (int)(-cos(rad) * RADIUS_TEXT + CENTER_X);
            text_buf[text_count][1] = (int)( sin(rad) * RADIUS_TEXT + CENTER_Y);
            text_buf[text_count][2] = tick_val;
            text_count++;
        } else {
            // Petite graduation
            pixel_buf[pixel_count][0] = px;
            pixel_buf[pixel_count][1] = py;
            pixel_count++;
        }
    }

    // ── Rendu ─────────────────────────────────────────────────
    display.clearBuffer();

    // Petites graduations
    for (int i = 0; i < pixel_count; i++)
        display.drawPixel(pixel_buf[i][0], pixel_buf[i][1]);

    // Grandes graduations
    for (int i = 0; i < line_count; i++)
        display.drawLine(line_buf[i][0], line_buf[i][1],
                         line_buf[i][2], line_buf[i][3]);

    // Labels
    display.setFont(u8g2_font_6x10_tr);
    for (int i = 0; i < text_count; i++) {
        itoa(text_buf[i][2], str_buf, 10);
        int sw = display.getStrWidth(str_buf);
        display.drawStr(text_buf[i][0] - sw / 2, text_buf[i][1], str_buf);
    }

    // ── Affichage texte (partie gauche) ───────────────────────

    // Durée — grande police, centrée verticalement à gauche
    display.setFont(u8g2_font_10x20_tr);
    snprintf(str_buf, sizeof(str_buf), "%d min", duration_minutes);
    int sw = display.getStrWidth(str_buf);
    // Fond blanc arrondi
    display.setDrawColor(1);
    display.drawRBox(0, 18, sw + 6, 22, 3);
    // Petite flèche pointant vers l'arc (vers la droite)
    display.drawTriangle(sw + 6, 24,
                         sw + 6, 32,
                         sw + 11, 28);
    // Texte en noir sur fond blanc
    display.setDrawColor(0);
    display.drawStr(3, 36, str_buf);
    display.setDrawColor(1);

    // Heure de fin — sous le bloc durée
    display.setFont(u8g2_font_profont12_tr);
    snprintf(str_buf, sizeof(str_buf), "> %dh%02d", end_hour, end_min);
    display.drawStr(3, 55, str_buf);

    display.sendBuffer();
}