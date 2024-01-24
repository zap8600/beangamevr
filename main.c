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

#include "raylib/raylib.h"
#include "raylib/rcamera.h"
#include "raylib/raymath.h"
#include "objects.h"
#include "player.h"
#include "net/net_client.h"

#define TSOPENXR_IMPLEMENTATION
#include "tsopenxr.h"

// #define MAX_COLUMNS 10

LocalBean bean = { 0 };

#define MAX_INPUT_CHARS 17

typedef enum GameScreen { TITLE, GAMEPLAY } GameScreen;

tsoContext TSO;
unsigned int fbo = 0;

uint32_t GetDepthTextureFromColorTexture( uint32_t colorTexture, int width, int height )
{
	int i;
	for( i = 0; i < numColorDepthPairs; i++ )
	{
		if( colorDepthPairs[i*2] == colorTexture )
			return colorDepthPairs[i*2+1];
	}
	colorDepthPairs = realloc( colorDepthPairs, (numColorDepthPairs+1)*2*sizeof(uint32_t) );
	colorDepthPairs[numColorDepthPairs*2+0] = colorTexture;
	int ret = colorDepthPairs[numColorDepthPairs*2+1] = CreateDepthTexture( colorTexture, width, height );
	numColorDepthPairs++;
	return ret;
}

int RenderLayer(tsoContext * ctx, XrTime predictedDisplayTime, XrCompositionLayerProjectionView * projectionLayerViews, int viewCountOutput )
{
	// Render view to the appropriate part of the swapchain image.
	for (uint32_t v = 0; v < viewCountOutput; v++)
	{
		XrCompositionLayerProjectionView * layerView = projectionLayerViews + v;
		uint32_t swapchainImageIndex;

		// Each view has a separate swapchain which is acquired, rendered to, and released.
		tsoAcquireSwapchain( ctx, v, &swapchainImageIndex );

		const XrSwapchainImageOpenGLKHR * swapchainImage = &ctx->tsoSwapchainImages[v][swapchainImageIndex];

		uint32_t colorTexture = swapchainImage->image;
		uint32_t depthTexture = GetDepthTextureFromColorTexture( colorTexture, ctx->tsoSwapchains[0].width, ctx->tsoSwapchains[0].height );

		int render_texture_width = &ctx->tsoViewConfigs[v].recommendedImageRectWidth * 2;
		int render_texture_height = &ctx->tsoViewConfigs[v].recommendedImageRectHeight;

		RenderTexture render_texture = (RenderTexture){
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

		// Show user different colors in right and left eye.
		memset( bufferdata, v*250, width*height * 4 );
		glBindTexture( GL_TEXTURE_2D, colorTexture );

		glTexSubImage2D( GL_TEXTURE_2D, 0,
			layerView->subImage.imageRect.offset.x, layerView->subImage.imageRect.offset.y,
			width, height, 
			GL_RGBA, GL_UNSIGNED_BYTE, bufferdata );
		glBindTexture( GL_TEXTURE_2D, 0 );

		free( bufferdata );

		tsoReleaseSwapchain( &TSO, v );
	}

	return 0;
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

    InitWindow(screenWidth, screenHeight, "Bean Game");

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
        BeginDrawing();

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

        EndDrawing();
        //----------------------------------------------------------------------------------
    }

    // De-Initialization
    //--------------------------------------------------------------------------------------
    Disconnect();
    CloseWindow();        // Close window and OpenGL context
    //--------------------------------------------------------------------------------------

    return 0;
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
