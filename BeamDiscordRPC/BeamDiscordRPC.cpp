// BeamDiscordRPC.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "include/discord_rpc.h"
#include <iostream>
#include <string>
#include <vector>
#include <winsock2.h>
#include <windows.h>
#include <tlhelp32.h>
#include <list>
#include <algorithm>
#include <windows.h>
#include <time.h>

#pragma warning(disable:4996)

using namespace std;

//state variables, set from socket communication via lua
std::string currentVehicle = "pickup";
std::string currentVehiclename = "Gavril D-Series";
std::string currentMap = "smallgrid";
std::string currentMapname = "";
std::string state = "init";
std::string exif = "EXIF";
bool showVehicle = false;
time_t lastTime;
int timerType = 0;

//MASSIVE LISTS!
std::list<std::string> defaultVehicles{ "autobello", "ball", "barrels", "barrier", "barstow", "blockwall", "bollard", "boxutility", "boxutility_large",
                                        "burnside", "cannon", "caravan", "citybus", "christmas_tree", "cones", "coupe", "dryvan", "etk800", "etkc",
                                        "etki", "flail", "flatbed", "flipramp", "fullsize", "hatch", "haybale", "hopper", "inflated_mat",
                                        "kickplate", "large_angletester", "large_bridge", "large_cannon", "large_crusher", "large_hamster_wheel",
                                        "large_metal_ramp", "large_roller", "large_spinner", "large_tilt", "legran", "metal_box", "metal_ramp",
                                        "midsize", "miramar", "moonhawk", "pessima", "piano", "pickup", "pigeon", "roadsigns", "roamer", "rocks",
                                        "rollover", "sawhorse", "sbr", "semi", "streetlight", "sunburst", "super", "suspensionbridge", "tanker",
                                        "tirestacks", "tirewall", "trafficbarrel", "tsfb", "tube", "van", "vivace", "wall", "weightpad", "woodcrate", "woodplanks"};

std::list<std::string> defaultLevels{ "cliff", "derby", "driver_training", "east_coast_usa", "garage", "gridmap", "hirochi_raceway",
                                      "industrial", "italy", "jungle_rock_island", "port", "showroom_v2_dark", "small_island", "smallgrid",
                                      "template", "utah", "west_coast_usa"};

bool debugMode = false;

bool IsDefaultVehicle(std::string key) {
    return std::find(defaultVehicles.begin(), defaultVehicles.end(), key) != defaultVehicles.end();
}

bool IsDefaultLevel(std::string key) {
    return std::find(defaultLevels.begin(), defaultLevels.end(), key) != defaultLevels.end();
}

std::string VerifyStr(std::string str, unsigned int len) {
    if (str.length() > len) {
        return str.substr(0, len - 3) + "...";
    }
    return str;
}

void UpdatePresence()
{
    if(debugMode)
        printf("Updating presence\n");

    char buffer[256];
    DiscordRichPresence discordPresence;
    memset(&discordPresence, 0, sizeof(discordPresence));

    //handle state / details lines
    if (state == "init") {
        discordPresence.state = "In Menu";
        discordPresence.largeImageKey = "icon";
    }
    else if (state == "freeroam") {
        discordPresence.details = "Playing Freeroam";
    }
    else if (state == "campaign") {
        discordPresence.details = "Playing Campaign";
        discordPresence.state = exif.c_str();
    }
    else if (state == "scenario") {
        discordPresence.details = "Playing Scenario";
        discordPresence.state = exif.c_str();
    }
    else if (state == "garage") {
        discordPresence.details = "In the Garage";
    }
    else if (state == "quickrace") {
        discordPresence.details = "Playing Quick Race";
        discordPresence.state = exif.c_str();
    }
    else if (state == "bus") {
        discordPresence.details = "Playing Bus Route";
        discordPresence.state = exif.c_str();
    }
    else {
        discordPresence.details = "ERR:Unknown state!";
        discordPresence.state = "Please complain to the mod developer.";
    }
   
    //image keys 
    if (state != "init") {
        if (showVehicle) {
            if (IsDefaultVehicle(currentVehicle)) {
                discordPresence.smallImageKey = currentVehicle.c_str();
            }
            else {
                discordPresence.smallImageKey = "unknown";
            }
        }

        if (IsDefaultLevel(currentMap)) {
            discordPresence.largeImageKey = currentMap.c_str();
        }
        else {
            discordPresence.largeImageKey = "unknown";
        }

        //names
        if (!currentMapname.empty())
            discordPresence.largeImageText = currentMapname.c_str();
        if (!currentVehiclename.empty() && showVehicle)
            discordPresence.smallImageText = currentVehiclename.c_str();
    }

    //timer
    if (timerType > 0) {
        //countdown
        if(debugMode)
            printf("DEBUG:A timer is specified\n");

        if (timerType == 1) {
            discordPresence.endTimestamp = lastTime;
                
            if(debugMode)
                printf("DEBUG:TYPE=COUNTDOWN\n");
        }
        else if (timerType == 2) {
            discordPresence.startTimestamp = lastTime;
            
            if(debugMode)
                printf("DEBUG:TYPE=COUNTUP\n");
        }
    }

    //finally, update Discord
    Discord_UpdatePresence(&discordPresence);
}

