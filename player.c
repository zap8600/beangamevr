#include "player.h"
#include "raylib/raylib.h"
#include "raylib/rcamera.h"
#include "raylib/raymath.h"
#include "net/net_client.h"
#include <stdio.h>

void UpdateCameraWithBean(LocalBean* bean) {
    if(bean->cameraMode == CAMERA_FIRST_PERSON) {
        bean->camera.position = bean->transform.translation;
        bean->camera.target = bean->target;
        bean->camera.up = bean->up;
    } else { // assume third person for time
        Vector3 newCameraPosition = Vector3Negate(Vector3Subtract(bean->target, bean->transform.translation));
        bean->camera.position = Vector3Add(bean->transform.translation, newCameraPosition);
        bean->camera.target = bean->transform.translation;
        bean->camera.up = bean->up;
    }
}

Vector3 GetBeanForward(LocalBean* bean) {
    return Vector3Normalize(Vector3Subtract(bean->target, bean->transform.translation));
}

Vector3 GetBeanUp(LocalBean* bean)
{
    return Vector3Normalize(bean->up);
}

Vector3 GetBeanRight(LocalBean* bean)
{
    Vector3 forward = GetBeanForward(bean);
    Vector3 up = GetBeanUp(bean);

    return Vector3CrossProduct(forward, up);
}

void BeanMoveForward(LocalBean* bean, float distance, bool moveInWorldPlane) {
    Vector3 forward = GetBeanForward(bean);

    if (moveInWorldPlane)
    {
        // Project vector onto world plane
        forward.y = 0;
        forward = Vector3Normalize(forward);
    }

    // Scale by distance
    forward = Vector3Scale(forward, distance);

    bean->posAdd = Vector3Add(bean->posAdd, forward);

    bean->transform.translation = Vector3Add(bean->transform.translation, forward);
    bean->target = Vector3Add(bean->target, forward);
    UpdateCameraWithBean(bean);
}

void BeanMoveRight(LocalBean* bean, float distance, bool moveInWorldPlane)
{
    Vector3 right = GetBeanRight(bean);

    if (moveInWorldPlane)
    {
        // Project vector onto world plane
        right.y = 0;
        right = Vector3Normalize(right);
    }

    // Scale by distance
    right = Vector3Scale(right, distance);

    bean->posAdd = Vector3Add(bean->posAdd, right);

    // Move position and target
    bean->transform.translation = Vector3Add(bean->transform.translation, right);
    bean->target = Vector3Add(bean->target, right);
    UpdateCameraWithBean(bean);
}

void BeanYaw(LocalBean* bean, float angle, bool rotateAroundTarget)
{
    // Rotation axis
    Vector3 up = GetBeanUp(bean);

    // View vector
    Vector3 targetPosition = Vector3Subtract(bean->target, bean->transform.translation);

    // Rotate view vector around up axis
    targetPosition = Vector3RotateByAxisAngle(targetPosition, up, angle);

    bean->target = Vector3Add(bean->transform.translation, targetPosition);
    
    UpdateCameraWithBean(bean);
}

void BeanPitch(LocalBean* bean, float angle, bool lockView, bool rotateAroundTarget, bool rotateUp)
{
    // Up direction
    Vector3 up = GetBeanUp(bean);

    // View vector
    Vector3 targetPosition = Vector3Subtract(bean->target, bean->transform.translation);

    if (lockView)
    {
        // In these camera modes we clamp the Pitch angle
        // to allow only viewing straight up or down.

        // Clamp view up
        float maxAngleUp = Vector3Angle(up, targetPosition);
        maxAngleUp -= 0.001f; // avoid numerical errors
        if (angle > maxAngleUp) angle = maxAngleUp;

        // Clamp view down
        float maxAngleDown = Vector3Angle(Vector3Negate(up), targetPosition);
        maxAngleDown *= -1.0f; // downwards angle is negative
        maxAngleDown += 0.001f; // avoid numerical errors
        if (angle < maxAngleDown) angle = maxAngleDown;
    }

    // Rotation axis
    Vector3 right = GetBeanRight(bean);

    // Rotate view vector around right axis
    targetPosition = Vector3RotateByAxisAngle(targetPosition, right, angle);
    
    bean->target = Vector3Add(bean->transform.translation, targetPosition);

    if (rotateUp)
    {
        // Rotate up direction around right axis
        bean->up = Vector3RotateByAxisAngle(bean->up, right, angle);
    }
    UpdateCameraWithBean(bean);
}

