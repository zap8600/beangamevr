#pragma once

#include <stdint.h>
#include <stdbool.h>

// It is ok to include raymath, since raymath doesn't have any conflict with windows.h
#include "raylib/raymath.h"

void UpdatePlayerList(Vector3 position, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void UpdateTheBigBean(Vector3 pos, Vector3 tar);

void Connect(const char* serverAddress);
void Update(double now, float deltaT);
void Disconnect();
bool Connected();
int GetLocalPlayerId();
bool GetPlayerPos(int id, Vector3* pos);

bool GetPlayerR(int id, unsigned char* r);
bool GetPlayerG(int id, unsigned char* g);
bool GetPlayerB(int id, unsigned char* b);
bool GetPlayerA(int id, unsigned char* a);
bool IsPlayerReal(int id);
