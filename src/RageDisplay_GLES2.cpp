#include "global.h"

#include "RageDisplay.h"
#include "RageDisplay_GLES2.h"
#include "RageUtil.h"
#include "RageLog.h"
#include "RageTimer.h"
#include "RageMath.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "RageSurface.h"
#include "RageSurfaceUtils.h"
#include "RageTextureManager.h"

#include "DisplaySpec.h"

#include "arch/LowLevelWindow/LowLevelWindow.h"

#include <vector>

#ifndef __EMSCRIPTEN__
#include <GL/glew.h>
#else
#include <GLES2/gl2.h>
#include <SDL2/SDL_opengles2.h>
#include <emscripten.h>

// Define missing desktop GL constants for GLES2/WebGL
// These formats won't actually be used in browser, but needed for compilation
#ifndef GL_BGR
#define GL_BGR 0x80E0
#endif
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#ifndef GL_RGB8
#define GL_RGB8 GL_RGB
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 GL_RGBA
#endif
#ifndef GL_RGB5
#define GL_RGB5 GL_RGB
#endif
#ifndef GL_COLOR_INDEX
#define GL_COLOR_INDEX 0x1900
#endif
#ifndef GL_COLOR_INDEX8_EXT
#define GL_COLOR_INDEX8_EXT 0x80E5
#endif
#ifndef GL_UNSIGNED_SHORT_1_5_5_5_REV
#define GL_UNSIGNED_SHORT_1_5_5_5_REV 0x8366
#endif
#ifndef GL_TEXTURE_WIDTH
#define GL_TEXTURE_WIDTH 0x1000
#endif
#ifndef GL_ALPHA_TEST
#define GL_ALPHA_TEST 0x0BC0
#endif
#endif

#ifdef NO_GL_FLUSH
#define glFlush()
#endif

namespace
{
	RageDisplay::RagePixelFormatDesc
	PIXEL_FORMAT_DESC[NUM_RagePixelFormat] = {
		{
			/* R8G8B8A8 */
			32,
			{ 0xFF000000,
			  0x00FF0000,
			  0x0000FF00,
			  0x000000FF }
		}, {
			/* B8G8R8A8 */
			32,
			{ 0x0000FF00,
			  0x00FF0000,
			  0xFF000000,
			  0x000000FF }
		}, {
			/* R4G4B4A4 */
			16,
			{ 0xF000,
			  0x0F00,
			  0x00F0,
			  0x000F },
		}, {
			/* R5G5B5A1 */
			16,
			{ 0xF800,
			  0x07C0,
			  0x003E,
			  0x0001 },
		}, {
			/* R5G5B5X1 */
			16,
			{ 0xF800,
			  0x07C0,
			  0x003E,
			  0x0000 },
		}, {
			/* R8G8B8 */
			24,
			{ 0xFF0000,
			  0x00FF00,
			  0x0000FF,
			  0x000000 }
		}, {
			/* Paletted */
			8,
			{ 0,0,0,0 } /* N/A */
		}, {
			/* B8G8R8 */
			24,
			{ 0x0000FF,
			  0x00FF00,
			  0xFF0000,
			  0x000000 }
		}, {
			/* A1R5G5B5 */
			16,
			{ 0x7C00,
			  0x03E0,
			  0x001F,
			  0x8000 },
		}, {
			/* X1R5G5B5 */
			16,
			{ 0x7C00,
			  0x03E0,
			  0x001F,
			  0x0000 },
		}
	};

	/* g_GLPixFmtInfo is used for both texture formats and surface formats.
	 * For example, it's fine to ask for a RagePixelFormat_RGB5 texture, but to
	 * supply a surface matching RagePixelFormat_RGB8.  OpenGL will simply
	 * discard the extra bits.
	 *
	 * It's possible for a format to be supported as a texture format but
	 * not as a surface format.  For example, if packed pixels aren't
	 * supported, we can still use GL_RGB5_A1, but we'll have to convert to
	 * a supported surface pixel format first.  It's not ideal, since we'll
	 * convert to RGBA8 and OGL will convert back, but it works fine.
	 */
	struct GLPixFmtInfo_t {
		GLenum internalfmt; /* target format */
		GLenum format; /* target format */
		GLenum type; /* data format */
	} const g_GLPixFmtInfo[NUM_RagePixelFormat] = {
		{
			/* R8G8B8A8 */
			GL_RGBA8,
			GL_RGBA,
			GL_UNSIGNED_BYTE,
		}, {
			/* R8G8B8A8 */
			GL_RGBA8,
			GL_BGRA,
			GL_UNSIGNED_BYTE,
		}, {
			/* B4G4R4A4 */
			GL_RGBA4,
			GL_RGBA,
			GL_UNSIGNED_SHORT_4_4_4_4,
		}, {
			/* B5G5R5A1 */
			GL_RGB5_A1,
			GL_RGBA,
			GL_UNSIGNED_SHORT_5_5_5_1,
		}, {
			/* B5G5R5 */
			GL_RGB5,
			GL_RGBA,
			GL_UNSIGNED_SHORT_5_5_5_1,
		}, {
			/* B8G8R8 */
			GL_RGB8,
			GL_RGB,
			GL_UNSIGNED_BYTE,
		}, {
			/* Paletted */
			GL_COLOR_INDEX8_EXT,
			GL_COLOR_INDEX,
			GL_UNSIGNED_BYTE,
		}, {
			/* B8G8R8 */
			GL_RGB8,
			GL_BGR,
			GL_UNSIGNED_BYTE,
		}, {
			// TODO: These don't work on ES2. Work out what needs to happen.
			/* A1R5G5B5 (matches D3DFMT_A1R5G5B5) */
			GL_RGB5_A1,
			GL_BGRA,
			GL_UNSIGNED_SHORT_1_5_5_5_REV,
		}, {
			/* X1R5G5B5 */
			GL_RGB5,
			GL_BGRA,
			GL_UNSIGNED_SHORT_1_5_5_5_REV,
		}
	};

	LowLevelWindow *g_pWind;

	void FixLittleEndian()
	{
#if defined(ENDIAN_LITTLE)
		static bool bInitialized = false;
		if (bInitialized)
			return;
		bInitialized = true;

		for( int i = 0; i < NUM_RagePixelFormat; ++i )
		{
			RageDisplay::RagePixelFormatDesc &pf = PIXEL_FORMAT_DESC[i];

			/* OpenGL and RageSurface handle byte formats differently; we need
			 * to flip non-paletted masks to make them line up. */
			if (g_GLPixFmtInfo[i].type != GL_UNSIGNED_BYTE || pf.bpp == 8)
				continue;

			for( int mask = 0; mask < 4; ++mask)
			{
				int m = pf.masks[mask];
				switch( pf.bpp )
				{
				case 24: m = Swap24(m); break;
				case 32: m = Swap32(m); break;
				default:
					 FAIL_M(ssprintf("Unsupported BPP value: %i", pf.bpp));
				}
				pf.masks[mask] = m;
			}
		}
#endif
	}
	namespace Caps
	{
		int iMaxTextureUnits = 1;
		int iMaxTextureSize = 256;
	}
	namespace State
	{
		bool bZTestEnabled = false;
		bool bZWriteEnabled = false;
		bool bAlphaTestEnabled = false;
	}

	// Basic shader program for GLES2
	GLuint g_ShaderProgram = 0;
	GLuint g_DefaultWhiteTexture = 0;
	GLint g_AttribPosition = -1;
	GLint g_AttribTexCoord = -1;
	GLint g_AttribColor = -1;
	GLint g_UniformProjection = -1;
	GLint g_UniformModelView = -1;
	GLint g_UniformTexture = -1;
	int g_CurrentFrameNumber = 0;

	// WebGL requires indices to live in a bound GL_ELEMENT_ARRAY_BUFFER.
	// Pre-build a quad-index buffer big enough for typical batches.
	GLuint g_QuadIndexEBO = 0;
	int g_QuadIndexEBOQuads = 0; // capacity in quads currently uploaded

	const char* g_VertexShaderSource =
		"attribute vec3 a_position;\n"
		"attribute vec2 a_texcoord;\n"
		"attribute vec4 a_color;\n"
		"uniform mat4 u_projection;\n"
		"uniform mat4 u_modelview;\n"
		"varying vec2 v_texcoord;\n"
		"varying vec4 v_color;\n"
		"void main()\n"
		"{\n"
		"	gl_Position = u_projection * u_modelview * vec4(a_position, 1.0);\n"
		"	v_texcoord = a_texcoord;\n"
		"	v_color = a_color;\n"
		"}\n";

	const char* g_FragmentShaderSource =
		"precision mediump float;\n"
		"uniform sampler2D u_texture;\n"
		"varying vec2 v_texcoord;\n"
		"varying vec4 v_color;\n"
		"void main()\n"
		"{\n"
		"	vec4 texColor = texture2D(u_texture, v_texcoord);\n"
		"	gl_FragColor = texColor * v_color;\n"
		"}\n";

