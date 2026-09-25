#include "editor/graph/flow_visual.h"

#include "support/math.h"

#include <math.h>

#define FLOW_ATTACK_HOLD 0.3
#define FLOW_ATTACK_DECAY 0.4
#define FLOW_SUSTAIN_HOLD 0.4
#define FLOW_RELEASE 1.5

double flow_visual_heat(double age_seconds)
{
    double attack = math_clamp01(1.0 - (age_seconds - FLOW_ATTACK_HOLD) / FLOW_ATTACK_DECAY);
    double sustain_start = FLOW_ATTACK_HOLD + FLOW_ATTACK_DECAY + FLOW_SUSTAIN_HOLD;
    double release = math_clamp01(1.0 - (age_seconds - sustain_start) / FLOW_RELEASE);
    return fmax(math_pow(attack, 1.8), math_pow(release, 1.4));
}
