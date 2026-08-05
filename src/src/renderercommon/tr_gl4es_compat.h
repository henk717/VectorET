/*
 * ET: Legacy
 * Copyright (C) 2012-2026 ET:Legacy team <mail@etlegacy.com>
 *
 * This file is part of ET: Legacy - http://www.etlegacy.com
 *
 * ET: Legacy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ET: Legacy is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ET: Legacy. If not, see <http://www.gnu.org/licenses/>.
 */
/**
 * @file tr_gl4es_compat.h
 * @brief GLEW replacement for the Emscripten/WebGL build.
 *
 * On Emscripten the vanilla GL1.x renderer runs on top of gl4es, which
 * translates desktop GL to the GLES2 calls Emscripten maps to WebGL2.
 * gl4es' own headers mangle every gl* name to gl4es_gl* at compile time
 * (USE_MGL_NAMESPACE), so no runtime extension-pointer loading (GLEW) is
 * needed or wanted. This header:
 *
 *  - pulls in the mangled gl4es GL headers
 *  - maps the ARB/EXT-suffixed entry points the renderer uses onto the
 *    core gl4es implementations (gl4es only exports core names)
 *  - provides the handful of GLEW_* feature flags and glew* functions the
 *    renderer references, as compile-time constants and inline stubs
 */
#ifndef INCLUDE_TR_GL4ES_COMPAT_H
#define INCLUDE_TR_GL4ES_COMPAT_H

#ifndef __EMSCRIPTEN__
#error "tr_gl4es_compat.h is only for Emscripten builds"
#endif

#include <GL/gl.h>
#include <GL/glext.h>

/* not in the gl4es public headers, but exported from the lib */
GLAPI void APIENTRY gl4es_glLockArrays(GLint first, GLsizei count);
GLAPI void APIENTRY gl4es_glUnlockArrays(void);

/* compiled vertex arrays are an optimization hint only; gl4es' emulation of
 * them misbehaves on the WebGL backend, so make them no-ops */
static ID_INLINE void etl_glLockArraysNoop(GLint first, GLsizei count)
{
	(void)first;
	(void)count;
}
static ID_INLINE void etl_glUnlockArraysNoop(void)
{
}

/* --- suffixed entry points -> core gl4es implementations -------------- */

#undef glActiveTextureARB
#undef glClientActiveTextureARB
#undef glLockArraysEXT
#undef glUnlockArraysEXT
#define glActiveTextureARB       gl4es_glActiveTexture
#define glClientActiveTextureARB gl4es_glClientActiveTexture
#define glLockArraysEXT          gl4es_glLockArrays
#define glUnlockArraysEXT        gl4es_glUnlockArrays

#undef glBindFramebufferEXT
#undef glBindRenderbufferEXT
#undef glCheckFramebufferStatusEXT
#undef glDeleteFramebuffersEXT
#undef glDeleteRenderbuffersEXT
#undef glFramebufferRenderbufferEXT
#undef glFramebufferTexture2DEXT
#undef glGenFramebuffersEXT
#undef glGenRenderbuffersEXT
#undef glRenderbufferStorageEXT
#define glBindFramebufferEXT         gl4es_glBindFramebuffer
#define glBindRenderbufferEXT        gl4es_glBindRenderbuffer
#define glCheckFramebufferStatusEXT  gl4es_glCheckFramebufferStatus
#define glDeleteFramebuffersEXT      gl4es_glDeleteFramebuffers
#define glDeleteRenderbuffersEXT     gl4es_glDeleteRenderbuffers
#define glFramebufferRenderbufferEXT gl4es_glFramebufferRenderbuffer
#define glFramebufferTexture2DEXT    gl4es_glFramebufferTexture2D
#define glGenFramebuffersEXT         gl4es_glGenFramebuffers
#define glGenRenderbuffersEXT        gl4es_glGenRenderbuffers
#define glRenderbufferStorageEXT     gl4es_glRenderbufferStorage

/* multisample renderbuffers don't exist on GLES2; callers are gated by
 * GLEW_EXT_framebuffer_multisample == 0 below, this keeps the link happy */
#undef glRenderbufferStorageMultisampleEXT
#define glRenderbufferStorageMultisampleEXT etl_glRenderbufferStorageMultisampleEXT
static ID_INLINE void etl_glRenderbufferStorageMultisampleEXT(GLenum target, GLsizei samples,
                                                          GLenum internalformat, GLsizei width, GLsizei height)
{
	(void)samples;
	gl4es_glRenderbufferStorage(target, internalformat, width, height);
}

/* --- ARB shader-object API -> core GLSL API --------------------------- */

