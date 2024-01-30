/*******************************************************************************************
*
*   raylib [core] example - 3d camera first person
*
*   Example originally created with raylib 1.3, last time updated with raylib 1.3
*
*   Example licensed under an unmodified zlib/libpng license, which is an OSI-certified,
*   BSD-like license that allows static linking with closed source software
*
*   Copyright (c) 2015-2023 Ramon Santamaria (@raysan5)
*
********************************************************************************************/

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>

#include <GLES3/gl3.h>
#include <GLES3/gl32.h>
#include <EGL/egl.h>
#include <android_native_app_glue.h>
#include <jni.h>
#include <android/native_activity.h>
#include <android_native_app_glue.h>
#include <android/log.h>

#define GLES_VER_TARG "100"
#define TARGET_OFFSCREEN

struct android_app *gapp;
EGLDisplay egl_display;
EGLContext egl_context;
EGLConfig  egl_config;

#define TSOPENXR_IMPLEMENTATION
#include "tsopenxr.h"

#include "raylib/raylib.h"
#include "raylib/rcamera.h"
#include "raylib/raymath.h"
#include "raylib/rlgl.h"
#include "objects.h"
#include "player.h"
#include "net/net_client.h"

// #define MAX_COLUMNS 10

LocalBean bean = { 0 };

#define MAX_INPUT_CHARS 17

typedef enum GameScreen { TITLE, GAMEPLAY } GameScreen;

tsoContext TSO;

bool fbo_set = false;
unsigned int fbo = 0;
unsigned int active_fbo = 0;

XrFrameState fs;
XrCompositionLayerProjection layer;
int layerCount;

XrSpace view_space;

XrCompositionLayerProjectionView *projectionLayerViews;

typedef struct
{
	XrSwapchain handle;
	int32_t width;
	int32_t height;
} depthSwapchainInfo;

depthSwapchainInfo * depthSwapchains;
XrSwapchainImageOpenGLKHR ** depthSwapchainImages;
int numDepthSwapchainsPerFrame;
uint32_t * depthSwapchainLengths;
XrCompositionLayerDepthInfoKHR * depthView;

int depthReleaseSwapchain( tsoContext * ctx, int swapchainNumber )
{
	const depthSwapchainInfo * viewSwapchain = depthSwapchains + swapchainNumber;

	XrResult result;
	XrSwapchainImageReleaseInfo ri = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
	result = xrReleaseSwapchainImage( viewSwapchain->handle, &ri );
	if (tsoCheck(ctx, result, "xrReleaseSwapchainImage"))
	{
		return result;
	}
	return 0;
}

int depthAcquireSwapchain( tsoContext * ctx, int swapchainNumber, uint32_t * swapchainImageIndex )
{
	const depthSwapchainInfo * viewSwapchain = depthSwapchains + swapchainNumber;

	XrSwapchainImageAcquireInfo ai = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
	XrResult result = xrAcquireSwapchainImage(viewSwapchain->handle, &ai, swapchainImageIndex);
	if (tsoCheck(ctx, result, "xrAquireSwapchainImage"))
	{
		return result;
	}

	XrSwapchainImageWaitInfo wi = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
	wi.timeout = XR_INFINITE_DURATION;
	result = xrWaitSwapchainImage(viewSwapchain->handle, &wi);
	if (tsoCheck(ctx, result, "xrWaitSwapchainImage"))
	{
		return result;
	}
	
	return 0;
}

