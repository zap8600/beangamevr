#include "raylib/raylib.h"
#include "net/net_constants.h"

// the player id of this client
//int LocalPlayerId = -1;

typedef struct LocalBean {
    Transform transform; // player position, rotation, and scale
    Vector3 target; // player target
    Vector3 up; // player up? used for rolling i think
    Vector3 posAdd;
    BoundingBox beanCollide;
    Color beanColor; // player color
    Vector3 topCap; // start cap for capsule
    Vector3 botCap; // end cap for capsule
    Camera camera; // camera
    int cameraMode; // camera mode
} LocalBean;

//Bean beans[MAX_PLAYERS] = { 0 };

// all of my pride and joy
void UpdateCameraWithBean(LocalBean* bean);
Vector3 GetBeanForward(LocalBean* bean);
Vector3 GetBeanUp(LocalBean* bean);
Vector3 GetBeanRight(LocalBean* bean);
void BeanYaw(LocalBean* bean, float angle, bool rotateAroundTarget);
void BeanPitch(LocalBean* bean, float angle, bool lockView, bool rotateAroundTarget, bool rotateUp);
void UpdateLocalBean(LocalBean* bean);
void HandleCollision();