	bool InitShaders()
	{
		LOG->Info("=== Initializing GLES2 shaders ===");
#ifdef __EMSCRIPTEN__
		EM_ASM({
			console.log("*** EM_ASM TEST: InitShaders called ***");
			console.log("*** Canvas info: ", Module.canvas);
			console.log("*** Canvas size: " + Module.canvas.width + "x" + Module.canvas.height);
			console.log("*** Canvas id: " + Module.canvas.id);

			// Try to get WebGL context
			var gl = Module.canvas.getContext('webgl') || Module.canvas.getContext('experimental-webgl');
			console.log("*** WebGL context from canvas: ", gl);
			if (gl) {
				console.log("*** WebGL version: " + gl.getParameter(gl.VERSION));
				console.log("*** WebGL vendor: " + gl.getParameter(gl.VENDOR));

				// Try to draw a test triangle directly
				console.log("*** DIRECT TEST: Drawing triangle from JavaScript");
				gl.clearColor(1.0, 0.0, 1.0, 1.0); // Magenta
				gl.clear(gl.COLOR_BUFFER_BIT);
				console.log("*** Cleared to magenta");
			} else {
				console.log("*** ERROR: Cannot get WebGL context!");
			}
		});
#endif

		// Create and compile vertex shader
		GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
		LOG->Info("Created vertex shader: %d", vertexShader);

		glShaderSource(vertexShader, 1, &g_VertexShaderSource, NULL);
		glCompileShader(vertexShader);

		GLint success = 0;
		glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
		if (!success) {
			char infoLog[512];
			glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
			LOG->Warn("Vertex shader compilation FAILED: %s", infoLog);
			glDeleteShader(vertexShader);
			return false;
		}
		LOG->Info("Vertex shader compiled successfully");

		// Create and compile fragment shader
		GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
		LOG->Info("Created fragment shader: %d", fragmentShader);

		glShaderSource(fragmentShader, 1, &g_FragmentShaderSource, NULL);
		glCompileShader(fragmentShader);

		glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
		if (!success) {
			char infoLog[512];
			glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
			LOG->Warn("Fragment shader compilation FAILED: %s", infoLog);
			glDeleteShader(vertexShader);
			glDeleteShader(fragmentShader);
			return false;
		}
		LOG->Info("Fragment shader compiled successfully");

		// Link shader program
		g_ShaderProgram = glCreateProgram();
		LOG->Info("Created shader program: %d", g_ShaderProgram);

		glAttachShader(g_ShaderProgram, vertexShader);
		glAttachShader(g_ShaderProgram, fragmentShader);
		glLinkProgram(g_ShaderProgram);

		glGetProgramiv(g_ShaderProgram, GL_LINK_STATUS, &success);
		if (!success) {
			char infoLog[512];
			glGetProgramInfoLog(g_ShaderProgram, 512, NULL, infoLog);
			LOG->Warn("Shader program linking FAILED: %s", infoLog);
			glDeleteShader(vertexShader);
			glDeleteShader(fragmentShader);
			g_ShaderProgram = 0;
			return false;
		}
		LOG->Info("Shader program linked successfully");

		// Clean up shaders (no longer needed after linking)
		glDeleteShader(vertexShader);
		glDeleteShader(fragmentShader);

		// Get attribute locations
		g_AttribPosition = glGetAttribLocation(g_ShaderProgram, "a_position");
		g_AttribTexCoord = glGetAttribLocation(g_ShaderProgram, "a_texcoord");
		g_AttribColor = glGetAttribLocation(g_ShaderProgram, "a_color");

		// Get uniform locations
		g_UniformProjection = glGetUniformLocation(g_ShaderProgram, "u_projection");
		g_UniformModelView = glGetUniformLocation(g_ShaderProgram, "u_modelview");
		g_UniformTexture = glGetUniformLocation(g_ShaderProgram, "u_texture");

		LOG->Info("=== Shader initialization complete ===");
		LOG->Info("  Program ID: %d", g_ShaderProgram);
		LOG->Info("  Attributes: position=%d texcoord=%d color=%d", g_AttribPosition, g_AttribTexCoord, g_AttribColor);
		LOG->Info("  Uniforms: projection=%d modelview=%d texture=%d", g_UniformProjection, g_UniformModelView, g_UniformTexture);

		// Create default 1x1 white texture for untextured rendering
		glGenTextures(1, &g_DefaultWhiteTexture);
		glBindTexture(GL_TEXTURE_2D, g_DefaultWhiteTexture);
		unsigned char whitePixel[4] = {255, 255, 255, 255};
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, whitePixel);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		LOG->Info("Created default white texture: %d", g_DefaultWhiteTexture);

		return true;
	}
}

RageDisplay_GLES2::RageDisplay_GLES2()
{
	LOG->Trace( "RageDisplay_GLES2::RageDisplay_GLES2()" );
	LOG->MapLog("renderer", "Current renderer: OpenGL ES 2.0");
	LOG->Info("=== RageDisplay_GLES2 BUILD VERSION: 2024-05-04-SHADER-FIX-v3 ===");

	FixLittleEndian();
//	RageDisplay_GLES2_Helpers::Init();

	g_pWind = nullptr;
}

RString
RageDisplay_GLES2::Init( const VideoModeParams &p, bool bAllowUnacceleratedRenderer )
{
	g_pWind = LowLevelWindow::Create();

	bool bIgnore = false;
	RString sError = SetVideoMode( p, bIgnore );
	if (sError != "")
		return sError;

	// Get GPU capabilities up front so we don't have to query later.
	glGetIntegerv( GL_MAX_TEXTURE_SIZE, &Caps::iMaxTextureSize );
	glGetIntegerv( GL_MAX_TEXTURE_IMAGE_UNITS, &Caps::iMaxTextureUnits );

	// Log driver details
	g_pWind->LogDebugInformation();
	LOG->Info( "OGL Vendor: %s", glGetString(GL_VENDOR) );
	LOG->Info( "OGL Renderer: %s", glGetString(GL_RENDERER) );
	LOG->Info( "OGL Version: %s", glGetString(GL_VERSION) );
	LOG->Info( "OGL Max texture size: %i", Caps::iMaxTextureSize );
	LOG->Info( "OGL Texture units: %i", Caps::iMaxTextureUnits );

	/* Pretty-print the extension string: */
	LOG->Info( "OGL Extensions:" );
	{
		// glGetString(GL_EXTENSIONS) doesn't work for GL3 core profiles.
		// this will be useful in the future.
#if 0
		vector<string> extensions;
		const char *ext = 0;
		for (int i = 0; (ext = (const char*)glGetStringi(GL_EXTENSIONS, i)); i++)
		{
			extensions.push_back(string(ext));
		}

		sort( extensions.begin(), extensions.end() );
		size_t next = 0;
		while( next < extensions.size() )
		{
			size_t last = next;
			string type;
			for( size_t i = next; i<extensions.size(); ++i )
			{
				vector<string> segments;
				split(extensions[i], '_', segments);
				string this_type;
				if (segments.size() > 2)
					this_type = join("_", segments.begin(), segments.begin()+2);
				if (i > next && this_type != type)
					break;
				type = this_type;
				last = i;
			}

			if (next == last)
			{
				printf( "  %s\n", extensions[next].c_str() );
				++next;
				continue;
			}

			string sList = ssprintf( "  %s: ", type.c_str() );
			while( next <= last )
			{
				vector<string> segments;
				split( extensions[next], '_', segments );
				string ext_short = join( "_", segments.begin()+2, segments.end() );
				sList += ext_short;
				if (next < last)
					sList += ", ";
				if (next == last || sList.size() + extensions[next+1].size() > 78)
				{
					printf( "%s\n", sList.c_str() );
					sList = "    ";
				}
				++next;
			}
		}
#else
		const char *szExtensionString = (const char *) glGetString(GL_EXTENSIONS);
		vector<RString> asExtensions;
		split( szExtensionString, " ", asExtensions );
		sort( asExtensions.begin(), asExtensions.end() );
		size_t iNextToPrint = 0;
		while( iNextToPrint < asExtensions.size() )
		{
			size_t iLastToPrint = iNextToPrint;
			RString sType;
			for( size_t i = iNextToPrint; i<asExtensions.size(); ++i )
			{
				vector<RString> asBits;
				split( asExtensions[i], "_", asBits );
				RString sThisType;
				if (asBits.size() > 2)
					sThisType = join( "_", asBits.begin(), asBits.begin()+2 );
				if (i > iNextToPrint && sThisType != sType)
					break;
				sType = sThisType;
				iLastToPrint = i;
			}

			if (iNextToPrint == iLastToPrint)
			{
				LOG->Info( "  %s", asExtensions[iNextToPrint].c_str() );
				++iNextToPrint;
				continue;
			}

			RString sList = ssprintf( "  %s: ", sType.c_str() );
			while( iNextToPrint <= iLastToPrint )
			{
				vector<RString> asBits;
				split( asExtensions[iNextToPrint], "_", asBits );
				RString sShortExt = join( "_", asBits.begin()+2, asBits.end() );
				sList += sShortExt;
				if (iNextToPrint < iLastToPrint)
					sList += ", ";
				if (iNextToPrint == iLastToPrint || sList.size() + asExtensions[iNextToPrint+1].size() > 120)
				{
					LOG->Info( "%s", sList.c_str() );
					sList = "    ";
				}
				++iNextToPrint;
			}
#endif
		}
	}

#ifndef __EMSCRIPTEN__
	glewExperimental = true;
	glewInit();
#endif

	/* Log this, so if people complain that the radar looks bad on their
	 * system we can compare them: */
	//glGetFloatv( GL_LINE_WIDTH_RANGE, g_line_range );
	//glGetFloatv( GL_POINT_SIZE_RANGE, g_point_range );

	return RString();
}

