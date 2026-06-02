#pragma once
#include <Arduino.h>

void duell_init();
void duell_update(float local_goal);

int duell_get_peers_count();
bool duell_is_active();

// Sender functions
void duell_send_ready();
void duell_send_result(float drank_weight);

// State getters
bool duell_has_start_signal(float* out_target_weight);
bool duell_has_ranking(int* out_rank);

// Internal state reset (e.g. after a game)
void duell_reset_state();
