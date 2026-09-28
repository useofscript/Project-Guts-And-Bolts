#pragma once

// OpenGL for computers (4.1 core, through GLEW) or OpenGL ES 3 for phones.
// The renderer only uses the parts both have in common.
#if defined(GB_GLES)
#include <GLES3/gl3.h>
#else
#include <GL/glew.h>
#endif
