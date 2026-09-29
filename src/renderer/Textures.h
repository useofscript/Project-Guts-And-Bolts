#pragma once
#include <string>

// Pictures on the graphics card (decals, and previews on the site / in Studio).
//
// An image id is "gb:<asset id>" (a decal uploaded to a Guts&Bolts server,
// once downloaded), a file in the games folder ("images/door.png"), or a full
// path. PNG, JPG, BMP, TGA and GIF (first frame) work.
namespace Textures {

// The GL texture for an image, loading it the first time (0 = not there yet,
// or not a picture). Cheap to call every frame.
unsigned get(const std::string& id);
bool     size(const std::string& id, int& width, int& height);
// Forget one image (e.g. a file that was just replaced) or all of them.
void     forget(const std::string& id);
void     clear();   // before the GL context goes away

} // namespace Textures
