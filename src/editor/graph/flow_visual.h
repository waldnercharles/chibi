#ifndef CBLUEPRINT_FLOW_VISUAL_H
#define CBLUEPRINT_FLOW_VISUAL_H

/* Host-independent UE-style flow envelope. The editor may sample this from
 * elapsed time; it never depends on frame count or evaluator callbacks. */
double flow_visual_heat(double age_seconds);
double flow_visual_marker(double age_seconds, double edge_length);
int flow_visual_settled(double age_seconds);

#endif