// Return true if mode change was successful.
// bNewDeviceOut is set true if a new device was created and textures
// need to be reloaded.
RString RageDisplay_GLES2::TryVideoMode( const VideoModeParams &p, bool &bNewDeviceOut )
{
	VideoModeParams vm = p;
	vm.windowed = 1; // force windowed until I trust this thing.
	LOG->Warn( "RageDisplay_GLES2::TryVideoMode( %d, %d, %d, %d, %d, %d )",
		vm.windowed, vm.width, vm.height, vm.bpp, vm.rate, vm.vsync );

	RString err = g_pWind->TryVideoMode( vm, bNewDeviceOut );
	if (err != "")
		return err;	// failed to set video mode

	if (bNewDeviceOut)
	{
		LOG->Info("New device created, initializing OpenGL state");
#ifndef __EMSCRIPTEN__
		// NOTE: This isn't needed in an actual GLES2 context...
		glewInit();
#endif

		/* We have a new OpenGL context, so we have to tell our textures that
		 * their OpenGL texture number is invalid. */
		if (TEXTUREMAN)
			TEXTUREMAN->InvalidateTextures();

		/* Delete all render targets.  They may have associated resources other than
		 * the texture itself. */
		//FOREACHM( unsigned, RenderTarget *, g_mapRenderTargets, rt )
		//	delete rt->second;
		//g_mapRenderTargets.clear();

		/* Recreate all vertex buffers. */
		//InvalidateObjects();

		InitShaders();
	}
	else
	{
		LOG->Info("Device not new, bNewDeviceOut=false");
	}

	// Make sure shaders are initialized even if bNewDeviceOut is false
	if (g_ShaderProgram == 0)
	{
		LOG->Info("Shader program not initialized, calling InitShaders()");
		InitShaders();
	}

	ResolutionChanged();

	return RString();
}

int RageDisplay_GLES2::GetMaxTextureSize() const
{
	return Caps::iMaxTextureSize;
}

