#if !defined(ROCKET_OPENGL_ES)
#define GLAD_GL_IMPLEMENTATION
#endif
#include "render/OpenGlApi.h"

namespace rocket {

bool loadDesktopOpenGl(OpenGlProcLoader loader)
{
#if defined(ROCKET_OPENGL_ES)
    (void)loader;
    return true;
#else
    return loader != nullptr && gladLoadGL(reinterpret_cast<GLADloadfunc>(loader)) != 0;
#endif
}

} // namespace rocket