void handleDiscordReady(const DiscordUser* connectedUser) {
    printf("Discord is ready!\n");
    UpdatePresence();
}

void handleDiscordError(int errorCode, const char* message) {
    printf("An error occurred in Discord RPC\n");
    printf("Error code: %d \n", errorCode);
    printf("Message: %s \n", message);
}

void InitDiscord()
{
    printf("InitDiscord() called\n");

    DiscordEventHandlers handlers;
    memset(&handlers, 0, sizeof(handlers));
    handlers.ready = handleDiscordReady;
    handlers.errored = handleDiscordError;
    handlers.disconnected = handleDiscordError;
    handlers.joinGame = NULL;
    handlers.spectateGame = NULL;
    handlers.joinRequest = NULL;
    Discord_Initialize("395028084962623489", &handlers, 1, "284160");
}

#define BUFLEN 512  //Max length of buffer
#define PORT 8888   //The port on which to listen for incoming data
#pragma comment(lib,"ws2_32.lib") //Winsock Library

SOCKET beamServer;
char buf[BUFLEN];

void InitServer() 
{
    WSADATA wsa;
    struct sockaddr_in server;

    //Initialise winsock
    printf("\nInitializing Winsock...\n");
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        printf("Failed. Error Code : %d", WSAGetLastError());
        exit(EXIT_FAILURE);
    }
    printf("Winsock Initialized.\n");

    //Create a socket
    if ((beamServer = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET)
    {
        printf("Could not create socket : %d", WSAGetLastError());
    }
    printf("Socket created.\n");

    //Prepare the sockaddr_in structure
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(PORT);

    //Bind
    if (bind(beamServer, (struct sockaddr *)&server, sizeof(server)) == SOCKET_ERROR)
    {
        printf("Bind failed with error code : %d", WSAGetLastError());
        exit(EXIT_FAILURE);
    }
    printf("Socket bind complete\n");

    //set options
    int iTimeout = 1500;
    int sockOptRes = setsockopt(beamServer, SOL_SOCKET, SO_RCVTIMEO, (const char*)&iTimeout, sizeof iTimeout);

    if (sockOptRes < 0) {
        printf("ERROR: Could not set timeout option for socket\n");
    }
}

vector<string> split(const string &str, const string &delim)
{
    const auto delim_pos = str.find(delim);

    if (delim_pos == string::npos)
        return { str };

    vector<string> ret{ str.substr(0, delim_pos) };
    auto tail = split(str.substr(delim_pos + delim.size(), string::npos), delim);

    ret.insert(ret.end(), tail.begin(), tail.end());

    return ret;
}