bool RageDisplay_GLES2::BeginFrame()
{
	/* We do this in here, rather than ResolutionChanged, or we won't update the
	 * viewport for the concurrent rendering context. */
	int fWidth = g_pWind->GetActualVideoModeParams().width;
	int fHeight = g_pWind->GetActualVideoModeParams().height;

	static int frameCount = 0;
	frameCount++;

	// Share frame count with DrawQuads for tracking
	g_CurrentFrameNumber = frameCount;
	if (frameCount <= 5) {
		LOG->Info("BeginFrame: Frame %d, Setting viewport to %dx%d", frameCount, fWidth, fHeight);

		// Initialize shaders on first frame if not already done
		if (frameCount == 1 && g_ShaderProgram == 0) {
			LOG->Info("BeginFrame: First frame, initializing shaders...");
			InitShaders();
		}

		// Clear any existing errors first
		while (glGetError() != GL_NO_ERROR);

		glViewport( 0, 0, fWidth, fHeight );

		GLenum err = glGetError();
		if (err != GL_NO_ERROR) {
			LOG->Warn("GL Error after glViewport: 0x%x (params: %d, %d, %d, %d)", err, 0, 0, fWidth, fHeight);
		}

		// REMOVED double increment - frameCount is already incremented at start of function
	} else {
		glViewport( 0, 0, fWidth, fHeight );
	}

	glClearColor( 0.0f, 0.0f, 0.5f, 1.0f );  // dark blue: makes "no BG draw" distinguishable from black-textured BG
	SetZWrite( true );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

	// Probe: read pixel right after clear on a specific frame.
	if (g_CurrentFrameNumber == 1850) {
		unsigned char px[4] = {0,0,0,0};
		glReadPixels(640, 360, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
		LOG->Info("[POST-CLEAR frame=%d] center px = #%02x%02x%02x%02x (expect 0000 80 ff)",
			g_CurrentFrameNumber, px[0], px[1], px[2], px[3]);
	}

	// Disable depth test for debugging
	glDisable(GL_DEPTH_TEST);

	// Log viewport for debugging - check every frame for first 5 frames
	if (frameCount <= 5 || frameCount == 60) {
		GLint viewport[4];
		glGetIntegerv(GL_VIEWPORT, viewport);

		EM_ASM({
			console.error('[VIEWPORT DEBUG] Frame ' + $0 + ': x=' + $1 + ' y=' + $2 + ' w=' + $3 + ' h=' + $4);
		}, frameCount, viewport[0], viewport[1], viewport[2], viewport[3]);
	}

	// EMERGENCY TEST is now drawn at end of frame in EndFrame() so the game's
	// own draws don't paint over it.

	GLenum err2 = glGetError();
	if (err2 != GL_NO_ERROR && frameCount < 5) {
		LOG->Warn("GL Error after glClear: 0x%x", err2);
	}

	return RageDisplay::BeginFrame();
}

void RageDisplay_GLES2::EndFrame()
{
	static int endFrameCount = 0;
	endFrameCount++;

	if (endFrameCount % 60 == 0) {
		LOG->Info("[EndFrame] #%d - About to flush and swap buffers", endFrameCount);
	}

	glFlush();

	// XXX: This is broken on NVidia, as their xrandr sucks.
	FrameLimitBeforeVsync( g_pWind->GetActualVideoModeParams().rate );
	g_pWind->SwapBuffers();
	FrameLimitAfterVsync();

	g_pWind->Update();

	RageDisplay::EndFrame();

	if (endFrameCount % 60 == 0) {
		LOG->Info("[EndFrame] #%d - Completed", endFrameCount);
	}
}

RageDisplay_GLES2::~RageDisplay_GLES2()
{
	delete g_pWind;
}

void
RageDisplay_GLES2::GetDisplaySpecs(DisplaySpecs &out) const
{
	out.clear();
	g_pWind->GetDisplaySpecs(out);
}

RageSurface*
RageDisplay_GLES2::CreateScreenshot()
{
	const RagePixelFormatDesc &desc = PIXEL_FORMAT_DESC[RagePixelFormat_RGB8];
	RageSurface *image = CreateSurface(
		640, 480, desc.bpp,
		desc.masks[0], desc.masks[1], desc.masks[2], desc.masks[3] );

	memset( image->pixels, 0, 480*image->pitch );

	return image;
}

const RageDisplay::RagePixelFormatDesc*
RageDisplay_GLES2::GetPixelFormatDesc(RagePixelFormat pf) const
{
	ASSERT( pf >= 0 && pf < NUM_RagePixelFormat );
	return &PIXEL_FORMAT_DESC[pf];
}

RageMatrix
RageDisplay_GLES2::GetOrthoMatrix( float l, float r, float b, float t, float zn, float zf )
{
	RageMatrix m(
		2/(r-l),      0,            0,           0,
		0,            2/(t-b),      0,           0,
		0,            0,            -2/(zf-zn),   0,
		-(r+l)/(r-l), -(t+b)/(t-b), -(zf+zn)/(zf-zn),  1 );
	return m;
}

class RageCompiledGeometryGLES2 : public RageCompiledGeometry
{
public:
	
	void Allocate( const vector<msMesh> &vMeshes )
	{
	}
	void Change( const vector<msMesh> &vMeshes )
	{
	}
	void Draw( int iMeshIndex ) const
	{
	}
};

RageCompiledGeometry*
RageDisplay_GLES2::CreateCompiledGeometry()
{
	return new RageCompiledGeometryGLES2;
}

void
RageDisplay_GLES2::DeleteCompiledGeometry( RageCompiledGeometry *p )
{
	delete p;
}

RString
RageDisplay_GLES2::GetApiDescription() const
{
	return "OpenGL ES 2.0";
}

ActualVideoModeParams
RageDisplay_GLES2::GetActualVideoModeParams() const
{
	return g_pWind->GetActualVideoModeParams();
}

void
RageDisplay_GLES2::SetBlendMode( BlendMode mode )
{
	glEnable(GL_BLEND);

	if (mode == BLEND_INVERT_DEST)
		glBlendEquation(GL_FUNC_SUBTRACT);
	else if (mode == BLEND_SUBTRACT)
		glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
	else
		glBlendEquation(GL_FUNC_ADD);

	int iSourceRGB, iDestRGB;
	int iSourceAlpha = GL_ONE, iDestAlpha = GL_ONE_MINUS_SRC_ALPHA;
	switch (mode)
	{
	case BLEND_NORMAL:
		iSourceRGB = GL_SRC_ALPHA; iDestRGB = GL_ONE_MINUS_SRC_ALPHA;
		break;
	case BLEND_ADD:
		iSourceRGB = GL_SRC_ALPHA; iDestRGB = GL_ONE;
		break;
	case BLEND_SUBTRACT:
		iSourceRGB = GL_SRC_ALPHA; iDestRGB = GL_ONE_MINUS_SRC_ALPHA;
		break;
	case BLEND_MODULATE:
		iSourceRGB = GL_ZERO; iDestRGB = GL_SRC_COLOR;
		break;
	case BLEND_COPY_SRC:
		iSourceRGB = GL_ONE; iDestRGB = GL_ZERO;
		iSourceAlpha = GL_ONE; iDestAlpha = GL_ZERO;
		break;
	case BLEND_ALPHA_MASK:
		iSourceRGB = GL_ZERO; iDestRGB = GL_ONE;
		iSourceAlpha = GL_ZERO; iDestAlpha = GL_SRC_ALPHA;
		break;
	case BLEND_ALPHA_KNOCK_OUT:
		iSourceRGB = GL_ZERO; iDestRGB = GL_ONE;
		iSourceAlpha = GL_ZERO; iDestAlpha = GL_ONE_MINUS_SRC_ALPHA;
		break;
	case BLEND_ALPHA_MULTIPLY:
		iSourceRGB = GL_SRC_ALPHA; iDestRGB = GL_ZERO;
		break;
	case BLEND_WEIGHTED_MULTIPLY:
		iSourceRGB = GL_DST_COLOR; iDestRGB = GL_SRC_COLOR;
		break;
	case BLEND_INVERT_DEST:
		iSourceRGB = GL_ONE; iDestRGB = GL_ONE;
		break;
	case BLEND_NO_EFFECT:
		iSourceRGB = GL_ZERO; iDestRGB = GL_ONE;
		iSourceAlpha = GL_ZERO; iDestAlpha = GL_ONE;
		break;
	DEFAULT_FAIL(mode);
	}

	// WebGL/GLES2 always supports glBlendFuncSeparate as a core function.
	glBlendFuncSeparate(iSourceRGB, iDestRGB, iSourceAlpha, iDestAlpha);
}

bool
RageDisplay_GLES2::SupportsTextureFormat( RagePixelFormat pixfmt, bool realtime )
{
	/* If we support a pixfmt for texture formats but not for surface formats, then
	 * we'll have to convert the texture to a supported surface format before uploading.
	 * This is too slow for dynamic textures. */
	if (realtime && !SupportsSurfaceFormat(pixfmt))
		return false;

	switch (g_GLPixFmtInfo[pixfmt].format)
	{
	case GL_COLOR_INDEX:
		return false;
	case GL_BGR:
	case GL_BGRA:
		//return !!GLEW_EXT_bgra;
		return false; // no BGRA on ES2 (without exts)
	default:
		return true;
	}

	return true;
}

bool
RageDisplay_GLES2::SupportsPerVertexMatrixScale()
{
	return true;
}

uintptr_t
RageDisplay_GLES2::CreateTexture(
	RagePixelFormat pixfmt,
	RageSurface* img,
	bool bGenerateMipMaps
	)
{
	static int textureCount = 0;
	textureCount++;

	LOG->Info("CreateTexture called! Count=%d", textureCount);

	if (!img || !img->pixels) {
		LOG->Warn("CreateTexture: Invalid surface, img=%p", img);
		return 0;
	}

	// Convert surface to RGBA32 if needed (handles palettized/8bpp textures)
	RageSurface *actualImg = img;
	bool needsDelete = false;

	if (img->format->BitsPerPixel != 32) {
		LOG->Info("CreateTexture #%d: input is %dbpp (size %dx%d), converting...", textureCount, img->format->BitsPerPixel, img->w, img->h);
		if (textureCount <= 5) {
			LOG->Info("  Converting %d bpp surface to RGBA32", img->format->BitsPerPixel);
			// Check if palette exists and sample it
			if (img->format->palette) {
				LOG->Info("  Palette exists: %d colors", img->format->palette->ncolors);
				if (img->format->palette->ncolors > 0) {
					LOG->Info("  Palette[0]: R=%d G=%d B=%d A=%d",
						img->format->palette->colors[0].r,
						img->format->palette->colors[0].g,
						img->format->palette->colors[0].b,
						img->format->palette->colors[0].a);
				}
				if (img->format->palette->ncolors > 255) {
					LOG->Info("  Palette[255]: R=%d G=%d B=%d A=%d",
						img->format->palette->colors[255].r,
						img->format->palette->colors[255].g,
						img->format->palette->colors[255].b,
						img->format->palette->colors[255].a);
				}
			}

			// Sample original 8bpp palette indices to see if there's variation
			if (img->pixels && img->w > 0 && img->h > 0) {
				unsigned char* p = (unsigned char*)img->pixels;
				LOG->Info("  Original 8bpp indices: [0]=%d [1]=%d [10]=%d [100]=%d",
					p[0], p[1], p[10], p[100]);

				// Sample across the texture to find any non-zero values
				int nonZeroCount = 0;
				int samplePoints[] = {0, 1, 10, 100, 500, 1000, 5000, 10000, 50000};
				for (int i = 0; i < 9; i++) {
					int idx = samplePoints[i];
					if (idx < img->w * img->h) {
						if (p[idx] != 0) {
							nonZeroCount++;
							LOG->Info("  Found non-zero at index %d: value=%d", idx, p[idx]);
						}
					}
				}
				LOG->Info("  Non-zero pixels found: %d out of %d sampled", nonZeroCount, 9);
			}
		}

		// Convert to RGBA32
		// On little-endian, byte order in memory should be R, G, B, A
		// Masks: R=0x000000FF (byte 0), G=0x0000FF00 (byte 1), B=0x00FF0000 (byte 2), A=0xFF000000 (byte 3)
		RageSurface *converted = NULL;
		RageSurfaceUtils::ConvertSurface(img, converted,
			img->w, img->h, 32,
			0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);

		if (textureCount <= 5) {
			LOG->Info("  ConvertSurface returned: %p", converted);
		}

		if (converted) {
			LOG->Info("CreateTexture #%d: converted to %dbpp; sample px [0]=%02x%02x%02x%02x [w/2,h/2]=%02x%02x%02x%02x",
				textureCount, converted->format->BitsPerPixel,
				((unsigned char*)converted->pixels)[0],
				((unsigned char*)converted->pixels)[1],
				((unsigned char*)converted->pixels)[2],
				((unsigned char*)converted->pixels)[3],
				((unsigned char*)converted->pixels)[(converted->h/2 * converted->pitch) + (converted->w/2)*4 + 0],
				((unsigned char*)converted->pixels)[(converted->h/2 * converted->pitch) + (converted->w/2)*4 + 1],
				((unsigned char*)converted->pixels)[(converted->h/2 * converted->pitch) + (converted->w/2)*4 + 2],
				((unsigned char*)converted->pixels)[(converted->h/2 * converted->pitch) + (converted->w/2)*4 + 3]);
			if (textureCount <= 5) {
				LOG->Info("  ENTERED converted block");
			}

			actualImg = converted;
			needsDelete = true;

			// NOTE: Alpha fix disabled - our RGBA PNGs already have correct alpha channels
			// The old code was for palette PNGs that lost alpha during conversion
			if (textureCount <= 5) {
				LOG->Info("  Alpha fix DISABLED - using original alpha from RGBA PNG");
				// Sample the alpha values to verify they're preserved
				unsigned char* p = (unsigned char*)actualImg->pixels;
				LOG->Info("  Sample - Pixel[0]: R=%02x G=%02x B=%02x A=%02x", p[0], p[1], p[2], p[3]);
				if (actualImg->w > 100 && actualImg->h > 10) {
					unsigned char* later = p + (actualImg->w * 10 + 100) * 4;
					LOG->Info("  Sample - Pixel[10,100]: R=%02x G=%02x B=%02x A=%02x", later[0], later[1], later[2], later[3]);
				}
			}
		} else {
			LOG->Warn("  Failed to convert surface to RGBA32");
		}
	}

	if (textureCount <= 5) {
		LOG->Info("=== CreateTexture #%d ===", textureCount);
		LOG->Info("  RagePixelFormat=%d, size=%dx%d, mipmaps=%d", pixfmt, img->w, img->h, bGenerateMipMaps);
		LOG->Info("  Original Surface: pitch=%d, bpp=%d, pixels=%p", img->pitch, img->format->BitsPerPixel, img->pixels);
		LOG->Info("  Actual Surface: pitch=%d, bpp=%d, pixels=%p", actualImg->pitch, actualImg->format->BitsPerPixel, actualImg->pixels);
	}

	GLuint iTexture;
	glGenTextures(1, &iTexture);
	glBindTexture(GL_TEXTURE_2D, iTexture);

	// Clear any existing errors
	while (glGetError() != GL_NO_ERROR);

	// After conversion above, the surface is now in RGBA format with byte order R, G, B, A
	// So we should use GL_RGBA and GL_UNSIGNED_BYTE regardless of original pixfmt
	GLenum format = GL_RGBA;
	GLenum type = GL_UNSIGNED_BYTE;

	// Exception: if the image has no alpha channel (RGB), use GL_RGB
	if (actualImg->format->BytesPerPixel == 3) {
		format = GL_RGB;
	}

	if (textureCount <= 5 || textureCount == 82) {
		LOG->Info("  [tc=%d] Using GL format=0x%x, type=0x%x, bpp=%d, w=%d h=%d pitch=%d",
			textureCount, format, type, actualImg->format->BytesPerPixel,
			actualImg->w, actualImg->h, actualImg->pitch);
		LOG->Info("  [tc=%d] Surface masks: R=0x%08x G=0x%08x B=0x%08x A=0x%08x",
			textureCount,
			actualImg->format->Rmask, actualImg->format->Gmask,
			actualImg->format->Bmask, actualImg->format->Amask);
		unsigned char* p = (unsigned char*)actualImg->pixels;
		LOG->Info("  [tc=%d] sample [0]=%02x%02x%02x%02x [pix(640,360)]=%02x%02x%02x%02x",
			textureCount,
			p[0], p[1], p[2], p[3],
			p[360 * actualImg->pitch + 640 * actualImg->format->BytesPerPixel + 0],
			p[360 * actualImg->pitch + 640 * actualImg->format->BytesPerPixel + 1],
			p[360 * actualImg->pitch + 640 * actualImg->format->BytesPerPixel + 2],
			p[360 * actualImg->pitch + 640 * actualImg->format->BytesPerPixel + 3]);
	}

	// Set pixel unpack alignment based on the surface format
	// WebGL/GLES2 requires setting GL_UNPACK_ALIGNMENT if rows aren't 4-byte aligned
	int bytesPerPixel = actualImg->format->BytesPerPixel;
	int rowBytes = actualImg->w * bytesPerPixel;
	int alignment = 4;  // Default OpenGL alignment
	if (rowBytes % 8 == 0) alignment = 8;
	else if (rowBytes % 4 == 0) alignment = 4;
	else if (rowBytes % 2 == 0) alignment = 2;
	else alignment = 1;
	glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);

	glTexImage2D(
		GL_TEXTURE_2D,
		0,
		format,
		actualImg->w,
		actualImg->h,
		0,
		format,
		type,
		actualImg->pixels
	);

	// Reset to default
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

	GLenum err = glGetError();
	if (err != GL_NO_ERROR) {
		LOG->Warn("CreateTexture: glTexImage2D error 0x%x (format=0x%x, type=0x%x, size=%dx%d)",
			err, format, type, actualImg->w, actualImg->h);
		glDeleteTextures(1, &iTexture);
		if (needsDelete) {
			delete actualImg;
		}
		return 0;
	}

	// Debug: Sample the uploaded texture data
	if (textureCount <= 5 && actualImg->pixels) {
		unsigned char* p = (unsigned char*)actualImg->pixels;
		LOG->Info("  After upload - Sample pixels: [0]=%02x [1]=%02x [2]=%02x [3]=%02x",
			p[0], p[1], p[2], p[3]);
	}

	// Set texture parameters
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	// DEBUG: force LINEAR (no mipmaps) to test if mipmap generation is broken
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	(void)bGenerateMipMaps;

	LOG->Info("CreateTexture: Count=%d -> GL handle %u", textureCount, iTexture);

	// Clean up converted surface if we created one
	if (needsDelete) {
		delete actualImg;
	}

	return iTexture;
}

