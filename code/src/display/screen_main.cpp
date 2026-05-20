#include "display/screen_main.h"
#include "config.h"
#include <U8g2lib.h>
#include <Wire.h>
#include <math.h>


static U8G2_SH1106_128X64_NONAME_F_HW_I2C display(
    U8G2_R0,
    /* reset= */ U8X8_PIN_NONE
);



// ─── Géométrie de la roue ──────────────────────────────────────
static const int CENTER_X       = 64;   // centre horizontal (milieu écran)
static const int CENTER_Y       = 108;  // centre vertical hors écran → effet arc
static const int RADIUS_PIXEL   = 92;   // rayon des petites graduations (pixels)
static const int RADIUS_LINE    = 87;   // rayon intérieur des grandes graduations (lignes)
static const int RADIUS_TEXT    = 75;   // rayon des labels numériques

// ─── Tables de précalcul sin/cos ──────────────────────────────
//  Évite les appels sin/cos dans la loop → gain de performance significatif
static uint8_t lut_x[180];
static uint8_t lut_y[180];

// ─── Buffers de rendu ─────────────────────────────────────────
//  On stocke ce qu'on veut dessiner AVANT d'entrer dans le rendu
//  (nécessaire en u8g1, conservé ici pour la clarté et la perf)
static int pixel_buf[50][2];
static int line_buf[10][4];
static int text_buf[10][3];
static int pixel_count, line_count, text_count;

static char str_buf[16];   // buffer générique pour itoa / sprintf



// ------------------------------------------------------------



void screenMainInit() {
    Wire.begin(PIN_SCREEN_MAIN_SDA, PIN_SCREEN_MAIN_SCL);
    display.begin();
    display.clearDisplay();
    display.clearBuffer();
    display.sendBuffer();

    // Précalcul des positions angulaires (fait une seule fois au démarrage)
    for (int i = 0; i < 180; i++) {
        lut_x[i] = (uint8_t)(sin(radians(i - 90)) * RADIUS_PIXEL + CENTER_X);
        lut_y[i] = (uint8_t)(-cos(radians(i - 90)) * RADIUS_PIXEL + CENTER_Y);
    }
}



// ------------------------------------------------------------


void screenMainUpdate(int duration_minutes, int current_hour, int current_minute) {
    // ── Mise à l'échelle interne ──────────────────────────────
    //  On travaille en "dixièmes de minute" (×10) pour conserver
    //  la même arithmétique entière que l'original (qui était ×10 pour les %)
    //  Plage : 0–90 min → 0–900 en interne
    int internal = duration_minutes * 10;

    // ── Calcul de l'heure de fin ──────────────────────────────
    int total_end_minutes = current_hour * 60 + current_minute + duration_minutes;
    int end_hour   = (total_end_minutes / 60) % 24;
    int end_minute =  total_end_minutes % 60;

    // ── Calcul des graduations ────────────────────────────────
    pixel_count = line_count = text_count = 0;

    for (int i = -48; i <= 48; i += 3) {

        // Angle réel de cette graduation sur la roue
        int angle = i + (internal * 3 / 10) % 3;

        // Valeur numérique correspondante (en minutes)
        int tick_val = (int)round(internal / 10.0 + angle / 3.0);

        // Position sur l'arc (lecture dans la LUT)
        int px = lut_x[angle + 90];
        int py = lut_y[angle + 90];

        // Hors écran → on ignore
        if (px < 0 || px >= 128 || py < 0 || py >= 64) continue;
        // Hors plage 0–90 min → on ignore
        if (tick_val < 0 || tick_val > 90) continue;

        if (tick_val % 5 == 0) {
            // ── Grande graduation : ligne + label ──────────────
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
            // ── Petite graduation : pixel ──────────────────────
            pixel_buf[pixel_count][0] = px;
            pixel_buf[pixel_count][1] = py;
            pixel_count++;
        }
    }

    // ── Rendu ─────────────────────────────────────────────────
    display.clearBuffer();

    // --- Petites graduations (pixels) ---
    for (int i = 0; i < pixel_count; i++) {
        display.drawPixel(pixel_buf[i][0], pixel_buf[i][1]);
    }

    // --- Grandes graduations (lignes) ---
    for (int i = 0; i < line_count; i++) {
        display.drawLine(line_buf[i][0], line_buf[i][1],
                         line_buf[i][2], line_buf[i][3]);
    }

    // --- Labels numériques ---
    display.setFont(u8g2_font_6x10_tr);
    for (int i = 0; i < text_count; i++) {
        itoa(text_buf[i][2], str_buf, 10);
        int sw = display.getStrWidth(str_buf);
        display.drawStr(text_buf[i][0] - sw / 2,
                        text_buf[i][1],
                        str_buf);
    }

    // --- Valeur centrale : durée sélectionnée ---
    display.setFont(u8g2_font_8x13_tr);
    snprintf(str_buf, sizeof(str_buf), "%d min", duration_minutes);
    int sw = display.getStrWidth(str_buf);

    display.setDrawColor(1);
    display.drawRBox(CENTER_X - (sw + 6) / 2, 0, sw + 6, 13, 2);   // fond arrondi
    display.drawTriangle(CENTER_X - 4, 13,
                         CENTER_X + 4, 13,
                         CENTER_X,     17);                          // petite flèche vers la roue

    display.setDrawColor(0);   // texte en noir sur fond blanc
    display.drawStr(CENTER_X - sw / 2, 11, str_buf);
    display.setDrawColor(1);

    // --- Heure de fin ---
    display.setFont(u8g2_font_profont10_tr);
    snprintf(str_buf, sizeof(str_buf), "-> %dh%02d", end_hour, end_minute);
    sw = display.getStrWidth(str_buf);
    display.drawStr(CENTER_X - sw / 2, 28, str_buf);   // sous la flèche, centré

    display.sendBuffer();
}
