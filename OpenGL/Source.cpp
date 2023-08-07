#define GLFW_INCLUDE_NONE
#include <fstream>
#include "glad/glad.h"
#include "GLFW/glfw3.h"

#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>


#ifdef NDEBUG
auto constexpr enableDebugging = false;
#else
auto constexpr enableDebugging = true;
#endif

auto constexpr WIDTH = 800;
auto constexpr HEIGHT = 600;
GLfloat constexpr CLEAR_COLOR[]{0.f, 0.f, 0.f, 1.f};
GLfloat constexpr CLEAR_DEPTH{1.f};
GLint constexpr CLEAR_STENCIL{0};
GLfloat constexpr GAMMA{2.2f};

std::filesystem::path const shaderSourceDir{"../shaders"};

void glfw_error_callback(int errorCode, char const* desc);
void APIENTRY gl_debug_message_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, GLchar const* message, void const* userParam);
void PrintFramebufferAttachmentInfo(GLuint framebuffer, GLenum attachment);
[[nodiscard]] std::string LoadShaderSource(std::filesystem::path const& path);



int main()
{
	if constexpr (enableDebugging)
	{
		glfwSetErrorCallback(glfw_error_callback);
	}

	if (glfwInit() == GLFW_FALSE)
	{
		return -1;
	}

	glfwWindowHint(GLFW_ALPHA_BITS, 0);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	if constexpr (enableDebugging)
	{
		glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
	}

	auto* const window = glfwCreateWindow(WIDTH, HEIGHT, "GL", nullptr, nullptr);

	if (!window)
	{
		glfwTerminate();
		return -1;
	}

	glfwMakeContextCurrent(window);

	if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
	{
		std::cerr << "Failed to init GLAD.\n";
		glfwDestroyWindow(window);
		glfwTerminate();
		return -1;
	}

	if constexpr (enableDebugging)
	{
		glEnable(GL_DEBUG_OUTPUT);
		glDebugMessageCallback(gl_debug_message_callback, nullptr);
	}
	else
	{
		glDisable(GL_DEBUG_OUTPUT);
	}

	int majorVersion, minorVersion, profileMask;
	glGetIntegerv(GL_MAJOR_VERSION, &majorVersion);
	glGetIntegerv(GL_MINOR_VERSION, &minorVersion);
	glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profileMask);
	std::cout << "OpenGL context version: " << majorVersion << '.' << minorVersion << '\n';
	std::cout << "OpenGL context profile: " << (profileMask & GL_CONTEXT_CORE_PROFILE_BIT ? "core" : "compatibility") << '\n';

	std::cout << "Renderer: " << reinterpret_cast<char const*>(glGetString(GL_RENDERER)) << "\n\n";

	std::cout << "Bindless textures " << (GLAD_GL_ARB_bindless_texture ? "" : "not ") << "supported.\n\n";

	PrintFramebufferAttachmentInfo(0, GL_BACK_LEFT);

	// Framebuffer setup

	GLuint colorBuffer;
	glCreateTextures(GL_TEXTURE_2D, 1, &colorBuffer);
	glTextureStorage2D(colorBuffer, 1, GL_RGB8, WIDTH, HEIGHT);

	GLuint depthStencilBuffer;
	glCreateTextures(GL_TEXTURE_2D, 1, &depthStencilBuffer);
	glTextureStorage2D(depthStencilBuffer, 1, GL_DEPTH24_STENCIL8, WIDTH, HEIGHT);

	GLuint framebuffer;
	glCreateFramebuffers(1, &framebuffer);
	glNamedFramebufferTexture(framebuffer, GL_COLOR_ATTACHMENT0, colorBuffer, 0);
	glNamedFramebufferTexture(framebuffer, GL_DEPTH_STENCIL_ATTACHMENT, depthStencilBuffer, 0);
	glNamedFramebufferDrawBuffer(framebuffer, GL_COLOR_ATTACHMENT0);
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

	PrintFramebufferAttachmentInfo(framebuffer, GL_COLOR_ATTACHMENT0);

	// Draw data setup

	GLfloat constexpr vertexPositions[]
	{
		0.0f, 0.5f,
		0.5f, -0.5f,
		-0.5f, -0.5f,

		-1.0f, -1.0f,
		1.0f, -1.0f,
		-1.0f, 1.0f,
		1.0f, 1.0f
	};

	GLint constexpr vertexTexelIndices[]
	{
		0, 1, 2,

		1, 2, 0, 1
	};

	GLubyte constexpr vertexIndices[]
	{
		0, 1, 2,

		0, 1, 2, 2, 1, 3
	};

	GLfloat constexpr modelMatrices[]
	{
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.5f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f,

		0.1f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.1f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.1f, 0.0f,
		-0.5f, -0.5f, 0.0f, 1.0f,

		0.1f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.1f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.1f, 0.0f,
		-0.5f, 0.5f, 0.0f, 1.0f,

		0.1f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.1f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.1f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f,

		0.1f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.1f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.1f, 0.0f,
		0.5f, -0.5f, 0.0f, 1.0f,
	};

	GLuint vertPosBuf;
	glCreateBuffers(1, &vertPosBuf);
	glNamedBufferStorage(vertPosBuf, sizeof vertexPositions, vertexPositions, 0);

	GLuint vertTexIndBuf;
	glCreateBuffers(1, &vertTexIndBuf);
	glNamedBufferStorage(vertTexIndBuf, sizeof vertexTexelIndices, vertexTexelIndices, 0);

	GLuint vertIndBuf;
	glCreateBuffers(1, &vertIndBuf);
	glNamedBufferStorage(vertIndBuf, sizeof vertexIndices, vertexIndices, 0);

	GLuint modelMatBuf;
	glCreateBuffers(1, &modelMatBuf);
	glNamedBufferStorage(modelMatBuf, sizeof modelMatrices, modelMatrices, 0);

	GLuint vao;
	glCreateVertexArrays(1, &vao);
	glBindVertexArray(vao);

	glVertexArrayVertexBuffer(vao, 0, vertPosBuf, 0, 2 * sizeof GLfloat);
	glVertexArrayAttribFormat(vao, 0, 2, GL_FLOAT, GL_FALSE, 0);
	glVertexArrayAttribBinding(vao, 0, 0);
	glEnableVertexArrayAttrib(vao, 0);

	glVertexArrayVertexBuffer(vao, 1, vertTexIndBuf, 0, sizeof GLint);
	glVertexArrayAttribIFormat(vao, 1, 1, GL_INT, 0);
	glVertexArrayAttribBinding(vao, 1, 1);
	glEnableVertexArrayAttrib(vao, 1);

	glVertexArrayVertexBuffer(vao, 2, modelMatBuf, 0, 16 * sizeof GLfloat);
	glVertexArrayBindingDivisor(vao, 2, 1);

	glVertexArrayAttribFormat(vao, 2, 4, GL_FLOAT, GL_FALSE, 0);
	glVertexArrayAttribBinding(vao, 2, 2);
	glEnableVertexArrayAttrib(vao, 2);

	glVertexArrayAttribFormat(vao, 3, 4, GL_FLOAT, GL_FALSE, 4 * sizeof GLfloat);
	glVertexArrayAttribBinding(vao, 3, 2);
	glEnableVertexArrayAttrib(vao, 3);

	glVertexArrayAttribFormat(vao, 4, 4, GL_FLOAT, GL_FALSE, 8 * sizeof GLfloat);
	glVertexArrayAttribBinding(vao, 4, 2);
	glEnableVertexArrayAttrib(vao, 4);

	glVertexArrayAttribFormat(vao, 5, 4, GL_FLOAT, GL_FALSE, 12 * sizeof GLfloat);
	glVertexArrayAttribBinding(vao, 5, 2);
	glEnableVertexArrayAttrib(vao, 5);

	glVertexArrayElementBuffer(vao, vertIndBuf);

	struct DrawElementsIndirectCommand
	{
		GLuint count;
		GLuint primCount;
		GLuint firstIndex;
		GLuint baseVertex;
		GLuint baseInstance;
	};

	DrawElementsIndirectCommand constexpr drawIndirectCommands[]
	{
		DrawElementsIndirectCommand
		{
			.count = 3,
			.primCount = 1,
			.firstIndex = 0,
			.baseVertex = 0,
			.baseInstance = 0
		},

		DrawElementsIndirectCommand
		{
			.count = 6,
			.primCount = 4,
			.firstIndex = 3,
			.baseVertex = 3,
			.baseInstance = 1
		}
	};

	GLuint indirectBuf;
	glCreateBuffers(1, &indirectBuf);
	glNamedBufferStorage(indirectBuf, sizeof drawIndirectCommands, drawIndirectCommands, 0);
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirectBuf);

	// Texture setup

	auto constexpr texWidth = 3;
	GLubyte constexpr texColorData[texWidth][3]{{255, 0, 0}, {0, 255, 0}, {0, 0, 255}};

	GLuint texture;
	glCreateTextures(GL_TEXTURE_1D, 1, &texture);

	glTextureStorage1D(texture, 1, GL_SRGB8, texWidth);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTextureSubImage1D(texture, 0, 0, texWidth, GL_RGB, GL_UNSIGNED_BYTE, texColorData);

	auto const textureHandle = glGetTextureHandleARB(texture);
	glMakeTextureHandleResidentARB(textureHandle);

	// UBO setup

	struct UniformBufferData
	{
		GLuint64 textureHandle;
		GLfloat gammaInv;
	};

	UniformBufferData uniformBufferData{};
	uniformBufferData.textureHandle = textureHandle;
	uniformBufferData.gammaInv = 1.f / GAMMA;

	GLbitfield constexpr mapFlags{GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT};
	GLbitfield constexpr createFlags{mapFlags | GL_DYNAMIC_STORAGE_BIT};

	GLuint uniformBuffer;
	glCreateBuffers(1, &uniformBuffer);
	glNamedBufferStorage(uniformBuffer, sizeof UniformBufferData, nullptr, createFlags);
	glBindBufferRange(GL_UNIFORM_BUFFER, 0, uniformBuffer, 0, sizeof UniformBufferData);

	auto const uniformBufferPtr = static_cast<UniformBufferData*>(glMapNamedBufferRange(uniformBuffer, 0, sizeof UniformBufferData, mapFlags));
	*uniformBufferPtr = uniformBufferData;

	// Shader setup

	auto const vertSrc = LoadShaderSource(shaderSourceDir / "shader.vert");
	auto const fragSrc = LoadShaderSource(shaderSourceDir / "shader.frag");
	auto const vertSrcP = vertSrc.data();
	auto const fragSrcP = fragSrc.data();

	auto const vertShader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertShader, 1, &vertSrcP, nullptr);
	glCompileShader(vertShader);
	GLint result;
	glGetShaderiv(vertShader, GL_COMPILE_STATUS, &result);
	if (result != GL_TRUE)
	{
		std::cerr << "Failed to compile vertex shader!\n\n";
	}

	auto const fragShader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragShader, 1, &fragSrcP, nullptr);
	glCompileShader(fragShader);
	glGetShaderiv(fragShader, GL_COMPILE_STATUS, &result);
	if (result != GL_TRUE)
	{
		std::cerr << "Failed to compile fragment shader!\n\n";
	}

	auto const program = glCreateProgram();
	glAttachShader(program, vertShader);
	glAttachShader(program, fragShader);
	glLinkProgram(program);
	glGetProgramiv(program, GL_LINK_STATUS, &result);
	if (result != GL_TRUE)
	{
		GLint logLngth;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLngth);
		std::string infoLog(logLngth, ' ');
		glGetProgramInfoLog(program, static_cast<GLsizei>(infoLog.size()), &logLngth, infoLog.data());
		std::cerr << "Failed to link shader program: " << infoLog << "\n\n";
	}

	glUseProgram(program);

	// Render loop

	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();

		glClearNamedFramebufferfv(framebuffer, GL_COLOR, 0, CLEAR_COLOR);
		glClearNamedFramebufferfi(framebuffer, GL_DEPTH_STENCIL, 0, CLEAR_DEPTH, CLEAR_STENCIL);
		
		glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_BYTE, nullptr, sizeof drawIndirectCommands / sizeof DrawElementsIndirectCommand, 0);

		glBlitNamedFramebuffer(framebuffer, 0, 0, 0, WIDTH, HEIGHT, 0, 0, WIDTH, HEIGHT, GL_COLOR_BUFFER_BIT, GL_NEAREST);

		glfwSwapBuffers(window);
	}

	// Cleanup

	glDeleteProgram(program);
	glDeleteShader(fragShader);
	glDeleteShader(vertShader);
	glMakeTextureHandleNonResidentARB(textureHandle);
	glDeleteTextures(1, &texture);
	glUnmapNamedBuffer(uniformBuffer);
	glDeleteBuffers(1, &uniformBuffer);
	glDeleteVertexArrays(1, &vao);
	glDeleteBuffers(1, &vertTexIndBuf);
	glDeleteBuffers(1, &vertPosBuf);
	glDeleteTextures(1, &depthStencilBuffer);
	glDeleteTextures(1, &colorBuffer);
	glDeleteFramebuffers(1, &framebuffer);

	glfwDestroyWindow(window);
	glfwTerminate();
}