void
RageDisplay_GLES2::UpdateTexture(
	uintptr_t iTexHandle,
	RageSurface* img,
	int xoffset, int yoffset, int width, int height
	)
{
	if (!img || !img->pixels || iTexHandle == 0) {
		LOG->Warn("UpdateTexture: Invalid parameters");
		return;
	}

	glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(iTexHandle));

	// Get pixel format - assume RGBA8 for now
	// TODO: Get actual format from somewhere
	glTexSubImage2D(
		GL_TEXTURE_2D,
		0,
		xoffset,
		yoffset,
		width,
		height,
		GL_RGBA,
		GL_UNSIGNED_BYTE,
		img->pixels
	);

	GLenum err = glGetError();
	if (err != GL_NO_ERROR) {
		LOG->Warn("UpdateTexture: GL error 0x%x", err);
	}
}

void
RageDisplay_GLES2::DeleteTexture( uintptr_t iTexHandle )
{
	if (iTexHandle == 0)
		return;

	GLuint tex = static_cast<GLuint>(iTexHandle);
	glDeleteTextures(1, &tex);
}

void
RageDisplay_GLES2::ClearAllTextures()
{
	FOREACH_ENUM( TextureUnit, i )
		SetTexture( i, 0 );

	// HACK:  Reset the active texture to 0.
	// TODO:  Change all texture functions to take a stage number.
	glActiveTexture(GL_TEXTURE0);
}

int
RageDisplay_GLES2::GetNumTextureUnits()
{
	return Caps::iMaxTextureUnits;
}

static bool
SetTextureUnit( TextureUnit tu )
{
	if ((int) tu > Caps::iMaxTextureUnits)
		return false;
	glActiveTexture( enum_add2(GL_TEXTURE0, tu) );
	return true;
}

void
RageDisplay_GLES2::SetTexture( TextureUnit tu, uintptr_t iTexture )
{
	static int setTextureCount = 0;
	setTextureCount++;
	// Log first 20 calls, or any call with non-zero texture
	if (setTextureCount <= 20 || iTexture != 0) {
		LOG->Info("SetTexture call #%d: unit=%d, texture=%u", setTextureCount, tu, (unsigned)iTexture);
	}

	if (!SetTextureUnit( tu ))
		return;

	if (iTexture)
	{
#ifndef __EMSCRIPTEN__
		glEnable( GL_TEXTURE_2D ); // Not available in GLES2/WebGL
#endif
		glBindTexture( GL_TEXTURE_2D, static_cast<GLuint>(iTexture) );
	}
	else
	{
#ifndef __EMSCRIPTEN__
		glDisable( GL_TEXTURE_2D ); // Not available in GLES2/WebGL
#endif
		glBindTexture( GL_TEXTURE_2D, 0 );
	}
}

void 
RageDisplay_GLES2::SetTextureMode( TextureUnit tu, TextureMode tm )
{
	// TODO
}

void
RageDisplay_GLES2::SetTextureWrapping( TextureUnit tu, bool b )
{
	// TODO
}

void
RageDisplay_GLES2::SetTextureFiltering( TextureUnit tu, bool b )
{
	// Check if a texture is bound before setting parameters
	GLint boundTexture = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundTexture);
	if (boundTexture == 0) {
		// No texture bound, skip setting parameters
		return;
	}

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, b ? GL_LINEAR : GL_NEAREST);

	GLint iMinFilter = 0;
	if (b)
	{
		GLint iWidth1 = -1;
		GLint iWidth2 = -1;
#ifndef __EMSCRIPTEN__
		// glGetTexLevelParameteriv not available in GLES2/WebGL
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &iWidth1);
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 1, GL_TEXTURE_WIDTH, &iWidth2);
#else
		// CreateTexture currently uploads only level 0 (no glGenerateMipmap),
		// so on WebGL mipmaps are NOT available. Setting a *_MIPMAP_* filter
		// on a non-mipmapped POT texture makes it sampling-incomplete and
		// returns (0,0,0,1) — silent black. Force the LINEAR branch.
		iWidth1 = 2;
		iWidth2 = 0;
#endif
		if (iWidth1 > 1 && iWidth2 != 0)
		{
			/* Mipmaps are enabled. */
			if (g_pWind->GetActualVideoModeParams().bTrilinearFiltering)
				iMinFilter = GL_LINEAR_MIPMAP_LINEAR;
			else
				iMinFilter = GL_LINEAR_MIPMAP_NEAREST;
		}
		else
		{
			iMinFilter = GL_LINEAR;
		}
	}
	else
	{
		iMinFilter = GL_NEAREST;
	}

	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, iMinFilter );
}

bool
RageDisplay_GLES2::IsZWriteEnabled() const
{
	return State::bZWriteEnabled;
}

bool
RageDisplay_GLES2::IsZTestEnabled() const
{
	return State::bZTestEnabled;
}

void
RageDisplay_GLES2::SetZWrite( bool b )
{
	if (State::bZWriteEnabled != b)
	{
		State::bZWriteEnabled = b;
		glDepthMask( b );
	}
}

void
RageDisplay_GLES2::SetZBias( float f )
{
	float fNear = SCALE( f, 0.0f, 1.0f, 0.05f, 0.0f );
	float fFar = SCALE( f, 0.0f, 1.0f, 1.0f, 0.95f );

#ifdef __EMSCRIPTEN__
	glDepthRangef( fNear, fFar );  // GLES2 uses glDepthRangef
#else
	glDepthRange( fNear, fFar );
#endif
}

void
RageDisplay_GLES2::SetZTestMode( ZTestMode mode )
{
	glEnable( GL_DEPTH_TEST );
	switch( mode )
	{
	case ZTEST_OFF:
		glDisable( GL_DEPTH_TEST );
		glDepthFunc( GL_ALWAYS );
		State::bZTestEnabled = false;
		break;
	case ZTEST_WRITE_ON_PASS: glDepthFunc( GL_LEQUAL ); break;
	case ZTEST_WRITE_ON_FAIL: glDepthFunc( GL_GREATER ); break;
	default:
		FAIL_M(ssprintf("Invalid ZTestMode: %i", mode));
	}
	State::bZTestEnabled = true;
}





