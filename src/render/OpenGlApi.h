#pragma once

#if defined(ROCKET_OPENGL_ES)
#include <GLES3/gl3.h>
#else
#include "RmlUi_Include_GL3.h"
#endif

namespace rocket {

using OpenGlProcAddress = void (*)(void);
using OpenGlProcLoader = OpenGlProcAddress (*)(const char* name);

bool loadDesktopOpenGl(OpenGlProcLoader loader);

} // namespace rocket
