#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <vector>
#include <mutex>
#include <thread>
#include <string>

#pragma comment(lib, "ws2_32.lib")

// 패킷 타입 정의 (서버와 동일하게)
enum class PacketType : uint16_t {
    None = 0,
    GameStart = 1,
    PlaceStone = 2,
    PlaceStoneResult = 3,
    GameEnd = 4,
    Chat = 5,
};

// 패킷 헤더
struct PacketHeader {
    uint16_t size;
    PacketType type;
};

// 게임 시작 패킷
struct GameStartPacket {
    PacketHeader header;
    int myStone;         // 1: 흑돌, 2: 백돌
    int opponentId;
};

// 돌 놓기 요청 패킷
struct PlaceStonePacket {
    PacketHeader header;
    int row;
    int col;
};

// 돌 놓기 결과 패킷
struct PlaceStoneResultPacket {
    PacketHeader header;
    int row;
    int col;
    int stone;           // 1 or 2
    int nextTurn;        // 다음 턴 (1 or 2)
};

// 게임 종료 패킷
struct GameEndPacket {
    PacketHeader header;
    int winner;          // 0: 무승부, 1: 흑 승, 2: 백 승
    int reason;          // 0: 오목, 1: 항복, 2: 시간 초과
};

// 채팅 패킷
struct ChatPacket {
    PacketHeader header;
    wchar_t message[256];
};

// 게임 상태
struct GameState {
    std::mutex mtx;
    int board[19][19] = {};
    int myStone = 0;
    int currentTurn = 1;
    bool isGameStarted = false;
    bool isGameOver = false;
    std::wstring statusMessage = L"서버 연결 대기 중...";
};

// 전역 변수
extern GameState g_gameState;
extern SOCKET g_socket;
extern std::mutex g_sendMutex;

// 함수 선언
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void OnCreate(HWND hwnd);
void OnPaint(HWND hwnd);
void OnLButtonDown(HWND hwnd, int x, int y);
void OnDestroy(HWND hwnd);

void DrawBoard(HDC hdc);
void DrawStones(HDC hdc);
void DrawStatus(HDC hdc, const RECT& clientRect);

bool ScreenToBoard(int screenX, int screenY, int& row, int& col);

bool ConnectToServer(const char* serverIP, int port);
void NetworkThreadFunc(HWND hwnd);
void ProcessPacket(HWND hwnd, const char* data, int size);

void OnGameStart(HWND hwnd, GameStartPacket* packet);
void OnPlaceStoneResult(HWND hwnd, PlaceStoneResultPacket* packet);
void OnGameEnd(HWND hwnd, GameEndPacket* packet);

bool SendPacket(const void* data, int size);
void SendPlaceStonePacket(int row, int col);
