#ifndef CHIBI_ARRANGE_PATH_H
#define CHIBI_ARRANGE_PATH_H

#include "support/math.h"

/* Wire shapes: pure geometry from a wire's pins and waypoints to the cubic
 * pieces renderers draw. Arrange routes and scores wires in the same
 * shapes. */

/* How wires are drawn between their waypoints. */
typedef enum arrange_route_style
{
    /* Cubics that cross every waypoint horizontally. */
    ARRANGE_ROUTE_CURVED,
    /* Horizontal and vertical runs with rounded corners. Each vertical run
     * sits on its own track in the gap between two columns, so parallel runs
     * never overlap. */
    ARRANGE_ROUTE_ORTHOGONAL,
    /* Line segments between waypoints, leaving and entering pins level. */
    ARRANGE_ROUTE_STRAIGHT,
    /* The orthogonal tracks with 45 degree diagonals in place of corners. */
    ARRANGE_ROUTE_SUBWAY,
    ARRANGE_ROUTE_STYLE_COUNT,
} arrange_route_style;

/* Positions use the shared scalar vector type while retaining a domain name
 * at the arrange API boundary. */
typedef vec2 arrange_point;

/* One drawable piece of a wire: a cubic Bezier. Straight pieces keep their
 * handles on their ends (p1 == p0, p2 == p3). `segment` is the stretch of the
 * wire the piece lies on: 0 runs from the start to the first waypoint. */
typedef struct arrange_piece
{
    arrange_point p0;
    arrange_point p1;
    arrange_point p2;
    arrange_point p3;
    int segment;
} arrange_piece;

/* Appends the pieces that draw one wire from its output pin `from` to its
 * input pin `to` through `count` waypoints, in canvas units: arrange_route's
 * waypoints or authored reroutes. Consecutive pieces join end to start. Every
 * renderer and arrange_measure draw wires with this, so what is measured is
 * what is drawn. A link with custom handles draws as its single authored
 * cubic in every style instead. */
void arrange_path(
    arrange_route_style style,
    arrange_point from,
    arrange_point to,
    const arrange_point *knots,
    int count,
    arrange_piece **pieces
);

#endif
