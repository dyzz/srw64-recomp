// The GL headers: vitaGL on the Vita, the system's OpenGL 2.1 on the desktop (macOS keeps
// the legacy profile; Linux takes Mesa's prototypes). Everything in src/gl uses only what
// OpenGL 2.1 and OpenGL ES 2.0 have in common, with framebuffer objects.
#pragma once
#if defined(__vita__)
#include <vitaGL.h>
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
