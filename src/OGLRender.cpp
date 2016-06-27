/*
	Copyright (C) 2006 yopyop
	Copyright (C) 2006-2007 shash
	Copyright (C) 2008-2016 DeSmuME team

	This file is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	This file is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with the this software.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "OGLRender.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>

#include "common.h"
#include "debug.h"
#include "gfx3d.h"
#include "NDSSystem.h"
#include "texcache.h"

#ifdef ENABLE_SSE2
#include <emmintrin.h>
#endif

typedef struct
{
	unsigned int major;
	unsigned int minor;
	unsigned int revision;
} OGLVersion;

static OGLVersion _OGLDriverVersion = {0, 0, 0};

// Lookup Tables
static CACHE_ALIGN GLfloat material_8bit_to_float[256] = {0};
CACHE_ALIGN const GLfloat divide5bitBy31_LUT[32]	= {0.0,             0.0322580645161, 0.0645161290323, 0.0967741935484,
													   0.1290322580645, 0.1612903225806, 0.1935483870968, 0.2258064516129,
													   0.2580645161290, 0.2903225806452, 0.3225806451613, 0.3548387096774,
													   0.3870967741935, 0.4193548387097, 0.4516129032258, 0.4838709677419,
													   0.5161290322581, 0.5483870967742, 0.5806451612903, 0.6129032258065,
													   0.6451612903226, 0.6774193548387, 0.7096774193548, 0.7419354838710,
													   0.7741935483871, 0.8064516129032, 0.8387096774194, 0.8709677419355,
													   0.9032258064516, 0.9354838709677, 0.9677419354839, 1.0};


CACHE_ALIGN const GLfloat divide6bitBy63_LUT[64]	= {0.0,             0.0158730158730, 0.0317460317460, 0.0476190476191,
													   0.0634920634921, 0.0793650793651, 0.0952380952381, 0.1111111111111,
													   0.1269841269841, 0.1428571428571, 0.1587301587302, 0.1746031746032,
													   0.1904761904762, 0.2063492063492, 0.2222222222222, 0.2380952380952,
													   0.2539682539683, 0.2698412698413, 0.2857142857143, 0.3015873015873,
													   0.3174603174603, 0.3333333333333, 0.3492063492064, 0.3650793650794,
													   0.3809523809524, 0.3968253968254, 0.4126984126984, 0.4285714285714,
													   0.4444444444444, 0.4603174603175, 0.4761904761905, 0.4920634920635,
													   0.5079365079365, 0.5238095238095, 0.5396825396825, 0.5555555555556,
													   0.5714285714286, 0.5873015873016, 0.6031746031746, 0.6190476190476,
													   0.6349206349206, 0.6507936507937, 0.6666666666667, 0.6825396825397,
													   0.6984126984127, 0.7142857142857, 0.7301587301587, 0.7460317460318,
													   0.7619047619048, 0.7777777777778, 0.7936507936508, 0.8095238095238,
													   0.8253968253968, 0.8412698412698, 0.8571428571429, 0.8730158730159,
													   0.8888888888889, 0.9047619047619, 0.9206349206349, 0.9365079365079,
													   0.9523809523810, 0.9682539682540, 0.9841269841270, 1.0};

const GLfloat PostprocessVtxBuffer[16]	= {-1.0f, -1.0f,  1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f,
										    0.0f,  0.0f,  1.0f,  0.0f,  1.0f,  1.0f,  0.0f,  1.0f};
const GLubyte PostprocessElementBuffer[6] = {0, 1, 2, 2, 3, 0};

const GLenum RenderDrawList[4] = {GL_COLOR_ATTACHMENT0_EXT, GL_COLOR_ATTACHMENT1_EXT, GL_COLOR_ATTACHMENT2_EXT, GL_COLOR_ATTACHMENT3_EXT};

bool BEGINGL()
{
	if(oglrender_beginOpenGL) 
		return oglrender_beginOpenGL();
	else return true;
}

void ENDGL()
{
	if(oglrender_endOpenGL) 
		oglrender_endOpenGL();
}

// Function Pointers
bool (*oglrender_init)() = NULL;
bool (*oglrender_beginOpenGL)() = NULL;
void (*oglrender_endOpenGL)() = NULL;
bool (*oglrender_framebufferDidResizeCallback)(size_t w, size_t h) = NULL;
void (*OGLLoadEntryPoints_3_2_Func)() = NULL;
void (*OGLCreateRenderer_3_2_Func)(OpenGLRenderer **rendererPtr) = NULL;

//------------------------------------------------------------

// Textures
#if !defined(GLX_H)
OGLEXT(PFNGLACTIVETEXTUREPROC, glActiveTexture) // Core in v1.3
OGLEXT(PFNGLACTIVETEXTUREARBPROC, glActiveTextureARB)
#endif

// Blending
OGLEXT(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate) // Core in v1.4
OGLEXT(PFNGLBLENDEQUATIONSEPARATEPROC, glBlendEquationSeparate) // Core in v2.0

OGLEXT(PFNGLBLENDFUNCSEPARATEEXTPROC, glBlendFuncSeparateEXT)
OGLEXT(PFNGLBLENDEQUATIONSEPARATEEXTPROC, glBlendEquationSeparateEXT)

// Shaders
OGLEXT(PFNGLCREATESHADERPROC, glCreateShader) // Core in v2.0
OGLEXT(PFNGLSHADERSOURCEPROC, glShaderSource) // Core in v2.0
OGLEXT(PFNGLCOMPILESHADERPROC, glCompileShader) // Core in v2.0
OGLEXT(PFNGLCREATEPROGRAMPROC, glCreateProgram) // Core in v2.0
OGLEXT(PFNGLATTACHSHADERPROC, glAttachShader) // Core in v2.0
OGLEXT(PFNGLDETACHSHADERPROC, glDetachShader) // Core in v2.0
OGLEXT(PFNGLLINKPROGRAMPROC, glLinkProgram) // Core in v2.0
OGLEXT(PFNGLUSEPROGRAMPROC, glUseProgram) // Core in v2.0
OGLEXT(PFNGLGETSHADERIVPROC, glGetShaderiv) // Core in v2.0
OGLEXT(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog) // Core in v2.0
OGLEXT(PFNGLDELETESHADERPROC, glDeleteShader) // Core in v2.0
OGLEXT(PFNGLDELETEPROGRAMPROC, glDeleteProgram) // Core in v2.0
OGLEXT(PFNGLGETPROGRAMIVPROC, glGetProgramiv) // Core in v2.0
OGLEXT(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog) // Core in v2.0
OGLEXT(PFNGLVALIDATEPROGRAMPROC, glValidateProgram) // Core in v2.0
OGLEXT(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation) // Core in v2.0
OGLEXT(PFNGLUNIFORM1IPROC, glUniform1i) // Core in v2.0
OGLEXT(PFNGLUNIFORM1IVPROC, glUniform1iv) // Core in v2.0
OGLEXT(PFNGLUNIFORM1FPROC, glUniform1f) // Core in v2.0
OGLEXT(PFNGLUNIFORM1FVPROC, glUniform1fv) // Core in v2.0
OGLEXT(PFNGLUNIFORM2FPROC, glUniform2f) // Core in v2.0
OGLEXT(PFNGLUNIFORM4FPROC, glUniform4f) // Core in v2.0
OGLEXT(PFNGLUNIFORM4FVPROC, glUniform4fv) // Core in v2.0
OGLEXT(PFNGLDRAWBUFFERSPROC, glDrawBuffers) // Core in v2.0
OGLEXT(PFNGLBINDATTRIBLOCATIONPROC, glBindAttribLocation) // Core in v2.0
OGLEXT(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray) // Core in v2.0
OGLEXT(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray) // Core in v2.0
OGLEXT(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer) // Core in v2.0

// VAO
OGLEXT(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)
OGLEXT(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays)
OGLEXT(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)

// Buffer Objects
OGLEXT(PFNGLGENBUFFERSARBPROC, glGenBuffersARB)
OGLEXT(PFNGLDELETEBUFFERSARBPROC, glDeleteBuffersARB)
OGLEXT(PFNGLBINDBUFFERARBPROC, glBindBufferARB)
OGLEXT(PFNGLBUFFERDATAARBPROC, glBufferDataARB)
OGLEXT(PFNGLBUFFERSUBDATAARBPROC, glBufferSubDataARB)
OGLEXT(PFNGLMAPBUFFERARBPROC, glMapBufferARB)
OGLEXT(PFNGLUNMAPBUFFERARBPROC, glUnmapBufferARB)

OGLEXT(PFNGLGENBUFFERSPROC, glGenBuffers) // Core in v1.5
OGLEXT(PFNGLDELETEBUFFERSPROC, glDeleteBuffers) // Core in v1.5
OGLEXT(PFNGLBINDBUFFERPROC, glBindBuffer) // Core in v1.5
OGLEXT(PFNGLBUFFERDATAPROC, glBufferData) // Core in v1.5
OGLEXT(PFNGLBUFFERSUBDATAPROC, glBufferSubData) // Core in v1.5
OGLEXT(PFNGLMAPBUFFERPROC, glMapBuffer) // Core in v1.5
OGLEXT(PFNGLUNMAPBUFFERPROC, glUnmapBuffer) // Core in v1.5

// FBO
OGLEXT(PFNGLGENFRAMEBUFFERSEXTPROC, glGenFramebuffersEXT)
OGLEXT(PFNGLBINDFRAMEBUFFEREXTPROC, glBindFramebufferEXT)
OGLEXT(PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC, glFramebufferRenderbufferEXT)
OGLEXT(PFNGLFRAMEBUFFERTEXTURE2DEXTPROC, glFramebufferTexture2DEXT)
OGLEXT(PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC, glCheckFramebufferStatusEXT)
OGLEXT(PFNGLDELETEFRAMEBUFFERSEXTPROC, glDeleteFramebuffersEXT)
OGLEXT(PFNGLBLITFRAMEBUFFEREXTPROC, glBlitFramebufferEXT)
OGLEXT(PFNGLGENRENDERBUFFERSEXTPROC, glGenRenderbuffersEXT)
OGLEXT(PFNGLBINDRENDERBUFFEREXTPROC, glBindRenderbufferEXT)
OGLEXT(PFNGLRENDERBUFFERSTORAGEEXTPROC, glRenderbufferStorageEXT)
OGLEXT(PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC, glRenderbufferStorageMultisampleEXT)
OGLEXT(PFNGLDELETERENDERBUFFERSEXTPROC, glDeleteRenderbuffersEXT)

static void OGLLoadEntryPoints_Legacy()
{
	// Textures
	#if !defined(GLX_H)
	INITOGLEXT(PFNGLACTIVETEXTUREPROC, glActiveTexture) // Core in v1.3
	INITOGLEXT(PFNGLACTIVETEXTUREARBPROC, glActiveTextureARB)
	#endif

	// Blending
	INITOGLEXT(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate) // Core in v1.4
	INITOGLEXT(PFNGLBLENDEQUATIONSEPARATEPROC, glBlendEquationSeparate) // Core in v2.0

	INITOGLEXT(PFNGLBLENDFUNCSEPARATEEXTPROC, glBlendFuncSeparateEXT)
	INITOGLEXT(PFNGLBLENDEQUATIONSEPARATEEXTPROC, glBlendEquationSeparateEXT)

	// Shaders
	INITOGLEXT(PFNGLCREATESHADERPROC, glCreateShader) // Core in v2.0
	INITOGLEXT(PFNGLSHADERSOURCEPROC, glShaderSource) // Core in v2.0
	INITOGLEXT(PFNGLCOMPILESHADERPROC, glCompileShader) // Core in v2.0
	INITOGLEXT(PFNGLCREATEPROGRAMPROC, glCreateProgram) // Core in v2.0
	INITOGLEXT(PFNGLATTACHSHADERPROC, glAttachShader) // Core in v2.0
	INITOGLEXT(PFNGLDETACHSHADERPROC, glDetachShader) // Core in v2.0
	INITOGLEXT(PFNGLLINKPROGRAMPROC, glLinkProgram) // Core in v2.0
	INITOGLEXT(PFNGLUSEPROGRAMPROC, glUseProgram) // Core in v2.0
	INITOGLEXT(PFNGLGETSHADERIVPROC, glGetShaderiv) // Core in v2.0
	INITOGLEXT(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog) // Core in v2.0
	INITOGLEXT(PFNGLDELETESHADERPROC, glDeleteShader) // Core in v2.0
	INITOGLEXT(PFNGLDELETEPROGRAMPROC, glDeleteProgram) // Core in v2.0
	INITOGLEXT(PFNGLGETPROGRAMIVPROC, glGetProgramiv) // Core in v2.0
	INITOGLEXT(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog) // Core in v2.0
	INITOGLEXT(PFNGLVALIDATEPROGRAMPROC, glValidateProgram) // Core in v2.0
	INITOGLEXT(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation) // Core in v2.0
	INITOGLEXT(PFNGLUNIFORM1IPROC, glUniform1i) // Core in v2.0
	INITOGLEXT(PFNGLUNIFORM1IVPROC, glUniform1iv) // Core in v2.0
	INITOGLEXT(PFNGLUNIFORM1FPROC, glUniform1f) // Core in v2.0
	INITOGLEXT(PFNGLUNIFORM1FVPROC, glUniform1fv) // Core in v2.0
	INITOGLEXT(PFNGLUNIFORM2FPROC, glUniform2f) // Core in v2.0
	INITOGLEXT(PFNGLUNIFORM4FPROC, glUniform4f) // Core in v2.0
	INITOGLEXT(PFNGLUNIFORM4FVPROC, glUniform4fv) // Core in v2.0
	INITOGLEXT(PFNGLDRAWBUFFERSPROC, glDrawBuffers) // Core in v2.0
	INITOGLEXT(PFNGLBINDATTRIBLOCATIONPROC, glBindAttribLocation) // Core in v2.0
	INITOGLEXT(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray) // Core in v2.0
	INITOGLEXT(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray) // Core in v2.0
	INITOGLEXT(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer) // Core in v2.0

	// VAO
	INITOGLEXT(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)
	INITOGLEXT(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays)
	INITOGLEXT(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)

	// Buffer Objects
	INITOGLEXT(PFNGLGENBUFFERSARBPROC, glGenBuffersARB)
	INITOGLEXT(PFNGLDELETEBUFFERSARBPROC, glDeleteBuffersARB)
	INITOGLEXT(PFNGLBINDBUFFERARBPROC, glBindBufferARB)
	INITOGLEXT(PFNGLBUFFERDATAARBPROC, glBufferDataARB)
	INITOGLEXT(PFNGLBUFFERSUBDATAARBPROC, glBufferSubDataARB)
	INITOGLEXT(PFNGLMAPBUFFERARBPROC, glMapBufferARB)
	INITOGLEXT(PFNGLUNMAPBUFFERARBPROC, glUnmapBufferARB)

	INITOGLEXT(PFNGLGENBUFFERSPROC, glGenBuffers) // Core in v1.5
	INITOGLEXT(PFNGLDELETEBUFFERSPROC, glDeleteBuffers) // Core in v1.5
	INITOGLEXT(PFNGLBINDBUFFERPROC, glBindBuffer) // Core in v1.5
	INITOGLEXT(PFNGLBUFFERDATAPROC, glBufferData) // Core in v1.5
	INITOGLEXT(PFNGLBUFFERSUBDATAPROC, glBufferSubData) // Core in v1.5
	INITOGLEXT(PFNGLMAPBUFFERPROC, glMapBuffer) // Core in v1.5
	INITOGLEXT(PFNGLUNMAPBUFFERPROC, glUnmapBuffer) // Core in v1.5

	// FBO
	INITOGLEXT(PFNGLGENFRAMEBUFFERSEXTPROC, glGenFramebuffersEXT)
	INITOGLEXT(PFNGLBINDFRAMEBUFFEREXTPROC, glBindFramebufferEXT)
	INITOGLEXT(PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC, glFramebufferRenderbufferEXT)
	INITOGLEXT(PFNGLFRAMEBUFFERTEXTURE2DEXTPROC, glFramebufferTexture2DEXT)
	INITOGLEXT(PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC, glCheckFramebufferStatusEXT)
	INITOGLEXT(PFNGLDELETEFRAMEBUFFERSEXTPROC, glDeleteFramebuffersEXT)
	INITOGLEXT(PFNGLBLITFRAMEBUFFEREXTPROC, glBlitFramebufferEXT)
	INITOGLEXT(PFNGLGENRENDERBUFFERSEXTPROC, glGenRenderbuffersEXT)
	INITOGLEXT(PFNGLBINDRENDERBUFFEREXTPROC, glBindRenderbufferEXT)
	INITOGLEXT(PFNGLRENDERBUFFERSTORAGEEXTPROC, glRenderbufferStorageEXT)
	INITOGLEXT(PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC, glRenderbufferStorageMultisampleEXT)
	INITOGLEXT(PFNGLDELETERENDERBUFFERSEXTPROC, glDeleteRenderbuffersEXT)
}

// Vertex Shader GLSL 1.00
static const char *vertexShader_100 = {"\
	attribute vec4 inPosition; \n\
	attribute vec2 inTexCoord0; \n\
	attribute vec3 inColor; \n\
	\n\
	uniform float polyAlpha; \n\
	uniform vec2 polyTexScale; \n\
	\n\
	varying vec4 vtxPosition; \n\
	varying vec2 vtxTexCoord; \n\
	varying vec4 vtxColor; \n\
	\n\
	void main() \n\
	{ \n\
		mat2 texScaleMtx	= mat2(	vec2(polyTexScale.x,            0.0), \n\
									vec2(           0.0, polyTexScale.y)); \n\
		\n\
		vtxPosition = inPosition; \n\
		vtxTexCoord = texScaleMtx * inTexCoord0; \n\
		vtxColor = vec4(inColor * 4.0, polyAlpha); \n\
		\n\
		gl_Position = vtxPosition; \n\
	} \n\
"};

// Fragment Shader GLSL 1.00
static const char *fragmentShader_100 = {"\
	varying vec4 vtxPosition; \n\
	varying vec2 vtxTexCoord; \n\
	varying vec4 vtxColor; \n\
	\n\
	uniform sampler2D texRenderObject; \n\
	uniform sampler1D texToonTable; \n\
	\n\
	uniform int stateToonShadingMode; \n\
	uniform bool stateEnableAlphaTest; \n\
	uniform bool stateEnableAntialiasing;\n\
	uniform bool stateEnableEdgeMarking;\n\
	uniform bool stateUseWDepth; \n\
	uniform float stateAlphaTestRef; \n\
	\n\
	uniform int polyMode; \n\
	uniform bool polyEnableDepthWrite;\n\
	uniform bool polySetNewDepthForTranslucent;\n\
	uniform int polyID;\n\
	\n\
	uniform bool polyEnableTexture;\n\
	uniform bool polyEnableFog;\n\
	uniform bool texSingleBitAlpha;\n\
	\n\
	vec3 packVec3FromFloat(const float value)\n\
	{\n\
		float expandedValue = value * 16777215.0;\n\
		vec3 packedValue = vec3( fract(expandedValue/256.0), fract(((expandedValue/256.0) - fract(expandedValue/256.0)) / 256.0), fract(((expandedValue/65536.0) - fract(expandedValue/65536.0)) / 256.0) );\n\
		return packedValue;\n\
	}\n\
	\n\
	void main() \n\
	{ \n\
		vec4 mainTexColor = (polyEnableTexture) ? texture2D(texRenderObject, vtxTexCoord) : vec4(1.0, 1.0, 1.0, 1.0); \n\
		\n\
		if (texSingleBitAlpha)\n\
		{\n\
			if (mainTexColor.a < 0.500)\n\
			{\n\
				mainTexColor.a = 0.0;\n\
			}\n\
			else\n\
			{\n\
				mainTexColor.rgb = mainTexColor.rgb / mainTexColor.a;\n\
				mainTexColor.a = 1.0;\n\
			}\n\
		}\n\
		\n\
		vec4 newFragColor = mainTexColor * vtxColor; \n\
		\n\
		if(polyMode == 1) \n\
		{ \n\
			newFragColor.rgb = (polyEnableTexture) ? mix(vtxColor.rgb, mainTexColor.rgb, mainTexColor.a) : vtxColor.rgb; \n\
			newFragColor.a = vtxColor.a; \n\
		} \n\
		else if(polyMode == 2) \n\
		{ \n\
			vec3 toonColor = vec3(texture1D(texToonTable, vtxColor.r).rgb); \n\
			newFragColor.rgb = (stateToonShadingMode == 0) ? mainTexColor.rgb * toonColor.rgb : min((mainTexColor.rgb * vtxColor.r) + toonColor.rgb, 1.0); \n\
		} \n\
		else if(polyMode == 3) \n\
		{ \n\
			if (polyID != 0) \n\
			{ \n\
				newFragColor = vtxColor; \n\
			} \n\
		} \n\
		\n\
		if (newFragColor.a < 0.001 || (stateEnableAlphaTest && newFragColor.a < stateAlphaTestRef)) \n\
		{ \n\
			discard; \n\
		} \n\
		\n\
		float vertW = (vtxPosition.w == 0.0) ? 0.00000001 : vtxPosition.w; \n\
		// hack: when using z-depth, drop some LSBs so that the overworld map in Dragon Quest IV shows up correctly\n\
		float newFragDepth = (stateUseWDepth) ? vtxPosition.w/4096.0 : clamp( (floor((((vtxPosition.z/vertW) * 0.5 + 0.5) * 16777215.0) / 4.0) * 4.0) / 16777215.0, 0.0, 1.0); \n\
		\n\
		gl_FragData[0] = newFragColor;\n\
		gl_FragData[1] = vec4( packVec3FromFloat(newFragDepth), float(polyEnableDepthWrite && (newFragColor.a > 0.999 || polySetNewDepthForTranslucent)));\n\
		gl_FragData[2] = vec4(float(polyID)/63.0, 0.0, 0.0, float(newFragColor.a > 0.999));\n\
		gl_FragData[3] = vec4(float(polyEnableFog), 0.0, 0.0, float((newFragColor.a > 0.999) ? 1.0 : 0.5));\n\
		gl_FragDepth = newFragDepth;\n\
	} \n\
"};

// Vertex shader for applying edge marking, GLSL 1.00
static const char *EdgeMarkVtxShader_100 = {"\
	attribute vec2 inPosition;\n\
	attribute vec2 inTexCoord0;\n\
	uniform vec2 framebufferSize;\n\
	varying vec2 texCoord[5];\n\
	\n\
	void main()\n\
	{\n\
		vec2 texInvScale = vec2(1.0/framebufferSize.x, 1.0/framebufferSize.y);\n\
		\n\
		texCoord[0] = inTexCoord0; // Center\n\
		texCoord[1] = inTexCoord0 + (vec2( 1.0, 0.0) * texInvScale); // Right\n\
		texCoord[2] = inTexCoord0 + (vec2( 0.0, 1.0) * texInvScale); // Down\n\
		texCoord[3] = inTexCoord0 + (vec2(-1.0, 0.0) * texInvScale); // Left\n\
		texCoord[4] = inTexCoord0 + (vec2( 0.0,-1.0) * texInvScale); // Up\n\
		\n\
		gl_Position = vec4(inPosition, 0.0, 1.0);\n\
	}\n\
"};

// Fragment shader for applying edge marking, GLSL 1.00
static const char *EdgeMarkFragShader_100 = {"\
	varying vec2 texCoord[5];\n\
	\n\
	uniform sampler2D texInFragDepth;\n\
	uniform sampler2D texInPolyID;\n\
	uniform vec4 stateEdgeColor[8];\n\
	\n\
	float unpackFloatFromVec3(const vec3 value)\n\
	{\n\
		const vec3 unpackRatio = vec3(256.0, 65536.0, 16777216.0);\n\
		return (dot(value, unpackRatio) / 16777215.0);\n\
	}\n\
	\n\
	void main()\n\
	{\n\
		int polyID[5];\n\
		polyID[0] = int((texture2D(texInPolyID, texCoord[0]).r * 63.0) + 0.5);\n\
		polyID[1] = int((texture2D(texInPolyID, texCoord[1]).r * 63.0) + 0.5);\n\
		polyID[2] = int((texture2D(texInPolyID, texCoord[2]).r * 63.0) + 0.5);\n\
		polyID[3] = int((texture2D(texInPolyID, texCoord[3]).r * 63.0) + 0.5);\n\
		polyID[4] = int((texture2D(texInPolyID, texCoord[4]).r * 63.0) + 0.5);\n\
		\n\
		float depth[5];\n\
		depth[0] = unpackFloatFromVec3(texture2D(texInFragDepth, texCoord[0]).rgb);\n\
		depth[1] = unpackFloatFromVec3(texture2D(texInFragDepth, texCoord[1]).rgb);\n\
		depth[2] = unpackFloatFromVec3(texture2D(texInFragDepth, texCoord[2]).rgb);\n\
		depth[3] = unpackFloatFromVec3(texture2D(texInFragDepth, texCoord[3]).rgb);\n\
		depth[4] = unpackFloatFromVec3(texture2D(texInFragDepth, texCoord[4]).rgb);\n\
		\n\
		vec4 edgeColor = vec4(0.0, 0.0, 0.0, 0.0);\n\
		\n\
		for (int i = 1; i < 5; i++)\n\
		{\n\
			if (polyID[0] != polyID[i] && depth[0] >= depth[i])\n\
			{\n\
				edgeColor = stateEdgeColor[polyID[i]/8];\n\
				break;\n\
			}\n\
		}\n\
		\n\
		gl_FragData[0] = edgeColor;\n\
	}\n\
"};

// Vertex shader for applying fog, GLSL 1.00
static const char *FogVtxShader_100 = {"\
	attribute vec2 inPosition;\n\
	attribute vec2 inTexCoord0;\n\
	varying vec2 texCoord;\n\
	\n\
	void main() \n\
	{ \n\
		texCoord = inTexCoord0; \n\
		gl_Position = vec4(inPosition, 0.0, 1.0);\n\
	}\n\
"};

// Fragment shader for applying fog, GLSL 1.00
static const char *FogFragShader_100 = {"\
	varying vec2 texCoord;\n\
	\n\
	uniform sampler2D texInFragColor;\n\
	uniform sampler2D texInFragDepth;\n\
	uniform sampler2D texInFogAttributes;\n\
	uniform bool stateEnableFogAlphaOnly;\n\
	uniform vec4 stateFogColor;\n\
	uniform float stateFogDensity[32];\n\
	uniform float stateFogOffset;\n\
	uniform float stateFogStep;\n\
	\n\
	float unpackFloatFromVec3(const vec3 value)\n\
	{\n\
		const vec3 unpackRatio = vec3(256.0, 65536.0, 16777216.0);\n\
		return (dot(value, unpackRatio) / 16777215.0);\n\
	}\n\
	\n\
	void main()\n\
	{\n\
		vec4 inFragColor = texture2D(texInFragColor, texCoord);\n\
		vec4 inFogAttributes = texture2D(texInFogAttributes, texCoord);\n\
		bool polyEnableFog = (inFogAttributes.r > 0.999);\n\
		vec4 newFoggedColor = inFragColor;\n\
		\n\
		if (polyEnableFog)\n\
		{\n\
			float inFragDepth = unpackFloatFromVec3(texture2D(texInFragDepth, texCoord).rgb);\n\
			float fogMixWeight = 0.0;\n\
			\n\
			if (inFragDepth <= min(stateFogOffset + stateFogStep, 1.0))\n\
			{\n\
				fogMixWeight = stateFogDensity[0];\n\
			}\n\
			else if (inFragDepth >= min(stateFogOffset + (stateFogStep*32.0), 1.0))\n\
			{\n\
				fogMixWeight = stateFogDensity[31];\n\
			}\n\
			else\n\
			{\n\
				for (int i = 1; i < 32; i++)\n\
				{\n\
					float currentFogStep = min(stateFogOffset + (stateFogStep * float(i+1)), 1.0);\n\
					if (inFragDepth <= currentFogStep)\n\
					{\n\
						float previousFogStep = min(stateFogOffset + (stateFogStep * float(i)), 1.0);\n\
						fogMixWeight = mix(stateFogDensity[i-1], stateFogDensity[i], (inFragDepth - previousFogStep) / (currentFogStep - previousFogStep));\n\
						break;\n\
					}\n\
				}\n\
			}\n\
			\n\
			newFoggedColor = mix(inFragColor, (stateEnableFogAlphaOnly) ? vec4(inFragColor.rgb, stateFogColor.a) : stateFogColor, fogMixWeight);\n\
		}\n\
		\n\
		gl_FragData[0] = newFoggedColor;\n\
	}\n\
"};

// Vertex shader for the final framebuffer, GLSL 1.00
static const char *FramebufferOutputVtxShader_100 = {"\
	attribute vec2 inPosition;\n\
	attribute vec2 inTexCoord0;\n\
	varying vec2 texCoord;\n\
	\n\
	void main()\n\
	{\n\
		texCoord = inTexCoord0;\n\
		gl_Position = vec4(inPosition, 0.0, 1.0);\n\
	}\n\
"};

// Fragment shader for the final RGBA6665 formatted framebuffer, GLSL 1.00
static const char *FramebufferOutputRGBA6665FragShader_100 = {"\
	varying vec2 texCoord;\n\
	\n\
	uniform sampler2D texInFragColor;\n\
	\n\
	void main()\n\
	{\n\
		// Note that we swap B and R since pixel readbacks are done in BGRA format for fastest\n\
		// performance. The final color is still in RGBA format.\n\
		vec4 colorRGBA6665 = texture2D(texInFragColor, texCoord).bgra;\n\
		colorRGBA6665     = floor((colorRGBA6665 * 255.0) + 0.5);\n\
		colorRGBA6665.rgb = floor(colorRGBA6665.rgb / 4.0);\n\
		colorRGBA6665.a   = floor(colorRGBA6665.a   / 8.0);\n\
		\n\
		gl_FragData[0] = (colorRGBA6665 / 255.0);\n\
	}\n\
"};

// Fragment shader for the final RGBA8888 formatted framebuffer, GLSL 1.00
static const char *FramebufferOutputRGBA8888FragShader_100 = {"\
	varying vec2 texCoord;\n\
	\n\
	uniform sampler2D texInFragColor;\n\
	\n\
	void main()\n\
	{\n\
		gl_FragData[0] = texture2D(texInFragColor, texCoord).bgra;\n\
	}\n\
"};

bool IsVersionSupported(unsigned int checkVersionMajor, unsigned int checkVersionMinor, unsigned int checkVersionRevision)
{
	bool result = false;
	
	if ( (_OGLDriverVersion.major > checkVersionMajor) ||
		 (_OGLDriverVersion.major >= checkVersionMajor && _OGLDriverVersion.minor > checkVersionMinor) ||
		 (_OGLDriverVersion.major >= checkVersionMajor && _OGLDriverVersion.minor >= checkVersionMinor && _OGLDriverVersion.revision >= checkVersionRevision) )
	{
		result = true;
	}
	
	return result;
}

static void OGLGetDriverVersion(const char *oglVersionString,
								unsigned int *versionMajor,
								unsigned int *versionMinor,
								unsigned int *versionRevision)
{
	size_t versionStringLength = 0;
	
	if (oglVersionString == NULL)
	{
		return;
	}
	
	// First, check for the dot in the revision string. There should be at
	// least one present.
	const char *versionStrEnd = strstr(oglVersionString, ".");
	if (versionStrEnd == NULL)
	{
		return;
	}
	
	// Next, check for the space before the vendor-specific info (if present).
	versionStrEnd = strstr(oglVersionString, " ");
	if (versionStrEnd == NULL)
	{
		// If a space was not found, then the vendor-specific info is not present,
		// and therefore the entire string must be the version number.
		versionStringLength = strlen(oglVersionString);
	}
	else
	{
		// If a space was found, then the vendor-specific info is present,
		// and therefore the version number is everything before the space.
		versionStringLength = versionStrEnd - oglVersionString;
	}
	
	// Copy the version substring and parse it.
	char *versionSubstring = (char *)malloc(versionStringLength * sizeof(char));
	strncpy(versionSubstring, oglVersionString, versionStringLength);
	
	unsigned int major = 0;
	unsigned int minor = 0;
	unsigned int revision = 0;
	
	sscanf(versionSubstring, "%u.%u.%u", &major, &minor, &revision);
	
	free(versionSubstring);
	versionSubstring = NULL;
	
	if (versionMajor != NULL)
	{
		*versionMajor = major;
	}
	
	if (versionMinor != NULL)
	{
		*versionMinor = minor;
	}
	
	if (versionRevision != NULL)
	{
		*versionRevision = revision;
	}
}

void texDeleteCallback(TexCacheItem *texItem, void *param1, void *param2)
{
	OpenGLRenderer *oglRenderer = (OpenGLRenderer *)param1;
	oglRenderer->DeleteTexture(texItem);
}

template<bool require_profile, bool enable_3_2>
static Render3D* OpenGLRendererCreate()
{
	OpenGLRenderer *newRenderer = NULL;
	Render3DError error = OGLERROR_NOERR;
	
	if (oglrender_init == NULL)
	{
		return NULL;
	}
	
	if (!oglrender_init())
	{
		return NULL;
	}
	
	if (!BEGINGL())
	{
		INFO("OpenGL<%s,%s>: Could not initialize -- BEGINGL() failed.\n",require_profile?"force":"auto",enable_3_2?"3_2":"old");
		return NULL;
	}
	
	// Get OpenGL info
	const char *oglVersionString = (const char *)glGetString(GL_VERSION);
	const char *oglVendorString = (const char *)glGetString(GL_VENDOR);
	const char *oglRendererString = (const char *)glGetString(GL_RENDERER);

	// Writing to gl_FragDepth causes the driver to fail miserably on systems equipped 
	// with a Intel G965 graphic card. Warn the user and fail gracefully.
	// http://forums.desmume.org/viewtopic.php?id=9286
	if(!strcmp(oglVendorString,"Intel") && strstr(oglRendererString,"965")) 
	{
		INFO("OpenGL: Incompatible graphic card detected. Disabling OpenGL support.\n");
		
		ENDGL();
		return newRenderer;
	}
	
	// Check the driver's OpenGL version
	OGLGetDriverVersion(oglVersionString, &_OGLDriverVersion.major, &_OGLDriverVersion.minor, &_OGLDriverVersion.revision);
	
	if (!IsVersionSupported(OGLRENDER_MINIMUM_DRIVER_VERSION_REQUIRED_MAJOR, OGLRENDER_MINIMUM_DRIVER_VERSION_REQUIRED_MINOR, OGLRENDER_MINIMUM_DRIVER_VERSION_REQUIRED_REVISION))
	{
		INFO("OpenGL: Driver does not support OpenGL v%u.%u.%u or later. Disabling 3D renderer.\n[ Driver Info -\n    Version: %s\n    Vendor: %s\n    Renderer: %s ]\n",
			 OGLRENDER_MINIMUM_DRIVER_VERSION_REQUIRED_MAJOR, OGLRENDER_MINIMUM_DRIVER_VERSION_REQUIRED_MINOR, OGLRENDER_MINIMUM_DRIVER_VERSION_REQUIRED_REVISION,
			 oglVersionString, oglVendorString, oglRendererString);
		
		ENDGL();
		return newRenderer;
	}
	
	// Create new OpenGL rendering object
	if (enable_3_2)
	{
		if (OGLLoadEntryPoints_3_2_Func != NULL && OGLCreateRenderer_3_2_Func != NULL)
		{
			OGLLoadEntryPoints_3_2_Func();
			OGLLoadEntryPoints_Legacy(); //zero 04-feb-2013 - this seems to be necessary as well
			OGLCreateRenderer_3_2_Func(&newRenderer);
		}
		else 
		{
			if(require_profile)
			{
				ENDGL();
				return newRenderer;
			}
		}
	}
	
	// If the renderer doesn't initialize with OpenGL v3.2 or higher, fall back
	// to one of the lower versions.
	if (newRenderer == NULL)
	{
		OGLLoadEntryPoints_Legacy();
		
		if (IsVersionSupported(2, 1, 0))
		{
			newRenderer = new OpenGLRenderer_2_1;
			newRenderer->SetVersion(2, 1, 0);
		}
		else if (IsVersionSupported(2, 0, 0))
		{
			newRenderer = new OpenGLRenderer_2_0;
			newRenderer->SetVersion(2, 0, 0);
		}
		else if (IsVersionSupported(1, 5, 0))
		{
			newRenderer = new OpenGLRenderer_1_5;
			newRenderer->SetVersion(1, 5, 0);
		}
		else if (IsVersionSupported(1, 4, 0))
		{
			newRenderer = new OpenGLRenderer_1_4;
			newRenderer->SetVersion(1, 4, 0);
		}
		else if (IsVersionSupported(1, 3, 0))
		{
			newRenderer = new OpenGLRenderer_1_3;
			newRenderer->SetVersion(1, 3, 0);
		}
		else if (IsVersionSupported(1, 2, 0))
		{
			newRenderer = new OpenGLRenderer_1_2;
			newRenderer->SetVersion(1, 2, 0);
		}
	}
	
	if (newRenderer == NULL)
	{
		INFO("OpenGL: Renderer did not initialize. Disabling 3D renderer.\n[ Driver Info -\n    Version: %s\n    Vendor: %s\n    Renderer: %s ]\n",
			 oglVersionString, oglVendorString, oglRendererString);
		
		ENDGL();
		return newRenderer;
	}
	
	// Initialize OpenGL extensions
	error = newRenderer->InitExtensions();
	if (error != OGLERROR_NOERR)
	{
		if ( IsVersionSupported(2, 0, 0) &&
			(error == OGLERROR_SHADER_CREATE_ERROR ||
			 error == OGLERROR_VERTEX_SHADER_PROGRAM_LOAD_ERROR ||
			 error == OGLERROR_FRAGMENT_SHADER_PROGRAM_LOAD_ERROR) )
		{
			INFO("OpenGL: Shaders are not working, even though they should be. Disabling 3D renderer.\n");
			delete newRenderer;
			newRenderer = NULL;
			
			ENDGL();
			return newRenderer;
		}
		else if (IsVersionSupported(3, 0, 0) && error == OGLERROR_FBO_CREATE_ERROR && OGLLoadEntryPoints_3_2_Func != NULL)
		{
			INFO("OpenGL: FBOs are not working, even though they should be. Disabling 3D renderer.\n");
			delete newRenderer;
			newRenderer = NULL;
			
			ENDGL();
			return newRenderer;
		}
	}
	
	ENDGL();
	
	// Initialization finished -- reset the renderer
	newRenderer->Reset();
	
	unsigned int major = 0;
	unsigned int minor = 0;
	unsigned int revision = 0;
	newRenderer->GetVersion(&major, &minor, &revision);
	
	INFO("OpenGL: Renderer initialized successfully (v%u.%u.%u).\n[ Driver Info -\n    Version: %s\n    Vendor: %s\n    Renderer: %s ]\n",
		 major, minor, revision, oglVersionString, oglVendorString, oglRendererString);
	
	return newRenderer;
}

static void OpenGLRendererDestroy()
{
	if(!BEGINGL())
		return;
	
	if (CurrentRenderer != BaseRenderer)
	{
		OpenGLRenderer *oldRenderer = (OpenGLRenderer *)CurrentRenderer;
		CurrentRenderer = BaseRenderer;
		delete oldRenderer;
	}
	
	ENDGL();
}

//automatically select 3.2 or old profile depending on whether 3.2 is available
GPU3DInterface gpu3Dgl = {
	"OpenGL",
	OpenGLRendererCreate<false,true>,
	OpenGLRendererDestroy
};

//forcibly use old profile
GPU3DInterface gpu3DglOld = {
	"OpenGL Old",
	OpenGLRendererCreate<true,false>,
	OpenGLRendererDestroy
};

//forcibly use new profile
GPU3DInterface gpu3Dgl_3_2 = {
	"OpenGL 3.2",
	OpenGLRendererCreate<true,true>,
	OpenGLRendererDestroy
};

OpenGLRenderer::OpenGLRenderer()
{
	_deviceInfo.renderID = RENDERID_OPENGL_AUTO;
	_deviceInfo.renderName = "OpenGL";
	_deviceInfo.isTexturingSupported = true;
	_deviceInfo.isEdgeMarkSupported = true;
	_deviceInfo.isFogSupported = true;
	_deviceInfo.isTextureSmoothingSupported = true;
	_deviceInfo.maxAnisotropy = 1.0f;
	_deviceInfo.maxSamples = 0;
	
	_internalRenderingFormat = NDSColorFormat_BGR888_Rev;
	
	versionMajor = 0;
	versionMinor = 0;
	versionRevision = 0;
	
	isVBOSupported = false;
	isPBOSupported = false;
	isFBOSupported = false;
	isMultisampledFBOSupported = false;
	isShaderSupported = false;
	isVAOSupported = false;
	willFlipFramebufferOnGPU = false;
	willConvertFramebufferOnGPU = false;
	
	// Init OpenGL rendering states
	ref = new OGLRenderRef;
	ref->fboRenderID = 0;
	ref->fboMSIntermediateRenderID = 0;
	ref->fboPostprocessID = 0;
	ref->selectedRenderingFBO = 0;
	
	currTexture = NULL;
	_mappedFramebuffer = NULL;
	_pixelReadNeedsFinish = false;
	_currentPolyIndex = 0;
	_shadowPolyID.reserve(POLYLIST_SIZE);
}

OpenGLRenderer::~OpenGLRenderer()
{
	free_aligned(_framebufferColor);
	
	// Destroy OpenGL rendering states
	delete ref;
	ref = NULL;
}

bool OpenGLRenderer::IsExtensionPresent(const std::set<std::string> *oglExtensionSet, const std::string extensionName) const
{
	if (oglExtensionSet == NULL || oglExtensionSet->size() == 0)
	{
		return false;
	}
	
	return (oglExtensionSet->find(extensionName) != oglExtensionSet->end());
}

bool OpenGLRenderer::ValidateShaderCompile(GLuint theShader) const
{
	bool isCompileValid = false;
	GLint status = GL_FALSE;
	
	glGetShaderiv(theShader, GL_COMPILE_STATUS, &status);
	if(status == GL_TRUE)
	{
		isCompileValid = true;
	}
	else
	{
		GLint logSize;
		GLchar *log = NULL;
		
		glGetShaderiv(theShader, GL_INFO_LOG_LENGTH, &logSize);
		log = new GLchar[logSize];
		glGetShaderInfoLog(theShader, logSize, &logSize, log);
		
		INFO("OpenGL: SEVERE - FAILED TO COMPILE SHADER : %s\n", log);
		delete[] log;
	}
	
	return isCompileValid;
}

bool OpenGLRenderer::ValidateShaderProgramLink(GLuint theProgram) const
{
	bool isLinkValid = false;
	GLint status = GL_FALSE;
	
	glGetProgramiv(theProgram, GL_LINK_STATUS, &status);
	if(status == GL_TRUE)
	{
		isLinkValid = true;
	}
	else
	{
		GLint logSize;
		GLchar *log = NULL;
		
		glGetProgramiv(theProgram, GL_INFO_LOG_LENGTH, &logSize);
		log = new GLchar[logSize];
		glGetProgramInfoLog(theProgram, logSize, &logSize, log);
		
		INFO("OpenGL: SEVERE - FAILED TO LINK SHADER PROGRAM : %s\n", log);
		delete[] log;
	}
	
	return isLinkValid;
}

void OpenGLRenderer::GetVersion(unsigned int *major, unsigned int *minor, unsigned int *revision) const
{
	*major = this->versionMajor;
	*minor = this->versionMinor;
	*revision = this->versionRevision;
}

void OpenGLRenderer::SetVersion(unsigned int major, unsigned int minor, unsigned int revision)
{
	this->versionMajor = major;
	this->versionMinor = minor;
	this->versionRevision = revision;
}

Render3DError OpenGLRenderer::_FlushFramebufferConvertOnCPU(const FragmentColor *__restrict srcFramebuffer, FragmentColor *__restrict dstFramebuffer, u16 *__restrict dstRGBA5551)
{
	if ( ((dstFramebuffer == NULL) && (dstRGBA5551 == NULL)) || (srcFramebuffer == NULL) )
	{
		return RENDER3DERROR_NOERR;
	}
	
	// Convert from 32-bit BGRA8888 format to 32-bit RGBA6665 reversed format. OpenGL
	// stores pixels using a flipped Y-coordinate, so this needs to be flipped back
	// to the DS Y-coordinate.
	
	size_t i = 0;
	
	if (this->willFlipFramebufferOnGPU)
	{
		const size_t pixCount = this->_framebufferWidth * this->_framebufferHeight;
		
		if (this->_outputFormat == NDSColorFormat_BGR666_Rev)
		{
			if ( (dstFramebuffer != NULL) && (dstRGBA5551 != NULL) )
			{
#ifdef ENABLE_SSE2
				const size_t ssePixCount = pixCount - (pixCount % 8);
				for (; i < ssePixCount; i += 8)
				{
					const __m128i srcColorLo = _mm_load_si128((__m128i *)(srcFramebuffer + i + 0));
					const __m128i srcColorHi = _mm_load_si128((__m128i *)(srcFramebuffer + i + 4));
					
					_mm_store_si128( (__m128i *)(dstFramebuffer + i + 0), ConvertColor8888To6665<true>(srcColorLo) );
					_mm_store_si128( (__m128i *)(dstFramebuffer + i + 4), ConvertColor8888To6665<true>(srcColorHi) );
					_mm_store_si128( (__m128i *)(dstRGBA5551 + i), ConvertColor8888To5551<true>(srcColorLo, srcColorHi) );
				}
#endif
				
#ifdef ENABLE_SSE2
#pragma LOOPVECTORIZE_DISABLE
#endif
				for (; i < pixCount; i++)
				{
					dstFramebuffer[i].color = ConvertColor8888To6665<true>(srcFramebuffer[i]);
					dstRGBA5551[i]          = ConvertColor8888To5551<true>(srcFramebuffer[i]);
				}
			}
			else if (dstFramebuffer != NULL)
			{
				ConvertColorBuffer8888To6665<true>((u32 *)srcFramebuffer, (u32 *)dstFramebuffer, pixCount);
			}
			else
			{
				ConvertColorBuffer8888To5551<true, false>((u32 *)srcFramebuffer, dstRGBA5551, pixCount);
			}
		}
		else if (this->_outputFormat == NDSColorFormat_BGR888_Rev)
		{
			if ( (dstFramebuffer != NULL) && (dstRGBA5551 != NULL) )
			{
#ifdef ENABLE_SSE2
				const size_t ssePixCount = pixCount - (pixCount % 8);
				for (; i < ssePixCount; i += 8)
				{
					const __m128i srcColorLo = _mm_load_si128((__m128i *)(srcFramebuffer + i + 0));
					const __m128i srcColorHi = _mm_load_si128((__m128i *)(srcFramebuffer + i + 4));
					
					_mm_store_si128( (__m128i *)(dstFramebuffer + i + 0), srcColorLo );
					_mm_store_si128( (__m128i *)(dstFramebuffer + i + 4), srcColorHi );
					_mm_store_si128( (__m128i *)(dstRGBA5551 + i), ConvertColor8888To5551<true>(srcColorLo, srcColorHi) );
				}
#endif
				
#ifdef ENABLE_SSE2
#pragma LOOPVECTORIZE_DISABLE
#endif
				for (; i < pixCount; i++)
				{
					dstFramebuffer[i].color = ConvertColor8888To6665<true>(srcFramebuffer[i]);
					dstRGBA5551[i]          = ConvertColor8888To5551<true>(srcFramebuffer[i]);
				}
			}
			else if (dstFramebuffer != NULL)
			{
				memcpy(dstFramebuffer, srcFramebuffer, this->_framebufferWidth * this->_framebufferHeight * sizeof(FragmentColor));
			}
			else
			{
				ConvertColorBuffer8888To5551<true, false>((u32 *)srcFramebuffer, dstRGBA5551, pixCount);
			}
		}
	}
	else // In the case where OpenGL couldn't flip the framebuffer on the GPU, we'll instead need to flip the framebuffer during conversion.
	{
		const size_t pixCount = this->_framebufferWidth;
		
		if (this->_outputFormat == NDSColorFormat_BGR666_Rev)
		{
			if ( (dstFramebuffer != NULL) && (dstRGBA5551 != NULL) )
			{
				for (size_t y = 0, ir = 0, iw = ((this->_framebufferHeight - 1) * this->_framebufferWidth); y < this->_framebufferHeight; y++, iw -= (this->_framebufferWidth * 2))
				{
					size_t x = 0;
#ifdef ENABLE_SSE2
					const size_t ssePixCount = pixCount - (pixCount % 8);
					for (; x < ssePixCount; x += 8, ir += 8, iw += 8)
					{
						const __m128i srcColorLo = _mm_load_si128((__m128i *)(srcFramebuffer + ir + 0));
						const __m128i srcColorHi = _mm_load_si128((__m128i *)(srcFramebuffer + ir + 4));
						
						_mm_store_si128( (__m128i *)(dstFramebuffer + iw + 0), ConvertColor8888To6665<true>(srcColorLo) );
						_mm_store_si128( (__m128i *)(dstFramebuffer + iw + 4), ConvertColor8888To6665<true>(srcColorHi) );
						_mm_store_si128( (__m128i *)(dstRGBA5551 + iw), ConvertColor8888To5551<true>(srcColorLo, srcColorHi) );
					}
#endif
					
#ifdef ENABLE_SSE2
#pragma LOOPVECTORIZE_DISABLE
#endif
					for (; x < pixCount; x++, ir++, iw++)
					{
						dstFramebuffer[iw].color = ConvertColor8888To6665<true>(srcFramebuffer[ir]);
						dstRGBA5551[iw]          = ConvertColor8888To5551<true>(srcFramebuffer[ir]);
					}
				}
			}
			else if (dstFramebuffer != NULL)
			{
				for (size_t y = 0, ir = 0, iw = ((this->_framebufferHeight - 1) * this->_framebufferWidth); y < this->_framebufferHeight; y++, iw -= (this->_framebufferWidth * 2))
				{
					ConvertColorBuffer8888To6665<true>((u32 *)srcFramebuffer + ir, (u32 *)dstFramebuffer + iw, pixCount);
				}
			}
			else
			{
				for (size_t y = 0, ir = 0, iw = ((this->_framebufferHeight - 1) * this->_framebufferWidth); y < this->_framebufferHeight; y++, iw -= (this->_framebufferWidth * 2))
				{
					ConvertColorBuffer8888To5551<true, false>((u32 *)srcFramebuffer + ir, dstRGBA5551 + iw, pixCount);
				}
			}
		}
		else if (this->_outputFormat == NDSColorFormat_BGR888_Rev)
		{
			if ( (dstFramebuffer != NULL) && (dstRGBA5551 != NULL) )
			{
				for (size_t y = 0, ir = 0, iw = ((this->_framebufferHeight - 1) * this->_framebufferWidth); y < this->_framebufferHeight; y++, iw -= (this->_framebufferWidth * 2))
				{
					size_t x = 0;
#ifdef ENABLE_SSE2
					const size_t ssePixCount = pixCount - (pixCount % 8);
					for (; x < ssePixCount; x += 8, ir += 8, iw += 8)
					{
						const __m128i srcColorLo = _mm_load_si128((__m128i *)(srcFramebuffer + ir + 0));
						const __m128i srcColorHi = _mm_load_si128((__m128i *)(srcFramebuffer + ir + 4));
						
						_mm_store_si128( (__m128i *)(dstFramebuffer + iw + 0), srcColorLo );
						_mm_store_si128( (__m128i *)(dstFramebuffer + iw + 4), srcColorHi );
						_mm_store_si128( (__m128i *)(dstRGBA5551 + iw), ConvertColor8888To5551<true>(srcColorLo, srcColorHi) );
					}
#endif
					
#ifdef ENABLE_SSE2
#pragma LOOPVECTORIZE_DISABLE
#endif
					for (; x < pixCount; x++, ir++, iw++)
					{
						dstFramebuffer[iw] = srcFramebuffer[ir];
						dstRGBA5551[iw]    = ConvertColor8888To5551<true>(srcFramebuffer[ir]);
					}
				}
			}
			else if (dstFramebuffer != NULL)
			{
				const size_t lineBytes = this->_framebufferWidth * sizeof(FragmentColor);
				const FragmentColor *__restrict srcPtr = srcFramebuffer;
				FragmentColor *__restrict dstPtr = dstFramebuffer + ((this->_framebufferHeight - 1) * this->_framebufferWidth);
				
				for (size_t y = 0; y < this->_framebufferHeight; y++)
				{
					memcpy(dstPtr, srcPtr, lineBytes);
					srcPtr += this->_framebufferWidth;
					dstPtr -= this->_framebufferWidth;
				}
			}
			else
			{
				for (size_t y = 0, ir = 0, iw = ((this->_framebufferHeight - 1) * this->_framebufferWidth); y < this->_framebufferHeight; y++, iw -= (this->_framebufferWidth * 2))
				{
					ConvertColorBuffer8888To5551<true, false>((u32 *)srcFramebuffer + ir, dstRGBA5551 + iw, pixCount);
				}
			}
		}
	}
	
	return RENDER3DERROR_NOERR;
}

Render3DError OpenGLRenderer::FlushFramebuffer(const FragmentColor *__restrict srcFramebuffer, FragmentColor *__restrict dstFramebuffer, u16 *__restrict dstRGBA5551)
{
	if (this->willConvertFramebufferOnGPU)
	{
		return Render3D::FlushFramebuffer(srcFramebuffer, NULL, dstRGBA5551);
	}
	else
	{
		return this->_FlushFramebufferConvertOnCPU(srcFramebuffer, dstFramebuffer, dstRGBA5551);
	}
	
	return RENDER3DERROR_NOERR;
}

FragmentColor* OpenGLRenderer::GetFramebuffer()
{
	return (this->willConvertFramebufferOnGPU) ? this->_mappedFramebuffer : GPU->GetEngineMain()->Get3DFramebufferRGBA6665();
}

OpenGLRenderer_1_2::~OpenGLRenderer_1_2()
{
	glFinish();
	
	_pixelReadNeedsFinish = false;
	
	delete[] ref->color4fBuffer;
	ref->color4fBuffer = NULL;
	
	delete[] ref->vertIndexBuffer;
	ref->vertIndexBuffer = NULL;
	
	DestroyGeometryProgram();
	DestroyPostprocessingPrograms();
	DestroyVAOs();
	DestroyVBOs();
	DestroyPBOs();
	DestroyFBOs();
	DestroyMultisampledFBO();
	
	// Kill the texture cache now before all of our texture IDs disappear.
	TexCache_Reset();
	
	while(!ref->freeTextureIDs.empty())
	{
		GLuint temp = ref->freeTextureIDs.front();
		ref->freeTextureIDs.pop();
		glDeleteTextures(1, &temp);
	}
	
	glFinish();
}

Render3DError OpenGLRenderer_1_2::InitExtensions()
{
	Render3DError error = OGLERROR_NOERR;
	
	// Get OpenGL extensions
	std::set<std::string> oglExtensionSet;
	this->GetExtensionSet(&oglExtensionSet);
	
	// Get host GPU device properties
	GLfloat maxAnisotropyOGL = 1.0f;
	glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAnisotropyOGL);
	this->_deviceInfo.maxAnisotropy = maxAnisotropyOGL;
	
	GLint maxSamplesOGL = 0;
	glGetIntegerv(GL_MAX_SAMPLES_EXT, &maxSamplesOGL);
	this->_deviceInfo.maxSamples = (u8)maxSamplesOGL;
	
	// Initialize OpenGL
	this->InitTables();
	
	this->isShaderSupported	= this->IsExtensionPresent(&oglExtensionSet, "GL_ARB_shader_objects") &&
							  this->IsExtensionPresent(&oglExtensionSet, "GL_ARB_vertex_shader") &&
							  this->IsExtensionPresent(&oglExtensionSet, "GL_ARB_fragment_shader") &&
							  this->IsExtensionPresent(&oglExtensionSet, "GL_ARB_vertex_program");
	if (this->isShaderSupported)
	{
		std::string vertexShaderProgram;
		std::string fragmentShaderProgram;
		
		error = this->LoadGeometryShaders(vertexShaderProgram, fragmentShaderProgram);
		if (error == OGLERROR_NOERR)
		{
			error = this->InitGeometryProgram(vertexShaderProgram, fragmentShaderProgram);
			if (error == OGLERROR_NOERR)
			{
				std::string edgeMarkVtxShaderString = std::string(EdgeMarkVtxShader_100);
				std::string edgeMarkFragShaderString = std::string(EdgeMarkFragShader_100);
				std::string fogVtxShaderString = std::string(FogVtxShader_100);
				std::string fogFragShaderString = std::string(FogFragShader_100);
				std::string framebufferOutputVtxShaderString = std::string(FramebufferOutputVtxShader_100);
				std::string framebufferOutputRGBA6665FragShaderString = std::string(FramebufferOutputRGBA6665FragShader_100);
				std::string framebufferOutputRGBA8888FragShaderString = std::string(FramebufferOutputRGBA8888FragShader_100);
				error = this->InitPostprocessingPrograms(edgeMarkVtxShaderString,
														 edgeMarkFragShaderString,
														 fogVtxShaderString,
														 fogFragShaderString,
														 framebufferOutputVtxShaderString,
														 framebufferOutputRGBA6665FragShaderString,
														 framebufferOutputRGBA8888FragShaderString);
				if (error != OGLERROR_NOERR)
				{
					this->_deviceInfo.isEdgeMarkSupported = false;
					this->_deviceInfo.isFogSupported = false;
					INFO("OpenGL: Edge mark and fog require OpenGL v2.0 or later. These features will be disabled.\n");
				}
			}
			else
			{
				this->_deviceInfo.isEdgeMarkSupported = false;
				this->_deviceInfo.isFogSupported = false;
				this->_deviceInfo.isTextureSmoothingSupported = false;
				this->isShaderSupported = false;
			}
		}
		else
		{
			this->_deviceInfo.isEdgeMarkSupported = false;
			this->_deviceInfo.isFogSupported = false;
			this->_deviceInfo.isTextureSmoothingSupported = false;
			this->isShaderSupported = false;
		}
	}
	else
	{
		this->_deviceInfo.isEdgeMarkSupported = false;
		this->_deviceInfo.isFogSupported = false;
		this->_deviceInfo.isTextureSmoothingSupported = false;
		INFO("OpenGL: Shaders are unsupported. Disabling shaders and using fixed-function pipeline. Some emulation features will be disabled.\n");
	}
	
	this->isVBOSupported = this->IsExtensionPresent(&oglExtensionSet, "GL_ARB_vertex_buffer_object");
	if (this->isVBOSupported)
	{
		this->CreateVBOs();
	}
	
	this->isPBOSupported	= this->isVBOSupported &&
							 (this->IsExtensionPresent(&oglExtensionSet, "GL_ARB_pixel_buffer_object") ||
							  this->IsExtensionPresent(&oglExtensionSet, "GL_EXT_pixel_buffer_object"));
	if (this->isPBOSupported)
	{
		this->CreatePBOs();
	}
	
	this->isVAOSupported	= this->isShaderSupported &&
							  this->isVBOSupported &&
							 (this->IsExtensionPresent(&oglExtensionSet, "GL_ARB_vertex_array_object") ||
							  this->IsExtensionPresent(&oglExtensionSet, "GL_APPLE_vertex_array_object"));
	if (this->isVAOSupported)
	{
		this->CreateVAOs();
	}
	
	// Don't use ARB versions since we're using the EXT versions for backwards compatibility.
	this->isFBOSupported	= this->IsExtensionPresent(&oglExtensionSet, "GL_EXT_framebuffer_object") &&
							  this->IsExtensionPresent(&oglExtensionSet, "GL_EXT_framebuffer_blit") &&
							  this->IsExtensionPresent(&oglExtensionSet, "GL_EXT_packed_depth_stencil");
	if (this->isFBOSupported)
	{
		this->willFlipFramebufferOnGPU = true;
		this->willConvertFramebufferOnGPU = (this->isShaderSupported && this->isVAOSupported && this->isPBOSupported && this->isFBOSupported);
		
		error = this->CreateFBOs();
		if (error != OGLERROR_NOERR)
		{
			this->isFBOSupported = false;
		}
	}
	else
	{
		INFO("OpenGL: FBOs are unsupported. Some emulation features will be disabled.\n");
	}
	
	// Set these again after FBO creation just in case FBO creation fails.
	this->willFlipFramebufferOnGPU = this->isFBOSupported;
	this->willConvertFramebufferOnGPU = (this->isShaderSupported && this->isVAOSupported && this->isPBOSupported && this->isFBOSupported);
	
	// Don't use ARB versions since we're using the EXT versions for backwards compatibility.
	this->isMultisampledFBOSupported	= this->isFBOSupported &&
										  this->IsExtensionPresent(&oglExtensionSet, "GL_EXT_framebuffer_multisample");
	if (this->isMultisampledFBOSupported)
	{
		error = this->CreateMultisampledFBO();
		if (error != OGLERROR_NOERR)
		{
			this->isMultisampledFBOSupported = false;
		}
	}
	else
	{
		INFO("OpenGL: Multisampled FBOs are unsupported. Multisample antialiasing will be disabled.\n");
	}
	
	this->InitTextures();
	this->InitFinalRenderStates(&oglExtensionSet); // This must be done last
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_2::CreateVBOs()
{
	OGLRenderRef &OGLRef = *this->ref;
	
	glGenBuffersARB(1, &OGLRef.vboGeometryVtxID);
	glGenBuffersARB(1, &OGLRef.iboGeometryIndexID);
	glGenBuffersARB(1, &OGLRef.vboPostprocessVtxID);
	glGenBuffersARB(1, &OGLRef.iboPostprocessIndexID);
	
	glBindBufferARB(GL_ARRAY_BUFFER_ARB, OGLRef.vboGeometryVtxID);
	glBufferDataARB(GL_ARRAY_BUFFER_ARB, VERTLIST_SIZE * sizeof(VERT), NULL, GL_STREAM_DRAW_ARB);
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, OGLRef.iboGeometryIndexID);
	glBufferDataARB(GL_ELEMENT_ARRAY_BUFFER_ARB, OGLRENDER_VERT_INDEX_BUFFER_COUNT * sizeof(GLushort), NULL, GL_STREAM_DRAW_ARB);
	
	glBindBufferARB(GL_ARRAY_BUFFER_ARB, OGLRef.vboPostprocessVtxID);
	glBufferDataARB(GL_ARRAY_BUFFER_ARB, sizeof(PostprocessVtxBuffer), PostprocessVtxBuffer, GL_STATIC_DRAW_ARB);
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, OGLRef.iboPostprocessIndexID);
	glBufferDataARB(GL_ELEMENT_ARRAY_BUFFER_ARB, sizeof(PostprocessElementBuffer), PostprocessElementBuffer, GL_STATIC_DRAW_ARB);
	
	glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, 0);
	
	return OGLERROR_NOERR;
}

void OpenGLRenderer_1_2::DestroyVBOs()
{
	if (!this->isVBOSupported)
	{
		return;
	}
	
	OGLRenderRef &OGLRef = *this->ref;
	
	glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, 0);
	
	glDeleteBuffersARB(1, &OGLRef.vboGeometryVtxID);
	glDeleteBuffersARB(1, &OGLRef.iboGeometryIndexID);
	glDeleteBuffersARB(1, &OGLRef.vboPostprocessVtxID);
	glDeleteBuffersARB(1, &OGLRef.iboPostprocessIndexID);
	
	this->isVBOSupported = false;
}

Render3DError OpenGLRenderer_1_2::CreatePBOs()
{
	OGLRenderRef &OGLRef = *this->ref;
	
	glGenBuffersARB(1, &OGLRef.pboRenderDataID);
	glBindBufferARB(GL_PIXEL_PACK_BUFFER_ARB, OGLRef.pboRenderDataID);
	glBufferDataARB(GL_PIXEL_PACK_BUFFER_ARB, this->_framebufferColorSizeBytes, NULL, GL_STREAM_READ_ARB);
	
	return OGLERROR_NOERR;
}

void OpenGLRenderer_1_2::DestroyPBOs()
{
	if (!this->isPBOSupported)
	{
		return;
	}
	
	glBindBufferARB(GL_PIXEL_PACK_BUFFER_ARB, 0);
	glDeleteBuffersARB(1, &this->ref->pboRenderDataID);
	
	this->isPBOSupported = false;
}

Render3DError OpenGLRenderer_1_2::LoadGeometryShaders(std::string &outVertexShaderProgram, std::string &outFragmentShaderProgram)
{
	outVertexShaderProgram.clear();
	outFragmentShaderProgram.clear();
	
	outVertexShaderProgram += std::string(vertexShader_100);
	outFragmentShaderProgram += std::string(fragmentShader_100);
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_2::InitGeometryProgramBindings()
{
	OGLRenderRef &OGLRef = *this->ref;
	
	glBindAttribLocation(OGLRef.programGeometryID, OGLVertexAttributeID_Position, "inPosition");
	glBindAttribLocation(OGLRef.programGeometryID, OGLVertexAttributeID_TexCoord0, "inTexCoord0");
	glBindAttribLocation(OGLRef.programGeometryID, OGLVertexAttributeID_Color, "inColor");
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_2::InitGeometryProgramShaderLocations()
{
	OGLRenderRef &OGLRef = *this->ref;
	
	glUseProgram(OGLRef.programGeometryID);
	
	const GLint uniformTexRenderObject			= glGetUniformLocation(OGLRef.programGeometryID, "texRenderObject");
	const GLint uniformTexToonTable				= glGetUniformLocation(OGLRef.programGeometryID, "texToonTable");
	glUniform1i(uniformTexRenderObject, 0);
	glUniform1i(uniformTexToonTable, OGLTextureUnitID_ToonTable);
	
	OGLRef.uniformStateToonShadingMode			= glGetUniformLocation(OGLRef.programGeometryID, "stateToonShadingMode");
	OGLRef.uniformStateEnableAlphaTest			= glGetUniformLocation(OGLRef.programGeometryID, "stateEnableAlphaTest");
	OGLRef.uniformStateEnableAntialiasing		= glGetUniformLocation(OGLRef.programGeometryID, "stateEnableAntialiasing");
	OGLRef.uniformStateEnableEdgeMarking		= glGetUniformLocation(OGLRef.programGeometryID, "stateEnableEdgeMarking");
	OGLRef.uniformStateUseWDepth				= glGetUniformLocation(OGLRef.programGeometryID, "stateUseWDepth");
	OGLRef.uniformStateAlphaTestRef				= glGetUniformLocation(OGLRef.programGeometryID, "stateAlphaTestRef");
	
	OGLRef.uniformPolyTexScale					= glGetUniformLocation(OGLRef.programGeometryID, "polyTexScale");
	OGLRef.uniformPolyMode						= glGetUniformLocation(OGLRef.programGeometryID, "polyMode");
	OGLRef.uniformPolyEnableDepthWrite			= glGetUniformLocation(OGLRef.programGeometryID, "polyEnableDepthWrite");
	OGLRef.uniformPolySetNewDepthForTranslucent	= glGetUniformLocation(OGLRef.programGeometryID, "polySetNewDepthForTranslucent");
	OGLRef.uniformPolyAlpha						= glGetUniformLocation(OGLRef.programGeometryID, "polyAlpha");
	OGLRef.uniformPolyID						= glGetUniformLocation(OGLRef.programGeometryID, "polyID");
	
	OGLRef.uniformPolyEnableTexture				= glGetUniformLocation(OGLRef.programGeometryID, "polyEnableTexture");
	OGLRef.uniformPolyEnableFog					= glGetUniformLocation(OGLRef.programGeometryID, "polyEnableFog");
	OGLRef.uniformTexSingleBitAlpha				= glGetUniformLocation(OGLRef.programGeometryID, "texSingleBitAlpha");
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_2::InitGeometryProgram(const std::string &vertexShaderProgram, const std::string &fragmentShaderProgram)
{
	OGLRenderRef &OGLRef = *this->ref;
	
	OGLRef.vertexGeometryShaderID = glCreateShader(GL_VERTEX_SHADER);
	if(!OGLRef.vertexGeometryShaderID)
	{
		INFO("OpenGL: Failed to create the vertex shader. Disabling shaders and using fixed-function pipeline. Some emulation features will be disabled.\n");		
		return OGLERROR_SHADER_CREATE_ERROR;
	}
	
	const char *vertexShaderProgramChar = vertexShaderProgram.c_str();
	glShaderSource(OGLRef.vertexGeometryShaderID, 1, (const GLchar **)&vertexShaderProgramChar, NULL);
	glCompileShader(OGLRef.vertexGeometryShaderID);
	if (!this->ValidateShaderCompile(OGLRef.vertexGeometryShaderID))
	{
		glDeleteShader(OGLRef.vertexGeometryShaderID);
		INFO("OpenGL: Failed to compile the vertex shader. Disabling shaders and using fixed-function pipeline. Some emulation features will be disabled.\n");
		return OGLERROR_SHADER_CREATE_ERROR;
	}
	
	OGLRef.fragmentGeometryShaderID = glCreateShader(GL_FRAGMENT_SHADER);
	if(!OGLRef.fragmentGeometryShaderID)
	{
		glDeleteShader(OGLRef.vertexGeometryShaderID);
		INFO("OpenGL: Failed to create the fragment shader. Disabling shaders and using fixed-function pipeline. Some emulation features will be disabled.\n");
		return OGLERROR_SHADER_CREATE_ERROR;
	}
	
	const char *fragmentShaderProgramChar = fragmentShaderProgram.c_str();
	glShaderSource(OGLRef.fragmentGeometryShaderID, 1, (const GLchar **)&fragmentShaderProgramChar, NULL);
	glCompileShader(OGLRef.fragmentGeometryShaderID);
	if (!this->ValidateShaderCompile(OGLRef.fragmentGeometryShaderID))
	{
		glDeleteShader(OGLRef.vertexGeometryShaderID);
		glDeleteShader(OGLRef.fragmentGeometryShaderID);
		INFO("OpenGL: Failed to compile the fragment shader. Disabling shaders and using fixed-function pipeline. Some emulation features will be disabled.\n");
		return OGLERROR_SHADER_CREATE_ERROR;
	}
	
	OGLRef.programGeometryID = glCreateProgram();
	if(!OGLRef.programGeometryID)
	{
		glDeleteShader(OGLRef.vertexGeometryShaderID);
		glDeleteShader(OGLRef.fragmentGeometryShaderID);
		INFO("OpenGL: Failed to create the shader program. Disabling shaders and using fixed-function pipeline. Some emulation features will be disabled.\n");
		return OGLERROR_SHADER_CREATE_ERROR;
	}
	
	glAttachShader(OGLRef.programGeometryID, OGLRef.vertexGeometryShaderID);
	glAttachShader(OGLRef.programGeometryID, OGLRef.fragmentGeometryShaderID);
	
	this->InitGeometryProgramBindings();
	
	glLinkProgram(OGLRef.programGeometryID);
	if (!this->ValidateShaderProgramLink(OGLRef.programGeometryID))
	{
		glDetachShader(OGLRef.programGeometryID, OGLRef.vertexGeometryShaderID);
		glDetachShader(OGLRef.programGeometryID, OGLRef.fragmentGeometryShaderID);
		glDeleteProgram(OGLRef.programGeometryID);
		glDeleteShader(OGLRef.vertexGeometryShaderID);
		glDeleteShader(OGLRef.fragmentGeometryShaderID);
		INFO("OpenGL: Failed to link the shader program. Disabling shaders and using fixed-function pipeline. Some emulation features will be disabled.\n");
		return OGLERROR_SHADER_CREATE_ERROR;
	}
	
	glValidateProgram(OGLRef.programGeometryID);
	
	this->InitGeometryProgramShaderLocations();
	
	INFO("OpenGL: Successfully created shaders.\n");
	
	this->CreateToonTable();
	
	return OGLERROR_NOERR;
}

void OpenGLRenderer_1_2::DestroyGeometryProgram()
{
	if(!this->isShaderSupported)
	{
		return;
	}
	
	OGLRenderRef &OGLRef = *this->ref;
	
	glUseProgram(0);
	
	glDetachShader(OGLRef.programGeometryID, OGLRef.vertexGeometryShaderID);
	glDetachShader(OGLRef.programGeometryID, OGLRef.fragmentGeometryShaderID);
	
	glDeleteProgram(OGLRef.programGeometryID);
	glDeleteShader(OGLRef.vertexGeometryShaderID);
	glDeleteShader(OGLRef.fragmentGeometryShaderID);
	
	this->DestroyToonTable();
	
	this->isShaderSupported = false;
}

Render3DError OpenGLRenderer_1_2::CreateVAOs()
{
	OGLRenderRef &OGLRef = *this->ref;
	
	glGenVertexArrays(1, &OGLRef.vaoGeometryStatesID);
	glGenVertexArrays(1, &OGLRef.vaoPostprocessStatesID);
	
	glBindVertexArray(OGLRef.vaoGeometryStatesID);
	glBindBufferARB(GL_ARRAY_BUFFER_ARB, OGLRef.vboGeometryVtxID);
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, OGLRef.iboGeometryIndexID);
	
	glEnableVertexAttribArray(OGLVertexAttributeID_Position);
	glEnableVertexAttribArray(OGLVertexAttributeID_TexCoord0);
	glEnableVertexAttribArray(OGLVertexAttributeID_Color);
	glVertexAttribPointer(OGLVertexAttributeID_Position, 4, GL_FLOAT, GL_FALSE, sizeof(VERT), (const GLvoid *)offsetof(VERT, coord));
	glVertexAttribPointer(OGLVertexAttributeID_TexCoord0, 2, GL_FLOAT, GL_FALSE, sizeof(VERT), (const GLvoid *)offsetof(VERT, texcoord));
	glVertexAttribPointer(OGLVertexAttributeID_Color, 3, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(VERT), (const GLvoid *)offsetof(VERT, color));
	
	glBindVertexArray(0);
	
	glBindVertexArray(OGLRef.vaoPostprocessStatesID);
	glBindBufferARB(GL_ARRAY_BUFFER_ARB, OGLRef.vboPostprocessVtxID);
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, OGLRef.iboPostprocessIndexID);
	
	glEnableVertexAttribArray(OGLVertexAttributeID_Position);
	glEnableVertexAttribArray(OGLVertexAttributeID_TexCoord0);
	glVertexAttribPointer(OGLVertexAttributeID_Position, 2, GL_FLOAT, GL_FALSE, 0, 0);
	glVertexAttribPointer(OGLVertexAttributeID_TexCoord0, 2, GL_FLOAT, GL_FALSE, 0, (const GLvoid *)(sizeof(GLfloat) * 8));
	
	glBindVertexArray(0);
	
	return OGLERROR_NOERR;
}

void OpenGLRenderer_1_2::DestroyVAOs()
{
	OGLRenderRef &OGLRef = *this->ref;
	
	if (!this->isVAOSupported)
	{
		return;
	}
	
	glBindVertexArray(0);
	glDeleteVertexArrays(1, &OGLRef.vaoGeometryStatesID);
	glDeleteVertexArrays(1, &OGLRef.vaoPostprocessStatesID);
	
	this->isVAOSupported = false;
}

Render3DError OpenGLRenderer_1_2::CreateFBOs()
{
	OGLRenderRef &OGLRef = *this->ref;
	
	// Set up FBO render targets
	glGenTextures(1, &OGLRef.texCIColorID);
	glGenTextures(1, &OGLRef.texCIDepthID);
	glGenTextures(1, &OGLRef.texCIFogAttrID);
	glGenTextures(1, &OGLRef.texCIPolyID);
	glGenTextures(1, &OGLRef.texCIDepthStencilID);
	
	glGenTextures(1, &OGLRef.texFinalColorID);
	glGenTextures(1, &OGLRef.texGColorID);
	glGenTextures(1, &OGLRef.texGDepthID);
	glGenTextures(1, &OGLRef.texGFogAttrID);
	glGenTextures(1, &OGLRef.texGPolyID);
	glGenTextures(1, &OGLRef.texGDepthStencilID);
	glGenTextures(1, &OGLRef.texPostprocessFogID);
	
	if (this->willFlipFramebufferOnGPU)
	{
		glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_FinalColor);
		glBindTexture(GL_TEXTURE_2D, OGLRef.texFinalColorID);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, this->_framebufferWidth, this->_framebufferHeight, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
	}
	
	glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_GColor);
	glBindTexture(GL_TEXTURE_2D, OGLRef.texGDepthStencilID);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8_EXT, this->_framebufferWidth, this->_framebufferHeight, 0, GL_DEPTH_STENCIL_EXT, GL_UNSIGNED_INT_24_8_EXT, NULL);
	
	glBindTexture(GL_TEXTURE_2D, OGLRef.texGColorID);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, this->_framebufferWidth, this->_framebufferHeight, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
	
	glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_GDepth);
	glBindTexture(GL_TEXTURE_2D, OGLRef.texGDepthID);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, this->_framebufferWidth, this->_framebufferHeight, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
	
	glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_GPolyID);
	glBindTexture(GL_TEXTURE_2D, OGLRef.texGPolyID);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, this->_framebufferWidth, this->_framebufferHeight, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
	
	glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_FogAttr);
	glBindTexture(GL_TEXTURE_2D, OGLRef.texGFogAttrID);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, this->_framebufferWidth, this->_framebufferHeight, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
	
	glActiveTexture(GL_TEXTURE0);
	
	glBindTexture(GL_TEXTURE_2D, OGLRef.texPostprocessFogID);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, this->_framebufferWidth, this->_framebufferHeight, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
	
	glBindTexture(GL_TEXTURE_2D, OGLRef.texCIColorID);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, GPU_FRAMEBUFFER_NATIVE_WIDTH, GPU_FRAMEBUFFER_NATIVE_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
	
	glBindTexture(GL_TEXTURE_2D, OGLRef.texCIDepthStencilID);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8_EXT, GPU_FRAMEBUFFER_NATIVE_WIDTH, GPU_FRAMEBUFFER_NATIVE_HEIGHT, 0, GL_DEPTH_STENCIL_EXT, GL_UNSIGNED_INT_24_8_EXT, NULL);
	
	glBindTexture(GL_TEXTURE_2D, OGLRef.texCIDepthID);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, GPU_FRAMEBUFFER_NATIVE_WIDTH, GPU_FRAMEBUFFER_NATIVE_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
	
	glBindTexture(GL_TEXTURE_2D, OGLRef.texCIPolyID);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, GPU_FRAMEBUFFER_NATIVE_WIDTH, GPU_FRAMEBUFFER_NATIVE_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
	
	glBindTexture(GL_TEXTURE_2D, OGLRef.texCIFogAttrID);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, GPU_FRAMEBUFFER_NATIVE_WIDTH, GPU_FRAMEBUFFER_NATIVE_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
	
	glBindTexture(GL_TEXTURE_2D, 0);
	
	// Set up RBOs
	if (this->willConvertFramebufferOnGPU)
	{
		glGenRenderbuffersEXT(1, &OGLRef.rboFramebufferRGBA6665ID);
		glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, OGLRef.rboFramebufferRGBA6665ID);
		glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_RGBA, this->_framebufferWidth, this->_framebufferHeight);
	}
	
	// Set up FBOs
	glGenFramebuffersEXT(1, &OGLRef.fboClearImageID);
	glGenFramebuffersEXT(1, &OGLRef.fboRenderID);
	glGenFramebuffersEXT(1, &OGLRef.fboPostprocessID);
	
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, OGLRef.fboClearImageID);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, OGLRef.texCIColorID, 0);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT1_EXT, GL_TEXTURE_2D, OGLRef.texCIDepthID, 0);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT2_EXT, GL_TEXTURE_2D, OGLRef.texCIPolyID, 0);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT3_EXT, GL_TEXTURE_2D, OGLRef.texCIFogAttrID, 0);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_TEXTURE_2D, OGLRef.texCIDepthStencilID, 0);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_STENCIL_ATTACHMENT_EXT, GL_TEXTURE_2D, OGLRef.texCIDepthStencilID, 0);
	
	if (glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT)
	{
		INFO("OpenGL: Failed to created FBOs. Some emulation features will be disabled.\n");
		
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
		glDeleteFramebuffersEXT(1, &OGLRef.fboClearImageID);
		glDeleteFramebuffersEXT(1, &OGLRef.fboRenderID);
		glDeleteFramebuffersEXT(1, &OGLRef.fboPostprocessID);
		glDeleteTextures(1, &OGLRef.texCIColorID);
		glDeleteTextures(1, &OGLRef.texCIDepthID);
		glDeleteTextures(1, &OGLRef.texCIFogAttrID);
		glDeleteTextures(1, &OGLRef.texCIPolyID);
		glDeleteTextures(1, &OGLRef.texCIDepthStencilID);
		glDeleteTextures(1, &OGLRef.texGColorID);
		glDeleteTextures(1, &OGLRef.texGDepthID);
		glDeleteTextures(1, &OGLRef.texGPolyID);
		glDeleteTextures(1, &OGLRef.texGFogAttrID);
		glDeleteTextures(1, &OGLRef.texGDepthStencilID);
		glDeleteTextures(1, &OGLRef.texPostprocessFogID);
		glDeleteTextures(1, &OGLRef.texFinalColorID);
		glDeleteRenderbuffersEXT(1, &OGLRef.rboFramebufferRGBA6665ID);
		
		OGLRef.fboClearImageID = 0;
		OGLRef.fboRenderID = 0;
		OGLRef.fboPostprocessID = 0;
		
		this->isFBOSupported = false;
		return OGLERROR_FBO_CREATE_ERROR;
	}
	
	glDrawBuffers(4, RenderDrawList);
	glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
	
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, OGLRef.fboRenderID);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, OGLRef.texGColorID, 0);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT1_EXT, GL_TEXTURE_2D, OGLRef.texGDepthID, 0);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT2_EXT, GL_TEXTURE_2D, OGLRef.texGPolyID, 0);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT3_EXT, GL_TEXTURE_2D, OGLRef.texGFogAttrID, 0);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_TEXTURE_2D, OGLRef.texGDepthStencilID, 0);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_STENCIL_ATTACHMENT_EXT, GL_TEXTURE_2D, OGLRef.texGDepthStencilID, 0);
	
	if (glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT)
	{
		INFO("OpenGL: Failed to created FBOs. Some emulation features will be disabled.\n");
		
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
		glDeleteFramebuffersEXT(1, &OGLRef.fboClearImageID);
		glDeleteFramebuffersEXT(1, &OGLRef.fboRenderID);
		glDeleteFramebuffersEXT(1, &OGLRef.fboPostprocessID);
		glDeleteTextures(1, &OGLRef.texCIColorID);
		glDeleteTextures(1, &OGLRef.texCIDepthID);
		glDeleteTextures(1, &OGLRef.texCIFogAttrID);
		glDeleteTextures(1, &OGLRef.texCIPolyID);
		glDeleteTextures(1, &OGLRef.texCIDepthStencilID);
		glDeleteTextures(1, &OGLRef.texGColorID);
		glDeleteTextures(1, &OGLRef.texGDepthID);
		glDeleteTextures(1, &OGLRef.texGPolyID);
		glDeleteTextures(1, &OGLRef.texGFogAttrID);
		glDeleteTextures(1, &OGLRef.texGDepthStencilID);
		glDeleteTextures(1, &OGLRef.texPostprocessFogID);
		glDeleteTextures(1, &OGLRef.texFinalColorID);
		glDeleteRenderbuffersEXT(1, &OGLRef.rboFramebufferRGBA6665ID);
		
		OGLRef.fboClearImageID = 0;
		OGLRef.fboRenderID = 0;
		OGLRef.fboPostprocessID = 0;
		
		this->isFBOSupported = false;
		return OGLERROR_FBO_CREATE_ERROR;
	}
	
	glDrawBuffers(4, RenderDrawList);
	glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
	
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, OGLRef.fboPostprocessID);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, OGLRef.texPostprocessFogID, 0);
	
	if (this->willFlipFramebufferOnGPU)
	{
		glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT1_EXT, GL_TEXTURE_2D, OGLRef.texFinalColorID, 0);
	}
	
	if (this->willConvertFramebufferOnGPU)
	{
		glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT2_EXT, GL_RENDERBUFFER_EXT, OGLRef.rboFramebufferRGBA6665ID);
	}
	
	if (glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT)
	{
		INFO("OpenGL: Failed to created FBOs. Some emulation features will be disabled.\n");
		
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
		glDeleteFramebuffersEXT(1, &OGLRef.fboClearImageID);
		glDeleteFramebuffersEXT(1, &OGLRef.fboRenderID);
		glDeleteFramebuffersEXT(1, &OGLRef.fboPostprocessID);
		glDeleteTextures(1, &OGLRef.texCIColorID);
		glDeleteTextures(1, &OGLRef.texCIDepthID);
		glDeleteTextures(1, &OGLRef.texCIFogAttrID);
		glDeleteTextures(1, &OGLRef.texCIPolyID);
		glDeleteTextures(1, &OGLRef.texCIDepthStencilID);
		glDeleteTextures(1, &OGLRef.texGColorID);
		glDeleteTextures(1, &OGLRef.texGDepthID);
		glDeleteTextures(1, &OGLRef.texGPolyID);
		glDeleteTextures(1, &OGLRef.texGFogAttrID);
		glDeleteTextures(1, &OGLRef.texGDepthStencilID);
		glDeleteTextures(1, &OGLRef.texPostprocessFogID);
		glDeleteTextures(1, &OGLRef.texFinalColorID);
		glDeleteRenderbuffersEXT(1, &OGLRef.rboFramebufferRGBA6665ID);
		
		OGLRef.fboClearImageID = 0;
		OGLRef.fboRenderID = 0;
		OGLRef.fboPostprocessID = 0;
		
		this->isFBOSupported = false;
		return OGLERROR_FBO_CREATE_ERROR;
	}
	
	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
	glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
	
	OGLRef.selectedRenderingFBO = OGLRef.fboRenderID;
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, OGLRef.selectedRenderingFBO);
	INFO("OpenGL: Successfully created FBOs.\n");
	
	return OGLERROR_NOERR;
}

void OpenGLRenderer_1_2::DestroyFBOs()
{
	if (!this->isFBOSupported)
	{
		return;
	}
	
	OGLRenderRef &OGLRef = *this->ref;
	
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
	glDeleteFramebuffersEXT(1, &OGLRef.fboClearImageID);
	glDeleteFramebuffersEXT(1, &OGLRef.fboRenderID);
	glDeleteFramebuffersEXT(1, &OGLRef.fboPostprocessID);
	glDeleteTextures(1, &OGLRef.texCIColorID);
	glDeleteTextures(1, &OGLRef.texCIDepthID);
	glDeleteTextures(1, &OGLRef.texCIFogAttrID);
	glDeleteTextures(1, &OGLRef.texCIPolyID);
	glDeleteTextures(1, &OGLRef.texCIDepthStencilID);
	glDeleteTextures(1, &OGLRef.texGColorID);
	glDeleteTextures(1, &OGLRef.texGDepthID);
	glDeleteTextures(1, &OGLRef.texGPolyID);
	glDeleteTextures(1, &OGLRef.texGFogAttrID);
	glDeleteTextures(1, &OGLRef.texGDepthStencilID);
	glDeleteTextures(1, &OGLRef.texPostprocessFogID);
	glDeleteTextures(1, &OGLRef.texFinalColorID);
	glDeleteRenderbuffersEXT(1, &OGLRef.rboFramebufferRGBA6665ID);
	
	OGLRef.fboClearImageID = 0;
	OGLRef.fboRenderID = 0;
	OGLRef.fboPostprocessID = 0;
	
	this->isFBOSupported = false;
}

Render3DError OpenGLRenderer_1_2::CreateMultisampledFBO()
{
	// Check the maximum number of samples that the driver supports and use that.
	// Since our target resolution is only 256x192 pixels, using the most samples
	// possible is the best thing to do.
	GLint maxSamples = (GLint)this->_deviceInfo.maxSamples;
	
	if (maxSamples < 2)
	{
		INFO("OpenGL: Driver does not support at least 2x multisampled FBOs. Multisample antialiasing will be disabled.\n");
		return OGLERROR_FEATURE_UNSUPPORTED;
	}
	else if (maxSamples > OGLRENDER_MAX_MULTISAMPLES)
	{
		maxSamples = OGLRENDER_MAX_MULTISAMPLES;
	}
	
	OGLRenderRef &OGLRef = *this->ref;
	
	// Set up FBO render targets
	glGenRenderbuffersEXT(1, &OGLRef.rboMSGColorID);
	glGenRenderbuffersEXT(1, &OGLRef.rboMSGDepthID);
	glGenRenderbuffersEXT(1, &OGLRef.rboMSGPolyID);
	glGenRenderbuffersEXT(1, &OGLRef.rboMSGFogAttrID);
	glGenRenderbuffersEXT(1, &OGLRef.rboMSGDepthStencilID);
	
	glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, OGLRef.rboMSGColorID);
	glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER_EXT, maxSamples, GL_RGBA, this->_framebufferWidth, this->_framebufferHeight);
	glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, OGLRef.rboMSGDepthID);
	glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER_EXT, maxSamples, GL_RGBA, this->_framebufferWidth, this->_framebufferHeight);
	glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, OGLRef.rboMSGPolyID);
	glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER_EXT, maxSamples, GL_RGBA, this->_framebufferWidth, this->_framebufferHeight);
	glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, OGLRef.rboMSGFogAttrID);
	glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER_EXT, maxSamples, GL_RGBA, this->_framebufferWidth, this->_framebufferHeight);
	glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, OGLRef.rboMSGDepthStencilID);
	glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER_EXT, maxSamples, GL_DEPTH24_STENCIL8_EXT, this->_framebufferWidth, this->_framebufferHeight);
	
	// Set up multisampled rendering FBO
	glGenFramebuffersEXT(1, &OGLRef.fboMSIntermediateRenderID);
	
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, OGLRef.fboMSIntermediateRenderID);
	glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_RENDERBUFFER_EXT, OGLRef.rboMSGColorID);
	glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT1_EXT, GL_RENDERBUFFER_EXT, OGLRef.rboMSGDepthID);
	glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT2_EXT, GL_RENDERBUFFER_EXT, OGLRef.rboMSGPolyID);
	glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT3_EXT, GL_RENDERBUFFER_EXT, OGLRef.rboMSGFogAttrID);
	glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, OGLRef.rboMSGDepthStencilID);
	glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_STENCIL_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, OGLRef.rboMSGDepthStencilID);
	
	if (glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT)
	{
		INFO("OpenGL: Failed to create multisampled FBO. Multisample antialiasing will be disabled.\n");
		
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
		glDeleteFramebuffersEXT(1, &OGLRef.fboMSIntermediateRenderID);
		glDeleteRenderbuffersEXT(1, &OGLRef.rboMSGColorID);
		glDeleteRenderbuffersEXT(1, &OGLRef.rboMSGDepthID);
		glDeleteRenderbuffersEXT(1, &OGLRef.rboMSGPolyID);
		glDeleteRenderbuffersEXT(1, &OGLRef.rboMSGFogAttrID);
		glDeleteRenderbuffersEXT(1, &OGLRef.rboMSGDepthStencilID);
		
		return OGLERROR_FBO_CREATE_ERROR;
	}
	
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, OGLRef.fboRenderID);
	INFO("OpenGL: Successfully created multisampled FBO.\n");
	
	return OGLERROR_NOERR;
}

void OpenGLRenderer_1_2::DestroyMultisampledFBO()
{
	if (!this->isMultisampledFBOSupported)
	{
		return;
	}
	
	OGLRenderRef &OGLRef = *this->ref;
	
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
	glDeleteFramebuffersEXT(1, &OGLRef.fboMSIntermediateRenderID);
	glDeleteRenderbuffersEXT(1, &OGLRef.rboMSGColorID);
	glDeleteRenderbuffersEXT(1, &OGLRef.rboMSGDepthID);
	glDeleteRenderbuffersEXT(1, &OGLRef.rboMSGPolyID);
	glDeleteRenderbuffersEXT(1, &OGLRef.rboMSGFogAttrID);
	glDeleteRenderbuffersEXT(1, &OGLRef.rboMSGDepthStencilID);
	
	this->isMultisampledFBOSupported = false;
}

Render3DError OpenGLRenderer_1_2::InitFinalRenderStates(const std::set<std::string> *oglExtensionSet)
{
	OGLRenderRef &OGLRef = *this->ref;
	
	bool isTexMirroredRepeatSupported = this->IsExtensionPresent(oglExtensionSet, "GL_ARB_texture_mirrored_repeat");
	bool isBlendFuncSeparateSupported = this->IsExtensionPresent(oglExtensionSet, "GL_EXT_blend_func_separate");
	bool isBlendEquationSeparateSupported = this->IsExtensionPresent(oglExtensionSet, "GL_EXT_blend_equation_separate");
	
	// Blending Support
	if (isBlendFuncSeparateSupported)
	{
		if (isBlendEquationSeparateSupported)
		{
			// we want to use alpha destination blending so we can track the last-rendered alpha value
			// test: new super mario brothers renders the stormclouds at the beginning
			glBlendFuncSeparateEXT(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA, GL_DST_ALPHA);
			glBlendEquationSeparateEXT(GL_FUNC_ADD, GL_MAX);
		}
		else
		{
			glBlendFuncSeparateEXT(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_DST_ALPHA);
		}
	}
	else
	{
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	
	// Mirrored Repeat Mode Support
	OGLRef.stateTexMirroredRepeat = isTexMirroredRepeatSupported ? GL_MIRRORED_REPEAT : GL_REPEAT;
	
	// Map the vertex list's colors with 4 floats per color. This is being done
	// because OpenGL needs 4-colors per vertex to support translucency. (The DS
	// uses 3-colors per vertex, and adds alpha through the poly, so we can't
	// simply reference the colors+alpha from just the vertices by themselves.)
	OGLRef.color4fBuffer = (this->isShaderSupported) ? NULL : new GLfloat[VERTLIST_SIZE * 4];
	
	// If VBOs aren't supported, then we need to create the index buffer on the
	// client side so that we have a buffer to update.
	OGLRef.vertIndexBuffer = (this->isVBOSupported) ? NULL : new GLushort[OGLRENDER_VERT_INDEX_BUFFER_COUNT];
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_2::InitTextures()
{
	this->ExpandFreeTextures();
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_2::InitTables()
{
	static bool needTableInit = true;
	
	if (needTableInit)
	{
		for (size_t i = 0; i < 256; i++)
			material_8bit_to_float[i] = (GLfloat)(i * 4) / 255.0f;
		
		needTableInit = false;
	}
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_2::InitPostprocessingPrograms(const std::string &edgeMarkVtxShader,
															 const std::string &edgeMarkFragShader,
															 const std::string &fogVtxShader,
															 const std::string &fogFragShader,
															 const std::string &framebufferOutputVtxShader,
															 const std::string &framebufferOutputRGBA6665FragShader,
															 const std::string &framebufferOutputRGBA8888FragShader)
{
	return OGLERROR_FEATURE_UNSUPPORTED;
}

Render3DError OpenGLRenderer_1_2::DestroyPostprocessingPrograms()
{
	return OGLERROR_FEATURE_UNSUPPORTED;
}

Render3DError OpenGLRenderer_1_2::InitEdgeMarkProgramBindings()
{
	return OGLERROR_FEATURE_UNSUPPORTED;
}

Render3DError OpenGLRenderer_1_2::InitEdgeMarkProgramShaderLocations()
{
	return OGLERROR_FEATURE_UNSUPPORTED;
}

Render3DError OpenGLRenderer_1_2::InitFogProgramBindings()
{
	return OGLERROR_FEATURE_UNSUPPORTED;
}

Render3DError OpenGLRenderer_1_2::InitFogProgramShaderLocations()
{
	return OGLERROR_FEATURE_UNSUPPORTED;
}

Render3DError OpenGLRenderer_1_2::InitFramebufferOutputProgramBindings()
{
	return OGLERROR_FEATURE_UNSUPPORTED;
}

Render3DError OpenGLRenderer_1_2::InitFramebufferOutputShaderLocations()
{
	return OGLERROR_FEATURE_UNSUPPORTED;
}

Render3DError OpenGLRenderer_1_2::CreateToonTable()
{
	OGLRenderRef &OGLRef = *this->ref;
	u16 tempToonTable[32];
	memset(tempToonTable, 0, sizeof(tempToonTable));
	
	// The toon table is a special 1D texture where each pixel corresponds
	// to a specific color in the toon table.
	glGenTextures(1, &OGLRef.texToonTableID);
	glActiveTextureARB(GL_TEXTURE0_ARB + OGLTextureUnitID_ToonTable);
	glBindTexture(GL_TEXTURE_1D, OGLRef.texToonTableID);
	
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexImage1D(GL_TEXTURE_1D, 0, GL_RGB, 32, 0, GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, tempToonTable);
	
	glActiveTextureARB(GL_TEXTURE0_ARB);
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_2::DestroyToonTable()
{
	glDeleteTextures(1, &this->ref->texToonTableID);
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_2::UploadClearImage(const u16 *__restrict colorBuffer, const u32 *__restrict depthBuffer, const u8 *__restrict fogBuffer, const u8 *__restrict polyIDBuffer)
{
	OGLRenderRef &OGLRef = *this->ref;
	
	if (this->isShaderSupported)
	{
		for (size_t i = 0; i < GPU_FRAMEBUFFER_NATIVE_WIDTH * GPU_FRAMEBUFFER_NATIVE_HEIGHT; i++)
		{
			OGLRef.workingCIDepthStencilBuffer[i] = (depthBuffer[i] << 8) | polyIDBuffer[i];
			OGLRef.workingCIDepthBuffer[i] = depthBuffer[i] | 0xFF000000;
			OGLRef.workingCIFogAttributesBuffer[i] = (fogBuffer[i]) ? 0xFF0000FF : 0xFF000000;
			OGLRef.workingCIPolyIDBuffer[i] = (GLuint)polyIDBuffer[i] | 0xFF000000;
		}
	}
	else
	{
		for (size_t i = 0; i < GPU_FRAMEBUFFER_NATIVE_WIDTH * GPU_FRAMEBUFFER_NATIVE_HEIGHT; i++)
		{
			OGLRef.workingCIDepthStencilBuffer[i] = (depthBuffer[i] << 8) | polyIDBuffer[i];
		}
	}
	
	glActiveTextureARB(GL_TEXTURE0_ARB);
	
	glBindTexture(GL_TEXTURE_2D, OGLRef.texCIColorID);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GPU_FRAMEBUFFER_NATIVE_WIDTH, GPU_FRAMEBUFFER_NATIVE_HEIGHT, GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, colorBuffer);
	glBindTexture(GL_TEXTURE_2D, OGLRef.texCIDepthStencilID);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GPU_FRAMEBUFFER_NATIVE_WIDTH, GPU_FRAMEBUFFER_NATIVE_HEIGHT, GL_DEPTH_STENCIL_EXT, GL_UNSIGNED_INT_24_8_EXT, OGLRef.workingCIDepthStencilBuffer);
	
	if (this->isShaderSupported)
	{
		glBindTexture(GL_TEXTURE_2D, OGLRef.texCIDepthID);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GPU_FRAMEBUFFER_NATIVE_WIDTH, GPU_FRAMEBUFFER_NATIVE_HEIGHT, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, OGLRef.workingCIDepthBuffer);
		
		glBindTexture(GL_TEXTURE_2D, OGLRef.texCIFogAttrID);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GPU_FRAMEBUFFER_NATIVE_WIDTH, GPU_FRAMEBUFFER_NATIVE_HEIGHT, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, OGLRef.workingCIFogAttributesBuffer);
		
		glBindTexture(GL_TEXTURE_2D, OGLRef.texCIPolyID);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GPU_FRAMEBUFFER_NATIVE_WIDTH, GPU_FRAMEBUFFER_NATIVE_HEIGHT, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, OGLRef.workingCIPolyIDBuffer);
	}
	
	glBindTexture(GL_TEXTURE_2D, 0);
	
	return OGLERROR_NOERR;
}

void OpenGLRenderer_1_2::GetExtensionSet(std::set<std::string> *oglExtensionSet)
{
	std::string oglExtensionString = std::string((const char *)glGetString(GL_EXTENSIONS));
	
	size_t extStringStartLoc = 0;
	size_t delimiterLoc = oglExtensionString.find_first_of(' ', extStringStartLoc);
	while (delimiterLoc != std::string::npos)
	{
		std::string extensionName = oglExtensionString.substr(extStringStartLoc, delimiterLoc - extStringStartLoc);
		oglExtensionSet->insert(extensionName);
		
		extStringStartLoc = delimiterLoc + 1;
		delimiterLoc = oglExtensionString.find_first_of(' ', extStringStartLoc);
	}
	
	if (extStringStartLoc - oglExtensionString.length() > 0)
	{
		std::string extensionName = oglExtensionString.substr(extStringStartLoc, oglExtensionString.length() - extStringStartLoc);
		oglExtensionSet->insert(extensionName);
	}
}

Render3DError OpenGLRenderer_1_2::ExpandFreeTextures()
{
	static const GLsizei kInitTextures = 128;
	GLuint oglTempTextureID[kInitTextures];
	glGenTextures(kInitTextures, oglTempTextureID);
	
	for(GLsizei i = 0; i < kInitTextures; i++)
	{
		this->ref->freeTextureIDs.push(oglTempTextureID[i]);
	}
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_2::EnableVertexAttributes()
{
	OGLRenderRef &OGLRef = *this->ref;
	
	if (this->isVAOSupported)
	{
		glBindVertexArray(OGLRef.vaoGeometryStatesID);
	}
	else
	{
		if (this->isShaderSupported)
		{
			glEnableVertexAttribArray(OGLVertexAttributeID_Position);
			glEnableVertexAttribArray(OGLVertexAttributeID_TexCoord0);
			glEnableVertexAttribArray(OGLVertexAttributeID_Color);
			glVertexAttribPointer(OGLVertexAttributeID_Position, 4, GL_FLOAT, GL_FALSE, sizeof(VERT), OGLRef.vtxPtrPosition);
			glVertexAttribPointer(OGLVertexAttributeID_TexCoord0, 2, GL_FLOAT, GL_FALSE, sizeof(VERT), OGLRef.vtxPtrTexCoord);
			glVertexAttribPointer(OGLVertexAttributeID_Color, 3, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(VERT), OGLRef.vtxPtrColor);
		}
		else
		{
			glEnableClientState(GL_TEXTURE_COORD_ARRAY);
			glEnableClientState(GL_COLOR_ARRAY);
			glEnableClientState(GL_VERTEX_ARRAY);
			
			if (this->isVBOSupported)
			{
				glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
				glColorPointer(4, GL_FLOAT, 0, OGLRef.vtxPtrColor);
				glBindBufferARB(GL_ARRAY_BUFFER_ARB, OGLRef.vboGeometryVtxID);
			}
			else
			{
				glColorPointer(4, GL_FLOAT, 0, OGLRef.vtxPtrColor);
			}
			
			glVertexPointer(4, GL_FLOAT, sizeof(VERT), OGLRef.vtxPtrPosition);
			glTexCoordPointer(2, GL_FLOAT, sizeof(VERT), OGLRef.vtxPtrTexCoord);
		}
	}
	
	glActiveTextureARB(GL_TEXTURE0_ARB);
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_2::DisableVertexAttributes()
{
	if (this->isVAOSupported)
	{
		glBindVertexArray(0);
	}
	else
	{
		if (this->isShaderSupported)
		{
			glDisableVertexAttribArray(OGLVertexAttributeID_Position);
			glDisableVertexAttribArray(OGLVertexAttributeID_TexCoord0);
			glDisableVertexAttribArray(OGLVertexAttributeID_Color);
		}
		else
		{
			glDisableClientState(GL_VERTEX_ARRAY);
			glDisableClientState(GL_COLOR_ARRAY);
			glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		}
	}
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_2::DownsampleFBO()
{
	OGLRenderRef &OGLRef = *this->ref;
	
	if (this->isMultisampledFBOSupported && OGLRef.selectedRenderingFBO == OGLRef.fboMSIntermediateRenderID)
	{
		glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT, OGLRef.fboMSIntermediateRenderID);
		glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT, OGLRef.fboRenderID);
		
		// Blit the color buffer
		glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
		glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
		glBlitFramebufferEXT(0, 0, this->_framebufferWidth, this->_framebufferHeight, 0, 0, this->_framebufferWidth, this->_framebufferHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		
		// Blit the working depth buffer
		glReadBuffer(GL_COLOR_ATTACHMENT1_EXT);
		glDrawBuffer(GL_COLOR_ATTACHMENT1_EXT);
		glBlitFramebufferEXT(0, 0, this->_framebufferWidth, this->_framebufferHeight, 0, 0, this->_framebufferWidth, this->_framebufferHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		
		// Blit the polygon ID buffer
		glReadBuffer(GL_COLOR_ATTACHMENT2_EXT);
		glDrawBuffer(GL_COLOR_ATTACHMENT2_EXT);
		glBlitFramebufferEXT(0, 0, this->_framebufferWidth, this->_framebufferHeight, 0, 0, this->_framebufferWidth, this->_framebufferHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		
		// Blit the fog buffer
		glReadBuffer(GL_COLOR_ATTACHMENT3_EXT);
		glDrawBuffer(GL_COLOR_ATTACHMENT3_EXT);
		glBlitFramebufferEXT(0, 0, this->_framebufferWidth, this->_framebufferHeight, 0, 0, this->_framebufferWidth, this->_framebufferHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		
		// Reset framebuffer targets
		glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
		glDrawBuffers(4, RenderDrawList);
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, OGLRef.fboRenderID);
	}
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_2::ReadBackPixels()
{
	OGLRenderRef &OGLRef = *this->ref;
	
	if (!this->isPBOSupported)
	{
		this->_pixelReadNeedsFinish = true;
		return OGLERROR_NOERR;
	}
	
	if (this->_mappedFramebuffer != NULL)
	{
		glUnmapBufferARB(GL_PIXEL_PACK_BUFFER_ARB);
		this->_mappedFramebuffer = NULL;
	}
	
	// Flip the framebuffer in Y to match the coordinates of OpenGL and the NDS hardware.
	if (this->willFlipFramebufferOnGPU)
	{
		glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT, OGLRef.fboPostprocessID);
		glDrawBuffer(GL_COLOR_ATTACHMENT1_EXT);
		glBlitFramebufferEXT(0, this->_framebufferHeight, this->_framebufferWidth, 0, 0, 0, this->_framebufferWidth, this->_framebufferHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, OGLRef.fboPostprocessID);
		glReadBuffer(GL_COLOR_ATTACHMENT1_EXT);
	}
	
	if (this->willConvertFramebufferOnGPU)
	{
		// Perform the color space conversion while we're still on the GPU so
		// that we can avoid having to do it on the CPU.
		const GLuint convertProgramID = (this->_outputFormat == NDSColorFormat_BGR666_Rev) ? OGLRef.programFramebufferRGBA6665OutputID : OGLRef.programFramebufferRGBA8888OutputID;
		glDrawBuffer(GL_COLOR_ATTACHMENT2_EXT);
		
		glUseProgram(convertProgramID);
		glViewport(0, 0, this->_framebufferWidth, this->_framebufferHeight);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_STENCIL_TEST);
		glDisable(GL_BLEND);
		glDisable(GL_CULL_FACE);
		
		glBindBuffer(GL_ARRAY_BUFFER, OGLRef.vboPostprocessVtxID);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, OGLRef.iboPostprocessIndexID);
		glBindVertexArray(OGLRef.vaoPostprocessStatesID);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, 0);
		glBindVertexArray(0);
		
		// Read back the pixels.
		glReadBuffer(GL_COLOR_ATTACHMENT2_EXT);
	}
	
	// Read back the pixels in BGRA format, since legacy OpenGL devices may experience a performance
	// penalty if the readback is in any other format.
	glReadPixels(0, 0, this->_framebufferWidth, this->_framebufferHeight, GL_BGRA, GL_UNSIGNED_BYTE, 0);
	
	// Set the read and draw target buffers back to color attachment 0, which is always the default.
	if (this->willFlipFramebufferOnGPU || this->willConvertFramebufferOnGPU)
	{
		glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
		glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
	}
	
	this->_pixelReadNeedsFinish = true;
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_2::DeleteTexture(const TexCacheItem *item)
{
	this->ref->freeTextureIDs.push((GLuint)item->texid);
	if(this->currTexture == item)
	{
		this->currTexture = NULL;
	}
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_2::BeginRender(const GFX3D &engine)
{
	OGLRenderRef &OGLRef = *this->ref;
	
	if(!BEGINGL())
	{
		return OGLERROR_BEGINGL_FAILED;
	}
	
	if (this->isShaderSupported)
	{
		glUseProgram(OGLRef.programGeometryID);
		glUniform1i(OGLRef.uniformStateToonShadingMode, engine.renderState.shading);
		glUniform1i(OGLRef.uniformStateEnableAlphaTest, (engine.renderState.enableAlphaTest) ? GL_TRUE : GL_FALSE);
		glUniform1i(OGLRef.uniformStateEnableAntialiasing, (engine.renderState.enableAntialiasing) ? GL_TRUE : GL_FALSE);
		glUniform1i(OGLRef.uniformStateEnableEdgeMarking, (engine.renderState.enableEdgeMarking) ? GL_TRUE : GL_FALSE);
		glUniform1i(OGLRef.uniformStateUseWDepth, (engine.renderState.wbuffer) ? GL_TRUE : GL_FALSE);
		glUniform1f(OGLRef.uniformStateAlphaTestRef, divide5bitBy31_LUT[engine.renderState.alphaTestRef]);
	}
	else
	{
		if(engine.renderState.enableAlphaTest && (engine.renderState.alphaTestRef > 0))
		{
			glAlphaFunc(GL_GEQUAL, divide5bitBy31_LUT[engine.renderState.alphaTestRef]);
		}
		else
		{
			glAlphaFunc(GL_GREATER, 0);
		}
		
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
	}
	
	GLushort *indexPtr = NULL;
	
	if (this->isVBOSupported)
	{
		glBindBufferARB(GL_ARRAY_BUFFER_ARB, OGLRef.vboGeometryVtxID);
		glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, OGLRef.iboGeometryIndexID);
		indexPtr = (GLushort *)glMapBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, GL_WRITE_ONLY_ARB);
	}
	else
	{
		// If VBOs aren't supported, we need to use the client-side buffers here.
		OGLRef.vtxPtrPosition = &engine.vertlist->list[0].coord;
		OGLRef.vtxPtrTexCoord = &engine.vertlist->list[0].texcoord;
		OGLRef.vtxPtrColor = (this->isShaderSupported) ? (GLvoid *)&engine.vertlist->list[0].color : OGLRef.color4fBuffer;
		indexPtr = OGLRef.vertIndexBuffer;
	}
	
	size_t vertIndexCount = 0;
	
	for (size_t i = 0; i < engine.polylist->count; i++)
	{
		const POLY *thePoly = &engine.polylist->list[engine.indexlist.list[i]];
		const size_t polyType = thePoly->type;
		
		if (this->isShaderSupported)
		{
			for (size_t j = 0; j < polyType; j++)
			{
				const GLushort vertIndex = thePoly->vertIndexes[j];
				
				// While we're looping through our vertices, add each vertex index to
				// a buffer. For GFX3D_QUADS and GFX3D_QUAD_STRIP, we also add additional
				// vertices here to convert them to GL_TRIANGLES, which are much easier
				// to work with and won't be deprecated in future OpenGL versions.
				indexPtr[vertIndexCount++] = vertIndex;
				if (thePoly->vtxFormat == GFX3D_QUADS || thePoly->vtxFormat == GFX3D_QUAD_STRIP)
				{
					if (j == 2)
					{
						indexPtr[vertIndexCount++] = vertIndex;
					}
					else if (j == 3)
					{
						indexPtr[vertIndexCount++] = thePoly->vertIndexes[0];
					}
				}
			}
		}
		else
		{
			const GLfloat thePolyAlpha = (!thePoly->isWireframe() && thePoly->isTranslucent()) ? divide5bitBy31_LUT[thePoly->getAttributeAlpha()] : 1.0f;
			
			for (size_t j = 0; j < polyType; j++)
			{
				const GLushort vertIndex = thePoly->vertIndexes[j];
				const size_t colorIndex = vertIndex * 4;
				
				// Consolidate the vertex color and the poly alpha to our internal color buffer
				// so that OpenGL can use it.
				const VERT *vert = &engine.vertlist->list[vertIndex];
				OGLRef.color4fBuffer[colorIndex+0] = material_8bit_to_float[vert->color[0]];
				OGLRef.color4fBuffer[colorIndex+1] = material_8bit_to_float[vert->color[1]];
				OGLRef.color4fBuffer[colorIndex+2] = material_8bit_to_float[vert->color[2]];
				OGLRef.color4fBuffer[colorIndex+3] = thePolyAlpha;
				
				// While we're looping through our vertices, add each vertex index to a
				// buffer. For GFX3D_QUADS and GFX3D_QUAD_STRIP, we also add additional
				// vertices here to convert them to GL_TRIANGLES, which are much easier
				// to work with and won't be deprecated in future OpenGL versions.
				indexPtr[vertIndexCount++] = vertIndex;
				if (thePoly->vtxFormat == GFX3D_QUADS || thePoly->vtxFormat == GFX3D_QUAD_STRIP)
				{
					if (j == 2)
					{
						indexPtr[vertIndexCount++] = vertIndex;
					}
					else if (j == 3)
					{
						indexPtr[vertIndexCount++] = thePoly->vertIndexes[0];
					}
				}
			}
		}
	}
	
	if (this->isVBOSupported)
	{
		glUnmapBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB);
		glBufferSubDataARB(GL_ARRAY_BUFFER_ARB, 0, sizeof(VERT) * engine.vertlist->count, engine.vertlist);
	}
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_2::RenderGeometry(const GFX3D_State &renderState, const POLYLIST *polyList, const INDEXLIST *indexList)
{
	OGLRenderRef &OGLRef = *this->ref;
	const size_t polyCount = polyList->count;
	
	// Map GFX3D_QUADS and GFX3D_QUAD_STRIP to GL_TRIANGLES since we will convert them.
	//
	// Also map GFX3D_TRIANGLE_STRIP to GL_TRIANGLES. This is okay since this is actually
	// how the POLY struct stores triangle strip vertices, which is in sets of 3 vertices
	// each. This redefinition is necessary since uploading more than 3 indices at a time
	// will cause glDrawElements() to draw the triangle strip incorrectly.
	static const GLenum oglPrimitiveType[]	= {GL_TRIANGLES, GL_TRIANGLES, GL_TRIANGLES, GL_TRIANGLES,
											   GL_LINE_LOOP, GL_LINE_LOOP, GL_LINE_STRIP, GL_LINE_STRIP};
	
	static const GLsizei indexIncrementLUT[] = {3, 6, 3, 6, 3, 4, 3, 4};
	
	if (polyCount > 0)
	{
		glEnable(GL_DEPTH_TEST);
		
		if(renderState.enableAlphaBlending)
		{
			glEnable(GL_BLEND);
		}
		else
		{
			glDisable(GL_BLEND);
		}
		
		this->EnableVertexAttributes();
		
		this->_shadowPolyID.clear();
		for (size_t i = 0; i < polyCount; i++)
		{
			const POLY &thePoly = polyList->list[i];
			
			if (thePoly.getAttributePolygonMode() != POLYGON_MODE_SHADOW)
			{
				continue;
			}
			
			const u8 polyID = thePoly.getAttributePolygonID();
			
			if ( (polyID == 0) || (std::find(this->_shadowPolyID.begin(), this->_shadowPolyID.end(), polyID) != this->_shadowPolyID.end()) )
			{
				continue;
			}
			
			this->_shadowPolyID.push_back(polyID);
		}
		
		const POLY &firstPoly = polyList->list[indexList->list[0]];
		u32 lastPolyAttr = firstPoly.polyAttr;
		u32 lastTexParams = firstPoly.texParam;
		u32 lastTexPalette = firstPoly.texPalette;
		u32 lastViewport = firstPoly.viewport;
		
		this->SetupPolygon(firstPoly);
		this->SetupTexture(firstPoly, renderState.enableTexturing);
		this->SetupViewport(lastViewport);
		
		GLsizei vertIndexCount = 0;
		GLushort *indexBufferPtr = OGLRef.vertIndexBuffer;
		
		// Enumerate through all polygons and render
		for (size_t i = 0; i < polyCount; i++)
		{
			const POLY &thePoly = polyList->list[indexList->list[i]];
			
			// Set up the polygon if it changed
			if (lastPolyAttr != thePoly.polyAttr)
			{
				lastPolyAttr = thePoly.polyAttr;
				this->SetupPolygon(thePoly);
			}
			
			// Set up the texture if it changed
			if (lastTexParams != thePoly.texParam || lastTexPalette != thePoly.texPalette)
			{
				lastTexParams = thePoly.texParam;
				lastTexPalette = thePoly.texPalette;
				this->SetupTexture(thePoly, renderState.enableTexturing);
			}
			
			// Set up the viewport if it changed
			if (lastViewport != thePoly.viewport)
			{
				lastViewport = thePoly.viewport;
				this->SetupViewport(thePoly.viewport);
			}
			
			// In wireframe mode, redefine all primitives as GL_LINE_LOOP rather than
			// setting the polygon mode to GL_LINE though glPolygonMode(). Not only is
			// drawing more accurate this way, but it also allows GFX3D_QUADS and
			// GFX3D_QUAD_STRIP primitives to properly draw as wireframe without the
			// extra diagonal line.
			const GLenum polyPrimitive = (!thePoly.isWireframe()) ? oglPrimitiveType[thePoly.vtxFormat] : GL_LINE_LOOP;
			
			// Increment the vertex count
			vertIndexCount += indexIncrementLUT[thePoly.vtxFormat];
			
			// Look ahead to the next polygon to see if we can simply buffer the indices
			// instead of uploading them now. We can buffer if all polygon states remain
			// the same and we're not drawing a line loop or line strip.
			if (i+1 < polyCount)
			{
				const POLY *nextPoly = &polyList->list[indexList->list[i+1]];
				
				if (lastPolyAttr == nextPoly->polyAttr &&
					lastTexParams == nextPoly->texParam &&
					lastTexPalette == nextPoly->texPalette &&
					lastViewport == nextPoly->viewport &&
					polyPrimitive == oglPrimitiveType[nextPoly->vtxFormat] &&
					polyPrimitive != GL_LINE_LOOP &&
					polyPrimitive != GL_LINE_STRIP &&
					oglPrimitiveType[nextPoly->vtxFormat] != GL_LINE_LOOP &&
					oglPrimitiveType[nextPoly->vtxFormat] != GL_LINE_STRIP)
				{
					continue;
				}
			}
			
			// Render the polygons
			this->SetPolygonIndex(i);
			glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
			indexBufferPtr += vertIndexCount;
			vertIndexCount = 0;
		}
		
		this->DisableVertexAttributes();
	}
	
	this->DownsampleFBO();
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_2::EndRender(const u64 frameCount)
{
	//needs to happen before endgl because it could free some textureids for expired cache items
	TexCache_EvictFrame();
	
	this->ReadBackPixels();
	
	ENDGL();
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_2::UpdateToonTable(const u16 *toonTableBuffer)
{
	glActiveTextureARB(GL_TEXTURE0_ARB + OGLTextureUnitID_ToonTable);
	glTexSubImage1D(GL_TEXTURE_1D, 0, 0, 32, GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, toonTableBuffer);
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_2::ClearUsingImage(const u16 *__restrict colorBuffer, const u32 *__restrict depthBuffer, const u8 *__restrict fogBuffer, const u8 *__restrict polyIDBuffer)
{
	if (!this->isFBOSupported)
	{
		return OGLERROR_FEATURE_UNSUPPORTED;
	}
	
	OGLRenderRef &OGLRef = *this->ref;
	
	this->UploadClearImage(colorBuffer, depthBuffer, fogBuffer, polyIDBuffer);
	
	glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT, OGLRef.fboClearImageID);
	glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT, OGLRef.fboRenderID);
	
	// It might seem wasteful to be doing a separate glClear(GL_STENCIL_BUFFER_BIT) instead
	// of simply blitting the stencil buffer with everything else.
	//
	// We do this because glBlitFramebufferEXT() for GL_STENCIL_BUFFER_BIT has been tested
	// to be unsupported on ATI/AMD GPUs running in compatibility mode. So we do the separate
	// glClear() for GL_STENCIL_BUFFER_BIT to keep these GPUs working.
	glClearStencil(polyIDBuffer[0]);
	glClear(GL_STENCIL_BUFFER_BIT);
	
	// Blit the working depth buffer
	glReadBuffer(GL_COLOR_ATTACHMENT1_EXT);
	glDrawBuffer(GL_COLOR_ATTACHMENT1_EXT);
	glBlitFramebufferEXT(0, GPU_FRAMEBUFFER_NATIVE_HEIGHT, GPU_FRAMEBUFFER_NATIVE_WIDTH, 0, 0, 0, this->_framebufferWidth, this->_framebufferHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	
	// Blit the polygon ID buffer
	glReadBuffer(GL_COLOR_ATTACHMENT2_EXT);
	glDrawBuffer(GL_COLOR_ATTACHMENT2_EXT);
	glBlitFramebufferEXT(0, GPU_FRAMEBUFFER_NATIVE_HEIGHT, GPU_FRAMEBUFFER_NATIVE_WIDTH, 0, 0, 0, this->_framebufferWidth, this->_framebufferHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	
	// Blit the fog buffer
	glReadBuffer(GL_COLOR_ATTACHMENT3_EXT);
	glDrawBuffer(GL_COLOR_ATTACHMENT3_EXT);
	glBlitFramebufferEXT(0, GPU_FRAMEBUFFER_NATIVE_HEIGHT, GPU_FRAMEBUFFER_NATIVE_WIDTH, 0, 0, 0, this->_framebufferWidth, this->_framebufferHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	
	// Blit the color buffer. Do this last so that color attachment 0 is set to the read FBO.
	glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
	glBlitFramebufferEXT(0, GPU_FRAMEBUFFER_NATIVE_HEIGHT, GPU_FRAMEBUFFER_NATIVE_WIDTH, 0, 0, 0, this->_framebufferWidth, this->_framebufferHeight, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
	
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, OGLRef.fboRenderID);
	glDrawBuffers(4, RenderDrawList);
	
	if (this->isMultisampledFBOSupported)
	{
		OGLRef.selectedRenderingFBO = (CommonSettings.GFX3D_Renderer_Multisample) ? OGLRef.fboMSIntermediateRenderID : OGLRef.fboRenderID;
		if (OGLRef.selectedRenderingFBO == OGLRef.fboMSIntermediateRenderID)
		{
			glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT, OGLRef.fboRenderID);
			glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT, OGLRef.selectedRenderingFBO);
			
			glClearStencil(polyIDBuffer[0]);
			glClear(GL_STENCIL_BUFFER_BIT);
			
			// Blit the working depth buffer
			glReadBuffer(GL_COLOR_ATTACHMENT1_EXT);
			glDrawBuffer(GL_COLOR_ATTACHMENT1_EXT);
			glBlitFramebufferEXT(0, 0, this->_framebufferWidth, this->_framebufferHeight, 0, 0, this->_framebufferWidth, this->_framebufferHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
			
			// Blit the polygon ID buffer
			glReadBuffer(GL_COLOR_ATTACHMENT2_EXT);
			glDrawBuffer(GL_COLOR_ATTACHMENT2_EXT);
			glBlitFramebufferEXT(0, 0, this->_framebufferWidth, this->_framebufferHeight, 0, 0, this->_framebufferWidth, this->_framebufferHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
			
			// Blit the fog buffer
			glReadBuffer(GL_COLOR_ATTACHMENT3_EXT);
			glDrawBuffer(GL_COLOR_ATTACHMENT3_EXT);
			glBlitFramebufferEXT(0, 0, this->_framebufferWidth, this->_framebufferHeight, 0, 0, this->_framebufferWidth, this->_framebufferHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
			
			// Blit the color buffer. Do this last so that color attachment 0 is set to the read FBO.
			glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
			glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
			glBlitFramebufferEXT(0, 0, this->_framebufferWidth, this->_framebufferHeight, 0, 0, this->_framebufferWidth, this->_framebufferHeight, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
			
			glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, OGLRef.selectedRenderingFBO);
			glDrawBuffers(4, RenderDrawList);
		}
	}
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_2::ClearUsingValues(const FragmentColor &clearColor6665, const FragmentAttributes &clearAttributes) const
{
	OGLRenderRef &OGLRef = *this->ref;
	
	if (this->isMultisampledFBOSupported)
	{
		OGLRef.selectedRenderingFBO = (CommonSettings.GFX3D_Renderer_Multisample) ? OGLRef.fboMSIntermediateRenderID : OGLRef.fboRenderID;
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, OGLRef.selectedRenderingFBO);
	}
	
	glDepthMask(GL_TRUE);
	
	if (this->isShaderSupported && this->isFBOSupported)
	{
		glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT); // texGColorID
		glClearColor(divide6bitBy63_LUT[clearColor6665.r], divide6bitBy63_LUT[clearColor6665.g], divide6bitBy63_LUT[clearColor6665.b], divide5bitBy31_LUT[clearColor6665.a]);
		glClearDepth((GLclampd)clearAttributes.depth / (GLclampd)0x00FFFFFF);
		glClearStencil(0xFF);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
		
		glDrawBuffer(GL_COLOR_ATTACHMENT1_EXT); // texGDepthID
		glClearColor((GLfloat)(clearAttributes.depth & 0x000000FF)/255.0f, (GLfloat)((clearAttributes.depth >> 8) & 0x000000FF)/255.0f, (GLfloat)((clearAttributes.depth >> 16) & 0x000000FF)/255.0f, 1.0);
		glClear(GL_COLOR_BUFFER_BIT);
		
		glDrawBuffer(GL_COLOR_ATTACHMENT2_EXT); // texGPolyID
		glClearColor((GLfloat)clearAttributes.opaquePolyID/63.0f, 0.0, 0.0, 1.0);
		glClear(GL_COLOR_BUFFER_BIT);
		
		glDrawBuffer(GL_COLOR_ATTACHMENT3_EXT); // texGFogAttrID
		glClearColor(clearAttributes.isFogged, 0.0, 0.0, 1.0);
		glClear(GL_COLOR_BUFFER_BIT);
		
		glDrawBuffers(4, RenderDrawList);
	}
	else
	{
		glClearColor(divide6bitBy63_LUT[clearColor6665.r], divide6bitBy63_LUT[clearColor6665.g], divide6bitBy63_LUT[clearColor6665.b], divide5bitBy31_LUT[clearColor6665.a]);
		glClearDepth((GLclampd)clearAttributes.depth / (GLclampd)0x00FFFFFF);
		glClearStencil(clearAttributes.opaquePolyID);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	}
	
	return OGLERROR_NOERR;
}

void OpenGLRenderer_1_2::SetPolygonIndex(const size_t index)
{
	this->_currentPolyIndex = index;
}

Render3DError OpenGLRenderer_1_2::SetupPolygon(const POLY &thePoly)
{
	const PolygonAttributes attr = thePoly.getAttributes();
	
	// Set up depth test mode
	static const GLenum oglDepthFunc[2] = {GL_LESS, GL_EQUAL};
	glDepthFunc(oglDepthFunc[attr.enableDepthEqualTest]);
	
	// Set up culling mode
	static const GLenum oglCullingMode[4] = {GL_FRONT_AND_BACK, GL_FRONT, GL_BACK, 0};
	GLenum cullingMode = oglCullingMode[attr.surfaceCullingMode];
	
	if (cullingMode == 0)
	{
		glDisable(GL_CULL_FACE);
	}
	else
	{
		glEnable(GL_CULL_FACE);
		glCullFace(cullingMode);
	}
	
	// Set up depth write
	GLboolean enableDepthWrite = GL_TRUE;
	
	// Handle shadow polys. Do this after checking for depth write, since shadow polys
	// can change this too.
	if (attr.polygonMode == POLYGON_MODE_SHADOW)
	{
		glEnable(GL_STENCIL_TEST);
		
		if (attr.polygonID == 0)
		{
			//when the polyID is zero, we are writing the shadow mask.
			//set stencilbuf = 1 where the shadow volume is obstructed by geometry.
			//do not write color or depth information.
			glStencilFunc(GL_NOTEQUAL, 0x80, 0xFF);
			glStencilOp(GL_KEEP, GL_ZERO, GL_KEEP);
			glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
			enableDepthWrite = GL_FALSE;
		}
		else
		{
			//when the polyid is nonzero, we are drawing the shadow poly.
			//only draw the shadow poly where the stencilbuf==1.
			glStencilFunc(GL_EQUAL, 0, 0xFF);
			glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			enableDepthWrite = GL_TRUE;
		}
	}
	else if ( attr.isTranslucent || (std::find(this->_shadowPolyID.begin(), this->_shadowPolyID.end(), attr.polygonID) == this->_shadowPolyID.end()) )
	{
		glDisable(GL_STENCIL_TEST);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		enableDepthWrite = (!attr.isTranslucent || ( (attr.polygonMode == POLYGON_MODE_DECAL) && attr.isOpaque ) || attr.enableAlphaDepthWrite) ? GL_TRUE : GL_FALSE;
	}
	else
	{
		glEnable(GL_STENCIL_TEST);
		glStencilFunc(GL_ALWAYS, 0x80, 0xFF);
		glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		enableDepthWrite = GL_TRUE;
	}
	
	glDepthMask(enableDepthWrite);
	
	// Set up polygon attributes
	if (this->isShaderSupported)
	{
		OGLRenderRef &OGLRef = *this->ref;
		glUniform1i(OGLRef.uniformPolyMode, attr.polygonMode);
		glUniform1i(OGLRef.uniformPolyEnableFog, (attr.enableRenderFog && !(attr.polygonMode == POLYGON_MODE_SHADOW && attr.polygonID == 0)) ? GL_TRUE : GL_FALSE);
		glUniform1f(OGLRef.uniformPolyAlpha, (!attr.isWireframe && attr.isTranslucent) ? divide5bitBy31_LUT[attr.alpha] : 1.0f);
		glUniform1i(OGLRef.uniformPolyID, attr.polygonID);
		glUniform1i(OGLRef.uniformPolyEnableDepthWrite, (!(attr.polygonMode == POLYGON_MODE_SHADOW && attr.polygonID == 0)) ? GL_TRUE : GL_FALSE);
		glUniform1i(OGLRef.uniformPolySetNewDepthForTranslucent, (attr.enableAlphaDepthWrite) ? GL_TRUE : GL_FALSE);
	}
	else
	{
		// Set the texture blending mode
		static const GLint oglTexBlendMode[4] = {GL_MODULATE, GL_DECAL, GL_MODULATE, GL_MODULATE};
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, oglTexBlendMode[attr.polygonMode]);
	}
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_2::SetupTexture(const POLY &thePoly, bool enableTexturing)
{
	OGLRenderRef &OGLRef = *this->ref;
	const PolygonTexParams params = thePoly.getTexParams();
	
	// Check if we need to use textures
	if (params.texFormat == TEXMODE_NONE || !enableTexturing)
	{
		if (this->isShaderSupported)
		{
			glUniform1i(OGLRef.uniformPolyEnableTexture, GL_FALSE);
			glUniform1i(OGLRef.uniformTexSingleBitAlpha, GL_FALSE);
		}
		else
		{
			glDisable(GL_TEXTURE_2D);
		}
		
		return OGLERROR_NOERR;
	}
	
	// Enable textures if they weren't already enabled
	if (this->isShaderSupported)
	{
		glUniform1i(OGLRef.uniformPolyEnableTexture, GL_TRUE);
		glUniform1i(OGLRef.uniformTexSingleBitAlpha, (params.texFormat != TEXMODE_A3I5 && params.texFormat != TEXMODE_A5I3) ? GL_TRUE : GL_FALSE);
	}
	else
	{
		glEnable(GL_TEXTURE_2D);
	}
	
	TexCacheItem *newTexture = TexCache_SetTexture(TexFormat_32bpp, thePoly.texParam, thePoly.texPalette);
	if(newTexture != this->currTexture)
	{
		this->currTexture = newTexture;
		//has the ogl renderer initialized the texture?
		if(this->currTexture->GetDeleteCallback() == NULL)
		{
			this->currTexture->SetDeleteCallback(&texDeleteCallback, this, NULL);
			
			if(OGLRef.freeTextureIDs.empty())
			{
				this->ExpandFreeTextures();
			}
			
			this->currTexture->texid = (u64)OGLRef.freeTextureIDs.front();
			OGLRef.freeTextureIDs.pop();
			
			glBindTexture(GL_TEXTURE_2D, (GLuint)this->currTexture->texid);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (params.enableRepeatS ? (params.enableMirroredRepeatS ? OGLRef.stateTexMirroredRepeat : GL_REPEAT) : GL_CLAMP_TO_EDGE));
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (params.enableRepeatT ? (params.enableMirroredRepeatT ? OGLRef.stateTexMirroredRepeat : GL_REPEAT) : GL_CLAMP_TO_EDGE));
			
			u32 *textureSrc = (u32 *)currTexture->decoded;
			size_t texWidth = currTexture->sizeX;
			size_t texHeight = currTexture->sizeY;
			
			if (this->_textureDeposterizeBuffer != NULL)
			{
				this->TextureDeposterize(textureSrc, texWidth, texHeight);
				textureSrc = this->_textureDeposterizeBuffer;
			}
			
			switch (this->_textureScalingFactor)
			{
				case 1:
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
					glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texWidth, texHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, textureSrc);
					break;
					
				case 2:
				{
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1);
					
					this->TextureUpscale<2>(textureSrc, texWidth, texHeight);
					glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texWidth, texHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, this->_textureUpscaleBuffer);
					
					glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA, currTexture->sizeX, currTexture->sizeY, 0, GL_RGBA, GL_UNSIGNED_BYTE, textureSrc);
					break;
				}
					
				case 4:
				{
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 2);
					
					this->TextureUpscale<4>(textureSrc, texWidth, texHeight);
					glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texWidth, texHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, this->_textureUpscaleBuffer);
					
					texWidth = currTexture->sizeX;
					texHeight = currTexture->sizeY;
					this->TextureUpscale<2>(textureSrc, texWidth, texHeight);
					glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA, texWidth, texHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, this->_textureUpscaleBuffer);
					
					glTexImage2D(GL_TEXTURE_2D, 2, GL_RGBA, currTexture->sizeX, currTexture->sizeY, 0, GL_RGBA, GL_UNSIGNED_BYTE, textureSrc);
					break;
				}
					
				default:
					break;
			}
			
			if (this->_textureSmooth)
			{
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (this->_textureScalingFactor > 1) ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, this->_deviceInfo.maxAnisotropy);
			}
			else
			{
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
				glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1.0f);
			}
		}
		else
		{
			//otherwise, just bind it
			glBindTexture(GL_TEXTURE_2D, (GLuint)this->currTexture->texid);
		}
		
		if (this->isShaderSupported)
		{
			glUniform2f(OGLRef.uniformPolyTexScale, this->currTexture->invSizeX, this->currTexture->invSizeY);
		}
		else
		{
			glMatrixMode(GL_TEXTURE);
			glLoadIdentity();
			glScalef(this->currTexture->invSizeX, this->currTexture->invSizeY, 1.0f);
		}
	}
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_2::SetupViewport(const u32 viewportValue)
{
	const GLfloat wScalar = this->_framebufferWidth / GPU_FRAMEBUFFER_NATIVE_WIDTH;
	const GLfloat hScalar = this->_framebufferHeight / GPU_FRAMEBUFFER_NATIVE_HEIGHT;
	
	VIEWPORT viewport;
	viewport.decode(viewportValue);
	glViewport(viewport.x * wScalar, viewport.y * hScalar, viewport.width * wScalar, viewport.height * hScalar);
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_2::Reset()
{
	OGLRenderRef &OGLRef = *this->ref;
	
	if(!BEGINGL())
	{
		return OGLERROR_BEGINGL_FAILED;
	}
	
	glFinish();
	
	if (!this->isShaderSupported)
	{
		glEnable(GL_NORMALIZE);
		glEnable(GL_TEXTURE_1D);
		glEnable(GL_TEXTURE_2D);
		glAlphaFunc(GL_GREATER, 0);
		glEnable(GL_ALPHA_TEST);
		glEnable(GL_BLEND);
	}
	
	ENDGL();
	
	this->_pixelReadNeedsFinish = false;
	
	if (OGLRef.color4fBuffer != NULL)
	{
		memset(OGLRef.color4fBuffer, 0, VERTLIST_SIZE * 4 * sizeof(GLfloat));
	}
	
	if (OGLRef.vertIndexBuffer != NULL)
	{
		memset(OGLRef.vertIndexBuffer, 0, OGLRENDER_VERT_INDEX_BUFFER_COUNT * sizeof(GLushort));
	}
	
	this->currTexture = NULL;
	this->_currentPolyIndex = 0;
	
	OGLRef.vtxPtrPosition = (GLvoid *)offsetof(VERT, coord);
	OGLRef.vtxPtrTexCoord = (GLvoid *)offsetof(VERT, texcoord);
	OGLRef.vtxPtrColor = (this->isShaderSupported) ? (GLvoid *)offsetof(VERT, color) : OGLRef.color4fBuffer;
	
	memset(this->clearImageColor16Buffer, 0, sizeof(this->clearImageColor16Buffer));
	memset(this->clearImageDepthBuffer, 0, sizeof(this->clearImageDepthBuffer));
	memset(this->clearImagePolyIDBuffer, 0, sizeof(this->clearImagePolyIDBuffer));
	memset(this->clearImageFogBuffer, 0, sizeof(this->clearImageFogBuffer));
	
	TexCache_Reset();
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_2::RenderFinish()
{
	if (!this->_renderNeedsFinish || !this->_pixelReadNeedsFinish)
	{
		return OGLERROR_NOERR;
	}
	
	FragmentColor *framebufferMain = (this->_willFlushFramebufferRGBA6665) ? GPU->GetEngineMain()->Get3DFramebufferRGBA6665() : NULL;
	u16 *framebufferRGBA5551 = (this->_willFlushFramebufferRGBA5551) ? GPU->GetEngineMain()->Get3DFramebufferRGBA5551() : NULL;
	
	if ( (framebufferMain != NULL) || (framebufferRGBA5551 != NULL) )
	{
		if(!BEGINGL())
		{
			return OGLERROR_BEGINGL_FAILED;
		}
		
		if (this->isPBOSupported)
		{
			this->_mappedFramebuffer = (FragmentColor *__restrict)glMapBufferARB(GL_PIXEL_PACK_BUFFER_ARB, GL_READ_ONLY_ARB);
			this->FlushFramebuffer(this->_mappedFramebuffer, framebufferMain, framebufferRGBA5551);
		}
		else
		{
			glReadPixels(0, 0, this->_framebufferWidth, this->_framebufferHeight, GL_BGRA, GL_UNSIGNED_BYTE, this->_framebufferColor);
			this->FlushFramebuffer(this->_framebufferColor, framebufferMain, framebufferRGBA5551);
		}
		
		ENDGL();
	}
	
	this->_pixelReadNeedsFinish = false;
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_2::SetFramebufferSize(size_t w, size_t h)
{
	OGLRenderRef &OGLRef = *this->ref;
	
	if (w < GPU_FRAMEBUFFER_NATIVE_WIDTH || h < GPU_FRAMEBUFFER_NATIVE_HEIGHT)
	{
		return OGLERROR_NOERR;
	}
	
	if (!BEGINGL())
	{
		return OGLERROR_BEGINGL_FAILED;
	}
	
	if (this->isPBOSupported)
	{
		if (this->_mappedFramebuffer != NULL)
		{
			glUnmapBufferARB(GL_PIXEL_PACK_BUFFER_ARB);
			this->_mappedFramebuffer = NULL;
		}
	}
	
	if (this->isFBOSupported)
	{
		glActiveTextureARB(GL_TEXTURE0_ARB + OGLTextureUnitID_GColor);
		glBindTexture(GL_TEXTURE_2D, OGLRef.texGDepthStencilID);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8_EXT, w, h, 0, GL_DEPTH_STENCIL_EXT, GL_UNSIGNED_INT_24_8_EXT, NULL);
		glBindTexture(GL_TEXTURE_2D, OGLRef.texGColorID);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
		
		glActiveTextureARB(GL_TEXTURE0_ARB + OGLTextureUnitID_GDepth);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
		
		glActiveTextureARB(GL_TEXTURE0_ARB + OGLTextureUnitID_GPolyID);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
		
		glActiveTextureARB(GL_TEXTURE0_ARB + OGLTextureUnitID_FogAttr);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
		
		glActiveTextureARB(GL_TEXTURE0_ARB);
		glBindTexture(GL_TEXTURE_2D, OGLRef.texPostprocessFogID);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
	}
	
	if (this->isMultisampledFBOSupported)
	{
		GLint maxSamples = (GLint)this->_deviceInfo.maxSamples;
		
		glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, OGLRef.rboMSGColorID);
		glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER_EXT, maxSamples, GL_RGBA, w, h);
		glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, OGLRef.rboMSGDepthID);
		glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER_EXT, maxSamples, GL_RGBA, w, h);
		glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, OGLRef.rboMSGPolyID);
		glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER_EXT, maxSamples, GL_RGBA, w, h);
		glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, OGLRef.rboMSGFogAttrID);
		glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER_EXT, maxSamples, GL_RGBA, w, h);
		glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, OGLRef.rboMSGDepthStencilID);
		glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER_EXT, maxSamples, GL_DEPTH24_STENCIL8_EXT, w, h);
	}
	
	if (this->willFlipFramebufferOnGPU)
	{
		glActiveTextureARB(GL_TEXTURE0_ARB + OGLTextureUnitID_FinalColor);
		glBindTexture(GL_TEXTURE_2D, OGLRef.texFinalColorID);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
	}
	
	if (this->willConvertFramebufferOnGPU)
	{
		glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, OGLRef.rboFramebufferRGBA6665ID);
		glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_RGBA, w, h);
	}
	
	const size_t newFramebufferColorSizeBytes = w * h * sizeof(FragmentColor);
	
	this->_framebufferWidth = w;
	this->_framebufferHeight = h;
	this->_framebufferColorSizeBytes = newFramebufferColorSizeBytes;
	
	if (this->isPBOSupported)
	{
		glBufferDataARB(GL_PIXEL_PACK_BUFFER_ARB, newFramebufferColorSizeBytes, NULL, GL_STREAM_READ_ARB);
		this->_framebufferColor = NULL;
	}
	else
	{
		FragmentColor *oldFramebufferColor = this->_framebufferColor;
		FragmentColor *newFramebufferColor = (FragmentColor *)malloc_alignedCacheLine(newFramebufferColorSizeBytes);
		this->_framebufferColor = newFramebufferColor;
		free_aligned(oldFramebufferColor);
	}
	
	if (oglrender_framebufferDidResizeCallback != NULL)
	{
		oglrender_framebufferDidResizeCallback(w, h);
	}
	
	ENDGL();
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_3::CreateToonTable()
{
	OGLRenderRef &OGLRef = *this->ref;
	u16 tempToonTable[32];
	memset(tempToonTable, 0, sizeof(tempToonTable));
	
	// The toon table is a special 1D texture where each pixel corresponds
	// to a specific color in the toon table.
	glGenTextures(1, &OGLRef.texToonTableID);
	glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_ToonTable);
	glBindTexture(GL_TEXTURE_1D, OGLRef.texToonTableID);
	
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexImage1D(GL_TEXTURE_1D, 0, GL_RGB, 32, 0, GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, tempToonTable);
	
	glActiveTexture(GL_TEXTURE0);
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_3::UpdateToonTable(const u16 *toonTableBuffer)
{
	glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_ToonTable);
	glTexSubImage1D(GL_TEXTURE_1D, 0, 0, 32, GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, toonTableBuffer);
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_3::UploadClearImage(const u16 *__restrict colorBuffer, const u32 *__restrict depthBuffer, const u8 *__restrict fogBuffer, const u8 *__restrict polyIDBuffer)
{
	OGLRenderRef &OGLRef = *this->ref;
	
	if (this->isShaderSupported)
	{
		for (size_t i = 0; i < GPU_FRAMEBUFFER_NATIVE_WIDTH * GPU_FRAMEBUFFER_NATIVE_HEIGHT; i++)
		{
			OGLRef.workingCIDepthStencilBuffer[i] = (depthBuffer[i] << 8) | polyIDBuffer[i];
			OGLRef.workingCIDepthBuffer[i] = depthBuffer[i] | 0xFF000000;
			OGLRef.workingCIFogAttributesBuffer[i] = (fogBuffer[i]) ? 0xFF0000FF : 0xFF000000;
			OGLRef.workingCIPolyIDBuffer[i] = (GLuint)polyIDBuffer[i] | 0xFF000000;
		}
	}
	else
	{
		for (size_t i = 0; i < GPU_FRAMEBUFFER_NATIVE_WIDTH * GPU_FRAMEBUFFER_NATIVE_HEIGHT; i++)
		{
			OGLRef.workingCIDepthStencilBuffer[i] = (depthBuffer[i] << 8) | polyIDBuffer[i];
		}
	}
	
	glActiveTexture(GL_TEXTURE0);
	
	glBindTexture(GL_TEXTURE_2D, OGLRef.texCIColorID);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GPU_FRAMEBUFFER_NATIVE_WIDTH, GPU_FRAMEBUFFER_NATIVE_HEIGHT, GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, colorBuffer);
	glBindTexture(GL_TEXTURE_2D, OGLRef.texCIDepthStencilID);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GPU_FRAMEBUFFER_NATIVE_WIDTH, GPU_FRAMEBUFFER_NATIVE_HEIGHT, GL_DEPTH_STENCIL_EXT, GL_UNSIGNED_INT_24_8_EXT, OGLRef.workingCIDepthStencilBuffer);
	
	if (this->isShaderSupported)
	{
		glBindTexture(GL_TEXTURE_2D, OGLRef.texCIDepthID);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GPU_FRAMEBUFFER_NATIVE_WIDTH, GPU_FRAMEBUFFER_NATIVE_HEIGHT, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, OGLRef.workingCIDepthBuffer);
		
		glBindTexture(GL_TEXTURE_2D, OGLRef.texCIFogAttrID);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GPU_FRAMEBUFFER_NATIVE_WIDTH, GPU_FRAMEBUFFER_NATIVE_HEIGHT, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, OGLRef.workingCIFogAttributesBuffer);
		
		glBindTexture(GL_TEXTURE_2D, OGLRef.texCIPolyID);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GPU_FRAMEBUFFER_NATIVE_WIDTH, GPU_FRAMEBUFFER_NATIVE_HEIGHT, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, OGLRef.workingCIPolyIDBuffer);
	}
	
	glBindTexture(GL_TEXTURE_2D, 0);
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_3::SetFramebufferSize(size_t w, size_t h)
{
	OGLRenderRef &OGLRef = *this->ref;
	
	if (w < GPU_FRAMEBUFFER_NATIVE_WIDTH || h < GPU_FRAMEBUFFER_NATIVE_HEIGHT)
	{
		return OGLERROR_NOERR;
	}
	
	if (!BEGINGL())
	{
		return OGLERROR_BEGINGL_FAILED;
	}
	
	if (this->isPBOSupported)
	{
		if (this->_mappedFramebuffer != NULL)
		{
			glUnmapBufferARB(GL_PIXEL_PACK_BUFFER_ARB);
			this->_mappedFramebuffer = NULL;
		}
	}
	
	if (this->isFBOSupported)
	{
		glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_GColor);
		glBindTexture(GL_TEXTURE_2D, OGLRef.texGDepthStencilID);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8_EXT, w, h, 0, GL_DEPTH_STENCIL_EXT, GL_UNSIGNED_INT_24_8_EXT, NULL);
		glBindTexture(GL_TEXTURE_2D, OGLRef.texGColorID);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
		
		glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_GDepth);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
		
		glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_GPolyID);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
		
		glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_FogAttr);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
		
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, OGLRef.texPostprocessFogID);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
	}
	
	if (this->isMultisampledFBOSupported)
	{
		GLint maxSamples = (GLint)this->_deviceInfo.maxSamples;
		
		glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, OGLRef.rboMSGColorID);
		glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER_EXT, maxSamples, GL_RGBA, w, h);
		glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, OGLRef.rboMSGDepthID);
		glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER_EXT, maxSamples, GL_RGBA, w, h);
		glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, OGLRef.rboMSGPolyID);
		glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER_EXT, maxSamples, GL_RGBA, w, h);
		glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, OGLRef.rboMSGFogAttrID);
		glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER_EXT, maxSamples, GL_RGBA, w, h);
		glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, OGLRef.rboMSGDepthStencilID);
		glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER_EXT, maxSamples, GL_DEPTH24_STENCIL8_EXT, w, h);
	}
	
	if (this->willFlipFramebufferOnGPU)
	{
		glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_FinalColor);
		glBindTexture(GL_TEXTURE_2D, OGLRef.texFinalColorID);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
	}
	
	if (this->willConvertFramebufferOnGPU)
	{
		glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, OGLRef.rboFramebufferRGBA6665ID);
		glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_RGBA, w, h);
	}
	
	const size_t newFramebufferColorSizeBytes = w * h * sizeof(FragmentColor);
	
	this->_framebufferWidth = w;
	this->_framebufferHeight = h;
	this->_framebufferColorSizeBytes = newFramebufferColorSizeBytes;
	
	if (this->isPBOSupported)
	{
		glBufferDataARB(GL_PIXEL_PACK_BUFFER_ARB, newFramebufferColorSizeBytes, NULL, GL_STREAM_READ_ARB);
		this->_framebufferColor = NULL;
	}
	else
	{
		FragmentColor *oldFramebufferColor = this->_framebufferColor;
		FragmentColor *newFramebufferColor = (FragmentColor *)malloc_alignedCacheLine(newFramebufferColorSizeBytes);
		this->_framebufferColor = newFramebufferColor;
		free_aligned(oldFramebufferColor);
	}
	
	if (oglrender_framebufferDidResizeCallback != NULL)
	{
		oglrender_framebufferDidResizeCallback(w, h);
	}
	
	ENDGL();
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_4::InitFinalRenderStates(const std::set<std::string> *oglExtensionSet)
{
	OGLRenderRef &OGLRef = *this->ref;
	
	bool isBlendEquationSeparateSupported = this->IsExtensionPresent(oglExtensionSet, "GL_EXT_blend_equation_separate");
	
	// Blending Support
	if (isBlendEquationSeparateSupported)
	{
		// we want to use alpha destination blending so we can track the last-rendered alpha value
		// test: new super mario brothers renders the stormclouds at the beginning
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA, GL_DST_ALPHA);
		glBlendEquationSeparateEXT(GL_FUNC_ADD, GL_MAX);
	}
	else
	{
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_DST_ALPHA);
	}
	
	// Mirrored Repeat Mode Support
	OGLRef.stateTexMirroredRepeat = GL_MIRRORED_REPEAT;
	
	// Map the vertex list's colors with 4 floats per color. This is being done
	// because OpenGL needs 4-colors per vertex to support translucency. (The DS
	// uses 3-colors per vertex, and adds alpha through the poly, so we can't
	// simply reference the colors+alpha from just the vertices by themselves.)
	OGLRef.color4fBuffer = (this->isShaderSupported) ? NULL : new GLfloat[VERTLIST_SIZE * 4];
	
	// If VBOs aren't supported, then we need to create the index buffer on the
	// client side so that we have a buffer to update.
	OGLRef.vertIndexBuffer = (this->isVBOSupported) ? NULL : new GLushort[OGLRENDER_VERT_INDEX_BUFFER_COUNT];
	
	return OGLERROR_NOERR;
}

OpenGLRenderer_1_5::~OpenGLRenderer_1_5()
{
	glFinish();
	
	DestroyVAOs();
	DestroyVBOs();
	DestroyPBOs();
}

Render3DError OpenGLRenderer_1_5::CreateVBOs()
{
	OGLRenderRef &OGLRef = *this->ref;
	
	glGenBuffers(1, &OGLRef.vboGeometryVtxID);
	glGenBuffers(1, &OGLRef.iboGeometryIndexID);
	glGenBuffers(1, &OGLRef.vboPostprocessVtxID);
	glGenBuffers(1, &OGLRef.iboPostprocessIndexID);
	
	glBindBuffer(GL_ARRAY_BUFFER, OGLRef.vboGeometryVtxID);
	glBufferData(GL_ARRAY_BUFFER, VERTLIST_SIZE * sizeof(VERT), NULL, GL_STREAM_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, OGLRef.iboGeometryIndexID);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, OGLRENDER_VERT_INDEX_BUFFER_COUNT * sizeof(GLushort), NULL, GL_STREAM_DRAW);
	
	glBindBuffer(GL_ARRAY_BUFFER, OGLRef.vboPostprocessVtxID);
	glBufferData(GL_ARRAY_BUFFER, sizeof(PostprocessVtxBuffer), PostprocessVtxBuffer, GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, OGLRef.iboPostprocessIndexID);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(PostprocessElementBuffer), PostprocessElementBuffer, GL_STATIC_DRAW);
	
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	
	return OGLERROR_NOERR;
}

void OpenGLRenderer_1_5::DestroyVBOs()
{
	if (!this->isVBOSupported)
	{
		return;
	}
	
	OGLRenderRef &OGLRef = *this->ref;
	
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	
	glDeleteBuffers(1, &OGLRef.vboGeometryVtxID);
	glDeleteBuffers(1, &OGLRef.iboGeometryIndexID);
	glDeleteBuffers(1, &OGLRef.vboPostprocessVtxID);
	glDeleteBuffers(1, &OGLRef.iboPostprocessIndexID);
	
	this->isVBOSupported = false;
}

Render3DError OpenGLRenderer_1_5::CreateVAOs()
{
	OGLRenderRef &OGLRef = *this->ref;
	
	glGenVertexArrays(1, &OGLRef.vaoGeometryStatesID);
	glGenVertexArrays(1, &OGLRef.vaoPostprocessStatesID);
	
	glBindVertexArray(OGLRef.vaoGeometryStatesID);
	glBindBuffer(GL_ARRAY_BUFFER, OGLRef.vboGeometryVtxID);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, OGLRef.iboGeometryIndexID);
	
	glEnableVertexAttribArray(OGLVertexAttributeID_Position);
	glEnableVertexAttribArray(OGLVertexAttributeID_TexCoord0);
	glEnableVertexAttribArray(OGLVertexAttributeID_Color);
	glVertexAttribPointer(OGLVertexAttributeID_Position, 4, GL_FLOAT, GL_FALSE, sizeof(VERT), (const GLvoid *)offsetof(VERT, coord));
	glVertexAttribPointer(OGLVertexAttributeID_TexCoord0, 2, GL_FLOAT, GL_FALSE, sizeof(VERT), (const GLvoid *)offsetof(VERT, texcoord));
	glVertexAttribPointer(OGLVertexAttributeID_Color, 3, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(VERT), (const GLvoid *)offsetof(VERT, color));
	
	glBindVertexArray(0);
	
	glBindVertexArray(OGLRef.vaoPostprocessStatesID);
	glBindBuffer(GL_ARRAY_BUFFER, OGLRef.vboPostprocessVtxID);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, OGLRef.iboPostprocessIndexID);
	
	glEnableVertexAttribArray(OGLVertexAttributeID_Position);
	glEnableVertexAttribArray(OGLVertexAttributeID_TexCoord0);
	glVertexAttribPointer(OGLVertexAttributeID_Position, 2, GL_FLOAT, GL_FALSE, 0, 0);
	glVertexAttribPointer(OGLVertexAttributeID_TexCoord0, 2, GL_FLOAT, GL_FALSE, 0, (const GLvoid *)(sizeof(GLfloat) * 8));
	
	glBindVertexArray(0);
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_5::EnableVertexAttributes()
{
	OGLRenderRef &OGLRef = *this->ref;
	
	if (this->isVAOSupported)
	{
		glBindVertexArray(OGLRef.vaoGeometryStatesID);
	}
	else
	{
		if (this->isShaderSupported)
		{
			glEnableVertexAttribArray(OGLVertexAttributeID_Position);
			glEnableVertexAttribArray(OGLVertexAttributeID_TexCoord0);
			glEnableVertexAttribArray(OGLVertexAttributeID_Color);
			glVertexAttribPointer(OGLVertexAttributeID_Position, 4, GL_FLOAT, GL_FALSE, sizeof(VERT), OGLRef.vtxPtrPosition);
			glVertexAttribPointer(OGLVertexAttributeID_TexCoord0, 2, GL_FLOAT, GL_FALSE, sizeof(VERT), OGLRef.vtxPtrTexCoord);
			glVertexAttribPointer(OGLVertexAttributeID_Color, 3, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(VERT), OGLRef.vtxPtrColor);
		}
		else
		{
			glEnableClientState(GL_TEXTURE_COORD_ARRAY);
			glEnableClientState(GL_COLOR_ARRAY);
			glEnableClientState(GL_VERTEX_ARRAY);
			
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			glColorPointer(4, GL_FLOAT, 0, OGLRef.vtxPtrColor);
			
			glBindBuffer(GL_ARRAY_BUFFER, OGLRef.vboGeometryVtxID);
			glVertexPointer(4, GL_FLOAT, sizeof(VERT), OGLRef.vtxPtrPosition);
			glTexCoordPointer(2, GL_FLOAT, sizeof(VERT), OGLRef.vtxPtrTexCoord);
		}
	}
	
	glActiveTexture(GL_TEXTURE0);
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_5::DisableVertexAttributes()
{
	if (this->isVAOSupported)
	{
		glBindVertexArray(0);
	}
	else
	{
		if (this->isShaderSupported)
		{
			glDisableVertexAttribArray(OGLVertexAttributeID_Position);
			glDisableVertexAttribArray(OGLVertexAttributeID_TexCoord0);
			glDisableVertexAttribArray(OGLVertexAttributeID_Color);
		}
		else
		{
			glDisableClientState(GL_VERTEX_ARRAY);
			glDisableClientState(GL_COLOR_ARRAY);
			glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		}
	}
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_5::BeginRender(const GFX3D &engine)
{
	OGLRenderRef &OGLRef = *this->ref;
	
	if(!BEGINGL())
	{
		return OGLERROR_BEGINGL_FAILED;
	}
	
	if (this->isShaderSupported)
	{
		glUseProgram(OGLRef.programGeometryID);
		glUniform1i(OGLRef.uniformStateToonShadingMode, engine.renderState.shading);
		glUniform1i(OGLRef.uniformStateEnableAlphaTest, (engine.renderState.enableAlphaTest) ? GL_TRUE : GL_FALSE);
		glUniform1i(OGLRef.uniformStateEnableAntialiasing, (engine.renderState.enableAntialiasing) ? GL_TRUE : GL_FALSE);
		glUniform1i(OGLRef.uniformStateEnableEdgeMarking, (engine.renderState.enableEdgeMarking) ? GL_TRUE : GL_FALSE);
		glUniform1i(OGLRef.uniformStateUseWDepth, (engine.renderState.wbuffer) ? GL_TRUE : GL_FALSE);
		glUniform1f(OGLRef.uniformStateAlphaTestRef, divide5bitBy31_LUT[engine.renderState.alphaTestRef]);
	}
	else
	{
		if(engine.renderState.enableAlphaTest && (engine.renderState.alphaTestRef > 0))
		{
			glAlphaFunc(GL_GEQUAL, divide5bitBy31_LUT[engine.renderState.alphaTestRef]);
		}
		else
		{
			glAlphaFunc(GL_GREATER, 0);
		}
		
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
	}
	
	glBindBuffer(GL_ARRAY_BUFFER, OGLRef.vboGeometryVtxID);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, OGLRef.iboGeometryIndexID);
	
	size_t vertIndexCount = 0;
	GLushort *indexPtr = (GLushort *)glMapBuffer(GL_ELEMENT_ARRAY_BUFFER, GL_WRITE_ONLY);
	
	for (size_t i = 0; i < engine.polylist->count; i++)
	{
		const POLY *thePoly = &engine.polylist->list[engine.indexlist.list[i]];
		const size_t polyType = thePoly->type;
		
		for (size_t j = 0; j < polyType; j++)
		{
			const GLushort vertIndex = thePoly->vertIndexes[j];
			
			// While we're looping through our vertices, add each vertex index to
			// a buffer. For GFX3D_QUADS and GFX3D_QUAD_STRIP, we also add additional
			// vertices here to convert them to GL_TRIANGLES, which are much easier
			// to work with and won't be deprecated in future OpenGL versions.
			indexPtr[vertIndexCount++] = vertIndex;
			if (thePoly->vtxFormat == GFX3D_QUADS || thePoly->vtxFormat == GFX3D_QUAD_STRIP)
			{
				if (j == 2)
				{
					indexPtr[vertIndexCount++] = vertIndex;
				}
				else if (j == 3)
				{
					indexPtr[vertIndexCount++] = thePoly->vertIndexes[0];
				}
			}
		}
	}
	
	glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);
	glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(VERT) * engine.vertlist->count, engine.vertlist);
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_2_0::InitExtensions()
{
	Render3DError error = OGLERROR_NOERR;
	
	// Get OpenGL extensions
	std::set<std::string> oglExtensionSet;
	this->GetExtensionSet(&oglExtensionSet);
	
	// Get host GPU device properties
	GLfloat maxAnisotropyOGL = 1.0f;
	glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAnisotropyOGL);
	this->_deviceInfo.maxAnisotropy = maxAnisotropyOGL;
	
	GLint maxSamplesOGL = 0;
	glGetIntegerv(GL_MAX_SAMPLES_EXT, &maxSamplesOGL);
	this->_deviceInfo.maxSamples = (u8)maxSamplesOGL;
	
	// Initialize OpenGL
	this->InitTables();
	
	// Load and create shaders. Return on any error, since a v2.0 driver will assume that shaders are available.
	this->isShaderSupported	= true;
	
	std::string vertexShaderProgram;
	std::string fragmentShaderProgram;
	error = this->LoadGeometryShaders(vertexShaderProgram, fragmentShaderProgram);
	if (error != OGLERROR_NOERR)
	{
		this->isShaderSupported = false;
		return error;
	}
	
	error = this->InitGeometryProgram(vertexShaderProgram, fragmentShaderProgram);
	if (error != OGLERROR_NOERR)
	{
		this->isShaderSupported = false;
		return error;
	}
	
	std::string edgeMarkVtxShaderString = std::string(EdgeMarkVtxShader_100);
	std::string edgeMarkFragShaderString = std::string(EdgeMarkFragShader_100);
	std::string fogVtxShaderString = std::string(FogVtxShader_100);
	std::string fogFragShaderString = std::string(FogFragShader_100);
	std::string framebufferOutputVtxShaderString = std::string(FramebufferOutputVtxShader_100);
	std::string framebufferOutputRGBA6665FragShaderString = std::string(FramebufferOutputRGBA6665FragShader_100);
	std::string framebufferOutputRGBA8888FragShaderString = std::string(FramebufferOutputRGBA8888FragShader_100);
	error = this->InitPostprocessingPrograms(edgeMarkVtxShaderString,
											 edgeMarkFragShaderString,
											 fogVtxShaderString,
											 fogFragShaderString,
											 framebufferOutputVtxShaderString,
											 framebufferOutputRGBA6665FragShaderString,
											 framebufferOutputRGBA8888FragShaderString);
	if (error != OGLERROR_NOERR)
	{
		this->DestroyGeometryProgram();
		this->isShaderSupported = false;
		this->_deviceInfo.isEdgeMarkSupported = false;
		this->_deviceInfo.isFogSupported = false;
	}
	
	this->isVBOSupported = true;
	this->CreateVBOs();
	
	this->isPBOSupported	= this->IsExtensionPresent(&oglExtensionSet, "GL_ARB_vertex_buffer_object") &&
							 (this->IsExtensionPresent(&oglExtensionSet, "GL_ARB_pixel_buffer_object") ||
							  this->IsExtensionPresent(&oglExtensionSet, "GL_EXT_pixel_buffer_object"));
	if (this->isPBOSupported)
	{
		this->CreatePBOs();
	}
	
	this->isVAOSupported	= this->isShaderSupported &&
							  this->isVBOSupported &&
							 (this->IsExtensionPresent(&oglExtensionSet, "GL_ARB_vertex_array_object") ||
							  this->IsExtensionPresent(&oglExtensionSet, "GL_APPLE_vertex_array_object"));
	if (this->isVAOSupported)
	{
		this->CreateVAOs();
	}
	
	// Don't use ARB versions since we're using the EXT versions for backwards compatibility.
	this->isFBOSupported	= this->IsExtensionPresent(&oglExtensionSet, "GL_EXT_framebuffer_object") &&
							  this->IsExtensionPresent(&oglExtensionSet, "GL_EXT_framebuffer_blit") &&
							  this->IsExtensionPresent(&oglExtensionSet, "GL_EXT_packed_depth_stencil");
	if (this->isFBOSupported)
	{
		this->willFlipFramebufferOnGPU = true;
		this->willConvertFramebufferOnGPU = (this->isShaderSupported && this->isVAOSupported && this->isPBOSupported && this->isFBOSupported);
		
		error = this->CreateFBOs();
		if (error != OGLERROR_NOERR)
		{
			this->isFBOSupported = false;
		}
	}
	else
	{
		INFO("OpenGL: FBOs are unsupported. Some emulation features will be disabled.\n");
	}
	
	// Set these again after FBO creation just in case FBO creation fails.
	this->willFlipFramebufferOnGPU = this->isFBOSupported;
	this->willConvertFramebufferOnGPU = (this->isShaderSupported && this->isVAOSupported && this->isPBOSupported && this->isFBOSupported);
	
	// Don't use ARB versions since we're using the EXT versions for backwards compatibility.
	this->isMultisampledFBOSupported	= this->isFBOSupported &&
										  this->IsExtensionPresent(&oglExtensionSet, "GL_EXT_framebuffer_multisample");
	if (this->isMultisampledFBOSupported)
	{
		error = this->CreateMultisampledFBO();
		if (error != OGLERROR_NOERR)
		{
			this->isMultisampledFBOSupported = false;
		}
	}
	else
	{
		INFO("OpenGL: Multisampled FBOs are unsupported. Multisample antialiasing will be disabled.\n");
	}
	
	this->InitTextures();
	this->InitFinalRenderStates(&oglExtensionSet); // This must be done last
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_2_0::InitFinalRenderStates(const std::set<std::string> *oglExtensionSet)
{
	OGLRenderRef &OGLRef = *this->ref;
	
	// we want to use alpha destination blending so we can track the last-rendered alpha value
	// test: new super mario brothers renders the stormclouds at the beginning
	
	// Blending Support
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA, GL_DST_ALPHA);
	glBlendEquationSeparate(GL_FUNC_ADD, GL_MAX);
	
	// Mirrored Repeat Mode Support
	OGLRef.stateTexMirroredRepeat = GL_MIRRORED_REPEAT;
	
	// Ignore our color buffer since we'll transfer the polygon alpha through a uniform.
	OGLRef.color4fBuffer = NULL;
	
	// VBOs are supported here, so just use the index buffer on the GPU.
	OGLRef.vertIndexBuffer = NULL;
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_2_0::InitPostprocessingPrograms(const std::string &edgeMarkVtxShader,
															 const std::string &edgeMarkFragShader,
															 const std::string &fogVtxShader,
															 const std::string &fogFragShader,
															 const std::string &framebufferOutputVtxShader,
															 const std::string &framebufferOutputRGBA6665FragShader,
															 const std::string &framebufferOutputRGBA8888FragShader)
{
	Render3DError error = OGLERROR_NOERR;
	OGLRenderRef &OGLRef = *this->ref;
	
	OGLRef.vertexEdgeMarkShaderID = glCreateShader(GL_VERTEX_SHADER);
	if(!OGLRef.vertexEdgeMarkShaderID)
	{
		INFO("OpenGL: Failed to create the edge mark vertex shader.\n");
		return OGLERROR_SHADER_CREATE_ERROR;
	}
	
	const char *edgeMarkVtxShaderCStr = edgeMarkVtxShader.c_str();
	glShaderSource(OGLRef.vertexEdgeMarkShaderID, 1, (const GLchar **)&edgeMarkVtxShaderCStr, NULL);
	glCompileShader(OGLRef.vertexEdgeMarkShaderID);
	if (!this->ValidateShaderCompile(OGLRef.vertexEdgeMarkShaderID))
	{
		glDeleteShader(OGLRef.vertexEdgeMarkShaderID);
		INFO("OpenGL: Failed to compile the edge mark vertex shader.\n");
		return OGLERROR_SHADER_CREATE_ERROR;
	}
	
	OGLRef.fragmentEdgeMarkShaderID = glCreateShader(GL_FRAGMENT_SHADER);
	if(!OGLRef.fragmentEdgeMarkShaderID)
	{
		glDeleteShader(OGLRef.vertexEdgeMarkShaderID);
		INFO("OpenGL: Failed to create the edge mark fragment shader.\n");
		return OGLERROR_SHADER_CREATE_ERROR;
	}
	
	const char *edgeMarkFragShaderCStr = edgeMarkFragShader.c_str();
	glShaderSource(OGLRef.fragmentEdgeMarkShaderID, 1, (const GLchar **)&edgeMarkFragShaderCStr, NULL);
	glCompileShader(OGLRef.fragmentEdgeMarkShaderID);
	if (!this->ValidateShaderCompile(OGLRef.fragmentEdgeMarkShaderID))
	{
		glDeleteShader(OGLRef.vertexEdgeMarkShaderID);
		glDeleteShader(OGLRef.fragmentEdgeMarkShaderID);
		INFO("OpenGL: Failed to compile the edge mark fragment shader.\n");
		return OGLERROR_SHADER_CREATE_ERROR;
	}
	
	OGLRef.programEdgeMarkID = glCreateProgram();
	if(!OGLRef.programEdgeMarkID)
	{
		glDeleteShader(OGLRef.vertexEdgeMarkShaderID);
		glDeleteShader(OGLRef.fragmentEdgeMarkShaderID);
		INFO("OpenGL: Failed to create the edge mark shader program.\n");
		return OGLERROR_SHADER_CREATE_ERROR;
	}
	
	glAttachShader(OGLRef.programEdgeMarkID, OGLRef.vertexEdgeMarkShaderID);
	glAttachShader(OGLRef.programEdgeMarkID, OGLRef.fragmentEdgeMarkShaderID);
	
	error = this->InitEdgeMarkProgramBindings();
	if (error != OGLERROR_NOERR)
	{
		glDetachShader(OGLRef.programEdgeMarkID, OGLRef.vertexEdgeMarkShaderID);
		glDetachShader(OGLRef.programEdgeMarkID, OGLRef.fragmentEdgeMarkShaderID);
		glDeleteProgram(OGLRef.programEdgeMarkID);
		glDeleteShader(OGLRef.vertexEdgeMarkShaderID);
		glDeleteShader(OGLRef.fragmentEdgeMarkShaderID);
		INFO("OpenGL: Failed to make the edge mark shader bindings.\n");
		return error;
	}
	
	glLinkProgram(OGLRef.programEdgeMarkID);
	if (!this->ValidateShaderProgramLink(OGLRef.programEdgeMarkID))
	{
		glDetachShader(OGLRef.programEdgeMarkID, OGLRef.vertexEdgeMarkShaderID);
		glDetachShader(OGLRef.programEdgeMarkID, OGLRef.fragmentEdgeMarkShaderID);
		glDeleteProgram(OGLRef.programEdgeMarkID);
		glDeleteShader(OGLRef.vertexEdgeMarkShaderID);
		glDeleteShader(OGLRef.fragmentEdgeMarkShaderID);
		INFO("OpenGL: Failed to link the edge mark shader program.\n");
		return OGLERROR_SHADER_CREATE_ERROR;
	}
	
	glValidateProgram(OGLRef.programEdgeMarkID);
	this->InitEdgeMarkProgramShaderLocations();
	
	// ------------------------------------------
	
	OGLRef.vertexFogShaderID = glCreateShader(GL_VERTEX_SHADER);
	if(!OGLRef.vertexFogShaderID)
	{
		INFO("OpenGL: Failed to create the fog vertex shader.\n");
		return OGLERROR_SHADER_CREATE_ERROR;
	}
	
	const char *fogVtxShaderCStr = fogVtxShader.c_str();
	glShaderSource(OGLRef.vertexFogShaderID, 1, (const GLchar **)&fogVtxShaderCStr, NULL);
	glCompileShader(OGLRef.vertexFogShaderID);
	if (!this->ValidateShaderCompile(OGLRef.vertexFogShaderID))
	{
		glDeleteShader(OGLRef.vertexFogShaderID);
		INFO("OpenGL: Failed to compile the fog vertex shader.\n");
		return OGLERROR_SHADER_CREATE_ERROR;
	}
	
	OGLRef.fragmentFogShaderID = glCreateShader(GL_FRAGMENT_SHADER);
	if(!OGLRef.fragmentFogShaderID)
	{
		glDeleteShader(OGLRef.vertexFogShaderID);
		INFO("OpenGL: Failed to create the fog fragment shader.\n");
		return OGLERROR_SHADER_CREATE_ERROR;
	}
	
	const char *fogFragShaderCStr = fogFragShader.c_str();
	glShaderSource(OGLRef.fragmentFogShaderID, 1, (const GLchar **)&fogFragShaderCStr, NULL);
	glCompileShader(OGLRef.fragmentFogShaderID);
	if (!this->ValidateShaderCompile(OGLRef.fragmentFogShaderID))
	{
		glDeleteShader(OGLRef.vertexFogShaderID);
		glDeleteShader(OGLRef.fragmentFogShaderID);
		INFO("OpenGL: Failed to compile the fog fragment shader.\n");
		return OGLERROR_SHADER_CREATE_ERROR;
	}
	
	OGLRef.programFogID = glCreateProgram();
	if(!OGLRef.programFogID)
	{
		glDeleteShader(OGLRef.vertexFogShaderID);
		glDeleteShader(OGLRef.fragmentFogShaderID);
		INFO("OpenGL: Failed to create the fog shader program.\n");
		return OGLERROR_SHADER_CREATE_ERROR;
	}
	
	glAttachShader(OGLRef.programFogID, OGLRef.vertexFogShaderID);
	glAttachShader(OGLRef.programFogID, OGLRef.fragmentFogShaderID);
	
	error = this->InitFogProgramBindings();
	if (error != OGLERROR_NOERR)
	{
		glDetachShader(OGLRef.programFogID, OGLRef.vertexFogShaderID);
		glDetachShader(OGLRef.programFogID, OGLRef.fragmentFogShaderID);
		glDeleteProgram(OGLRef.programFogID);
		glDeleteShader(OGLRef.vertexFogShaderID);
		glDeleteShader(OGLRef.fragmentFogShaderID);
		INFO("OpenGL: Failed to make the fog shader bindings.\n");
		return error;
	}
	
	glLinkProgram(OGLRef.programFogID);
	if (!this->ValidateShaderProgramLink(OGLRef.programFogID))
	{
		glDetachShader(OGLRef.programFogID, OGLRef.vertexFogShaderID);
		glDetachShader(OGLRef.programFogID, OGLRef.fragmentFogShaderID);
		glDeleteProgram(OGLRef.programFogID);
		glDeleteShader(OGLRef.vertexFogShaderID);
		glDeleteShader(OGLRef.fragmentFogShaderID);
		INFO("OpenGL: Failed to link the fog shader program.\n");
		return OGLERROR_SHADER_CREATE_ERROR;
	}
	
	glValidateProgram(OGLRef.programFogID);
	this->InitFogProgramShaderLocations();
	
	// ------------------------------------------
	
	OGLRef.vertexFramebufferOutputShaderID = glCreateShader(GL_VERTEX_SHADER);
	if(!OGLRef.vertexFramebufferOutputShaderID)
	{
		INFO("OpenGL: Failed to create the framebuffer output vertex shader.\n");
		return OGLERROR_SHADER_CREATE_ERROR;
	}
	
	const char *framebufferOutputVtxShaderCStr = framebufferOutputVtxShader.c_str();
	glShaderSource(OGLRef.vertexFramebufferOutputShaderID, 1, (const GLchar **)&framebufferOutputVtxShaderCStr, NULL);
	glCompileShader(OGLRef.vertexFramebufferOutputShaderID);
	if (!this->ValidateShaderCompile(OGLRef.vertexFramebufferOutputShaderID))
	{
		glDeleteShader(OGLRef.vertexFramebufferOutputShaderID);
		INFO("OpenGL: Failed to compile the framebuffer output vertex shader.\n");
		return OGLERROR_SHADER_CREATE_ERROR;
	}
	
	OGLRef.fragmentFramebufferRGBA6665OutputShaderID = glCreateShader(GL_FRAGMENT_SHADER);
	if(!OGLRef.fragmentFramebufferRGBA6665OutputShaderID)
	{
		glDeleteShader(OGLRef.vertexFramebufferOutputShaderID);
		INFO("OpenGL: Failed to create the framebuffer output fragment shader.\n");
		return OGLERROR_SHADER_CREATE_ERROR;
	}
	
	OGLRef.fragmentFramebufferRGBA8888OutputShaderID = glCreateShader(GL_FRAGMENT_SHADER);
	if(!OGLRef.fragmentFramebufferRGBA8888OutputShaderID)
	{
		glDeleteShader(OGLRef.vertexFramebufferOutputShaderID);
		glDeleteShader(OGLRef.fragmentFramebufferRGBA6665OutputShaderID);
		INFO("OpenGL: Failed to create the framebuffer output fragment shader.\n");
		return OGLERROR_SHADER_CREATE_ERROR;
	}
	
	const char *framebufferOutputRGBA6665FragShaderCStr = framebufferOutputRGBA6665FragShader.c_str();
	glShaderSource(OGLRef.fragmentFramebufferRGBA6665OutputShaderID, 1, (const GLchar **)&framebufferOutputRGBA6665FragShaderCStr, NULL);
	glCompileShader(OGLRef.fragmentFramebufferRGBA6665OutputShaderID);
	if (!this->ValidateShaderCompile(OGLRef.fragmentFramebufferRGBA6665OutputShaderID))
	{
		glDeleteShader(OGLRef.vertexFramebufferOutputShaderID);
		glDeleteShader(OGLRef.fragmentFramebufferRGBA6665OutputShaderID);
		glDeleteShader(OGLRef.fragmentFramebufferRGBA8888OutputShaderID);
		INFO("OpenGL: Failed to compile the framebuffer output fragment shader.\n");
		return OGLERROR_SHADER_CREATE_ERROR;
	}
	
	const char *framebufferOutputRGBA8888FragShaderCStr = framebufferOutputRGBA8888FragShader.c_str();
	glShaderSource(OGLRef.fragmentFramebufferRGBA8888OutputShaderID, 1, (const GLchar **)&framebufferOutputRGBA8888FragShaderCStr, NULL);
	glCompileShader(OGLRef.fragmentFramebufferRGBA8888OutputShaderID);
	if (!this->ValidateShaderCompile(OGLRef.fragmentFramebufferRGBA8888OutputShaderID))
	{
		glDeleteShader(OGLRef.vertexFramebufferOutputShaderID);
		glDeleteShader(OGLRef.fragmentFramebufferRGBA6665OutputShaderID);
		glDeleteShader(OGLRef.fragmentFramebufferRGBA8888OutputShaderID);
		INFO("OpenGL: Failed to compile the framebuffer output fragment shader.\n");
		return OGLERROR_SHADER_CREATE_ERROR;
	}
	
	OGLRef.programFramebufferRGBA6665OutputID = glCreateProgram();
	if(!OGLRef.programFramebufferRGBA6665OutputID)
	{
		glDeleteShader(OGLRef.vertexFramebufferOutputShaderID);
		glDeleteShader(OGLRef.fragmentFramebufferRGBA6665OutputShaderID);
		glDeleteShader(OGLRef.fragmentFramebufferRGBA8888OutputShaderID);
		INFO("OpenGL: Failed to create the framebuffer output shader program.\n");
		return OGLERROR_SHADER_CREATE_ERROR;
	}
	
	OGLRef.programFramebufferRGBA8888OutputID = glCreateProgram();
	if(!OGLRef.programFramebufferRGBA8888OutputID)
	{
		glDeleteShader(OGLRef.vertexFramebufferOutputShaderID);
		glDeleteShader(OGLRef.fragmentFramebufferRGBA6665OutputShaderID);
		glDeleteShader(OGLRef.fragmentFramebufferRGBA8888OutputShaderID);
		INFO("OpenGL: Failed to create the framebuffer output shader program.\n");
		return OGLERROR_SHADER_CREATE_ERROR;
	}
	
	glAttachShader(OGLRef.programFramebufferRGBA6665OutputID, OGLRef.vertexFramebufferOutputShaderID);
	glAttachShader(OGLRef.programFramebufferRGBA6665OutputID, OGLRef.fragmentFramebufferRGBA6665OutputShaderID);
	glAttachShader(OGLRef.programFramebufferRGBA8888OutputID, OGLRef.vertexFramebufferOutputShaderID);
	glAttachShader(OGLRef.programFramebufferRGBA8888OutputID, OGLRef.fragmentFramebufferRGBA8888OutputShaderID);
	
	error = this->InitFramebufferOutputProgramBindings();
	if (error != OGLERROR_NOERR)
	{
		glDetachShader(OGLRef.programFramebufferRGBA6665OutputID, OGLRef.vertexFramebufferOutputShaderID);
		glDetachShader(OGLRef.programFramebufferRGBA6665OutputID, OGLRef.fragmentFramebufferRGBA6665OutputShaderID);
		glDetachShader(OGLRef.programFramebufferRGBA8888OutputID, OGLRef.vertexFramebufferOutputShaderID);
		glDetachShader(OGLRef.programFramebufferRGBA8888OutputID, OGLRef.fragmentFramebufferRGBA8888OutputShaderID);
		
		glDeleteProgram(OGLRef.programFramebufferRGBA6665OutputID);
		glDeleteProgram(OGLRef.programFramebufferRGBA8888OutputID);
		glDeleteShader(OGLRef.vertexFramebufferOutputShaderID);
		glDeleteShader(OGLRef.fragmentFramebufferRGBA6665OutputShaderID);
		glDeleteShader(OGLRef.fragmentFramebufferRGBA8888OutputShaderID);
		INFO("OpenGL: Failed to make the framebuffer output shader bindings.\n");
		return error;
	}
	
	glLinkProgram(OGLRef.programFramebufferRGBA6665OutputID);
	glLinkProgram(OGLRef.programFramebufferRGBA8888OutputID);
	
	if (!this->ValidateShaderProgramLink(OGLRef.programFramebufferRGBA6665OutputID) || !this->ValidateShaderProgramLink(OGLRef.programFramebufferRGBA8888OutputID))
	{
		glDetachShader(OGLRef.programFramebufferRGBA6665OutputID, OGLRef.vertexFramebufferOutputShaderID);
		glDetachShader(OGLRef.programFramebufferRGBA6665OutputID, OGLRef.fragmentFramebufferRGBA6665OutputShaderID);
		glDetachShader(OGLRef.programFramebufferRGBA8888OutputID, OGLRef.vertexFramebufferOutputShaderID);
		glDetachShader(OGLRef.programFramebufferRGBA8888OutputID, OGLRef.fragmentFramebufferRGBA8888OutputShaderID);
		
		glDeleteProgram(OGLRef.programFramebufferRGBA6665OutputID);
		glDeleteProgram(OGLRef.programFramebufferRGBA8888OutputID);
		glDeleteShader(OGLRef.vertexFramebufferOutputShaderID);
		glDeleteShader(OGLRef.fragmentFramebufferRGBA6665OutputShaderID);
		glDeleteShader(OGLRef.fragmentFramebufferRGBA8888OutputShaderID);
		INFO("OpenGL: Failed to link the framebuffer output shader program.\n");
		return OGLERROR_SHADER_CREATE_ERROR;
	}
	
	glValidateProgram(OGLRef.programFramebufferRGBA6665OutputID);
	glValidateProgram(OGLRef.programFramebufferRGBA8888OutputID);
	this->InitFramebufferOutputShaderLocations();
	
	// ------------------------------------------
	
	glUseProgram(OGLRef.programGeometryID);
	INFO("OpenGL: Successfully created postprocess shaders.\n");
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_2_0::DestroyPostprocessingPrograms()
{
	OGLRenderRef &OGLRef = *this->ref;
	
	glUseProgram(0);
	glDetachShader(OGLRef.programEdgeMarkID, OGLRef.vertexEdgeMarkShaderID);
	glDetachShader(OGLRef.programEdgeMarkID, OGLRef.fragmentEdgeMarkShaderID);
	glDetachShader(OGLRef.programFogID, OGLRef.vertexFogShaderID);
	glDetachShader(OGLRef.programFogID, OGLRef.fragmentFogShaderID);
	
	glDeleteProgram(OGLRef.programEdgeMarkID);
	glDeleteProgram(OGLRef.programFogID);
	
	glDeleteShader(OGLRef.vertexEdgeMarkShaderID);
	glDeleteShader(OGLRef.fragmentEdgeMarkShaderID);
	glDeleteShader(OGLRef.vertexFogShaderID);
	glDeleteShader(OGLRef.fragmentFogShaderID);
	
	glDetachShader(OGLRef.programFramebufferRGBA6665OutputID, OGLRef.vertexFramebufferOutputShaderID);
	glDetachShader(OGLRef.programFramebufferRGBA6665OutputID, OGLRef.fragmentFramebufferRGBA6665OutputShaderID);
	glDetachShader(OGLRef.programFramebufferRGBA8888OutputID, OGLRef.vertexFramebufferOutputShaderID);
	glDetachShader(OGLRef.programFramebufferRGBA8888OutputID, OGLRef.fragmentFramebufferRGBA8888OutputShaderID);
	
	glDeleteProgram(OGLRef.programFramebufferRGBA6665OutputID);
	glDeleteProgram(OGLRef.programFramebufferRGBA8888OutputID);
	glDeleteShader(OGLRef.vertexFramebufferOutputShaderID);
	glDeleteShader(OGLRef.fragmentFramebufferRGBA6665OutputShaderID);
	glDeleteShader(OGLRef.fragmentFramebufferRGBA8888OutputShaderID);
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_2_0::InitEdgeMarkProgramBindings()
{
	OGLRenderRef &OGLRef = *this->ref;
	glBindAttribLocation(OGLRef.programEdgeMarkID, OGLVertexAttributeID_Position, "inPosition");
	glBindAttribLocation(OGLRef.programEdgeMarkID, OGLVertexAttributeID_TexCoord0, "inTexCoord0");
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_2_0::InitEdgeMarkProgramShaderLocations()
{
	OGLRenderRef &OGLRef = *this->ref;
	
	glUseProgram(OGLRef.programEdgeMarkID);
	
	const GLint uniformTexGDepth	= glGetUniformLocation(OGLRef.programEdgeMarkID, "texInFragDepth");
	const GLint uniformTexGPolyID	= glGetUniformLocation(OGLRef.programEdgeMarkID, "texInPolyID");
	glUniform1i(uniformTexGDepth, OGLTextureUnitID_GDepth);
	glUniform1i(uniformTexGPolyID, OGLTextureUnitID_GPolyID);
	
	OGLRef.uniformFramebufferSize	= glGetUniformLocation(OGLRef.programEdgeMarkID, "framebufferSize");
	OGLRef.uniformStateEdgeColor	= glGetUniformLocation(OGLRef.programEdgeMarkID, "stateEdgeColor");
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_2_0::InitFogProgramBindings()
{
	OGLRenderRef &OGLRef = *this->ref;
	glBindAttribLocation(OGLRef.programFogID, OGLVertexAttributeID_Position, "inPosition");
	glBindAttribLocation(OGLRef.programFogID, OGLVertexAttributeID_TexCoord0, "inTexCoord0");
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_2_0::InitFogProgramShaderLocations()
{
	OGLRenderRef &OGLRef = *this->ref;
	
	glUseProgram(OGLRef.programFogID);
	
	const GLint uniformTexGColor			= glGetUniformLocation(OGLRef.programFogID, "texInFragColor");
	const GLint uniformTexGDepth			= glGetUniformLocation(OGLRef.programFogID, "texInFragDepth");
	const GLint uniformTexGFog				= glGetUniformLocation(OGLRef.programFogID, "texInFogAttributes");
	glUniform1i(uniformTexGColor, OGLTextureUnitID_GColor);
	glUniform1i(uniformTexGDepth, OGLTextureUnitID_GDepth);
	glUniform1i(uniformTexGFog, OGLTextureUnitID_FogAttr);
	
	OGLRef.uniformStateEnableFogAlphaOnly	= glGetUniformLocation(OGLRef.programFogID, "stateEnableFogAlphaOnly");
	OGLRef.uniformStateFogColor				= glGetUniformLocation(OGLRef.programFogID, "stateFogColor");
	OGLRef.uniformStateFogDensity			= glGetUniformLocation(OGLRef.programFogID, "stateFogDensity");
	OGLRef.uniformStateFogOffset			= glGetUniformLocation(OGLRef.programFogID, "stateFogOffset");
	OGLRef.uniformStateFogStep				= glGetUniformLocation(OGLRef.programFogID, "stateFogStep");
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_2_0::InitFramebufferOutputProgramBindings()
{
	OGLRenderRef &OGLRef = *this->ref;
	glBindAttribLocation(OGLRef.programFramebufferRGBA6665OutputID, OGLVertexAttributeID_Position, "inPosition");
	glBindAttribLocation(OGLRef.programFramebufferRGBA6665OutputID, OGLVertexAttributeID_TexCoord0, "inTexCoord0");
	glBindAttribLocation(OGLRef.programFramebufferRGBA8888OutputID, OGLVertexAttributeID_Position, "inPosition");
	glBindAttribLocation(OGLRef.programFramebufferRGBA8888OutputID, OGLVertexAttributeID_TexCoord0, "inTexCoord0");
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_2_0::InitFramebufferOutputShaderLocations()
{
	OGLRenderRef &OGLRef = *this->ref;
	
	glUseProgram(OGLRef.programFramebufferRGBA6665OutputID);
	const GLint uniformTexFinalColorRGBA6665 = glGetUniformLocation(OGLRef.programFramebufferRGBA6665OutputID, "texInFragColor");
	glUniform1i(uniformTexFinalColorRGBA6665, OGLTextureUnitID_FinalColor);
	
	glUseProgram(OGLRef.programFramebufferRGBA8888OutputID);
	const GLint uniformTexFinalColorRGBA8888 = glGetUniformLocation(OGLRef.programFramebufferRGBA8888OutputID, "texInFragColor");
	glUniform1i(uniformTexFinalColorRGBA8888, OGLTextureUnitID_FinalColor);
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_2_0::EnableVertexAttributes()
{
	OGLRenderRef &OGLRef = *this->ref;
	
	if (this->isVAOSupported)
	{
		glBindVertexArray(OGLRef.vaoGeometryStatesID);
	}
	else
	{
		glEnableVertexAttribArray(OGLVertexAttributeID_Position);
		glEnableVertexAttribArray(OGLVertexAttributeID_TexCoord0);
		glEnableVertexAttribArray(OGLVertexAttributeID_Color);
		glVertexAttribPointer(OGLVertexAttributeID_Position, 4, GL_FLOAT, GL_FALSE, sizeof(VERT), OGLRef.vtxPtrPosition);
		glVertexAttribPointer(OGLVertexAttributeID_TexCoord0, 2, GL_FLOAT, GL_FALSE, sizeof(VERT), OGLRef.vtxPtrTexCoord);
		glVertexAttribPointer(OGLVertexAttributeID_Color, 3, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(VERT), OGLRef.vtxPtrColor);
	}
	
	glActiveTexture(GL_TEXTURE0);
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_2_0::DisableVertexAttributes()
{
	if (this->isVAOSupported)
	{
		glBindVertexArray(0);
	}
	else
	{
		glDisableVertexAttribArray(OGLVertexAttributeID_Position);
		glDisableVertexAttribArray(OGLVertexAttributeID_TexCoord0);
		glDisableVertexAttribArray(OGLVertexAttributeID_Color);
	}
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_2_0::BeginRender(const GFX3D &engine)
{
	OGLRenderRef &OGLRef = *this->ref;
	
	if(!BEGINGL())
	{
		return OGLERROR_BEGINGL_FAILED;
	}
	
	// Setup render states
	glUseProgram(OGLRef.programGeometryID);
	glUniform1i(OGLRef.uniformStateToonShadingMode, engine.renderState.shading);
	glUniform1i(OGLRef.uniformStateEnableAlphaTest, (engine.renderState.enableAlphaTest) ? GL_TRUE : GL_FALSE);
	glUniform1i(OGLRef.uniformStateEnableAntialiasing, (engine.renderState.enableAntialiasing) ? GL_TRUE : GL_FALSE);
	glUniform1i(OGLRef.uniformStateEnableEdgeMarking, (engine.renderState.enableEdgeMarking) ? GL_TRUE : GL_FALSE);
	glUniform1i(OGLRef.uniformStateUseWDepth, (engine.renderState.wbuffer) ? GL_TRUE : GL_FALSE);
	glUniform1f(OGLRef.uniformStateAlphaTestRef, divide5bitBy31_LUT[engine.renderState.alphaTestRef]);
	
	glBindBuffer(GL_ARRAY_BUFFER, OGLRef.vboGeometryVtxID);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, OGLRef.iboGeometryIndexID);
	
	size_t vertIndexCount = 0;
	GLushort *indexPtr = (GLushort *)glMapBuffer(GL_ELEMENT_ARRAY_BUFFER, GL_WRITE_ONLY);
	
	for (size_t i = 0; i < engine.polylist->count; i++)
	{
		const POLY *thePoly = &engine.polylist->list[engine.indexlist.list[i]];
		const size_t polyType = thePoly->type;
		
		for (size_t j = 0; j < polyType; j++)
		{
			const GLushort vertIndex = thePoly->vertIndexes[j];
			
			// While we're looping through our vertices, add each vertex index to
			// a buffer. For GFX3D_QUADS and GFX3D_QUAD_STRIP, we also add additional
			// vertices here to convert them to GL_TRIANGLES, which are much easier
			// to work with and won't be deprecated in future OpenGL versions.
			indexPtr[vertIndexCount++] = vertIndex;
			if (thePoly->vtxFormat == GFX3D_QUADS || thePoly->vtxFormat == GFX3D_QUAD_STRIP)
			{
				if (j == 2)
				{
					indexPtr[vertIndexCount++] = vertIndex;
				}
				else if (j == 3)
				{
					indexPtr[vertIndexCount++] = thePoly->vertIndexes[0];
				}
			}
		}
	}
	
	glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);
	glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(VERT) * engine.vertlist->count, engine.vertlist);
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_2_0::RenderEdgeMarking(const u16 *colorTable, const bool useAntialias)
{
	OGLRenderRef &OGLRef = *this->ref;
	
	const GLfloat alpha = (useAntialias) ? (16.0f/31.0f) : 1.0f;
	const GLfloat oglColor[4*8]	= {divide5bitBy31_LUT[(colorTable[0]      ) & 0x001F],
								   divide5bitBy31_LUT[(colorTable[0] >>  5) & 0x001F],
								   divide5bitBy31_LUT[(colorTable[0] >> 10) & 0x001F],
								   alpha,
								   divide5bitBy31_LUT[(colorTable[1]      ) & 0x001F],
								   divide5bitBy31_LUT[(colorTable[1] >>  5) & 0x001F],
								   divide5bitBy31_LUT[(colorTable[1] >> 10) & 0x001F],
								   alpha,
								   divide5bitBy31_LUT[(colorTable[2]      ) & 0x001F],
								   divide5bitBy31_LUT[(colorTable[2] >>  5) & 0x001F],
								   divide5bitBy31_LUT[(colorTable[2] >> 10) & 0x001F],
								   alpha,
								   divide5bitBy31_LUT[(colorTable[3]      ) & 0x001F],
								   divide5bitBy31_LUT[(colorTable[3] >>  5) & 0x001F],
								   divide5bitBy31_LUT[(colorTable[3] >> 10) & 0x001F],
								   alpha,
								   divide5bitBy31_LUT[(colorTable[4]      ) & 0x001F],
								   divide5bitBy31_LUT[(colorTable[4] >>  5) & 0x001F],
								   divide5bitBy31_LUT[(colorTable[4] >> 10) & 0x001F],
								   alpha,
								   divide5bitBy31_LUT[(colorTable[5]      ) & 0x001F],
								   divide5bitBy31_LUT[(colorTable[5] >>  5) & 0x001F],
								   divide5bitBy31_LUT[(colorTable[5] >> 10) & 0x001F],
								   alpha,
								   divide5bitBy31_LUT[(colorTable[6]      ) & 0x001F],
								   divide5bitBy31_LUT[(colorTable[6] >>  5) & 0x001F],
								   divide5bitBy31_LUT[(colorTable[6] >> 10) & 0x001F],
								   alpha,
								   divide5bitBy31_LUT[(colorTable[7]      ) & 0x001F],
								   divide5bitBy31_LUT[(colorTable[7] >>  5) & 0x001F],
								   divide5bitBy31_LUT[(colorTable[7] >> 10) & 0x001F],
								   alpha};
	
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, OGLRef.fboRenderID);
	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
	glUseProgram(OGLRef.programEdgeMarkID);
	glUniform2f(OGLRef.uniformFramebufferSize, this->_framebufferWidth, this->_framebufferHeight);
	glUniform4fv(OGLRef.uniformStateEdgeColor, 8, oglColor);
	
	glViewport(0, 0, this->_framebufferWidth, this->_framebufferHeight);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_STENCIL_TEST);
	glEnable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	
	glBindBuffer(GL_ARRAY_BUFFER, OGLRef.vboPostprocessVtxID);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, OGLRef.iboPostprocessIndexID);
	
	if (this->isVAOSupported)
	{
		glBindVertexArray(OGLRef.vaoPostprocessStatesID);
	}
	else
	{
		glEnableVertexAttribArray(OGLVertexAttributeID_Position);
		glEnableVertexAttribArray(OGLVertexAttributeID_TexCoord0);
		glVertexAttribPointer(OGLVertexAttributeID_Position, 2, GL_FLOAT, GL_FALSE, 0, 0);
		glVertexAttribPointer(OGLVertexAttributeID_TexCoord0, 2, GL_FLOAT, GL_FALSE, 0, (const GLvoid *)(sizeof(GLfloat) * 8));
	}
	
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, 0);
	
	if (this->isVAOSupported)
	{
		glBindVertexArray(0);
	}
	else
	{
		glDisableVertexAttribArray(OGLVertexAttributeID_Position);
		glDisableVertexAttribArray(OGLVertexAttributeID_TexCoord0);
	}
		
	return RENDER3DERROR_NOERR;
}

Render3DError OpenGLRenderer_2_0::RenderFog(const u8 *densityTable, const u32 color, const u32 offset, const u8 shift, const bool alphaOnly)
{
	OGLRenderRef &OGLRef = *this->ref;
	static GLfloat oglDensityTable[32];
	
	if (!this->isFBOSupported)
	{
		return OGLERROR_FEATURE_UNSUPPORTED;
	}
	
	for (size_t i = 0; i < 32; i++)
	{
		oglDensityTable[i] = (densityTable[i] == 127) ? 1.0f : (GLfloat)densityTable[i] / 128.0f;
	}
	
	const GLfloat oglColor[4]	= {divide5bitBy31_LUT[(color      ) & 0x0000001F],
								   divide5bitBy31_LUT[(color >>  5) & 0x0000001F],
								   divide5bitBy31_LUT[(color >> 10) & 0x0000001F],
								   divide5bitBy31_LUT[(color >> 16) & 0x0000001F]};
	
	const GLfloat oglOffset = (GLfloat)(offset & 0x7FFF) / 32767.0f;
	const GLfloat oglFogStep = (GLfloat)(0x0400 >> shift) / 32767.0f;
	
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, OGLRef.fboPostprocessID);
	glUseProgram(OGLRef.programFogID);
	glUniform1i(OGLRef.uniformStateEnableFogAlphaOnly, (alphaOnly) ? GL_TRUE : GL_FALSE);
	glUniform4f(OGLRef.uniformStateFogColor, oglColor[0], oglColor[1], oglColor[2], oglColor[3]);
	glUniform1f(OGLRef.uniformStateFogOffset, oglOffset);
	glUniform1f(OGLRef.uniformStateFogStep, oglFogStep);
	glUniform1fv(OGLRef.uniformStateFogDensity, 32, oglDensityTable);
	
	glViewport(0, 0, this->_framebufferWidth, this->_framebufferHeight);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	
	glBindBuffer(GL_ARRAY_BUFFER, OGLRef.vboPostprocessVtxID);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, OGLRef.iboPostprocessIndexID);
	
	if (this->isVAOSupported)
	{
		glBindVertexArray(OGLRef.vaoPostprocessStatesID);
	}
	else
	{
		glEnableVertexAttribArray(OGLVertexAttributeID_Position);
		glEnableVertexAttribArray(OGLVertexAttributeID_TexCoord0);
		glVertexAttribPointer(OGLVertexAttributeID_Position, 2, GL_FLOAT, GL_FALSE, 0, 0);
		glVertexAttribPointer(OGLVertexAttributeID_TexCoord0, 2, GL_FLOAT, GL_FALSE, 0, (const GLvoid *)(sizeof(GLfloat) * 8));
	}
	
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, 0);
	
	if (this->isVAOSupported)
	{
		glBindVertexArray(0);
	}
	else
	{
		glDisableVertexAttribArray(OGLVertexAttributeID_Position);
		glDisableVertexAttribArray(OGLVertexAttributeID_TexCoord0);
	}
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_2_0::SetupPolygon(const POLY &thePoly)
{
	const PolygonAttributes attr = thePoly.getAttributes();
	
	// Set up depth test mode
	static const GLenum oglDepthFunc[2] = {GL_LESS, GL_EQUAL};
	glDepthFunc(oglDepthFunc[attr.enableDepthEqualTest]);
	
	// Set up culling mode
	static const GLenum oglCullingMode[4] = {GL_FRONT_AND_BACK, GL_FRONT, GL_BACK, 0};
	GLenum cullingMode = oglCullingMode[attr.surfaceCullingMode];
	
	if (cullingMode == 0)
	{
		glDisable(GL_CULL_FACE);
	}
	else
	{
		glEnable(GL_CULL_FACE);
		glCullFace(cullingMode);
	}
	
	// Set up depth write
	GLboolean enableDepthWrite = GL_TRUE;
	
	// Handle shadow polys. Do this after checking for depth write, since shadow polys
	// can change this too.
	if (attr.polygonMode == POLYGON_MODE_SHADOW)
	{
		glEnable(GL_STENCIL_TEST);
		
		if (attr.polygonID == 0)
		{
			//when the polyID is zero, we are writing the shadow mask.
			//set stencilbuf = 1 where the shadow volume is obstructed by geometry.
			//do not write color or depth information.
			glStencilFunc(GL_NOTEQUAL, 0x80, 0xFF);
			glStencilOp(GL_KEEP, GL_ZERO, GL_KEEP);
			glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
			enableDepthWrite = GL_FALSE;
		}
		else
		{
			//when the polyid is nonzero, we are drawing the shadow poly.
			//only draw the shadow poly where the stencilbuf==1.
			glStencilFunc(GL_EQUAL, 0, 0xFF);
			glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			enableDepthWrite = GL_TRUE;
		}
	}
	else if ( attr.isTranslucent || (std::find(this->_shadowPolyID.begin(), this->_shadowPolyID.end(), attr.polygonID) == this->_shadowPolyID.end()) )
	{
		glDisable(GL_STENCIL_TEST);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		enableDepthWrite = (!attr.isTranslucent || ( (attr.polygonMode == POLYGON_MODE_DECAL) && attr.isOpaque ) || attr.enableAlphaDepthWrite) ? GL_TRUE : GL_FALSE;
	}
	else
	{
		glEnable(GL_STENCIL_TEST);
		glStencilFunc(GL_ALWAYS, 0x80, 0xFF);
		glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		enableDepthWrite = GL_TRUE;
	}
	
	glDepthMask(enableDepthWrite);
	
	// Set up polygon attributes
	OGLRenderRef &OGLRef = *this->ref;
	glUniform1i(OGLRef.uniformPolyMode, attr.polygonMode);
	glUniform1i(OGLRef.uniformPolyEnableFog, (attr.enableRenderFog && !(attr.polygonMode == POLYGON_MODE_SHADOW && attr.polygonID == 0)) ? GL_TRUE : GL_FALSE);
	glUniform1f(OGLRef.uniformPolyAlpha, (!attr.isWireframe && attr.isTranslucent) ? divide5bitBy31_LUT[attr.alpha] : 1.0f);
	glUniform1i(OGLRef.uniformPolyID, attr.polygonID);
	glUniform1i(OGLRef.uniformPolyEnableDepthWrite, (!(attr.polygonMode == POLYGON_MODE_SHADOW && attr.polygonID == 0)) ? GL_TRUE : GL_FALSE);
	glUniform1i(OGLRef.uniformPolySetNewDepthForTranslucent, (attr.enableAlphaDepthWrite) ? GL_TRUE : GL_FALSE);
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_2_0::SetupTexture(const POLY &thePoly, bool enableTexturing)
{
	OGLRenderRef &OGLRef = *this->ref;
	const PolygonTexParams params = thePoly.getTexParams();
	
	// Check if we need to use textures
	if (params.texFormat == TEXMODE_NONE || !enableTexturing)
	{
		glUniform1i(OGLRef.uniformPolyEnableTexture, GL_FALSE);
		glUniform1i(OGLRef.uniformTexSingleBitAlpha, GL_FALSE);
		return OGLERROR_NOERR;
	}
	
	glUniform1i(OGLRef.uniformPolyEnableTexture, GL_TRUE);
	glUniform1i(OGLRef.uniformTexSingleBitAlpha, (params.texFormat != TEXMODE_A3I5 && params.texFormat != TEXMODE_A5I3) ? GL_TRUE : GL_FALSE);
	
	TexCacheItem *newTexture = TexCache_SetTexture(TexFormat_32bpp, thePoly.texParam, thePoly.texPalette);
	if(newTexture != this->currTexture)
	{
		this->currTexture = newTexture;
		//has the ogl renderer initialized the texture?
		if(this->currTexture->GetDeleteCallback() == NULL)
		{
			this->currTexture->SetDeleteCallback(&texDeleteCallback, this, NULL);
			
			if(OGLRef.freeTextureIDs.empty())
			{
				this->ExpandFreeTextures();
			}
			
			this->currTexture->texid = (u64)OGLRef.freeTextureIDs.front();
			OGLRef.freeTextureIDs.pop();
			
			glBindTexture(GL_TEXTURE_2D, (GLuint)this->currTexture->texid);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (params.enableRepeatS ? (params.enableMirroredRepeatS ? GL_MIRRORED_REPEAT : GL_REPEAT) : GL_CLAMP_TO_EDGE));
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (params.enableRepeatT ? (params.enableMirroredRepeatT ? GL_MIRRORED_REPEAT : GL_REPEAT) : GL_CLAMP_TO_EDGE));
			
			u32 *textureSrc = (u32 *)currTexture->decoded;
			size_t texWidth = currTexture->sizeX;
			size_t texHeight = currTexture->sizeY;
			
			if (this->_textureDeposterizeBuffer != NULL)
			{
				this->TextureDeposterize(textureSrc, texWidth, texHeight);
				textureSrc = this->_textureDeposterizeBuffer;
			}
			
			switch (this->_textureScalingFactor)
			{
				case 1:
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
					glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texWidth, texHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, textureSrc);
					break;
					
				case 2:
				{
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1);
					
					this->TextureUpscale<2>(textureSrc, texWidth, texHeight);
					glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texWidth, texHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, this->_textureUpscaleBuffer);
					
					glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA, currTexture->sizeX, currTexture->sizeY, 0, GL_RGBA, GL_UNSIGNED_BYTE, textureSrc);
					break;
				}
					
				case 4:
				{
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 2);
					
					this->TextureUpscale<4>(textureSrc, texWidth, texHeight);
					glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texWidth, texHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, this->_textureUpscaleBuffer);
					
					texWidth = currTexture->sizeX;
					texHeight = currTexture->sizeY;
					this->TextureUpscale<2>(textureSrc, texWidth, texHeight);
					glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA, texWidth, texHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, this->_textureUpscaleBuffer);
					
					glTexImage2D(GL_TEXTURE_2D, 2, GL_RGBA, currTexture->sizeX, currTexture->sizeY, 0, GL_RGBA, GL_UNSIGNED_BYTE, textureSrc);
					break;
				}
					
				default:
					break;
			}
			
			if (this->_textureSmooth)
			{
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (this->_textureScalingFactor > 1) ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, this->_deviceInfo.maxAnisotropy);
			}
			else
			{
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
				glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1.0f);
			}
		}
		else
		{
			//otherwise, just bind it
			glBindTexture(GL_TEXTURE_2D, (GLuint)this->currTexture->texid);
		}
		
		glUniform2f(OGLRef.uniformPolyTexScale, this->currTexture->invSizeX, this->currTexture->invSizeY);
	}
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_2_1::ReadBackPixels()
{
	OGLRenderRef &OGLRef = *this->ref;
	
	if (this->_mappedFramebuffer != NULL)
	{
		glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
		this->_mappedFramebuffer = NULL;
	}
	
	// Flip the framebuffer in Y to match the coordinates of OpenGL and the NDS hardware.
	if (this->willFlipFramebufferOnGPU)
	{
		glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT, OGLRef.fboPostprocessID);
		glDrawBuffer(GL_COLOR_ATTACHMENT1_EXT);
		glBlitFramebufferEXT(0, this->_framebufferHeight, this->_framebufferWidth, 0, 0, 0, this->_framebufferWidth, this->_framebufferHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, OGLRef.fboPostprocessID);
		glReadBuffer(GL_COLOR_ATTACHMENT1_EXT);
	}
	
	if (this->willConvertFramebufferOnGPU)
	{
		// Perform the color space conversion while we're still on the GPU so
		// that we can avoid having to do it on the CPU.
		const GLuint convertProgramID = (this->_outputFormat == NDSColorFormat_BGR666_Rev) ? OGLRef.programFramebufferRGBA6665OutputID : OGLRef.programFramebufferRGBA8888OutputID;
		glDrawBuffer(GL_COLOR_ATTACHMENT2_EXT);
		
		glUseProgram(convertProgramID);
		glViewport(0, 0, this->_framebufferWidth, this->_framebufferHeight);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_STENCIL_TEST);
		glDisable(GL_BLEND);
		glDisable(GL_CULL_FACE);
		
		glBindBuffer(GL_ARRAY_BUFFER, OGLRef.vboPostprocessVtxID);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, OGLRef.iboPostprocessIndexID);
		glBindVertexArray(OGLRef.vaoPostprocessStatesID);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, 0);
		glBindVertexArray(0);
		
		// Read back the pixels.
		glReadBuffer(GL_COLOR_ATTACHMENT2_EXT);
	}
	
	// Read back the pixels in BGRA format, since legacy OpenGL devices may experience a performance
	// penalty if the readback is in any other format.
	glReadPixels(0, 0, this->_framebufferWidth, this->_framebufferHeight, GL_BGRA, GL_UNSIGNED_BYTE, 0);
	
	// Set the read and draw target buffers back to color attachment 0, which is always the default.
	if (this->willFlipFramebufferOnGPU || this->willConvertFramebufferOnGPU)
	{
		glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
		glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
	}
	
	this->_pixelReadNeedsFinish = true;
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_2_1::RenderFinish()
{
	if (!this->_renderNeedsFinish || !this->_pixelReadNeedsFinish)
	{
		return OGLERROR_NOERR;
	}
	
	FragmentColor *framebufferMain = (this->_willFlushFramebufferRGBA6665) ? GPU->GetEngineMain()->Get3DFramebufferRGBA6665() : NULL;
	u16 *framebufferRGBA5551 = (this->_willFlushFramebufferRGBA5551) ? GPU->GetEngineMain()->Get3DFramebufferRGBA5551() : NULL;
	
	if ( (framebufferMain != NULL) || (framebufferRGBA5551 != NULL) )
	{
		if(!BEGINGL())
		{
			return OGLERROR_BEGINGL_FAILED;
		}
		
		this->_mappedFramebuffer = (FragmentColor *__restrict)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
		this->FlushFramebuffer(this->_mappedFramebuffer, framebufferMain, framebufferRGBA5551);
		
		ENDGL();
	}
	
	this->_pixelReadNeedsFinish = false;
	
	return OGLERROR_NOERR;
}
