#include "raylib/raylib.h"

typedef struct Sphere {
    Vector3 position;
    float radius;
    Color color;
    BoundingBox sphereCollide;
} Sphere;

typedef struct Plane {
    Vector3 position;
    Vector2 size;
    Color Color;
    BoundingBox planeCollide;
} Plane;
