#pragma once

// Builds the driver configurations from app_config.h and programs both ICs.
// Shared by Phases 2-4 so every phase runs identical register settings.

constexpr unsigned kBlinkPpgConfigFail = 2;
constexpr unsigned kBlinkEcgConfigFail = 3;

bool sensors_configure_ecg();
bool sensors_configure_ppg();

// Initialize both buses and configure both sensors; on failure prints the
// reason to Serial and halts with the matching blink code.
void sensors_bring_up_or_halt();
