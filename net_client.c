#include "net/net_client.h"
#include <stdio.h>

#define ENET_IMPLEMENTATION
#include "net/net_common.h"

int LocalPlayerId = -1;

// the enet address we are connected to
ENetAddress address = { 0 };

// the server object we are connecting to
ENetPeer* server = { 0 };

// the client peer we are using
ENetHost* client = { 0 };

// how long in seconds since the last time we sent an update
double LastInputSend = -100;

// how long to wait between updates (20 update ticks a second)
double InputUpdateInterval = 1.0f / 20.0f;

double LastNow = 0;

// this struct wont be used until networking is added
typedef struct Bean {
    Vector3 position; // player position
    unsigned char r; // replacements
    unsigned char g; // for
    unsigned char b; // color
    unsigned char a; // type
    bool active; // are they awake
    double updateTime; // time of last update
} Bean;

Bean beans[MAX_PLAYERS] = { 0 };

// Connect to a server
void Connect(const char* serverAddress)
{
	// startup the network library
	enet_initialize();

	// create a client that we will use to connect to the server
	client = enet_host_create(NULL, 1, 1, 0, 0);

	// set the address and port we will connect to
	enet_address_set_host(&address, serverAddress);
	address.port = 4545;

	// start the connection process. Will be finished as part of our update
	server = enet_host_connect(client, &address, 1, 0);
}

Vector3 ReadPosition(ENetPacket* packet, size_t* offset)
{
	Vector3 pos = { 0 };
	pos.x = ReadFloat(packet, offset);
	pos.y = ReadFloat(packet, offset);
    pos.z = ReadFloat(packet, offset);

	return pos;
}

// A new remote player was added to our local simulation
void HandleAddPlayer(ENetPacket* packet, size_t* offset)
{
	// find out who the server is talking about
	int remotePlayer = ReadByte(packet, offset);
	if (remotePlayer >= MAX_PLAYERS || remotePlayer == LocalPlayerId)
		return;

	// set them as active and update the location
	printf("Bean %d added\n", remotePlayer);
	beans[remotePlayer].position = ReadPosition(packet, offset);
    beans[remotePlayer].r = ReadByte(packet, offset);
    beans[remotePlayer].g = ReadByte(packet, offset);
    beans[remotePlayer].b = ReadByte(packet, offset);
    beans[remotePlayer].a = ReadByte(packet, offset);
    beans[remotePlayer].active = true;
	beans[remotePlayer].updateTime = LastNow;
	printf("Bean %d position: x=%f, y=%f, z=%f\n", remotePlayer, beans[remotePlayer].position.x, beans[remotePlayer].position.y, beans[remotePlayer].position.z);

	// In a more robust game, this message would have more info about the new player, such as what sprite or model to use, player name, or other data a client would need
	// this is where static data about the player would be sent, and any initial state needed to setup the local simulation
}

// A remote player has left the game and needs to be removed from the local simulation
void HandleRemovePlayer(ENetPacket* packet, size_t* offset)
{
	// find out who the server is talking about
	int remotePlayer = ReadByte(packet, offset);
	if (remotePlayer >= MAX_PLAYERS || remotePlayer == LocalPlayerId)
		return;

	// remove the player from the simulation. No other data is needed except the player id
	printf("Bean %d removed\n", remotePlayer); // they may be black
	beans[remotePlayer].active = false;
}

// The server has a new position for a player in our local simulation
void HandleUpdatePlayer(ENetPacket* packet, size_t* offset)
{
	// find out who the server is talking about
	int remotePlayer = ReadByte(packet, offset);
	if (remotePlayer >= MAX_PLAYERS || remotePlayer == LocalPlayerId || !beans[remotePlayer].active)
		return;

	// update the last known position and movement
	//printf("Bean %d update\n", remotePlayer);
	beans[remotePlayer].position = ReadPosition(packet, offset);
	beans[remotePlayer].r = ReadByte(packet, offset);
    beans[remotePlayer].g = ReadByte(packet, offset);
    beans[remotePlayer].b = ReadByte(packet, offset);
    beans[remotePlayer].a = ReadByte(packet, offset);
	beans[remotePlayer].updateTime = LastNow;

	// in a more robust game this message would have a tick ID for what time this information was valid, and extra info about
	// what the input state was so the local simulation could do prediction and smooth out the motion
}