/*


void RageDisplay_Legacy::SetBlendMode( BlendMode mode )
{
	glEnable(GL_BLEND);

	if (glBlendEquation != nullptr)
	{
		if (mode == BLEND_INVERT_DEST)
			glBlendEquation( GL_FUNC_SUBTRACT );
		else if (mode == BLEND_SUBTRACT)
			glBlendEquation( GL_FUNC_REVERSE_SUBTRACT );
		else
			glBlendEquation( GL_FUNC_ADD );
	}

	int iSourceRGB, iDestRGB;
	int iSourceAlpha = GL_ONE, iDestAlpha = GL_ONE_MINUS_SRC_ALPHA;
	switch( mode )
	{
	case BLEND_NORMAL:
		iSourceRGB = GL_SRC_ALPHA; iDestRGB = GL_ONE_MINUS_SRC_ALPHA;
		break;
	case BLEND_ADD:
		iSourceRGB = GL_SRC_ALPHA; iDestRGB = GL_ONE;
		break;
	case BLEND_SUBTRACT:
		iSourceRGB = GL_SRC_ALPHA; iDestRGB = GL_ONE_MINUS_SRC_ALPHA;
		break;
	case BLEND_MODULATE:
		iSourceRGB = GL_ZERO; iDestRGB = GL_SRC_COLOR;
		break;
	case BLEND_COPY_SRC:
		iSourceRGB = GL_ONE; iDestRGB = GL_ZERO;
		iSourceAlpha = GL_ONE; iDestAlpha = GL_ZERO;
		break;
	case BLEND_ALPHA_MASK:
		iSourceRGB = GL_ZERO; iDestRGB = GL_ONE;
		iSourceAlpha = GL_ZERO; iDestAlpha = GL_SRC_ALPHA;
		break;
	case BLEND_ALPHA_KNOCK_OUT:
		iSourceRGB = GL_ZERO; iDestRGB = GL_ONE;
		iSourceAlpha = GL_ZERO; iDestAlpha = GL_ONE_MINUS_SRC_ALPHA;
		break;
	case BLEND_ALPHA_MULTIPLY:
		iSourceRGB = GL_SRC_ALPHA; iDestRGB = GL_ZERO;
		break;
	case BLEND_WEIGHTED_MULTIPLY:
		// output = 2*(dst*src).  0.5,0.5,0.5 is identity; darker colors darken the image,
		// and brighter colors lighten the image.
		iSourceRGB = GL_DST_COLOR; iDestRGB = GL_SRC_COLOR;
		break;
	case BLEND_INVERT_DEST:
		// out = src - dst.  The source color should almost always be #FFFFFF, to make it "1 - dst".
		iSourceRGB = GL_ONE; iDestRGB = GL_ONE;
		break;
	case BLEND_NO_EFFECT:
		iSourceRGB = GL_ZERO; iDestRGB = GL_ONE;
		iSourceAlpha = GL_ZERO; iDestAlpha = GL_ONE;
		break;
	DEFAULT_FAIL( mode );
	}

	if (GLEW_EXT_blend_equation_separate)
		glBlendFuncSeparateEXT( iSourceRGB, iDestRGB, iSourceAlpha, iDestAlpha );
	else
		glBlendFunc( iSourceRGB, iDestRGB );
}

bool RageDisplay_Legacy::IsZWriteEnabled() const
{
	bool a;
	glGetBooleanv( GL_DEPTH_WRITEMASK, (unsigned char*)&a );
	return a;
}


*/

void
RageDisplay_GLES2::ClearZBuffer()
{
	bool write = IsZWriteEnabled();
	SetZWrite( true );
	glClear( GL_DEPTH_BUFFER_BIT );
	SetZWrite( write );
}

void
RageDisplay_GLES2::SetCullMode( CullMode mode )
{
	if (mode != CULL_NONE)
		glEnable(GL_CULL_FACE);
	switch( mode )
	{
	case CULL_BACK:
		glCullFace( GL_BACK );
		break;
	case CULL_FRONT:
		glCullFace( GL_FRONT );
		break;
	case CULL_NONE:
		glDisable( GL_CULL_FACE );
		break;
	default:
		FAIL_M(ssprintf("Invalid CullMode: %i", mode));
	}
}

void
RageDisplay_GLES2::SetAlphaTest( bool b )
{
	if (State::bAlphaTestEnabled != b)
	{
		State::bAlphaTestEnabled = b;
#ifndef __EMSCRIPTEN__
		// GL_ALPHA_TEST not available in GLES2/WebGL - handled in shaders
		b ? glEnable(GL_ALPHA_TEST) : glDisable(GL_ALPHA_TEST);
#endif
	}
}

void
RageDisplay_GLES2::SetMaterial( 
	const RageColor &emissive,
	const RageColor &ambient,
	const RageColor &diffuse,
	const RageColor &specular,
	float shininess
	)
{
	// TODO
}

void
RageDisplay_GLES2::SetLineWidth(float fWidth)
{
	glLineWidth(fWidth);
}

void
RageDisplay_GLES2::SetPolygonMode(PolygonMode pm)
{
#ifndef __EMSCRIPTEN__
	// glPolygonMode is not available in WebGL/GLES2
	GLenum m;
	switch (pm)
	{
	case POLYGON_FILL:	m = GL_FILL; break;
	case POLYGON_LINE:	m = GL_LINE; break;
	default:
		FAIL_M(ssprintf("Invalid PolygonMode: %i", pm));
	}
	glPolygonMode(GL_FRONT_AND_BACK, m);
#endif
}

void
RageDisplay_GLES2::SetLighting( bool b )
{
	// TODO
}

void
RageDisplay_GLES2::SetLightOff( int index )
{
	// TODO
}

void
RageDisplay_GLES2::SetLightDirectional( 
	int index, 
	const RageColor &ambient, 
	const RageColor &diffuse, 
	const RageColor &specular, 
	const RageVector3 &dir )
{
	// TODO
}

void
RageDisplay_GLES2::SetSphereEnvironmentMapping( TextureUnit tu, bool b )
{
	// TODO
}

void
RageDisplay_GLES2::SetCelShaded( int stage )
{
	// TODO
}

