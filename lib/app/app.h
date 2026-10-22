#pragma once

// Entry points for each firmware phase; src/main.cpp picks one via FW_PHASE.
// Earlier phases stay buildable as bring-up diagnostics.

void phase1_setup();
void phase1_loop();

void phase2_setup();
void phase2_loop();

void phase3_setup();
void phase3_loop();

void phase4_setup();
void phase4_loop();