void glfw_error_callback(int const errorCode, char const* const desc)
{
	std::cerr << "GLFW error, code: " << errorCode << ", description: " << desc << "\n\n";
}



void gl_debug_message_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, GLchar const* message, void const* userParam)
{
	std::cout << message << "\n\n";
}



void PrintFramebufferAttachmentInfo(GLuint const framebuffer, GLenum const attachment)
{
	std::cout << "Framebuffer " << framebuffer << " attachment info:\n";

	GLint objectType;
	glGetNamedFramebufferAttachmentParameteriv(framebuffer, attachment, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &objectType);
	std::cout << "\tObject type: " << (objectType == GL_NONE ? "none" : objectType == GL_RENDERBUFFER ? "renderbuffer" : objectType == GL_TEXTURE ? "texture" : objectType == GL_FRAMEBUFFER_DEFAULT ? "default" : "unknown") << '\n';

	if (objectType == GL_NONE)
	{
		std::cout << '\n';
		return;
	}

	GLint redBits, greenBits, blueBits, alphaBits;
	glGetNamedFramebufferAttachmentParameteriv(framebuffer, attachment, GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE, &redBits);
	glGetNamedFramebufferAttachmentParameteriv(framebuffer, attachment, GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE, &greenBits);
	glGetNamedFramebufferAttachmentParameteriv(framebuffer, attachment, GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE, &blueBits);
	glGetNamedFramebufferAttachmentParameteriv(framebuffer, attachment, GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE, &alphaBits);

	std::cout << "\tFormat: R" << redBits;

	if (greenBits)
	{
		std::cout << 'G' << greenBits;
	}

	if (blueBits)
	{
		std::cout << 'B' << blueBits;
	}

	if (alphaBits)
	{
		std::cout << 'A' << alphaBits;
	}

	std::cout << '\n';

	GLint colorEncoding;
	glGetNamedFramebufferAttachmentParameteriv(framebuffer, attachment, GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING, &colorEncoding);
	std::cout << "\tColor encoding: " << (colorEncoding == GL_SRGB ? "sRGB" : colorEncoding == GL_LINEAR ? "linear RGB" : "unknown") << '\n';

	std::cout << '\n';
}



std::string LoadShaderSource(std::filesystem::path const& path)
{
	std::ifstream const file{path};
	std::stringstream buffer;
	buffer << file.rdbuf();
	return buffer.str();
}