void
RageDisplay_GLES2::DrawQuadsInternal( const RageSpriteVertex v[], int iNumVerts )
{
	if (iNumVerts == 0)
		return;

	static int drawCallCount = 0;
	drawCallCount++;

	// Track which frame we're on
	static int lastLoggedFrame = -1;
	if (lastLoggedFrame != g_CurrentFrameNumber) {
		lastLoggedFrame = g_CurrentFrameNumber;
		if (g_CurrentFrameNumber < 5 || g_CurrentFrameNumber % 60 == 0) {
			LOG->Info("[FRAME %d] DrawQuadsInternal called", g_CurrentFrameNumber);
		}
	}

	// Use the shader program
	if (g_ShaderProgram == 0) {
		LOG->Warn("DrawQuadsInternal: Shader program not initialized");
		return;
	}

	// Verify shader program is valid
	if (drawCallCount <= 3) {
		GLint isProgram = glIsProgram(g_ShaderProgram);
		GLint currentProgram = 0;
		glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);
		LOG->Info("DrawQuadsInternal call #%d: %d verts", drawCallCount, iNumVerts);
		LOG->Info("  Shader program: %d (valid=%d, current=%d)", g_ShaderProgram, isProgram, currentProgram);
		LOG->Info("  Attrib locations: pos=%d tex=%d color=%d", g_AttribPosition, g_AttribTexCoord, g_AttribColor);
		LOG->Info("  V0: pos=(%.1f,%.1f,%.1f), tex=(%.3f,%.3f), color BGRA=(%d,%d,%d,%d)",
			v[0].p.x, v[0].p.y, v[0].p.z, v[0].t.x, v[0].t.y,
			(int)v[0].c.b, (int)v[0].c.g, (int)v[0].c.r, (int)v[0].c.a);
	}
	// One specific frame — dump every quad with no dedupe so we can see
	// whether something opaque is overdrawing the BG.
	if (iNumVerts == 4 && g_CurrentFrameNumber == 1850) {
		GLint boundTex = 0;
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundTex);
		GLint activeTex = 0;
		glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTex);
		// Also check which texture is bound on units 0 through 3
		GLint boundOn[4] = {0,0,0,0};
		for (int u = 0; u < 4; u++) {
			glActiveTexture(GL_TEXTURE0 + u);
			glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundOn[u]);
		}
		glActiveTexture(activeTex); // restore
		GLint blendOn = 0; glGetIntegerv(GL_BLEND, &blendOn);
		GLint srcRGB = 0, dstRGB = 0;
		glGetIntegerv(GL_BLEND_SRC_RGB, &srcRGB);
		glGetIntegerv(GL_BLEND_DST_RGB, &dstRGB);
		// Check what u_texture is set to
		GLint uTexUnit = -1;
		glGetUniformiv(g_ShaderProgram, g_UniformTexture, &uTexUnit);
		float minX = v[0].p.x, maxX = v[0].p.x, minY = v[0].p.y, maxY = v[0].p.y;
		for (int i = 1; i < 4; i++) {
			if (v[i].p.x < minX) minX = v[i].p.x;
			if (v[i].p.x > maxX) maxX = v[i].p.x;
			if (v[i].p.y < minY) minY = v[i].p.y;
			if (v[i].p.y > maxY) maxY = v[i].p.y;
		}
		extern std::string g_LastSpriteDebugTex;
		LOG->Info("[ALL frame=%d] tex=%d active=0x%x u_tex=%d on[0..3]=(%d,%d,%d,%d) rect=(%.0f,%.0f)-(%.0f,%.0f) c.rgba=(%d,%d,%d,%d) sprite='%s'",
			g_CurrentFrameNumber, boundTex, activeTex - GL_TEXTURE0, uTexUnit,
			boundOn[0], boundOn[1], boundOn[2], boundOn[3],
			minX, minY, maxX, maxY,
			(int)v[0].c.r, (int)v[0].c.g, (int)v[0].c.b, (int)v[0].c.a,
			g_LastSpriteDebugTex.c_str());
	}
	// Detect "fullscreen quad" candidates so we can spot the BG draws.
	if (iNumVerts == 4) {
		float minX = v[0].p.x, maxX = v[0].p.x, minY = v[0].p.y, maxY = v[0].p.y;
		for (int i = 1; i < 4; i++) {
			if (v[i].p.x < minX) minX = v[i].p.x;
			if (v[i].p.x > maxX) maxX = v[i].p.x;
			if (v[i].p.y < minY) minY = v[i].p.y;
			if (v[i].p.y > maxY) maxY = v[i].p.y;
		}
		float w = maxX - minX, h = maxY - minY;
		// Log unique (texture, color) combos for fullscreen quads
		if (w > 800 && h > 500) {
			GLint boundTex = 0;
			glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundTex);
			static int seenKeys[64] = {0};
			static int seenCount = 0;
			static int lastResetFrame = 0;
			// Reset dedupe every 600 frames (~10s) so we sample over time
			if (g_CurrentFrameNumber - lastResetFrame > 600) {
				seenCount = 0;
				lastResetFrame = g_CurrentFrameNumber;
			}
			int key = boundTex * 256 + (int)v[0].c.r;
			bool seen = false;
			for (int k = 0; k < seenCount; k++) if (seenKeys[k] == key) seen = true;
			if (!seen && seenCount < 64) {
				seenKeys[seenCount++] = key;
				const unsigned char *cb = reinterpret_cast<const unsigned char*>(&v[0].c);
				extern std::string g_LastSpriteDebugName;
				extern std::string g_LastSpriteDebugTex;
				extern RageColor g_LastSpriteDebugDiffuse;
				LOG->Info("[FS-quad #%d frame=%d] tex=%d rect=(%.0f,%.0f)-(%.0f,%.0f) c.rgba=(%d,%d,%d,%d) bytes=[%d,%d,%d,%d] uv=[(%.3f,%.3f),(%.3f,%.3f),(%.3f,%.3f),(%.3f,%.3f)] LAST_SPRITE: name='%s' tex='%s' diff=(%.3f,%.3f,%.3f,%.3f)",
					seenCount, g_CurrentFrameNumber, boundTex, minX, minY, maxX, maxY,
					(int)v[0].c.r, (int)v[0].c.g, (int)v[0].c.b, (int)v[0].c.a,
					(int)cb[0], (int)cb[1], (int)cb[2], (int)cb[3],
					v[0].t.x, v[0].t.y, v[1].t.x, v[1].t.y, v[2].t.x, v[2].t.y, v[3].t.x, v[3].t.y,
					g_LastSpriteDebugName.c_str(),
					g_LastSpriteDebugTex.c_str(),
					g_LastSpriteDebugDiffuse.r, g_LastSpriteDebugDiffuse.g, g_LastSpriteDebugDiffuse.b, g_LastSpriteDebugDiffuse.a);
			}
		}
	}

	glUseProgram(g_ShaderProgram);

	// Verify program is now bound
	if (drawCallCount <= 3) {
		GLint currentProgram = 0;
		glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);
		if (currentProgram != (GLint)g_ShaderProgram) {
			LOG->Warn("  Program not bound after glUseProgram! current=%d expected=%d", currentProgram, g_ShaderProgram);
		}
	}

	// Check if a texture is bound, if not use default white texture
	GLint boundTexture = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundTexture);
	if (drawCallCount <= 3) {
		LOG->Info("  Bound texture ID: %d", boundTexture);
	}
	if (boundTexture == 0 && g_DefaultWhiteTexture != 0) {
		glBindTexture(GL_TEXTURE_2D, g_DefaultWhiteTexture);
		if (drawCallCount <= 3) {
			LOG->Info("  Using default white texture: %d", g_DefaultWhiteTexture);
		}
	}


	// Get the actual projection and modelview matrices from the game's matrix stack.
	// Match the OGL backend: projection = Centering * ProjectionTop, modelview = View * World.
	RageMatrix projection;
	RageMatrixMultiply(&projection, GetCentering(), GetProjectionTop());

	RageMatrix modelView;
	RageMatrixMultiply(&modelView, GetViewTop(), GetWorldTop());

	glUniformMatrix4fv(g_UniformProjection, 1, GL_FALSE, (const float*)&projection);
	glUniformMatrix4fv(g_UniformModelView, 1, GL_FALSE, (const float*)&modelView);
	glUniform1i(g_UniformTexture, 0); // Use texture unit 0

	if (drawCallCount <= 3) {
		const float* mp = (const float*)&projection;
		const float* mv = (const float*)&modelView;
		LOG->Info("  Proj row0: %.3f %.3f %.3f %.3f", mp[0], mp[4], mp[8], mp[12]);
		LOG->Info("  Proj row3: %.3f %.3f %.3f %.3f", mp[3], mp[7], mp[11], mp[15]);
		LOG->Info("  MV row3: %.3f %.3f %.3f %.3f", mv[3], mv[7], mv[11], mv[15]);
	}

	// WebGL requires vertex data to be in VBOs, not client-side arrays
	// Create a temporary VBO for this draw call
	static GLuint tempVBO = 0;
	if (tempVBO == 0) {
		glGenBuffers(1, &tempVBO);
	}

	// Upload vertex data to VBO
	glBindBuffer(GL_ARRAY_BUFFER, tempVBO);
	glBufferData(GL_ARRAY_BUFFER, iNumVerts * sizeof(RageSpriteVertex), v, GL_STREAM_DRAW);

	// Enable vertex attributes with offsets into the VBO
	if (g_AttribPosition >= 0) {
		glEnableVertexAttribArray(g_AttribPosition);
		glVertexAttribPointer(g_AttribPosition, 3, GL_FLOAT, GL_FALSE, sizeof(RageSpriteVertex),
			(void*)offsetof(RageSpriteVertex, p));
	}

	if (g_AttribTexCoord >= 0) {
		glEnableVertexAttribArray(g_AttribTexCoord);
		glVertexAttribPointer(g_AttribTexCoord, 2, GL_FLOAT, GL_FALSE, sizeof(RageSpriteVertex),
			(void*)offsetof(RageSpriteVertex, t));
	}

	if (g_AttribColor >= 0) {
		glEnableVertexAttribArray(g_AttribColor);
		glVertexAttribPointer(g_AttribColor, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(RageSpriteVertex),
			(void*)offsetof(RageSpriteVertex, c));
	}

	// Draw quads as triangles (GLES2 doesn't have GL_QUADS)
	// Each quad is 4 vertices, converted to 6 triangle indices.
	// WebGL requires indices in a bound GL_ELEMENT_ARRAY_BUFFER.
	int iNumQuads = iNumVerts / 4;

	if (g_QuadIndexEBO == 0)
		glGenBuffers(1, &g_QuadIndexEBO);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_QuadIndexEBO);

	if (iNumQuads > g_QuadIndexEBOQuads)
	{
		int newCap = g_QuadIndexEBOQuads > 0 ? g_QuadIndexEBOQuads : 64;
		while (newCap < iNumQuads) newCap *= 2;
		// Cap at 65535/4 vertices (since indices are GLushort)
		if (newCap > 16383) newCap = 16383;
		std::vector<GLushort> idx;
		idx.reserve(newCap * 6);
		for (int q = 0; q < newCap; q++) {
			GLushort base = (GLushort)(q * 4);
			idx.push_back(base + 0);
			idx.push_back(base + 1);
			idx.push_back(base + 2);
			idx.push_back(base + 0);
			idx.push_back(base + 2);
			idx.push_back(base + 3);
		}
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(GLushort),
			idx.data(), GL_STATIC_DRAW);
		g_QuadIndexEBOQuads = newCap;
	}

	if (drawCallCount <= 3) {
#ifdef __EMSCRIPTEN__
		EM_ASM({
			console.log("[DRAW DEBUG] About to call glDrawElements for " + $0 + " quads", $0);
		}, iNumQuads);
#endif
	}

	int quadsToDraw = iNumQuads;
	if (quadsToDraw > g_QuadIndexEBOQuads) quadsToDraw = g_QuadIndexEBOQuads;
	glDrawElements(GL_TRIANGLES, quadsToDraw * 6, GL_UNSIGNED_SHORT, (void*)0);

	// Probe just the first 5 quads on frame 1850 with full GL state.
	if (iNumVerts == 4 && g_CurrentFrameNumber == 1850) {
		static int probeIdx1850 = 0;
		if (probeIdx1850 < 5) {
			probeIdx1850++;
			unsigned char p1[4] = {0,0,0,0};
			GLint boundTex = 0, uTex = -1;
			GLint blendOn = 0, srcRGB = 0, dstRGB = 0, srcA = 0, dstA = 0;
			GLint depthTest = 0, scissor = 0, cullFace = 0, colorMask[4] = {0,0,0,0};
			GLfloat clearC[4] = {0,0,0,0};
			GLint viewport[4] = {0,0,0,0};
			glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundTex);
			glGetUniformiv(g_ShaderProgram, g_UniformTexture, &uTex);
			glGetIntegerv(GL_BLEND, &blendOn);
			glGetIntegerv(GL_BLEND_SRC_RGB, &srcRGB);
			glGetIntegerv(GL_BLEND_DST_RGB, &dstRGB);
			glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcA);
			glGetIntegerv(GL_BLEND_DST_ALPHA, &dstA);
			glGetIntegerv(GL_DEPTH_TEST, &depthTest);
			glGetIntegerv(GL_SCISSOR_TEST, &scissor);
			glGetIntegerv(GL_CULL_FACE, &cullFace);
			glGetIntegerv(GL_COLOR_WRITEMASK, colorMask);
			glGetFloatv(GL_COLOR_CLEAR_VALUE, clearC);
			glGetIntegerv(GL_VIEWPORT, viewport);
			glReadPixels(640, 360, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p1);
			LOG->Info("[STATE frame=%d call=%d probe=%d tex=%d uTex=%d blend=%d func=(0x%x,0x%x,0x%x,0x%x) depth=%d scissor=%d cull=%d cmask=(%d,%d,%d,%d) vp=(%d,%d,%d,%d) clear=(%.2f,%.2f,%.2f,%.2f) center=#%02x%02x%02x%02x",
				g_CurrentFrameNumber, drawCallCount, probeIdx1850,
				boundTex, uTex, blendOn,
				srcRGB, dstRGB, srcA, dstA,
				depthTest, scissor, cullFace,
				colorMask[0], colorMask[1], colorMask[2], colorMask[3],
				viewport[0], viewport[1], viewport[2], viewport[3],
				clearC[0], clearC[1], clearC[2], clearC[3],
				p1[0], p1[1], p1[2], p1[3]);
		}
	}

	if (drawCallCount <= 3) {
		GLenum err = glGetError();
		if (err != GL_NO_ERROR) {
			LOG->Warn("glDrawElements error: 0x%x for %d quads", err, quadsToDraw);
		}
	}

	// Unbind buffers
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	if (drawCallCount <= 3) {
		GLenum finalErr = glGetError();
		if (finalErr != GL_NO_ERROR) {
			LOG->Warn("Final GL error after DrawQuads: 0x%x", finalErr);
		}
	}

	// Disable vertex attributes
	if (g_AttribPosition >= 0)
		glDisableVertexAttribArray(g_AttribPosition);
	if (g_AttribTexCoord >= 0)
		glDisableVertexAttribArray(g_AttribTexCoord);
	if (g_AttribColor >= 0)
		glDisableVertexAttribArray(g_AttribColor);
}

