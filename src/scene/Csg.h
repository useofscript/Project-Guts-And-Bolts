#pragma once
#include <vector>
#include <glm/glm.hpp>

// Solid modeling, like Roblox Studio's Union / Negate / Intersect: join shapes
// together, cut one out of another, or keep only where they overlap.
//
// Shapes are lists of flat, convex polygons (corners counter-clockwise seen
// from outside) that close up into a solid. It works by sorting polygons into
// a BSP tree (each polygon's plane splits space into "in front" and "behind"),
// then throwing away the pieces of each shape that end up inside the other -
// the same way the well-known csg.js library does it.
namespace Csg {

struct Polygon {
    std::vector<glm::dvec3> verts;
};
using Solid = std::vector<Polygon>;

Solid unite(const Solid& a, const Solid& b);       // everything in a or b
Solid subtract(const Solid& a, const Solid& b);    // a with b cut out of it
Solid intersect(const Solid& a, const Solid& b);   // only where a and b overlap

} // namespace Csg