void UpdateServer() {
    struct sockaddr_in si_other;
    int slen , recv_len;
    slen = sizeof(si_other);

    memset(buf, '\0', BUFLEN);
    if ((recv_len = recvfrom(beamServer, buf, BUFLEN, 0, (struct sockaddr *) &si_other, &slen)) == SOCKET_ERROR)
    {
        int errType = WSAGetLastError();
        if (errType == 10060) //TIMEOUT
            return;

        printf("recvfrom() failed with error code : %d", errType);
        exit(EXIT_FAILURE);
    }
}

bool IsProcessRunning(const wchar_t *processName)
{
    bool exists = false;
    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(PROCESSENTRY32);

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);

    if (Process32First(snapshot, &entry)) {
        while (Process32Next(snapshot, &entry)) {
            if (!wcsicmp(entry.szExeFile, processName)) {
                exists = true;
                break;
            }
        }
    }

    CloseHandle(snapshot);
    return exists;
}

std::string GetLastMessage() {
    return std::string(buf);
}

inline bool IsBeamNGRunning() {
    return IsProcessRunning(L"BeamNG.drive.x64.exe") || IsProcessRunning(L"BeamNG.drive.x86.exe");
}

int main(int argc, char* argv[])
{
    SetConsoleTitle(L"BeamNG - Discord Rich Presence Helper [by Dummiesman]");
    printf("BeamNG Discord Rich Presence mod\n");
    printf("Please make sure you have read the documentation included with the application\n");
    printf("This is a 3rd party application and is not associated with, or created by BeamNG\n");
    printf("--------------------------------------------------------------------------------\n");

    //check if beamng is  already running
    //this means the user started the app in reverse order!!
    if (IsBeamNGRunning()) {
        printf("ERROR: Please start this application BEFORE running BeamNG.drive!");
        exit(EXIT_FAILURE);
    }

    //parse args
    for (int i = 0; i < argc; ++i) {
        if (!strcmpi(argv[i], "-debug")) {
            debugMode = true;
        }
    }

    //
    if(debugMode)
        printf("! Debug mode active !\n");

    //wait for beamng first
    printf("Waiting for BeamNG process...\n");
    while (true) {
        if (IsBeamNGRunning()) {
            break;
        }
        Sleep(13); //reduce cpu workload
    }

    printf("Found BeamNG! Initializing application...\n");

    //init stuff
    InitServer();
    InitDiscord();

    //main application loop
    while (true) {
        //Check if BeamNG running. If we missed the `quit` message
        //this will break the loop and cleanly exit Discord RPC's thread
        if (!IsBeamNGRunning()) {
            printf("Warning:BeamNG not found!\n");
            break;
        }

        //update server
        UpdateServer();

        //process messages
        auto message = GetLastMessage();
        if (message.empty())
            continue; // no use in trying to debug / process empty messages

        auto splits = split(message, "|");
        if(debugMode)
            printf("message debug: %s \n", message.c_str());


        if (message == "quit") {
            break;
        }
        else if (message == "init") {
            UpdatePresence();
        }
        else if (message == "update") {
            UpdatePresence();
        }
        else if (message == "showvehicle") {
            showVehicle = true;
        }
        else if (message == "hidevehicle") {
            showVehicle = false;
        }
        else if (message == "synctimer") {
            lastTime = time(NULL);
        }

        if (splits.size() >= 2) {
            auto cmd = splits[0];
            auto val = splits[1];

            if (cmd == "vehicle") {
                currentVehicle = val;
            }
            else if (cmd == "level") {
                currentMap = val;
            }
            else if (cmd == "levelname") {
                currentMapname = val;
            }
            else if (cmd == "vehiclename") {
                currentVehiclename = val;
            }
            else if (cmd == "state") {
                state = val;
            }
            else if (cmd == "exif") {
                exif = val;
            }
            else if (cmd == "timertype") {
                timerType = atoi(val.c_str());
            }
            else if (cmd == "addtime") {
                lastTime += atoi(val.c_str());
            }
        }

    }

    printf("Shutting down socket...\n");
    closesocket(beamServer);
    printf("Shutting down Discord RPC...\n");
    Discord_Shutdown();
    printf("Goodbye!\n");
    return 0;
}