int depthCreateSwapchains(tsoContext * ctx) {
    XrSession tsoSession = ctx->tsoSession;
	XrViewConfigurationView * tsoViewConfigs = ctx->tsoViewConfigs;
	int tsoNumViewConfigs = ctx->tsoNumViewConfigs;
	depthSwapchainInfo ** tsoSwapchains = &depthSwapchains; // Will allocate to tsoNumViewConfigs
	XrSwapchainImageOpenGLKHR *** tsoSwapchainImages = &depthSwapchainImages;
	uint32_t ** tsoSwapchainLengths = &depthSwapchainLengths; // Actually array of pointers to pointers
					  
	int i;
	XrResult result;
	uint32_t swapchainFormatCount;
	result = xrEnumerateSwapchainFormats(tsoSession, 0, &swapchainFormatCount, NULL);
	if (tsoCheck(ctx, result, "xrEnumerateSwapchainFormats"))
	{
		return result;
	}

	int64_t * swapchainFormats = alloca(swapchainFormatCount * sizeof( int64_t ));
	result = xrEnumerateSwapchainFormats(tsoSession, swapchainFormatCount, &swapchainFormatCount, swapchainFormats);
	if (tsoCheck(ctx, result, "xrEnumerateSwapchainFormats"))
	{
		return result;
	}
	
	int selfmt = 0;
	int isdefault = 1;

	for( i = 0; i < swapchainFormatCount; i++ )
	{
		// Prefer SRGB8_ALPHA8
		if( swapchainFormats[i] == GL_DEPTH_COMPONENT16 )
		{
			isdefault = 0;
			selfmt = i;
		}
	}
	
#if TSOPENXR_ENABLE_DEBUG
	if( ctx->tsoPrintAll )
	{
		TSOPENXR_INFO( "Formats:" );
		for( i = 0; i < swapchainFormatCount; i++ )
		{
			TSOPENXR_INFO( "%c%04x%c", ( i == selfmt )?'*':' ', (int)swapchainFormats[i], (i == swapchainFormatCount-1)?'\n':',' );
		} 
	}
#endif

	if( *tsoSwapchains )
	{
		tsoDestroySwapchains( ctx );
	}

	// For now we just pick the default one.
	int64_t swapchainFormatToUse = swapchainFormats[selfmt];

	int numSwapchainsPerFrame = ctx->numSwapchainsPerFrame = (ctx->flags & TSO_DOUBLEWIDE)?1:tsoNumViewConfigs;

	uint32_t swapchain_width = 0;
	for (uint32_t i = 0; i < tsoNumViewConfigs; i++) {
		swapchain_width += ctx->tsoViewConfigs[i].recommendedImageRectWidth;
	}

	*tsoSwapchains = realloc( *tsoSwapchains, numSwapchainsPerFrame * sizeof( depthSwapchainInfo ) );
	*tsoSwapchainLengths = realloc( *tsoSwapchainLengths, numSwapchainsPerFrame * sizeof( uint32_t ) );
	for (uint32_t i = 0; i < numSwapchainsPerFrame; i++)
	{
		XrSwapchainCreateInfo sci = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
		sci.createFlags = 0;
		sci.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
		sci.format = swapchainFormatToUse;
		sci.sampleCount = tsoViewConfigs[i].recommendedSwapchainSampleCount;
		sci.width = swapchain_width;
		sci.height = tsoViewConfigs[i].recommendedImageRectHeight;
		sci.faceCount = 1;
		sci.arraySize = 1;
		sci.mipCount = 1;

		XrSwapchain swapchainHandle;
		result = xrCreateSwapchain(tsoSession, &sci, &swapchainHandle);
		if (tsoCheck(ctx, result, "xrCreateSwapchain"))
		{
			return result;
		}

		(*tsoSwapchains)[i].handle = swapchainHandle;
		(*tsoSwapchains)[i].width = sci.width;
		(*tsoSwapchains)[i].height = sci.height;

		result = xrEnumerateSwapchainImages((*tsoSwapchains)[i].handle, 0, &(*tsoSwapchainLengths)[i], NULL);
		if (tsoCheck(ctx, result, "xrEnumerateSwapchainImages [view]"))
		{
			return result;
		}
	}

	*tsoSwapchainImages = realloc( *tsoSwapchainImages, numSwapchainsPerFrame * sizeof( XrSwapchainImageOpenGLKHR * ) ); 
	for (uint32_t i = 0; i < numSwapchainsPerFrame; i++)
	{
		(*tsoSwapchainImages)[i] = malloc( (*tsoSwapchainLengths)[i] * sizeof(XrSwapchainImageOpenGLKHR) );
		for (uint32_t j = 0; j < (*tsoSwapchainLengths)[i]; j++)
		{
#ifdef ANDROID
			(*tsoSwapchainImages)[i][j].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
#else
			(*tsoSwapchainImages)[i][j].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
#endif
			(*tsoSwapchainImages)[i][j].next = NULL;
		}

		result = xrEnumerateSwapchainImages((*tsoSwapchains)[i].handle, (*tsoSwapchainLengths)[i], &(*tsoSwapchainLengths)[i],
											(XrSwapchainImageBaseHeader*)((*tsoSwapchainImages)[i]));

		if (tsoCheck(ctx, result, "xrEnumerateSwapchainImages [final]"))
		{
			return result;
		}
	}
	
	return 0;
}

