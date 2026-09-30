#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include "SceneNode.h"

// A shape you build yourself in Studio's Modeling mode (like Blender's Edit
// Mode): a list of corner points ("vertices") and the flat sides ("faces")
// between them. Each face lists its vertices counter-clockwise as seen from
// outside, so its front is the outside.
//
// The vertices always fit inside the part's box (-0.5..0.5 on each axis),
// like a Roblox MeshPart: after an edit, fit() grows or shrinks the part so
// the box wraps the new shape. That keeps physics, picking and resizing
// working with no special cases.
struct EditMesh {
    std::vector<glm::vec3>             verts;
    std::vector<std::vector<uint32_t>> faces;
    bool                               smooth = false;   // smooth shading (no hard edges)
    // Optional texture coordinates, one per corner of each face (the character's
    // clothing layout). Empty, or out of step with `faces` after an edit = box mapping.
    std::vector<std::vector<glm::vec2>> uvs;
};

namespace MeshEdit {

using Selection = std::vector<char>;   // one flag per vertex

// A mesh shaped like a built-in part (Cube, Sphere, Cylinder or Plane).
std::shared_ptr<EditMesh> fromPrimitive(PrimitiveType type);
// Turn `node` into a custom-mesh part using `mesh`.
void attach(SceneNode& node, std::shared_ptr<EditMesh> mesh);
// Rebuild what the GPU draws after the mesh changed.
void refresh(SceneNode& node);
// Wrap the part's box around the mesh again (keeps everything where it is in the world).
void fit(SceneNode& node);
// Make sure this node has its own copy of the mesh before changing it.
EditMesh& own(SceneNode& node);

glm::vec3 faceNormal(const EditMesh& m, const std::vector<uint32_t>& face);   // unit length
glm::vec3 faceCenter(const EditMesh& m, const std::vector<uint32_t>& face);
std::vector<std::pair<uint32_t, uint32_t>> edges(const EditMesh& m);        // each edge once (a < b)
std::vector<int> selectedFaces(const EditMesh& m, const Selection& sel);   // all corners selected
int  countSelected(const Selection& sel);

// Editing tools. Each works on the selection and leaves the result selected.
bool extrude(EditMesh& m, Selection& sel);          // pull faces (or edges) out; new copies stay in place
bool inset(EditMesh& m, Selection& sel, float t);   // a smaller face inside each selected face
bool subdivide(EditMesh& m, Selection& sel);        // split selected faces (all faces if none)
bool remove(EditMesh& m, Selection& sel, int what); // what: 0 vertices, 1 edges, 2 faces
bool merge(EditMesh& m, Selection& sel);            // selected vertices -> one in the middle
bool fill(EditMesh& m, Selection& sel);             // make a face from the selected vertices
bool flip(EditMesh& m, const Selection& sel);       // turn selected faces inside out (all if none)

// Drop vertices no face uses (keeps `sel` in step).
void compact(EditMesh& m, Selection& sel);

} // namespace MeshEdit