#undef glCreateProgramObjectARB
#undef glCreateShaderObjectARB
#undef glAttachObjectARB
#undef glDetachObjectARB
#undef glShaderSourceARB
#undef glCompileShaderARB
#undef glLinkProgramARB
#undef glUseProgramObjectARB
#define glCreateProgramObjectARB gl4es_glCreateProgram
#define glCreateShaderObjectARB  gl4es_glCreateShader
#define glAttachObjectARB        gl4es_glAttachShader
#define glDetachObjectARB        gl4es_glDetachShader
#define glShaderSourceARB        gl4es_glShaderSource
#define glCompileShaderARB       gl4es_glCompileShader
#define glLinkProgramARB         gl4es_glLinkProgram
#define glUseProgramObjectARB    gl4es_glUseProgram

/* the object API is ambiguous between shaders and programs; dispatch */
#undef glDeleteObjectARB
#define glDeleteObjectARB etl_glDeleteObjectARB
static ID_INLINE void etl_glDeleteObjectARB(GLhandleARB obj)
{
	if (gl4es_glIsShader((GLuint)obj))
	{
		gl4es_glDeleteShader((GLuint)obj);
	}
	else
	{
		gl4es_glDeleteProgram((GLuint)obj);
	}
}

#undef glGetObjectParameterivARB
#define glGetObjectParameterivARB etl_glGetObjectParameterivARB
static ID_INLINE void etl_glGetObjectParameterivARB(GLhandleARB obj, GLenum pname, GLint *params)
{
	GLenum corePname = pname;

	/* translate the ARB object enums to their core equivalents */
	switch (pname)
	{
	case 0x8B81: /* GL_OBJECT_COMPILE_STATUS_ARB == GL_COMPILE_STATUS */
	case 0x8B82: /* GL_OBJECT_LINK_STATUS_ARB == GL_LINK_STATUS */
	case 0x8B84: /* GL_OBJECT_INFO_LOG_LENGTH_ARB == GL_INFO_LOG_LENGTH */
	default:
		break;
	}

	if (gl4es_glIsShader((GLuint)obj))
	{
		gl4es_glGetShaderiv((GLuint)obj, corePname, params);
	}
	else
	{
		gl4es_glGetProgramiv((GLuint)obj, corePname, params);
	}
}

#undef glGetInfoLogARB
#define glGetInfoLogARB etl_glGetInfoLogARB
static ID_INLINE void etl_glGetInfoLogARB(GLhandleARB obj, GLsizei maxLength, GLsizei *length, GLcharARB *infoLog)
{
	if (gl4es_glIsShader((GLuint)obj))
	{
		gl4es_glGetShaderInfoLog((GLuint)obj, maxLength, length, infoLog);
	}
	else
	{
		gl4es_glGetProgramInfoLog((GLuint)obj, maxLength, length, infoLog);
	}
}

/* --- misc stubs -------------------------------------------------------- */

#undef glDebugMessageCallbackARB
#define glDebugMessageCallbackARB etl_glDebugMessageCallbackARB
static ID_INLINE void etl_glDebugMessageCallbackARB(void *callback, const void *userParam)
{
	(void)callback;
	(void)userParam;
}

#undef glGetQueryivARB
#define glGetQueryivARB etl_glGetQueryivARB
static ID_INLINE void etl_glGetQueryivARB(GLenum target, GLenum pname, GLint *params)
{
	(void)target;
	(void)pname;
	*params = 0;
}

/* --- GLEW emulation ----------------------------------------------------- */

#define GLEW_OK                    0
#define GLEW_ERROR_NO_GLX_DISPLAY  4
#define GLEW_VERSION               1

/* capabilities provided by gl4es on top of WebGL2 */
#define GLEW_ARB_multitexture              1
#define GLEW_EXT_texture_env_add           1
#define GLEW_ARB_texture_non_power_of_two  1
#define GLEW_ARB_texture_compression       1
#define GLEW_ARB_framebuffer_object        1

/* not available / not wanted in the browser build */
#define GLEW_EXT_texture_compression_s3tc  0
#define GLEW_S3_s3tc                       0
#define GLEW_EXT_texture_filter_anisotropic 0
#define GLEW_ARB_fragment_program          0
#define GLEW_EXT_framebuffer_multisample   0

static GLboolean glewExperimental = GL_FALSE;

static ID_INLINE GLenum glewInit(void)
{
	(void)glewExperimental;
	return GLEW_OK;
}

static ID_INLINE GLboolean glewIsSupported(const char *name)
{
	(void)name;
	return GL_FALSE;
}

static ID_INLINE const GLubyte *glewGetString(GLenum name)
{
	(void)name;
	return (const GLubyte *)"gl4es";
}

static ID_INLINE const GLubyte *glewGetErrorString(GLenum error)
{
	(void)error;
	return (const GLubyte *)"gl4es: no error";
}

#endif // INCLUDE_TR_GL4ES_COMPAT_H
