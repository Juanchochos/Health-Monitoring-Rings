#include <Arduino.h>

#include "app.h"

#ifndef FW_PHASE
#define FW_PHASE 4
#endif

void setup()
{
#if FW_PHASE == 1
  phase1_setup();
#elif FW_PHASE == 2
  phase2_setup();
#elif FW_PHASE == 3
  phase3_setup();
#elif FW_PHASE == 4
  phase4_setup();
#else
#error "FW_PHASE must be 1, 2, 3 or 4"
#endif
}

void loop()
{
#if FW_PHASE == 1
  phase1_loop();
#elif FW_PHASE == 2
  phase2_loop();
#elif FW_PHASE == 3
  phase3_loop();
#else
  phase4_loop();
#endif
}