static Matrix xr_projection_matrix(const XrFovf fov)
{
    /*
	if(RL_CULL_DISTANCE_FAR > RL_CULL_DISTANCE_NEAR) {
        __android_log_print(ANDROID_LOG_ERROR, "beangamevr", "Infinite far plane distances are not supported!");
        return { 0 };
    }
    */

	Matrix matrix = { 0 };

	const float near = (float)RL_CULL_DISTANCE_NEAR;
	const float far = (float)RL_CULL_DISTANCE_FAR;

	const float tanAngleLeft = tanf(fov.angleLeft);
	const float tanAngleRight = tanf(fov.angleRight);

	const float tanAngleDown = tanf(fov.angleDown);
	const float tanAngleUp = tanf(fov.angleUp);

	const float tanAngleWidth = tanAngleRight - tanAngleLeft;
	const float tanAngleHeight = tanAngleUp - tanAngleDown;

	matrix.m0 = 2 / tanAngleWidth;
	matrix.m4 = 0;
	matrix.m8 = (tanAngleRight + tanAngleLeft) / tanAngleWidth;
	matrix.m12 = 0;

	matrix.m1 = 0;
	matrix.m5 = 2 / tanAngleHeight;
	matrix.m9 = (tanAngleUp + tanAngleDown) / tanAngleHeight;
	matrix.m13 = 0;

	matrix.m2 = 0;
	matrix.m6 = 0;
	matrix.m10 = -(far + near) / (far - near);
	matrix.m14 = -(far * (near + near)) / (far - near);

	matrix.m3 = 0;
	matrix.m7 = 0;
	matrix.m11 = -1;
	matrix.m15 = 0;

	return matrix;
}

static Matrix xr_matrix(const XrPosef pose)
{
	Matrix translation = MatrixTranslate(pose.position.x, pose.position.y, pose.position.z);
	Matrix rotation = QuaternionToMatrix((Quaternion){pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w});
	return MatrixMultiply(rotation, translation);
}

