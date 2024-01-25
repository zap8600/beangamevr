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

#define GLES_VER_TARG "100"
#define TARGET_OFFSCREEN

struct android_app *gapp;
EGLDisplay egl_display;
EGLSurface egl_surface;
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
unsigned int fbo = 0;
unsigned int active_fbo = 0;

int RenderLayer(tsoContext * ctx, XrTime predictedDisplayTime, XrCompositionLayerProjectionView * projectionLayerViews, int viewCountOutput )
{
	EndTextureMode();
    active_fbo = 0;

    tsoReleaseSwapchain( &TSO, 0 );

	return 0;
}


void BeginDrawingXR () {
    //fbo = rlLoadFramebuffer(0, 0); // HACK: I don't think this function uses width and height at all
	uint32_t swapchainImageIndex;

	// Each view has a separate swapchain which is acquired, rendered to, and released.
	tsoAcquireSwapchain( &TSO, 0, &swapchainImageIndex );

	const XrSwapchainImageOpenGLKHR * swapchainImage = &(&TSO)->tsoSwapchainImages[swapchainImageIndex];

	uint32_t colorTexture = swapchainImage->image;

    rlFramebufferAttach(fbo, colorTexture, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);

    assert(rlFramebufferComplete(fbo));

	int render_texture_width = TSO.tsoViewConfigs[0].recommendedImageRectWidth * 2;
	int render_texture_height = TSO.tsoViewConfigs[0].recommendedImageRectHeight;
        // uint32_t * bufferdata = malloc( width*height*4 ); // do i need this?

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
            ULONG_MAX,
            render_texture_width,
            render_texture_height,
            1,
            -1
        }
    };

    BeginTextureMode(render_texture);
    active_fbo = fbo;

	//tsoReleaseSwapchain( &TSO, v );
}

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

    SetTargetFPS(60);                   // Set our game to run at 60 frames-per-second

    bool connected = false;
    bool client = false;
    bool start = false;

    int r;

    int32_t major = 0;
	int32_t minor = 0;
	glGetIntegerv(GL_MAJOR_VERSION, &major);
	glGetIntegerv(GL_MINOR_VERSION, &minor);

    if( ( r = tsoInitialize( &TSO, major, minor, TSO_DO_DEBUG, "Bean Game VR", 0 ) ) ) return r;

    fbo = rlLoadFramebuffer(0, 0);

    TSO.tsoRenderLayer = RenderLayer;

    if ( ( r = tsoDefaultCreateActions( &TSO ) ) ) return r;
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
        
        if ( ( r = tsoRenderFrame( &TSO ) ) ) {
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

            ClearBackground(RAYWHITE);

            switch(currentScreen) {
                case TITLE: {
                    BeginDrawing();
                    DrawText("Server IP:", 240, 140, 20, GRAY);

                    DrawRectangleRec(textBox, LIGHTGRAY);
                    if (mouseOnText) DrawRectangleLines((int)textBox.x, (int)textBox.y, (int)textBox.width, (int)textBox.height, RED);
                    else DrawRectangleLines((int)textBox.x, (int)textBox.y, (int)textBox.width, (int)textBox.height, DARKGRAY);

                    DrawText(serverIp, (int)textBox.x + 5, (int)textBox.y + 8, 35, MAROON);

                    DrawText("Press ENTER to Continue", 315, 250, 20, DARKGRAY);
                    EndDrawing();
                    break;
                }
                case GAMEPLAY:
                {
                    BeginDrawingXR();
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
                    //if ( ( r = tsoRenderFrame( &TSO ) ) ) {
                    //    return r;
                    //}

                    //BeginDrawing();

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
                    EndDrawing();
                    break;
                }
            }

        EndDrawing();
        //----------------------------------------------------------------------------------
    }

    // De-Initialization
    //--------------------------------------------------------------------------------------
    Disconnect();
    //rlUnloadFramebuffer(fbo);
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
