#pragma once
#include <stdint.h>
#include "ui_model.h"

// ── Renderer ──────────────────────────────────────────────────────────────────
// Zeichnet ein ui::Frame nur, wenn es sich geaendert hat, hoechstens alle
// MIN_FRAME_MS. Spart I2C-Last und haelt den Loop schnell.

void ui_begin(uint8_t rotation);
void ui_setRotation(uint8_t rotation);
void ui_render(const ui::Frame &f, uint32_t now);
void ui_force(const char *l1, const char *l2);  // sofort (blockierende Ablaeufe, UTF-8)
void ui_off();                                  // Display aus (Deep-Sleep)
