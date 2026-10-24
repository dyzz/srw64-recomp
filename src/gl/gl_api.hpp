// The GL headers: vitaGL on the Vita, Mesa's OpenGL ES 2 on the Switch, the system's OpenGL
// 2.1 on the desktop (macOS keeps the legacy profile; Linux takes Mesa's prototypes). Everything in src/gl uses only what
// OpenGL 2.1 and OpenGL ES 2.0 have in common, with framebuffer objects.
#pragma once
#if defined(__vita__)
#include <vitaGL.h>
#define SRW64_GLES 1
#elif defined(__SWITCH__)
// GLES 3 headers over the ES 2 subset the renderer uses: Mesa there gives an ES 3.2 context,
// and the program cache needs ES 3's program binaries.
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#define SRW64_GLES 1
#elif defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif
