// 
//---------------------------------------------------------------------------
//
// Copyright(C) 2005-2016 Christoph Oelckers
// All rights reserved.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/
//
//--------------------------------------------------------------------------
//
/*
** gl1_renderer.cpp
** Renderer interface
**
*/

#include "gl/system/gl_system.h"
#include "files.h"
#include "m_swap.h"
#include "v_video.h"
#include "r_data/r_translate.h"
#include "m_png.h"
#include "m_crc32.h"
#include "w_wad.h"
//#include "gl/gl_intern.h"
#include "gl/gl_functions.h"
#include "vectors.h"
#include "doomstat.h"

#include "gl/system/gl_interface.h"
#include "gl/system/gl_framebuffer.h"
#include "gl/system/gl_cvars.h"
#include "gl/system/gl_debug.h"
#include "gl/renderer/gl_renderer.h"
#include "gl/renderer/gl_lightdata.h"
#include "gl/renderer/gl_renderstate.h"
#include "gl/renderer/gl_renderbuffers.h"
#include "gl/data/gl_data.h"
#include "gl/data/gl_vertexbuffer.h"
#include "gl/scene/gl_drawinfo.h"
#include "gl/scene/gl_portal.h"
#include "gl/shaders/gl_shader.h"
#include "gl/shaders/gl_ambientshader.h"
#include "gl/shaders/gl_bloomshader.h"
#include "gl/shaders/gl_blurshader.h"
#include "gl/shaders/gl_tonemapshader.h"
#include "gl/shaders/gl_colormapshader.h"
#include "gl/shaders/gl_lensshader.h"
#include "gl/shaders/gl_fxaashader.h"
#include "gl/shaders/gl_presentshader.h"
#include "gl/shaders/gl_present3dRowshader.h"
#include "gl/shaders/gl_shadowmapshader.h"
#include "gl/shaders/gl_postprocessshader.h"
#include "gl/stereo3d/gl_stereo3d.h"
#include "gl/textures/gl_texture.h"
#include "gl/textures/gl_translate.h"
#include "gl/textures/gl_material.h"
#include "gl/textures/gl_samplers.h"
#include "gl/utility/gl_clock.h"
#include "gl/utility/gl_templates.h"
#include "gl/models/gl_models.h"
#include "gl/dynlights/gl_lightbuffer.h"
#include "r_videoscale.h"

EXTERN_CVAR(Int, screenblocks)

CVAR(Bool, gl_scale_viewport, true, CVAR_ARCHIVE);

//===========================================================================
// 
// Renderer interface
//
//===========================================================================

//-----------------------------------------------------------------------------
//
// Initialize
//
//-----------------------------------------------------------------------------

FGLRenderer::FGLRenderer(OpenGLFrameBuffer *fb) 
{
	framebuffer = fb;
	mClipPortal = nullptr;
	mCurrentPortal = nullptr;
	mMirrorCount = 0;
	mPlaneMirrorCount = 0;
	mLightCount = 0;
	mAngles = FRotator(0.f, 0.f, 0.f);
	mViewVector = FVector2(0,0);
	mVBO = nullptr;
	mSkyVBO = nullptr;
	gl_spriteindex = 0;
	mShaderManager = nullptr;
	mLights = nullptr;
	mTonemapPalette = nullptr;
	mBuffers = nullptr;
	mPresentShader = nullptr;
	mPresent3dCheckerShader = nullptr;
	mPresent3dColumnShader = nullptr;
	mPresent3dRowShader = nullptr;
	mBloomExtractShader = nullptr;
	mBloomCombineShader = nullptr;
	mExposureExtractShader = nullptr;
	mExposureAverageShader = nullptr;
	mExposureCombineShader = nullptr;
	mBlurShader = nullptr;
	mTonemapShader = nullptr;
	mTonemapPalette = nullptr;
	mColormapShader = nullptr;
	mLensShader = nullptr;
	mLinearDepthShader = nullptr;
	mDepthBlurShader = nullptr;
	mSSAOShader = nullptr;
	mSSAOCombineShader = nullptr;
	mFXAAShader = nullptr;
	mFXAALumaShader = nullptr;
	mShadowMapShader = nullptr;
	mCustomPostProcessShaders = nullptr;
}

void gl_LoadModels();
void gl_FlushModels();

void FGLRenderer::Initialize(int width, int height)
{
	mBuffers = new FGLRenderBuffers();
	mLinearDepthShader = new FLinearDepthShader();
	mDepthBlurShader = new FDepthBlurShader();
	mSSAOShader = new FSSAOShader();
	mSSAOCombineShader = new FSSAOCombineShader();
	mBloomExtractShader = new FBloomExtractShader();
	mBloomCombineShader = new FBloomCombineShader();
	mExposureExtractShader = new FExposureExtractShader();
	mExposureAverageShader = new FExposureAverageShader();
	mExposureCombineShader = new FExposureCombineShader();
	mBlurShader = new FBlurShader();
	mTonemapShader = new FTonemapShader();
	mColormapShader = new FColormapShader();
	mTonemapPalette = nullptr;
	mLensShader = new FLensShader();
	mFXAAShader = new FFXAAShader;
	mFXAALumaShader = new FFXAALumaShader;
	mPresentShader = new FPresentShader();
	mPresent3dCheckerShader = new FPresent3DCheckerShader();
	mPresent3dColumnShader = new FPresent3DColumnShader();
	mPresent3dRowShader = new FPresent3DRowShader();
	mShadowMapShader = new FShadowMapShader();
	mCustomPostProcessShaders = new FCustomPostProcessShaders();

	GetSpecialTextures();

	// needed for the core profile, because someone decided it was a good idea to remove the default VAO.
	if (!gl.legacyMode)
	{
		glGenVertexArrays(1, &mVAOID);
		glBindVertexArray(mVAOID);
		FGLDebug::LabelObject(GL_VERTEX_ARRAY, mVAOID, "FGLRenderer.mVAOID");
	}
	else mVAOID = 0;

	mVBO = new FFlatVertexBuffer(width, height);
	mSkyVBO = new FSkyVertexBuffer;
	if (!gl.legacyMode) mLights = new FLightBuffer();
	else mLights = NULL;
	gl_RenderState.SetVertexBuffer(mVBO);
	mFBID = 0;
	mOldFBID = 0;

	SetupLevel();
	mShaderManager = new FShaderManager;
	mSamplerManager = new FSamplerManager;
	gl_LoadModels();

	GLPortal::Initialize();
}

FGLRenderer::~FGLRenderer() 
{
	GLPortal::Shutdown();

	gl_FlushModels();
	AActor::DeleteAllAttachedLights();
	FMaterial::FlushAll();
	if (mShaderManager != NULL) delete mShaderManager;
	if (mSamplerManager != NULL) delete mSamplerManager;
	if (mVBO != NULL) delete mVBO;
	if (mSkyVBO != NULL) delete mSkyVBO;
	if (mLights != NULL) delete mLights;
	if (mFBID != 0) glDeleteFramebuffers(1, &mFBID);
	if (mVAOID != 0)
	{
		glBindVertexArray(0);
		glDeleteVertexArrays(1, &mVAOID);
	}
	if (mBuffers) delete mBuffers;
	if (mPresentShader) delete mPresentShader;
	if (mLinearDepthShader) delete mLinearDepthShader;
	if (mDepthBlurShader) delete mDepthBlurShader;
	if (mSSAOShader) delete mSSAOShader;
	if (mSSAOCombineShader) delete mSSAOCombineShader;
	if (mPresent3dCheckerShader) delete mPresent3dCheckerShader;
	if (mPresent3dColumnShader) delete mPresent3dColumnShader;
	if (mPresent3dRowShader) delete mPresent3dRowShader;
	if (mBloomExtractShader) delete mBloomExtractShader;
	if (mBloomCombineShader) delete mBloomCombineShader;
	if (mExposureExtractShader) delete mExposureExtractShader;
	if (mExposureAverageShader) delete mExposureAverageShader;
	if (mExposureCombineShader) delete mExposureCombineShader;
	if (mBlurShader) delete mBlurShader;
	if (mTonemapShader) delete mTonemapShader;
	if (mTonemapPalette) delete mTonemapPalette;
	if (mColormapShader) delete mColormapShader;
	if (mLensShader) delete mLensShader;
	if (mShadowMapShader) delete mShadowMapShader;
	delete mCustomPostProcessShaders;
	delete mFXAAShader;
	delete mFXAALumaShader;
}


void FGLRenderer::GetSpecialTextures()
{
	if (gl.legacyMode) glLight = TexMan.CheckForTexture("glstuff/gllight.png", ETextureType::MiscPatch);
	glPart2 = TexMan.CheckForTexture("glstuff/glpart2.png", ETextureType::MiscPatch);
	glPart = TexMan.CheckForTexture("glstuff/glpart.png", ETextureType::MiscPatch);
	mirrorTexture = TexMan.CheckForTexture("glstuff/mirror.png", ETextureType::MiscPatch);

}

//==========================================================================
//
// Calculates the viewport values needed for 2D and 3D operations
//
//==========================================================================

void FGLRenderer::SetOutputViewport(GL_IRECT *bounds)
{
	if (bounds)
	{
		mSceneViewport = *bounds;
		mScreenViewport = *bounds;
		mOutputLetterbox = *bounds;
		return;
	}

	// Special handling so the view with a visible status bar displays properly
	int height, width;
	if (screenblocks >= 10)
	{
		height = framebuffer->GetHeight();
		width = framebuffer->GetWidth();
	}
	else
	{
		height = (screenblocks*framebuffer->GetHeight() / 10) & ~7;
		width = (screenblocks*framebuffer->GetWidth() / 10);
	}

	// Back buffer letterbox for the final output
	int clientWidth = framebuffer->GetClientWidth();
	int clientHeight = framebuffer->GetClientHeight();
	if (clientWidth == 0 || clientHeight == 0)
	{
		// When window is minimized there may not be any client area.
		// Pretend to the rest of the render code that we just have a very small window.
		clientWidth = 160;
		clientHeight = 120;
	}
	int screenWidth = framebuffer->GetWidth();
	int screenHeight = framebuffer->GetHeight();
	float scaleX, scaleY;
	if (ViewportIsScaled43())
	{
		scaleX = MIN(clientWidth / (float)screenWidth, clientHeight / (screenHeight * 1.2f));
		scaleY = scaleX * 1.2f;
	}
	else
	{
		scaleX = MIN(clientWidth / (float)screenWidth, clientHeight / (float)screenHeight);
		scaleY = scaleX;
	}
	mOutputLetterbox.width = (int)round(screenWidth * scaleX);
	mOutputLetterbox.height = (int)round(screenHeight * scaleY);
	mOutputLetterbox.left = (clientWidth - mOutputLetterbox.width) / 2;
	mOutputLetterbox.top = (clientHeight - mOutputLetterbox.height) / 2;

	// The entire renderable area, including the 2D HUD
	mScreenViewport.left = 0;
	mScreenViewport.top = 0;
	mScreenViewport.width = screenWidth;
	mScreenViewport.height = screenHeight;

	// Viewport for the 3D scene
	mSceneViewport.left = viewwindowx;
	mSceneViewport.top = screenHeight - (height + viewwindowy - ((height - viewheight) / 2));
	mSceneViewport.width = viewwidth;
	mSceneViewport.height = height;

	// Scale viewports to fit letterbox
	bool notScaled = ((mScreenViewport.width == ViewportScaledWidth(mScreenViewport.width, mScreenViewport.height)) &&
		(mScreenViewport.width == ViewportScaledHeight(mScreenViewport.width, mScreenViewport.height)) &&
		!ViewportIsScaled43());
	if ((gl_scale_viewport && !framebuffer->IsFullscreen() && notScaled) || !FGLRenderBuffers::IsEnabled())
	{
		mScreenViewport.width = mOutputLetterbox.width;
		mScreenViewport.height = mOutputLetterbox.height;
		mSceneViewport.left = (int)round(mSceneViewport.left * scaleX);
		mSceneViewport.top = (int)round(mSceneViewport.top * scaleY);
		mSceneViewport.width = (int)round(mSceneViewport.width * scaleX);
		mSceneViewport.height = (int)round(mSceneViewport.height * scaleY);

		// Without render buffers we have to render directly to the letterbox
		if (!FGLRenderBuffers::IsEnabled())
		{
			mScreenViewport.left += mOutputLetterbox.left;
			mScreenViewport.top += mOutputLetterbox.top;
			mSceneViewport.left += mOutputLetterbox.left;
			mSceneViewport.top += mOutputLetterbox.top;
		}
	}

	s3d::Stereo3DMode::getCurrentMode().AdjustViewports();
}

//===========================================================================
// 
// Calculates the OpenGL window coordinates for a zdoom screen position
//
//===========================================================================

int FGLRenderer::ScreenToWindowX(int x)
{
	return mScreenViewport.left + (int)round(x * mScreenViewport.width / (float)framebuffer->GetWidth());
}

int FGLRenderer::ScreenToWindowY(int y)
{
	return mScreenViewport.top + mScreenViewport.height - (int)round(y * mScreenViewport.height / (float)framebuffer->GetHeight());
}

//===========================================================================
// 
//
//
//===========================================================================

void FGLRenderer::SetupLevel()
{
	mVBO->CreateVBO();
}

void FGLRenderer::Begin2D()
{
	if (mBuffers->Setup(mScreenViewport.width, mScreenViewport.height, mSceneViewport.width, mSceneViewport.height))
	{
		if (mDrawingScene2D)
			mBuffers->BindSceneFB(false);
		else
			mBuffers->BindCurrentFB();
	}
	glViewport(mScreenViewport.left, mScreenViewport.top, mScreenViewport.width, mScreenViewport.height);
	glScissor(mScreenViewport.left, mScreenViewport.top, mScreenViewport.width, mScreenViewport.height);

	gl_RenderState.EnableFog(false);
}

//===========================================================================
// 
//
//
//===========================================================================

void FGLRenderer::FlushTextures()
{
	FMaterial::FlushAll();
}

//===========================================================================
// 
//
//
//===========================================================================

bool FGLRenderer::StartOffscreen()
{
	bool firstBind = (mFBID == 0);
	if (mFBID == 0)
		glGenFramebuffers(1, &mFBID);
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &mOldFBID);
	glBindFramebuffer(GL_FRAMEBUFFER, mFBID);
	if (firstBind)
		FGLDebug::LabelObject(GL_FRAMEBUFFER, mFBID, "OffscreenFB");
	return true;
}

//===========================================================================
// 
//
//
//===========================================================================

void FGLRenderer::EndOffscreen()
{
	glBindFramebuffer(GL_FRAMEBUFFER, mOldFBID); 
}

//===========================================================================
// 
//
//
//===========================================================================

unsigned char *FGLRenderer::GetTextureBuffer(FTexture *tex, int &w, int &h)
{
	FMaterial * gltex = FMaterial::ValidateTexture(tex, false);
	if (gltex)
	{
		return gltex->CreateTexBuffer(0, w, h);
	}
	return NULL;
}

//===========================================================================
// 
// Vertex buffer for 2D drawer
//
//===========================================================================
#define TDiO ((F2DDrawer::TwoDVertex*)NULL)

class F2DVertexBuffer : public FSimpleVertexBuffer
{
	uint32_t ibo_id;

	// Make sure we can build upon FSimpleVertexBuffer.
	static_assert(&VSiO->x == &TDiO->x, "x not aligned");
	static_assert(&VSiO->u == &TDiO->u, "y not aligned");
	static_assert(&VSiO->color == &TDiO->color0, "color not aligned");

public:

	F2DVertexBuffer()
	{
		glGenBuffers(1, &ibo_id);
	}
	~F2DVertexBuffer()
	{
		if (ibo_id != 0)
		{
			glDeleteBuffers(1, &ibo_id);
		}
	}
	void UploadData(F2DDrawer::TwoDVertex *vertices, int vertcount, int *indices, int indexcount)
	{
		glBindBuffer(GL_ARRAY_BUFFER, vbo_id);
		glBufferData(GL_ARRAY_BUFFER, vertcount * sizeof(vertices[0]), vertices, GL_STREAM_DRAW);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_id);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, indexcount * sizeof(indices[0]), indices, GL_STREAM_DRAW);
	}

	void BindVBO() override
	{
		FSimpleVertexBuffer::BindVBO();
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_id);
	}
};

//===========================================================================
// 
// Draws the 2D stuff. This is the version for OpenGL 3 and later.
//
//===========================================================================

void LegacyColorOverlay(F2DDrawer *drawer, F2DDrawer::RenderCommand & cmd);
int LegacyDesaturation(F2DDrawer::RenderCommand &cmd);

