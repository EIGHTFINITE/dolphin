// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "FileUtil.h"
#include "VideoCommon.h"
#include "VideoConfig.h"
#include "GLUtil.h"
#include "PostProcessing.h"
#include "ProgramShaderCache.h"
#include "FramebufferManager.h"

namespace OGL
{

namespace PostProcessing
{

static std::string s_currentShader;
static SHADER s_shader;
static bool s_enable;

static u32 s_width;
static u32 s_height;
static GLuint s_fbo;
static GLuint s_texture;
static GLuint s_vao;
static GLuint s_vbo;

static GLuint s_uniform_resolution;

static char s_vertex_shader[] =
	"in vec2 rawpos;\n"
	"in vec2 tex0;\n"
	"out vec2 uv0;\n"
	"void main(void) {\n"
	"	gl_Position = vec4(rawpos,0,1);\n"
	"	uv0 = tex0;\n"
	"}\n"; 

void Init()
{
	s_currentShader = "";
	s_enable = 0;
	s_width = 0;
	s_height = 0;
	
	glGenFramebuffers(1, &s_fbo);
	glGenTextures(1, &s_texture);
	glBindTexture(GL_TEXTURE_2D, s_texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0); // disable mipmaps
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glBindFramebuffer(GL_FRAMEBUFFER, s_fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_texture, 0);
	FramebufferManager::SetFramebuffer(0);
	
	glGenBuffers(1, &s_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
	GLfloat vertices[] = { 
		-1.f, -1.f, 0.f, 0.f, 
		-1.f,  1.f, 0.f, 1.f, 
		 1.f, -1.f, 1.f, 0.f, 
		 1.f,  1.f, 1.f, 1.f
	};
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
	
	glGenVertexArrays(1, &s_vao);
	glBindVertexArray( s_vao );
	glEnableVertexAttribArray(SHADER_POSITION_ATTRIB);
	glVertexAttribPointer(SHADER_POSITION_ATTRIB, 2, GL_FLOAT, 0, sizeof(GLfloat)*4, NULL);
	glEnableVertexAttribArray(SHADER_TEXTURE0_ATTRIB);
	glVertexAttribPointer(SHADER_TEXTURE0_ATTRIB, 2, GL_FLOAT, 0, sizeof(GLfloat)*4, (GLfloat*)NULL+2);
}

void Shutdown()
{
	s_shader.Destroy();
	
	glDeleteFramebuffers(1, &s_vbo);
	glDeleteTextures(1, &s_texture);
	
	glDeleteBuffers(1, &s_vbo);
	glDeleteVertexArrays(1, &s_vao);
}

void ReloadShader()
{
	s_currentShader = "";
}

void BindTargetFramebuffer ()
{
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_enable ? s_fbo : 0);
}

void BlitToScreen()
{
	if(!s_enable) return;
	
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glViewport(0, 0, s_width, s_height);
	
	glBindVertexArray(s_vao);
	s_shader.Bind();
	
	glUniform4f(s_uniform_resolution, (float)s_width, (float)s_height, 1.0f/(float)s_width, 1.0f/(float)s_height);
	
	glActiveTexture(GL_TEXTURE0+9);
	glBindTexture(GL_TEXTURE_2D, s_texture);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glBindTexture(GL_TEXTURE_2D, 0);
	
/*	glBindFramebuffer(GL_READ_FRAMEBUFFER, s_fbo);
	
	glBlitFramebuffer(rc.left, rc.bottom, rc.right, rc.top,
			rc.left, rc.bottom, rc.right, rc.top,
			GL_COLOR_BUFFER_BIT, GL_NEAREST);*/
}

void Update ( u32 width, u32 height )
{
	ApplyShader();

	if(s_enable && (width != s_width || height != s_height)) {
		s_width = width;
		s_height = height;
		
		// alloc texture for framebuffer
		glActiveTexture(GL_TEXTURE0+9);
		glBindTexture(GL_TEXTURE_2D, s_texture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
}

void ApplyShader()
{
	// shader didn't changed
	if (s_currentShader == g_ActiveConfig.sPostProcessingShader) return;
	s_currentShader = g_ActiveConfig.sPostProcessingShader;
	s_enable = false;
	s_shader.Destroy();
	
	// shader disabled
	if (g_ActiveConfig.sPostProcessingShader == "") return;
	
	// so need to compile shader
	
	// loading shader code
	std::string code;
	std::string path = File::GetUserPath(D_SHADERS_IDX) + g_ActiveConfig.sPostProcessingShader + ".txt";
	if(!File::ReadFileToString(true, path.c_str(), code)) {
		ERROR_LOG(VIDEO, "Post-processing shader not found: %s", path.c_str());
		return;
	}
	
	// and compile it
	if (!ProgramShaderCache::CompileShader(s_shader, s_vertex_shader, code.c_str())) {
		ERROR_LOG(VIDEO, "Failed to compile post-processing shader %s", s_currentShader.c_str());
		return;
	}
	
	// read uniform locations
	s_uniform_resolution = glGetUniformLocation(s_shader.glprogid, "resolution");
	
	// successful
	s_enable = true;
}

}  // namespace

}  // namespace OGL