// process one frame of updates
void Update(double now, float deltaT)
{
	LastNow = now;
	// if we are not connected to anything yet, we can't do anything, so bail out early
	if (server == NULL)
		return;

	// Check if we have been accepted, and if so, check the clock to see if it is time for us to send the updated position for the local player
	// we do this so that we don't spam the server with updates 60 times a second and waste bandwidth
	// in a real game we'd send our normalized movement vector or input keys along with what the current tick index was
	// this way the server can know how long it's been since the last update and can do interpolation to know were we are between updates.
	if (LocalPlayerId >= 0 && now - LastInputSend > InputUpdateInterval)
	{
		// Pack up a buffer with the data we want to send
		uint8_t buffer[17] = { 0 }; // 10 bytes for a 1 byte command number and two bytes for each X and Y value
		buffer[0] = (uint8_t)UpdateInput;   // this tells the server what kind of data to expect in this packet
		*(float*)(buffer + 1) = (float)beans[LocalPlayerId].position.x;
		*(float*)(buffer + 5) = (float)beans[LocalPlayerId].position.y;
		*(float*)(buffer + 9) = (float)beans[LocalPlayerId].position.z;
		*(uint8_t*)(buffer + 13) = (uint8_t)beans[LocalPlayerId].r;
		*(uint8_t*)(buffer + 14) = (uint8_t)beans[LocalPlayerId].g;
		*(uint8_t*)(buffer + 15) = (uint8_t)beans[LocalPlayerId].b;
		*(uint8_t*)(buffer + 16) = (uint8_t)beans[LocalPlayerId].a;

        // copy this data into a packet provided by enet (TODO : add pack functions that write directly to the packet to avoid the copy)
		ENetPacket* packet = enet_packet_create(buffer, 17, ENET_PACKET_FLAG_RELIABLE);

		// send the packet to the server
		enet_peer_send(server, 0, packet);

		// NOTE enet_host_service will handle releasing send packets when the network system has finally sent them,
		// you don't have to destroy them

		// mark that now was the last time we sent an update
		LastInputSend = now;
    }

    // read one event from enet and process it
	ENetEvent Event = { 0 };

    if (enet_host_service(client, &Event, 0) > 0)
	{
		// see what kind of event it is
		switch (Event.type)
		{
			// the server sent us some data, we should process it
			case ENET_EVENT_TYPE_RECEIVE:
			{
				// we know that all valid packets have a size >= 1, so if we get this, something is bad and we ignore it.
				if (Event.packet->dataLength < 1)
					break;

				// keep an offset of what data we have read so far
				size_t offset = 0;

				// read off the command that the server wants us to do
				NetworkCommands command = (NetworkCommands)ReadByte(Event.packet, &offset);

                // if the server has not accepted us yet, we are limited in what packets we can receive
				if (LocalPlayerId == -1)
				{
					if (command == AcceptPlayer)    // this is the only thing we can do in this state, so ignore anything else
					{
						// See who the server says we are
						LocalPlayerId = ReadByte(Event.packet, &offset);
						printf("Local ID = %d\n", LocalPlayerId);

						// Make sure that it makes sense
						if (LocalPlayerId < 0 || LocalPlayerId > MAX_PLAYERS)
						{
							LocalPlayerId = -1;
							break;
						}

						// Force the next frame to do an update by pretending it's been a very long time since our last update
						LastInputSend = -InputUpdateInterval;

						// We are active
						beans[LocalPlayerId].active = true;

						// Set our player at some location on the field.
						// optimally we would do a much more robust connection negotiation where we tell the server what our name is, what we look like
						// and then the server tells us where we are
						// But for this simple test, everyone starts at the same place on the field
						beans[LocalPlayerId].position = (Vector3){ 0.0f, 1.7f, 4.0f };    // Camera position
                        //bean->target = (Vector3){ 0.0f, 1.7f, 0.0f };      // Camera looking at point
                        UpdateTheBigBean(beans[LocalPlayerId].position, (Vector3){ 0.0f, 1.7f, 0.0f });
					}
				}
                else // we have been accepted, so process play messages from the server
				{
					// see what the server wants us to do
					switch (command)
					{
						case AddPlayer:
							HandleAddPlayer(Event.packet, &offset);
							break;

						case RemovePlayer:
							HandleRemovePlayer(Event.packet, &offset);
							break;

						case UpdatePlayer:
							HandleUpdatePlayer(Event.packet, &offset);
							break;
					}
				}
				// tell enet that it can recycle the packet data
				enet_packet_destroy(Event.packet);
				break;
			}

            // we were disconnected, we have a sad
			case ENET_EVENT_TYPE_DISCONNECT:
				server = NULL;
				LocalPlayerId = -1;
				break;
		}
	}
}

// force a disconnect by shutting down enet
void Disconnect()
{
	// close our connection to the server
	if (server != NULL)
		enet_peer_disconnect(server, 0);

	// close our client
	if (client != NULL)
		enet_host_destroy(client);

	client = NULL;
	server = NULL;

	// clean up enet
	enet_deinitialize();
}

// true if we are connected and have been accepted
bool Connected()
{
	return server != NULL && LocalPlayerId >= 0;
}

int GetLocalPlayerId()
{
	return LocalPlayerId;
}

// get the info for a particular player
bool GetPlayerPos(int id, Vector3* pos)
{
	// make sure the player is valid and active
	if (id < 0 || id >= MAX_PLAYERS || !beans[id].active)
		return false;

	*pos = beans[id].position;
	return true;
}

// get the info for a particular player
bool GetPlayerR(int id, unsigned char* r)
{
	// make sure the player is valid and active
	if (id < 0 || id >= MAX_PLAYERS || !beans[id].active)
		return false;

	*r = beans[id].r;
	return true;
}

// get the info for a particular player
bool GetPlayerG(int id, unsigned char* g)
{
	// make sure the player is valid and active
	if (id < 0 || id >= MAX_PLAYERS || !beans[id].active)
		return false;

	*g = beans[id].g;
	return true;
}

// get the info for a particular player
bool GetPlayerB(int id, unsigned char* b)
{
	// make sure the player is valid and active
	if (id < 0 || id >= MAX_PLAYERS || !beans[id].active)
		return false;

	*b = beans[id].b;
	return true;
}

// get the info for a particular player
bool GetPlayerA(int id, unsigned char* a)
{
	// make sure the player is valid and active
	if (id < 0 || id >= MAX_PLAYERS || !beans[id].active)
		return false;

	*a = beans[id].a;
	return true;
}

bool IsPlayerReal(int id) { // we could be a schizo
	if (id < 0 || id >= MAX_PLAYERS || !beans[id].active)
		return false;
	
	return true;
}

void UpdatePlayerList(Vector3 position, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    beans[LocalPlayerId].position = position;
	beans[LocalPlayerId].r = r;
    beans[LocalPlayerId].g = g;
    beans[LocalPlayerId].b = b;
    beans[LocalPlayerId].a = a;
}