void FGLRenderer::Draw2D(F2DDrawer *drawer)
{


	auto &vertices = drawer->mVertices;
	auto &indices = drawer->mIndices;
	auto &commands = drawer->mData;

	if (commands.Size() == 0) return;

	for (auto &v : vertices)
	{
		// Change from BGRA to RGBA
		std::swap(v.color0.r, v.color0.b);
	}
	auto vb = new F2DVertexBuffer;
	vb->UploadData(&vertices[0], vertices.Size(), &indices[0], indices.Size());
	gl_RenderState.SetVertexBuffer(vb);
	gl_RenderState.SetFixedColormap(CM_DEFAULT);

	for(auto &cmd : commands)
	{

		int gltrans = -1;
		int tm, sb, db, be;
		// The texture mode being returned here cannot be used, because the higher level code 
		// already manipulated the data so that some cases will not be handled correctly.
		// Since we already get a proper mode from the calling code this doesn't really matter.
		gl_GetRenderStyle(cmd.mRenderStyle, false, false, &tm, &sb, &db, &be);
		gl_RenderState.BlendEquation(be); 
		gl_RenderState.BlendFunc(sb, db);

		// Rather than adding remapping code, let's enforce that the constants here are equal.
		static_assert(F2DDrawer::DTM_Normal == TM_MODULATE, "DTM_Normal != TM_MODULATE");
		static_assert(F2DDrawer::DTM_Opaque == TM_OPAQUE, "DTM_Opaque != TM_OPAQUE");
		static_assert(F2DDrawer::DTM_Invert == TM_INVERSE, "DTM_Invert != TM_INVERSE");
		static_assert(F2DDrawer::DTM_InvertOpaque == TM_INVERTOPAQUE, "DTM_InvertOpaque != TM_INVERTOPAQUE");
		static_assert(F2DDrawer::DTM_Stencil == TM_MASK, "DTM_Stencil != TM_MASK");
		static_assert(F2DDrawer::DTM_AlphaTexture == TM_REDTOALPHA, "DTM_AlphaTexture != TM_REDTOALPHA");
		gl_RenderState.SetTextureMode(cmd.mDrawMode);
		if (cmd.mFlags & F2DDrawer::DTF_Scissor)
		{
			glEnable(GL_SCISSOR_TEST);
			// scissor test doesn't use the current viewport for the coordinates, so use real screen coordinates
			// Note that the origin here is the lower left corner!
			auto sciX = ScreenToWindowX(cmd.mScissor[0]);
			auto sciY = ScreenToWindowY(cmd.mScissor[3]);
			auto sciW = ScreenToWindowX(cmd.mScissor[2]) - sciX;
			auto sciH = ScreenToWindowY(cmd.mScissor[1]) - sciY;
			glScissor(sciX, sciY, sciW, sciH);
		}
		else glDisable(GL_SCISSOR_TEST);

		if (cmd.mSpecialColormap != nullptr)
		{
			auto index = cmd.mSpecialColormap - &SpecialColormaps[0];
			if (index < 0 || (unsigned)index >= SpecialColormaps.Size()) index = 0;	// if it isn't in the table FBitmap cannot use it. Shouldn't happen anyway.
			if (!gl.legacyMode)
			{ 
				gl_RenderState.SetFixedColormap(CM_FIRSTSPECIALCOLORMAPFORCED + int(index));
			}
			else
			{
				// map the special colormap to a translation for the legacy renderer.
				// This only gets used on the software renderer's weapon sprite.
				gltrans = STRange_Specialcolormap + index;
			}
		}
		else
		{
			if (!gl.legacyMode)
			{
				gl_RenderState.Set2DOverlayColor(cmd.mColor1);
				gl_RenderState.SetFixedColormap(CM_PLAIN2D);
			}
			else if (cmd.mDesaturate > 0)
			{
				gltrans = LegacyDesaturation(cmd);
			}
		}

		gl_RenderState.SetColor(1, 1, 1, 1, cmd.mDesaturate); 

		gl_RenderState.AlphaFunc(GL_GEQUAL, 0.f);

		if (cmd.mTexture != nullptr)
		{
			auto mat = FMaterial::ValidateTexture(cmd.mTexture, false);
			if (mat == nullptr) continue;

			if (gltrans == -1) gltrans = GLTranslationPalette::GetInternalTranslation(cmd.mTranslation);
			gl_RenderState.SetMaterial(mat, cmd.mFlags & F2DDrawer::DTF_Wrap ? CLAMP_NONE : CLAMP_XY_NOMIP, -gltrans, -1, cmd.mDrawMode == F2DDrawer::DTM_AlphaTexture);
			gl_RenderState.EnableTexture(true);

			// Canvas textures are stored upside down
			if (cmd.mTexture->bHasCanvas)
			{
				gl_RenderState.mTextureMatrix.loadIdentity();
				gl_RenderState.mTextureMatrix.scale(1.f, -1.f, 1.f);
				gl_RenderState.mTextureMatrix.translate(0.f, 1.f, 0.0f);
				gl_RenderState.EnableTextureMatrix(true);
			}
		}
		else
		{
			gl_RenderState.EnableTexture(false);
		}
		gl_RenderState.Apply();

		switch (cmd.mType)
		{
		case F2DDrawer::DrawTypeTriangles:
			glDrawElements(GL_TRIANGLES, cmd.mIndexCount, GL_UNSIGNED_INT, (const void *)(cmd.mIndexIndex * sizeof(unsigned int)));
			if (gl.legacyMode && cmd.mColor1 != 0)
			{
				// Draw the overlay as a separate operation.
				LegacyColorOverlay(drawer, cmd);
			}
			break;

		case F2DDrawer::DrawTypeLines:
			glDrawArrays(GL_LINES, cmd.mVertIndex, cmd.mVertCount);
			break;

		case F2DDrawer::DrawTypePoints:
			glDrawArrays(GL_POINTS, cmd.mVertIndex, cmd.mVertCount);
			break;

		}
		gl_RenderState.EnableTextureMatrix(false);
	}
	glDisable(GL_SCISSOR_TEST);

	gl_RenderState.SetVertexBuffer(GLRenderer->mVBO);
	gl_RenderState.EnableTexture(true);
	gl_RenderState.SetTextureMode(TM_MODULATE);
	gl_RenderState.SetFixedColormap(CM_DEFAULT);
	gl_RenderState.ResetColor();
	gl_RenderState.Apply();
	delete vb;
}
