// Partagé par les dispositions portrait (screen.cpp) et paysage (screen_wide.cpp).
#pragma once
#include <stdio.h>
#include "board_config.h"
#include "draw.h"
#include "gfx.h"
#include "images.h"
#include "screen.h"

namespace {
const HpImage& mascot(bool rabbit, hpui::Mood mood) {
  static const HpImage* const pictures[2][4] = {
      {&img_fox_bravo, &img_fox_content, &img_fox_think, &img_fox_comfort},
      {&img_rabbit_bravo, &img_rabbit_content, &img_rabbit_think, &img_rabbit_comfort}};
  return *pictures[rabbit][mood];
}
const HpImage& skyIcon(hpui::Sky sky) {
  static const HpImage* const pictures[] = {&img_sky_sun, &img_sky_partly, &img_sky_cloud, &img_sky_fog,
                                            &img_sky_rain, &img_sky_snow, &img_sky_storm};
  return *pictures[sky];
}
void clockText(char* out, size_t size, int minute) { snprintf(out, size, "%02d:%02d", (minute / 60) % 24, minute % 60); }
}  // namespace
