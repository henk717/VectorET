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
 * @file sdl_gl4es_procs.c
 * @brief GLES2 proc address resolver for gl4es on Emscripten.
 *
 * Emscripten's eglGetProcAddress() resolves names through a precompiled
 * system library (system/lib/gl/webgl1.c) that is built WITHOUT the
 * FULL_ES2 client-side vertex array emulation - its glVertexAttribPointer
 * and friends pass client pointers straight to WebGL as buffer offsets.
 * The GLES2 symbols that this program links against statically, however,
 * ARE the emulated versions from the GL JS library.
 *
 * gl4es resolves its GLES backend through a getprocaddress callback, so
 * hand it the statically linked symbols instead of eglGetProcAddress.
 *
 * This file must not include any gl4es headers (they remangle gl* names).
 */

#ifdef __EMSCRIPTEN__

#include <string.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <SDL2/SDL_video.h>

#define PROC(fn) { #fn, (void *)&fn },

static const struct
{
	const char *name;
	void *func;
} etl_gles2_procs[] =
{
	PROC(glActiveTexture)
	PROC(glAttachShader)
	PROC(glBindAttribLocation)
	PROC(glBindBuffer)
	PROC(glBindFramebuffer)
	PROC(glBindRenderbuffer)
	PROC(glBindTexture)
	PROC(glBlendColor)
	PROC(glBlendEquation)
	PROC(glBlendEquationSeparate)
	PROC(glBlendFunc)
	PROC(glBlendFuncSeparate)
	PROC(glBufferData)
	PROC(glBufferSubData)
	PROC(glCheckFramebufferStatus)
	PROC(glClear)
	PROC(glClearColor)
	PROC(glClearDepthf)
	PROC(glClearStencil)
	PROC(glColorMask)
	PROC(glCompileShader)
	PROC(glCompressedTexImage2D)
	PROC(glCompressedTexSubImage2D)
	PROC(glCopyTexImage2D)
	PROC(glCopyTexSubImage2D)
	PROC(glCreateProgram)
	PROC(glCreateShader)
	PROC(glCullFace)
	PROC(glDeleteBuffers)
	PROC(glDeleteFramebuffers)
	PROC(glDeleteProgram)
	PROC(glDeleteRenderbuffers)
	PROC(glDeleteShader)
	PROC(glDeleteTextures)
	PROC(glDepthFunc)
	PROC(glDepthMask)
	PROC(glDepthRangef)
	PROC(glDetachShader)
	PROC(glDisable)
	PROC(glDisableVertexAttribArray)
	PROC(glDrawArrays)
	PROC(glDrawElements)
	PROC(glEnable)
	PROC(glEnableVertexAttribArray)
	PROC(glFinish)
	PROC(glFlush)
	PROC(glFramebufferRenderbuffer)
	PROC(glFramebufferTexture2D)
	PROC(glFrontFace)
	PROC(glGenBuffers)
	PROC(glGenerateMipmap)
	PROC(glGenFramebuffers)
	PROC(glGenRenderbuffers)
	PROC(glGenTextures)
	PROC(glGetActiveAttrib)
	PROC(glGetActiveUniform)
	PROC(glGetAttachedShaders)
	PROC(glGetAttribLocation)
	PROC(glGetBooleanv)
	PROC(glGetBufferParameteriv)
	PROC(glGetError)
	PROC(glGetFloatv)
	PROC(glGetFramebufferAttachmentParameteriv)
	PROC(glGetIntegerv)
	PROC(glGetProgramiv)
	PROC(glGetProgramInfoLog)
	PROC(glGetRenderbufferParameteriv)
	PROC(glGetShaderiv)
	PROC(glGetShaderInfoLog)
	PROC(glGetShaderPrecisionFormat)
	PROC(glGetShaderSource)
	PROC(glGetString)
	PROC(glGetTexParameterfv)
	PROC(glGetTexParameteriv)
	PROC(glGetUniformfv)
	PROC(glGetUniformiv)
	PROC(glGetUniformLocation)
	PROC(glGetVertexAttribfv)
	PROC(glGetVertexAttribiv)
	PROC(glGetVertexAttribPointerv)
	PROC(glHint)
	PROC(glIsBuffer)
	PROC(glIsEnabled)
	PROC(glIsFramebuffer)
	PROC(glIsProgram)
	PROC(glIsRenderbuffer)
	PROC(glIsShader)
	PROC(glIsTexture)
	PROC(glLineWidth)
	PROC(glLinkProgram)
	PROC(glPixelStorei)
	PROC(glPolygonOffset)
	PROC(glReadPixels)
	PROC(glReleaseShaderCompiler)
	PROC(glRenderbufferStorage)
	PROC(glSampleCoverage)
	PROC(glScissor)
	PROC(glShaderBinary)
	PROC(glShaderSource)
	PROC(glStencilFunc)
	PROC(glStencilFuncSeparate)
	PROC(glStencilMask)
	PROC(glStencilMaskSeparate)
	PROC(glStencilOp)
	PROC(glStencilOpSeparate)
	PROC(glTexImage2D)
	PROC(glTexParameterf)
	PROC(glTexParameterfv)
	PROC(glTexParameteri)
	PROC(glTexParameteriv)
	PROC(glTexSubImage2D)
	PROC(glUniform1f)
	PROC(glUniform1fv)
	PROC(glUniform1i)
	PROC(glUniform1iv)
	PROC(glUniform2f)
	PROC(glUniform2fv)
	PROC(glUniform2i)
	PROC(glUniform2iv)
	PROC(glUniform3f)
	PROC(glUniform3fv)
	PROC(glUniform3i)
	PROC(glUniform3iv)
	PROC(glUniform4f)
	PROC(glUniform4fv)
	PROC(glUniform4i)
	PROC(glUniform4iv)
	PROC(glUniformMatrix2fv)
	PROC(glUniformMatrix3fv)
	PROC(glUniformMatrix4fv)
	PROC(glUseProgram)
	PROC(glValidateProgram)
	PROC(glVertexAttrib1f)
	PROC(glVertexAttrib1fv)
	PROC(glVertexAttrib2f)
	PROC(glVertexAttrib2fv)
	PROC(glVertexAttrib3f)
	PROC(glVertexAttrib3fv)
	PROC(glVertexAttrib4f)
	PROC(glVertexAttrib4fv)
	PROC(glVertexAttribPointer)
	PROC(glViewport)
	{ NULL, NULL }
};

#undef PROC

void *ETL_GLES2_GetProcAddress(const char *name)
{
	int i;

	for (i = 0; etl_gles2_procs[i].name; i++)
	{
		if (!strcmp(name, etl_gles2_procs[i].name))
		{
			return etl_gles2_procs[i].func;
		}
	}

	// extension entry points etc. - let SDL/EGL resolve (or return NULL)
	return SDL_GL_GetProcAddress(name);
}

#endif // __EMSCRIPTEN__