int BeginDrawingXR(tsoContext * ctx)
{
    __android_log_print(ANDROID_LOG_INFO, "beangamevr", "Begin BeginDrawingXR");
    if(!fbo_set) {
        fbo = rlLoadFramebuffer(0, 0);
    }

    XrSession tsoSession = ctx->tsoSession;
	int tsoNumViewConfigs = ctx->tsoNumViewConfigs;
	XrSpace tsoStageSpace = ctx->tsoStageSpace;

	fs.type = XR_TYPE_FRAME_STATE;
	fs.next = NULL;

	XrFrameWaitInfo fwi;
	fwi.type = XR_TYPE_FRAME_WAIT_INFO;
	fwi.next = NULL;

	XrResult result = xrWaitFrame(tsoSession, &fwi, &fs);
	if (tsoCheck(ctx, result, "xrWaitFrame"))
	{
		return result;
	}

	XrFrameBeginInfo fbi;
	fbi.type = XR_TYPE_FRAME_BEGIN_INFO;
	fbi.next = NULL;
	result = xrBeginFrame(tsoSession, &fbi);
	if (tsoCheck(ctx, result, "xrBeginFrame"))
	{
		return result;
	}

	// Potentially resize.
	tsoEnumeratetsoViewConfigs( ctx );
	
	// Originally written this way to allow for  || ctx->tsoViewConfigs[0].recommendedImageRectWidth != ctx->tsoSwapchains[0].width  ... But this doesn't work in any current runtimes.
	if( !ctx->tsoNumViewConfigs || !ctx->tsoSwapchains )
	{
		if ( ( result = tsoCreateSwapchains( ctx ) ) ) return result;
    }

    if( !ctx->tsoNumViewConfigs || !depthSwapchains )
	{
		if ( ( result = depthCreateSwapchains( ctx ) ) ) return result;
    }

    layerCount = 0;

	XrView * views = alloca( sizeof( XrView) * tsoNumViewConfigs );
	for (size_t i = 0; i < tsoNumViewConfigs; i++)
	{
		views[i].type = XR_TYPE_VIEW;
		views[i].next = NULL;
	}
	
	uint32_t viewCountOutput;
	XrViewState viewState = { XR_TYPE_VIEW_STATE };

	XrViewLocateInfo vli;
	vli.type = XR_TYPE_VIEW_LOCATE_INFO;
	vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	ctx->tsoPredictedDisplayTime = vli.displayTime = fs.predictedDisplayTime;
	vli.space = tsoStageSpace;
	result = xrLocateViews( tsoSession, &vli, &viewState, tsoNumViewConfigs, &viewCountOutput, views );
	if (tsoCheck(ctx, result, "xrLocateViews"))
	{
		return result;
	}

    XrSpaceLocation view_location;
    view_location.type = XR_TYPE_SPACE_LOCATION;
    view_location.next = NULL;
    result = xrLocateSpace(ctx->tsoViewSpace, ctx->tsoStageSpace, ctx->tsoPredictedDisplayTime, &view_location);
    if (tsoCheck(ctx, result, "xrLocateSpace"))
	{
		return result;
	}

	projectionLayerViews = malloc( viewCountOutput * sizeof( XrCompositionLayerProjectionView ) );
	memset( projectionLayerViews, 0, sizeof( XrCompositionLayerProjectionView ) * viewCountOutput );

	int i;
	int viewCounts = (ctx->flags & TSO_DOUBLEWIDE)?1:viewCountOutput;
	for( i = 0; i < viewCountOutput; i++ )
	{
		// Each view has a separate swapchain which is acquired, rendered to, and released.
		XrCompositionLayerProjectionView * layerView = projectionLayerViews + i;
		layerView->type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
		layerView->pose = views[i].pose;
		layerView->fov = views[i].fov;
		
		if( ctx->flags & TSO_DOUBLEWIDE )
		{
			layerView->subImage.swapchain = ctx->tsoSwapchains->handle;
			layerView->subImage.imageRect.offset.x = i * ctx->tsoViewConfigs[i].recommendedImageRectWidth;
			layerView->subImage.imageRect.offset.y = 0;
			layerView->subImage.imageRect.extent.width = ctx->tsoViewConfigs[i].recommendedImageRectWidth;
			layerView->subImage.imageRect.extent.height = ctx->tsoViewConfigs[i].recommendedImageRectHeight;
			layerView->subImage.imageArrayIndex = 0;
		}
		else
		{
			layerView->subImage.swapchain = ctx->tsoSwapchains->handle;
			layerView->subImage.imageRect.offset.x = 0;
			layerView->subImage.imageRect.offset.y = 0;
			layerView->subImage.imageRect.extent.width = ctx->tsoViewConfigs[i].recommendedImageRectWidth;
			layerView->subImage.imageRect.extent.height = ctx->tsoViewConfigs[i].recommendedImageRectHeight;
			layerView->subImage.imageArrayIndex = 0;
		}
	}

    depthView = malloc( viewCountOutput * sizeof( XrCompositionLayerDepthInfoKHR ) );
    memset( depthView, 0, sizeof( XrCompositionLayerDepthInfoKHR ) * viewCountOutput );
    for( i = 0; i < viewCountOutput; i++ )
	{
		depthView[i].type = XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR;
        depthView[i].minDepth = 0.f;
        depthView[i].maxDepth = 1.f;
        depthView[i].nearZ = (float)RL_CULL_DISTANCE_NEAR;
        depthView[i].farZ = (float)RL_CULL_DISTANCE_FAR;
		
		if( ctx->flags & TSO_DOUBLEWIDE )
		{
			depthView[i].subImage.swapchain = depthSwapchains->handle;
			depthView[i].subImage.imageRect.offset.x = i * ctx->tsoViewConfigs[i].recommendedImageRectWidth;
			depthView[i].subImage.imageRect.offset.y = 0;
			depthView[i].subImage.imageRect.extent.width = ctx->tsoViewConfigs[i].recommendedImageRectWidth;
			depthView[i].subImage.imageRect.extent.height = ctx->tsoViewConfigs[i].recommendedImageRectHeight;
			depthView[i].subImage.imageArrayIndex = 0;
		}
		else
		{
			depthView[i].subImage.swapchain = depthSwapchains->handle;
			depthView[i].subImage.imageRect.offset.x = 0;
			depthView[i].subImage.imageRect.offset.y = 0;
			depthView[i].subImage.imageRect.extent.width = ctx->tsoViewConfigs[i].recommendedImageRectWidth;
			depthView[i].subImage.imageRect.extent.height = ctx->tsoViewConfigs[i].recommendedImageRectHeight;
			depthView[i].subImage.imageArrayIndex = 0;
		}

        projectionLayerViews[i].next = &depthView[i];
	}

    layer = (XrCompositionLayerProjection){
        .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION,
        .layerFlags = 0,
        .next = NULL,
        .space = tsoStageSpace,
    };

    if(!fbo_set) {
        uint32_t swapchain_width = 0;
        for( i = 0; i < viewCountOutput; i++ ) {
            swapchain_width += ctx->tsoViewConfigs[i].recommendedImageRectWidth;
        }
        fbo = rlLoadFramebuffer(swapchain_width, ctx->tsoViewConfigs[0].recommendedImageRectHeight);
        fbo_set = true;
    }
		
	// We only support up to 1 layer.
	if (fs.shouldRender == XR_TRUE && XR_UNQUALIFIED_SUCCESS(result))
	{
		uint32_t swapchainImageIndex;
        uint32_t depthSwapchainImageIndex;

        tsoAcquireSwapchain( ctx, 0, &swapchainImageIndex );
        depthAcquireSwapchain( ctx, 0, &depthSwapchainImageIndex );

        const XrSwapchainImageOpenGLKHR * swapchainImage = &ctx->tsoSwapchainImages[0][swapchainImageIndex];
        const XrSwapchainImageOpenGLKHR * depthSwapchainImage = &depthSwapchainImages[0][depthSwapchainImageIndex];

        uint32_t colorTexture = swapchainImage->image;
        uint32_t depthTexture = depthSwapchainImage->image;

        int render_texture_width = ctx->tsoViewConfigs[0].recommendedImageRectWidth * 2;
        int render_texture_height = ctx->tsoViewConfigs[0].recommendedImageRectHeight;

        rlFramebufferAttach(fbo, colorTexture, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
        rlFramebufferAttach(fbo, depthTexture, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);

        if(!rlFramebufferComplete(fbo)) return 1;

        RenderTexture2D render_texture = (RenderTexture2D){
            fbo,
            (Texture2D){
                colorTexture,
                render_texture_width,
                render_texture_height,
                1,
                -1
            },
            (Texture2D){
                depthTexture,
                render_texture_width,
                render_texture_height,
                1,
                -1
            }
        };

        BeginTextureMode(render_texture);

        rlEnableStereoRender();

        // doesnt work unless swapped
        Matrix proj_left = xr_projection_matrix(views[1].fov);
        Matrix proj_right = xr_projection_matrix(views[0].fov);
        rlSetMatrixProjectionStereo(proj_right, proj_left);

        const Matrix view_matrix = MatrixInvert(xr_matrix(view_location.pose));
        const Matrix view_offset_left = MatrixMultiply(xr_matrix(views[0].pose), view_matrix);
        const Matrix view_offset_right = MatrixMultiply(xr_matrix(views[1].pose), view_matrix);
        rlSetMatrixViewOffsetStereo(view_offset_right, view_offset_left);

        layer.viewCount = viewCountOutput;
		layer.views = projectionLayerViews;
		layerCount = 1;
	}

    __android_log_print(ANDROID_LOG_INFO, "beangamevr", "End BeginDrawingXR");

	return 0;
}

int EndDrawingXR(tsoContext * ctx) {
    __android_log_print(ANDROID_LOG_INFO, "beangamevr", "Begin EndDrawingXR");

    XrSession tsoSession = ctx->tsoSession;
    const XrCompositionLayerBaseHeader * layers[1] = { (XrCompositionLayerBaseHeader *)&layer };

    EndTextureMode();
    active_fbo = 0;

    rlDisableStereoRender();

    tsoReleaseSwapchain( &TSO, 0 );
    depthReleaseSwapchain( &TSO, 0 );

    XrFrameEndInfo fei = { XR_TYPE_FRAME_END_INFO };
	fei.displayTime = fs.predictedDisplayTime;
	fei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	fei.layerCount = layerCount;
	fei.layers = layers;

	XrResult result = xrEndFrame(tsoSession, &fei);
	if (tsoCheck(ctx, result, "xrEndFrame"))
	{
		return result;
	}

    free(projectionLayerViews);
    free(depthView);

    __android_log_print(ANDROID_LOG_INFO, "beangamevr", "End EndDrawingXR");

	return 0;
}

// defined in rcore_android.c, needed for tsOpenXR
extern struct android_app *GetAndroidApp(void);

//------------------------------------------------------------------------------------
// Program main entry point
//------------------------------------------------------------------------------------
int main(int argc, char *argv[])
{
    /* new ui time lets go
    if(argc >= 2) {
        serverIp = argv[1];
    }
    */ 
    // Initialization
    //--------------------------------------------------------------------------------------
    const int screenWidth = 800;
    const int screenHeight = 450;

    InitWindow(screenWidth, screenHeight, "Bean Game VR");

    __android_log_print(ANDROID_LOG_INFO, "beangamevr", "Window initialized");

    char serverIp[MAX_INPUT_CHARS + 1] = "172.233.208.111\0";
    int letterCount = 15;
    
    Rectangle textBox = { screenWidth/2.0f - 100, 180, 250, 50 };
    bool mouseOnText = false;

    int framesCounter = 0;

    // Define the camera to look into our 3d world (position, target, up vector)
    //bean.transform.translation = (Vector3){ 0.0f, 1.7f, 4.0f };    // Camera position
    //bean.target = (Vector3){ 0.0f, 1.7f, 0.0f };      // Camera looking at point
    bean.up = (Vector3){ 0.0f, 1.0f, 0.0f };          // Camera up vector (rotation towards target)
    
    bean.camera.fovy = 60.0f;                                // Camera field-of-view Y
    bean.camera.projection = CAMERA_PERSPECTIVE;             // Camera projection type
    bean.cameraMode = CAMERA_FIRST_PERSON;
    UpdateCameraWithBean(&bean);

    GameScreen currentScreen = TITLE;

    // Generates some random columns
    /*
    float heights[MAX_COLUMNS] = { 0 };
    Vector3 positions[MAX_COLUMNS] = { 0 };
    Color colors[MAX_COLUMNS] = { 0 };

    for (int i = 0; i < MAX_COLUMNS; i++)
    {
        heights[i] = (float)(GetRandomValue(1, 12));
        positions[i] = (Vector3){ (float)(GetRandomValue(-15, 15)), heights[i]/2.0f, (float)(GetRandomValue(-15, 15)) };
        colors[i] = (Color){ (GetRandomValue(20, 255)), (GetRandomValue(10, 55)), 30, 255 };
    }
    */

    bean.beanColor = (Color){ (GetRandomValue(0, 255)), (GetRandomValue(0, 255)), (GetRandomValue(0, 255)), (GetRandomValue(0, 255)) };

    // sphere time
    /*
    Sphere thatSphere = { 0 };
    thatSphere.position = (Vector3){-1.0f, 1.0f, -2.0f};
    thatSphere.radius = 1.0f;
    thatSphere.color = GREEN;
    thatSphere.sphereCollide = (BoundingBox){
        Vector3SubtractValue(thatSphere.position, thatSphere.radius),
        Vector3AddValue(thatSphere.position, thatSphere.radius)
    };
    */

    //DisableCursor();                    // Limit cursor to relative movement inside the window

    __android_log_print(ANDROID_LOG_INFO, "beangamevr", "Setting target FPS...");
    SetTargetFPS(60);                   // Set our game to run at 60 frames-per-second
    __android_log_print(ANDROID_LOG_INFO, "beangamevr", "Target FPS set");

    bool connected = false;
    bool client = false;
    bool start = false;

    int r;

    int32_t major = 0;
	int32_t minor = 0;
	glGetIntegerv(GL_MAJOR_VERSION, &major);
	glGetIntegerv(GL_MINOR_VERSION, &minor);

    // Set the stuff for tsopenxr
    egl_display = eglGetCurrentDisplay();
    egl_context = eglGetCurrentContext();
    EGLint numConfigs = 0;
    eglGetConfigs(egl_display, &egl_config, 1, &numConfigs);

    __android_log_print(ANDROID_LOG_INFO, "beangamevr", "Creating framebuffer...");
    
    __android_log_print(ANDROID_LOG_INFO, "beangamevr", "Framebuffer created");

    __android_log_print(ANDROID_LOG_INFO, "beangamevr", "Setting gapp...");
    gapp = GetAndroidApp();
    __android_log_print(ANDROID_LOG_INFO, "beangamevr", "gapp set");

    __android_log_print(ANDROID_LOG_INFO, "beangamevr", "Initializing tsOpenxr");
    if( ( r = tsoInitialize( &TSO, major, minor, TSO_DO_DEBUG | TSO_DOUBLEWIDE, "Bean Game VR", 0 ) ) ) return r;
    __android_log_print(ANDROID_LOG_INFO, "beangamevr", "tsOpenxr initialized");

    if ( ( r = tsoDefaultCreateActions( &TSO ) ) ) return r;

    //if ( ( r = tsoCreateSwapchains( &TSO ) ) ) return r;
    //--------------------------------------------------------------------------------------

    // Main game loop
    while (!(WindowShouldClose()))        // Detect window close button or ESC key
    {
        /*
        // Update
        //----------------------------------------------------------------------------------
        // Switch camera mode
        if (IsKeyPressed(KEY_ONE))
        {
            cameraMode = CAMERA_FREE;
            camera.up = (Vector3){ 0.0f, 1.0f, 0.0f }; // Reset roll
        }
        */
        
        tsoHandleLoop( &TSO );

        if(!TSO.tsoSessionReady) {
            usleep(100000);
            continue;
        }
        
        if ( ( r = tsoSyncInput( &TSO ) ) ) {
            return r;
        }

        switch(currentScreen) {
            case TITLE:
            {
                if (CheckCollisionPointRec(GetMousePosition(), textBox)) mouseOnText = true;
                else mouseOnText = false;

                if(mouseOnText) {
                    SetMouseCursor(MOUSE_CURSOR_IBEAM);

                    int key = GetCharPressed();

                    while(key > 0) {
                        if((key >= 32) && (key <= 125) && (letterCount < MAX_INPUT_CHARS)) {
                            serverIp[letterCount] = (char)key;
                            serverIp[letterCount + 1] = '\0';
                            letterCount++;
                        }
                        key = GetCharPressed();
                    }

                    if(IsKeyPressed(KEY_BACKSPACE)) {
                        letterCount--;
                        if(letterCount < 0) letterCount = 0;
                        serverIp[letterCount] = '\0';
                    }
                } else SetMouseCursor(MOUSE_CURSOR_DEFAULT);

                if(mouseOnText) framesCounter++;
                else framesCounter = 0;

                if(IsKeyPressed(KEY_ENTER)) {
                    currentScreen = GAMEPLAY;
                }
                
                break;
            }
            case GAMEPLAY:
            {
                if(IsGamepadAvailable(0)) {
                    if(IsGamepadButtonPressed(0, GAMEPAD_AXIS_LEFT_TRIGGER)) {
                        if(bean.cameraMode != CAMERA_FIRST_PERSON) {
                            bean.cameraMode = CAMERA_FIRST_PERSON;
                            bean.up = (Vector3){ 0.0f, 1.0f, 0.0f }; // Reset roll
                            UpdateCameraWithBean(&bean);
                            //updateBeanCollide(&camera, cameraMode);
                        }
                    }
                    
                    if(IsGamepadButtonPressed(0, GAMEPAD_AXIS_RIGHT_TRIGGER)) {
                        if(bean.cameraMode != CAMERA_THIRD_PERSON) {
                            bean.cameraMode = CAMERA_THIRD_PERSON;
                            bean.up = (Vector3){ 0.0f, 1.0f, 0.0f }; // Reset roll
                            UpdateCameraWithBean(&bean);
                            //updateBeanCollide(&camera, cameraMode);
                        }
                    }
                    
                    if(IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_THUMB)) {
                        if(!client) {
                            client = true;
                            Connect(serverIp);
                        }
                    }
                    
                    if(IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_THUMB)) {
                        bean.beanColor = (Color){ (GetRandomValue(0, 255)), (GetRandomValue(0, 255)), (GetRandomValue(0, 255)), (GetRandomValue(0, 255)) };
                    }
                }
                
                if ((IsKeyPressed(KEY_ONE))) {
                    if(bean.cameraMode != CAMERA_FIRST_PERSON) {
                        bean.cameraMode = CAMERA_FIRST_PERSON;
                        bean.up = (Vector3){ 0.0f, 1.0f, 0.0f }; // Reset roll
                        UpdateCameraWithBean(&bean);
                        //updateBeanCollide(&camera, cameraMode);
                    }
                }
                
                if ((IsKeyPressed(KEY_TWO))) {
                    if(bean.cameraMode != CAMERA_THIRD_PERSON) {
                        bean.cameraMode = CAMERA_THIRD_PERSON;
                        bean.up = (Vector3){ 0.0f, 1.0f, 0.0f }; // Reset roll
                        UpdateCameraWithBean(&bean);
                        //updateBeanCollide(&camera, cameraMode);
                    }
                }
                
                if (IsKeyPressed(KEY_FOUR)) {
                    // rng new color, might as well add this
                    bean.beanColor = (Color){ (GetRandomValue(0, 255)), (GetRandomValue(0, 255)), (GetRandomValue(0, 255)), (GetRandomValue(0, 255)) };
                }
                    
                if(!start) {
                    DisableCursor();
                    Connect(serverIp);
                    start = true;
                }

                if (Connected()) {
                    connected = true;
                    UpdateLocalBean(&bean);
                } else if (connected) {
                    // they hate us sadge
                    Connect(serverIp);
                    connected = false;
                }
                Update(GetTime(), GetFrameTime());
                break;
            }
        }
        
        if ((IsKeyPressed(KEY_FIVE)))
        {
            EnableCursor();
        }
        
        if ((IsKeyPressed(KEY_SIX)))
        {
            DisableCursor();
        }


        //UpdateLocalBean(&bean);

        //----------------------------------------------------------------------------------

        // Draw
        //----------------------------------------------------------------------------------
            BeginDrawingXR(&TSO);

            ClearBackground(RAYWHITE);

            switch(currentScreen) {
                case TITLE: {
                    DrawText("Server IP:", 240, 140, 20, GRAY);

                    DrawRectangleRec(textBox, LIGHTGRAY);
                    if (mouseOnText) DrawRectangleLines((int)textBox.x, (int)textBox.y, (int)textBox.width, (int)textBox.height, RED);
                    else DrawRectangleLines((int)textBox.x, (int)textBox.y, (int)textBox.width, (int)textBox.height, DARKGRAY);

                    DrawText(serverIp, (int)textBox.x + 5, (int)textBox.y + 8, 35, MAROON);

                    DrawText("Press ENTER to Continue", 315, 250, 20, DARKGRAY);
                    break;
                }
                case GAMEPLAY:
                {
                    BeginMode3D(bean.camera);
                    
                    DrawPlane((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector2){ 32.0f, 32.0f }, LIGHTGRAY); // Draw ground
                    
                    if (!Connected()) {
                        //DrawText("Connecting", 15, 75, 10, BLACK);
                    } else {
                        //DrawText(TextFormat("Player %d", GetLocalPlayerId()), 15, 75, 10, BLACK);
                        //printf("Bean %d position: x=%f, y=%f, z=%f\n", LocalPlayerId, bean.transform.translation.x, bean.transform.translation.y, bean.transform.translation.z);
                        
                        for (int i = 0; i < MAX_PLAYERS; i++) {
                            if(i != GetLocalPlayerId()) {
                                Vector3 pos = { 0 };
                                uint8_t r;
                                uint8_t g;
                                uint8_t b;
                                uint8_t a;
                                if(GetPlayerPos(i, &pos) && GetPlayerR(i, &r) && GetPlayerG(i, &g) && GetPlayerB(i, &b), GetPlayerA(i, &a)) {
                                    DrawCapsule(
                                        (Vector3){pos.x, pos.y + 0.2f, pos.z},
                                        (Vector3){pos.x, pos.y - 1.0f, pos.z},
                                        0.7f, 8, 8, (Color){ r, g, b, a }
                                    );
                                    DrawCapsuleWires(
                                        (Vector3){pos.x, pos.y + 0.2f, pos.z},
                                        (Vector3){pos.x, pos.y - 1.0f, pos.z},
                                        0.7f, 8, 8, BLACK // an L color tbh
                                    );
                                }
                            }
                        }
                    }
                    
                    // Draw bean
                    if (bean.cameraMode == CAMERA_THIRD_PERSON) {
                        DrawCapsule(bean.topCap, bean.botCap, 0.7f, 8, 8, bean.beanColor);
                        DrawCapsuleWires(bean.topCap, bean.botCap, 0.7f, 8, 8, BLACK);
                        //DrawBoundingBox(beanCollide, VIOLET);
                    }

                    EndMode3D();

                    // Draw info boxes
                    DrawRectangle(5, 5, 330, 85, RED);
                    DrawRectangleLines(5, 5, 330, 85, BLUE);
                    DrawText("Player controls:", 15, 15, 10, BLACK);
                    if(IsGamepadAvailable(0)) {
                        DrawText("- Move: Left Analog Stick", 15, 30, 10, BLACK);
                        DrawText("- Look around: Right Analog Stick", 15, 45, 10, BLACK);
                        DrawText("- Camera mode: Left Trigger, Right Trigger", 15, 60, 10, BLACK);
                        DrawText("- Generate a new color: Left Thumb", 15, 75, 10, BLACK);
                    } else {
                        DrawText("- Move keys: W, A, S, D, Space, Left-Ctrl", 15, 30, 10, BLACK);
                        DrawText("- Look around: arrow keys or mouse", 15, 45, 10, BLACK);
                        DrawText("- Camera mode keys: 1, 2", 15, 60, 10, BLACK);
                        DrawText("- Generate a new color: 3", 15, 75, 10, BLACK);
                    }
                    break;
                }
            }
            EndDrawingXR(&TSO);
        //----------------------------------------------------------------------------------
    }

    // De-Initialization
    //--------------------------------------------------------------------------------------
    Disconnect();
    rlUnloadFramebuffer(fbo);
    CloseWindow();        // Close window and OpenGL context
    //--------------------------------------------------------------------------------------

    return tsoTeardown( &TSO );
    //return 0;
}