// Streaming VBO shared by the simple draw paths.
static GLuint g_StreamVBO = 0;

static void UploadAndBindAttribs( const RageSpriteVertex v[], int iNumVerts )
{
	if (g_StreamVBO == 0) glGenBuffers(1, &g_StreamVBO);
	glBindBuffer(GL_ARRAY_BUFFER, g_StreamVBO);
	glBufferData(GL_ARRAY_BUFFER, iNumVerts * sizeof(RageSpriteVertex), v, GL_STREAM_DRAW);

	if (g_AttribPosition >= 0) {
		glEnableVertexAttribArray(g_AttribPosition);
		glVertexAttribPointer(g_AttribPosition, 3, GL_FLOAT, GL_FALSE, sizeof(RageSpriteVertex),
			(void*)offsetof(RageSpriteVertex, p));
	}
	if (g_AttribTexCoord >= 0) {
		glEnableVertexAttribArray(g_AttribTexCoord);
		glVertexAttribPointer(g_AttribTexCoord, 2, GL_FLOAT, GL_FALSE, sizeof(RageSpriteVertex),
			(void*)offsetof(RageSpriteVertex, t));
	}
	if (g_AttribColor >= 0) {
		glEnableVertexAttribArray(g_AttribColor);
		glVertexAttribPointer(g_AttribColor, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(RageSpriteVertex),
			(void*)offsetof(RageSpriteVertex, c));
	}
}

static void TeardownDrawState()
{
	if (g_AttribPosition >= 0) glDisableVertexAttribArray(g_AttribPosition);
	if (g_AttribTexCoord >= 0) glDisableVertexAttribArray(g_AttribTexCoord);
	if (g_AttribColor >= 0) glDisableVertexAttribArray(g_AttribColor);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void
RageDisplay_GLES2::DrawQuadStripInternal( const RageSpriteVertex v[], int iNumVerts )
{
	if (iNumVerts < 4 || g_ShaderProgram == 0)
		return;

	glUseProgram(g_ShaderProgram);
	{
		RageMatrix projection, modelView;
		RageMatrixMultiply(&projection, GetCentering(), GetProjectionTop());
		RageMatrixMultiply(&modelView, GetViewTop(), GetWorldTop());
		glUniformMatrix4fv(g_UniformProjection, 1, GL_FALSE, (const float*)&projection);
		glUniformMatrix4fv(g_UniformModelView, 1, GL_FALSE, (const float*)&modelView);
		glUniform1i(g_UniformTexture, 0);
	}

	UploadAndBindAttribs(v, iNumVerts);
	// Quad strip is exactly equivalent to a triangle strip in GLES2/WebGL
	glDrawArrays(GL_TRIANGLE_STRIP, 0, iNumVerts);
	TeardownDrawState();
}

void
RageDisplay_GLES2::DrawFanInternal( const RageSpriteVertex v[], int iNumVerts )
{
	if (iNumVerts < 3 || g_ShaderProgram == 0)
		return;

	glUseProgram(g_ShaderProgram);
	{
		RageMatrix projection, modelView;
		RageMatrixMultiply(&projection, GetCentering(), GetProjectionTop());
		RageMatrixMultiply(&modelView, GetViewTop(), GetWorldTop());
		glUniformMatrix4fv(g_UniformProjection, 1, GL_FALSE, (const float*)&projection);
		glUniformMatrix4fv(g_UniformModelView, 1, GL_FALSE, (const float*)&modelView);
		glUniform1i(g_UniformTexture, 0);
	}

	UploadAndBindAttribs(v, iNumVerts);
	glDrawArrays(GL_TRIANGLE_FAN, 0, iNumVerts);
	TeardownDrawState();
}

void
RageDisplay_GLES2::DrawStripInternal( const RageSpriteVertex v[], int iNumVerts )
{
	if (iNumVerts < 3 || g_ShaderProgram == 0)
		return;

	glUseProgram(g_ShaderProgram);
	{
		RageMatrix projection, modelView;
		RageMatrixMultiply(&projection, GetCentering(), GetProjectionTop());
		RageMatrixMultiply(&modelView, GetViewTop(), GetWorldTop());
		glUniformMatrix4fv(g_UniformProjection, 1, GL_FALSE, (const float*)&projection);
		glUniformMatrix4fv(g_UniformModelView, 1, GL_FALSE, (const float*)&modelView);
		glUniform1i(g_UniformTexture, 0);
	}

	UploadAndBindAttribs(v, iNumVerts);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, iNumVerts);
	TeardownDrawState();
}

void
RageDisplay_GLES2::DrawTrianglesInternal( const RageSpriteVertex v[], int iNumVerts )
{
	if (iNumVerts < 3 || g_ShaderProgram == 0)
		return;

	glUseProgram(g_ShaderProgram);
	{
		RageMatrix projection, modelView;
		RageMatrixMultiply(&projection, GetCentering(), GetProjectionTop());
		RageMatrixMultiply(&modelView, GetViewTop(), GetWorldTop());
		glUniformMatrix4fv(g_UniformProjection, 1, GL_FALSE, (const float*)&projection);
		glUniformMatrix4fv(g_UniformModelView, 1, GL_FALSE, (const float*)&modelView);
		glUniform1i(g_UniformTexture, 0);
	}

	UploadAndBindAttribs(v, iNumVerts);
	glDrawArrays(GL_TRIANGLES, 0, iNumVerts);
	TeardownDrawState();
}

void
RageDisplay_GLES2::DrawCompiledGeometryInternal( const RageCompiledGeometry *p, int
	iMeshIndex )
{
}

void
RageDisplay_GLES2::DrawLineStripInternal( const RageSpriteVertex v[], int iNumVerts, float LineWidth )
{
}

// Is this even used?
void
RageDisplay_GLES2::DrawSymmetricQuadStripInternal( const RageSpriteVertex v[], int iNumVerts )
{
}

bool
RageDisplay_GLES2::SupportsSurfaceFormat( RagePixelFormat pixfmt )
{
	switch (g_GLPixFmtInfo[pixfmt].type)
	{
	case GL_UNSIGNED_SHORT_1_5_5_5_REV:
		return false;
		//return GLEW_EXT_bgra && g_bReversePackedPixelsWorks;
	default:
		return true;
	}
}

/*
 * Copyright (c) 2012 Colby Klein
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