#define BEAN_MOVE_SPEED 0.09f
#define CAMERA_MOUSE_SPEED 0.003f
#define CAMERA_ROTATION 0.03f

void UpdateLocalBean(LocalBean* bean) {
    Vector2 mousePositionDelta = GetMouseDelta();

    bool moveInWorldPlane = ((bean->cameraMode == CAMERA_FIRST_PERSON) || (bean->cameraMode == CAMERA_THIRD_PERSON));
    bool rotateAroundTarget = ((bean->cameraMode == CAMERA_THIRD_PERSON) || (bean->cameraMode == CAMERA_ORBITAL));
    bool lockView = ((bean->cameraMode == CAMERA_FREE) || (bean->cameraMode == CAMERA_FIRST_PERSON) || (bean->cameraMode == CAMERA_THIRD_PERSON) || (bean->cameraMode == CAMERA_ORBITAL));
    bool rotateUp = false;

    bean->posAdd = Vector3Zero();

    if(IsKeyDown(KEY_DOWN)) BeanPitch(bean, -CAMERA_ROTATION, lockView, rotateAroundTarget, rotateUp);
    if(IsKeyDown(KEY_UP)) BeanPitch(bean, CAMERA_ROTATION, lockView, rotateAroundTarget, rotateUp);
    if(IsKeyDown(KEY_RIGHT)) BeanYaw(bean, -CAMERA_ROTATION, rotateAroundTarget);
    if(IsKeyDown(KEY_LEFT)) BeanYaw(bean, -CAMERA_ROTATION, rotateAroundTarget);

    BeanYaw(bean, -mousePositionDelta.x*CAMERA_MOUSE_SPEED, rotateAroundTarget);
    BeanPitch(bean, -mousePositionDelta.y*CAMERA_MOUSE_SPEED, lockView, rotateAroundTarget, rotateUp);

    if (IsKeyDown(KEY_W)) BeanMoveForward(bean, BEAN_MOVE_SPEED, moveInWorldPlane);
    if (IsKeyDown(KEY_A)) BeanMoveRight(bean, -BEAN_MOVE_SPEED, moveInWorldPlane);
    if (IsKeyDown(KEY_S)) BeanMoveForward(bean, -BEAN_MOVE_SPEED, moveInWorldPlane);
    if (IsKeyDown(KEY_D)) BeanMoveRight(bean, BEAN_MOVE_SPEED, moveInWorldPlane);

    if (IsGamepadAvailable(0)) {
        // Gamepad controller support
        BeanYaw(bean, -(GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_X) * 2)*CAMERA_MOUSE_SPEED, rotateAroundTarget);
        BeanPitch(bean, -(GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_Y) * 2)*CAMERA_MOUSE_SPEED, lockView, rotateAroundTarget, rotateUp);
        
        if (GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y) <= -0.25f) BeanMoveForward(bean, BEAN_MOVE_SPEED, moveInWorldPlane);
        if (GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X) <= -0.25f) BeanMoveRight(bean, -BEAN_MOVE_SPEED, moveInWorldPlane);
        if (GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y) >= 0.25f) BeanMoveForward(bean, -BEAN_MOVE_SPEED, moveInWorldPlane);
        if (GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X) >= 0.25f) BeanMoveRight(bean, BEAN_MOVE_SPEED, moveInWorldPlane);
    }

    bean->beanCollide = (BoundingBox){
        (Vector3){bean->transform.translation.x - 0.7f, bean->transform.translation.y - 1.7f, bean->transform.translation.z - 0.7f},
        (Vector3){bean->transform.translation.x + 0.7f, bean->transform.translation.y + 0.9f, bean->transform.translation.z + 0.7f}};

    HandleCollision();

    bean->topCap = (Vector3){bean->transform.translation.x, bean->transform.translation.y + 0.2f, bean->transform.translation.z};
    bean->botCap = (Vector3){bean->transform.translation.x, bean->transform.translation.y - 1.0f, bean->transform.translation.z};

    // update the local player in the player list
    UpdatePlayerList(bean->transform.translation, bean->beanColor.r, bean->beanColor.g, bean->beanColor.b, bean->beanColor.a);
}