void UpdateTheBigBean(Vector3 pos, Vector3 tar) {
    bean.transform.translation = pos;
    bean.target = tar;
}

bool GetPlayerBoundingBox(int id, BoundingBox* box) {
    if(IsPlayerReal(id)) {
        Vector3 pos = { 0 };
        if(GetPlayerPos(id, &pos)) {
            *box = (BoundingBox){
                (Vector3){pos.x - 0.7f, pos.y - 1.7f, pos.z - 0.7f},
                (Vector3){pos.x + 0.7f, pos.y + 0.9f, pos.z + 0.7f}
            };
            return true;
        }
    } else {
        return false;
    }
}

void HandleCollision() {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if(i != GetLocalPlayerId()) {
            BoundingBox box = { 0 };
            if(GetPlayerBoundingBox(i, &box)) {
                if(CheckCollisionBoxes(bean.beanCollide, box)) {
                    bean.transform.translation = Vector3Subtract(bean.transform.translation, bean.posAdd);
                    bean.target = Vector3Subtract(bean.target, bean.posAdd);
                    bean.beanCollide = (BoundingBox){
                        (Vector3){bean.transform.translation.x - 0.7f, bean.transform.translation.y - 1.7f, bean.transform.translation.z - 0.7f},
                        (Vector3){bean.transform.translation.x + 0.7f, bean.transform.translation.y + 0.9f, bean.transform.translation.z + 0.7f}};
                    UpdateCameraWithBean(&bean);
                    return;
                }
            }
        }
    }
}
