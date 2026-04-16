#include "OmokClient.h"

// 전역 변수 정의
GameState g_gameState;
SOCKET g_socket = INVALID_SOCKET;
std::mutex g_sendMutex;
std::thread g_networkThread;

constexpr int BOARD_SIZE = 19;
constexpr int CELL_SIZE = 30;
constexpr int MARGIN = 40;
constexpr int STONE_RADIUS = 13;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"OmokClientClass";

    if (!RegisterClassEx(&wc)) {
        MessageBox(nullptr, L"윈도우 클래스 등록 실패", L"Error", MB_OK);
        return 0;
    }

    int windowWidth = CELL_SIZE * (BOARD_SIZE - 1) + MARGIN * 2;
    int windowHeight = CELL_SIZE * (BOARD_SIZE - 1) + MARGIN * 2 + 100;

    HWND hwnd = CreateWindowEx(
        0, L"OmokClientClass", L"오목 게임 클라이언트",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        windowWidth, windowHeight, nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) {
        MessageBox(nullptr, L"윈도우 생성 실패", L"Error", MB_OK);
        return 0;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
        OnCreate(hwnd);
        return 0;
    case WM_PAINT:
        OnPaint(hwnd);
        return 0;
    case WM_LBUTTONDOWN:
        OnLButtonDown(hwnd, LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_DESTROY:
        OnDestroy(hwnd);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void OnCreate(HWND hwnd)
{
    // 네트워크 스레드 시작
    g_networkThread = std::thread(NetworkThreadFunc, hwnd);
}

void OnPaint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT clientRect;
    GetClientRect(hwnd, &clientRect);

    // 더블 버퍼링을 위한 메모리 DC 생성
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, clientRect.right, clientRect.bottom);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

    // 배경 칠하기
    HBRUSH bgBrush = CreateSolidBrush(RGB(220, 179, 92));
    FillRect(memDC, &clientRect, bgBrush);
    DeleteObject(bgBrush);

    // 오목판 그리기
    DrawBoard(memDC);

    // 돌 그리기
    DrawStones(memDC);

    // 상태 텍스트 그리기
    DrawStatus(memDC, clientRect);

    // 메모리 DC를 화면으로 복사
    BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);

    EndPaint(hwnd, &ps);
}

void DrawBoard(HDC hdc)
{
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);

    // 가로선
    for (int i = 0; i < BOARD_SIZE; ++i) {
        int y = MARGIN + i * CELL_SIZE;
        MoveToEx(hdc, MARGIN, y, nullptr);
        LineTo(hdc, MARGIN + (BOARD_SIZE - 1) * CELL_SIZE, y);
    }

    // 세로선
    for (int i = 0; i < BOARD_SIZE; ++i) {
        int x = MARGIN + i * CELL_SIZE;
        MoveToEx(hdc, x, MARGIN, nullptr);
        LineTo(hdc, x, MARGIN + (BOARD_SIZE - 1) * CELL_SIZE);
    }

    // 화점 표시
    HBRUSH dotBrush = CreateSolidBrush(RGB(0, 0, 0));
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, dotBrush);

    int starPoints[] = { 3, 9, 15 };
    for (int i : starPoints) {
        for (int j : starPoints) {
            int x = MARGIN + j * CELL_SIZE;
            int y = MARGIN + i * CELL_SIZE;
            Ellipse(hdc, x - 3, y - 3, x + 3, y + 3);
        }
    }

    SelectObject(hdc, oldBrush);
    DeleteObject(dotBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void DrawStones(HDC hdc)
{
    std::lock_guard<std::mutex> lock(g_gameState.mtx);

    for (int row = 0; row < BOARD_SIZE; ++row) {
        for (int col = 0; col < BOARD_SIZE; ++col) {
            int stone = g_gameState.board[row][col];
            if (stone == 0) continue;

            int x = MARGIN + col * CELL_SIZE;
            int y = MARGIN + row * CELL_SIZE;

            HBRUSH brush = CreateSolidBrush(stone == 1 ? RGB(0, 0, 0) : RGB(255, 255, 255));
            HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, brush);

            HPEN pen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
            HPEN oldPen = (HPEN)SelectObject(hdc, pen);

            Ellipse(hdc, x - STONE_RADIUS, y - STONE_RADIUS,
                         x + STONE_RADIUS, y + STONE_RADIUS);

            SelectObject(hdc, oldPen);
            DeleteObject(pen);
            SelectObject(hdc, oldBrush);
            DeleteObject(brush);
        }
    }
}

void DrawStatus(HDC hdc, const RECT& clientRect)
{
    std::lock_guard<std::mutex> lock(g_gameState.mtx);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(0, 0, 0));

    RECT textRect = clientRect;
    textRect.top = clientRect.bottom - 80;
    textRect.bottom = clientRect.bottom - 20;

    HFONT font = CreateFont(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            DEFAULT_QUALITY, DEFAULT_PITCH, L"맑은 고딕");
    HFONT oldFont = (HFONT)SelectObject(hdc, font);

    DrawText(hdc, g_gameState.statusMessage.c_str(), -1, &textRect,
             DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, oldFont);
    DeleteObject(font);
}

void OnLButtonDown(HWND hwnd, int x, int y)
{
    int row, col;
    if (!ScreenToBoard(x, y, row, col)) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_gameState.mtx);

    if (!g_gameState.isGameStarted || g_gameState.isGameOver) {
        MessageBox(hwnd, L"게임이 진행 중이 아닙니다.", L"알림", MB_OK);
        return;
    }

    if (g_gameState.currentTurn != g_gameState.myStone) {
        MessageBox(hwnd, L"상대방의 턴입니다.", L"알림", MB_OK);
        return;
    }

    if (g_gameState.board[row][col] != 0) {
        MessageBox(hwnd, L"이미 돌이 놓여 있습니다.", L"알림", MB_OK);
        return;
    }

    SendPlaceStonePacket(row, col);
}

void OnDestroy(HWND hwnd)
{
    if (g_socket != INVALID_SOCKET) {
        closesocket(g_socket);
    }

    if (g_networkThread.joinable()) {
        g_networkThread.join();
    }

    WSACleanup();
}

bool ScreenToBoard(int screenX, int screenY, int& row, int& col)
{
    int relX = screenX - MARGIN;
    int relY = screenY - MARGIN;

    col = (relX + CELL_SIZE / 2) / CELL_SIZE;
    row = (relY + CELL_SIZE / 2) / CELL_SIZE;

    if (row < 0 || row >= BOARD_SIZE || col < 0 || col >= BOARD_SIZE) {
        return false;
    }

    return true;
}

bool ConnectToServer(const char* serverIP, int port)
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return false;
    }

    g_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_socket == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }

    sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(static_cast<u_short>(port));
    inet_pton(AF_INET, serverIP, &serverAddr.sin_addr);

    if (connect(g_socket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        closesocket(g_socket);
        g_socket = INVALID_SOCKET;
        WSACleanup();
        return false;
    }

    return true;
}

void NetworkThreadFunc(HWND hwnd)
{
    if (!ConnectToServer("127.0.0.1", 9000)) {
        MessageBox(hwnd, L"서버 연결 실패", L"Error", MB_OK);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_gameState.mtx);
        g_gameState.statusMessage = L"서버 연결 완료. 매칭 대기 중...";
    }
    InvalidateRect(hwnd, nullptr, FALSE);

    std::vector<char> recvBuffer;
    recvBuffer.reserve(4096);

    char tempBuffer[4096];
    while (true) {
        int bytesReceived = recv(g_socket, tempBuffer, sizeof(tempBuffer), 0);
        if (bytesReceived <= 0) {
            break;
        }

        recvBuffer.insert(recvBuffer.end(), tempBuffer, tempBuffer + bytesReceived);

        while (recvBuffer.size() >= sizeof(PacketHeader)) {
            PacketHeader* header = (PacketHeader*)recvBuffer.data();
            if (recvBuffer.size() < header->size) {
                break;
            }

            ProcessPacket(hwnd, recvBuffer.data(), header->size);

            recvBuffer.erase(recvBuffer.begin(), recvBuffer.begin() + header->size);
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_gameState.mtx);
        g_gameState.statusMessage = L"서버 연결 종료";
    }
    InvalidateRect(hwnd, nullptr, FALSE);

    closesocket(g_socket);
    g_socket = INVALID_SOCKET;
}

void ProcessPacket(HWND hwnd, const char* data, int size)
{
    PacketHeader* header = (PacketHeader*)data;

    switch (header->type) {
    case PacketType::GameStart:
        OnGameStart(hwnd, (GameStartPacket*)data);
        break;
    case PacketType::PlaceStoneResult:
        OnPlaceStoneResult(hwnd, (PlaceStoneResultPacket*)data);
        break;
    case PacketType::GameEnd:
        OnGameEnd(hwnd, (GameEndPacket*)data);
        break;
    }
}

void OnGameStart(HWND hwnd, GameStartPacket* packet)
{
    std::lock_guard<std::mutex> lock(g_gameState.mtx);

    g_gameState.isGameStarted = true;
    g_gameState.isGameOver = false;
    g_gameState.myStone = packet->myStone;
    g_gameState.currentTurn = 1;

    memset(g_gameState.board, 0, sizeof(g_gameState.board));

    if (g_gameState.myStone == 1) {
        g_gameState.statusMessage = L"게임 시작! 당신은 흑돌입니다. 당신의 턴입니다.";
    } else {
        g_gameState.statusMessage = L"게임 시작! 당신은 백돌입니다. 상대방의 턴입니다.";
    }

    InvalidateRect(hwnd, nullptr, FALSE);
}

void OnPlaceStoneResult(HWND hwnd, PlaceStoneResultPacket* packet)
{
    std::lock_guard<std::mutex> lock(g_gameState.mtx);

    g_gameState.board[packet->row][packet->col] = packet->stone;
    g_gameState.currentTurn = packet->nextTurn;

    if (g_gameState.currentTurn == g_gameState.myStone) {
        g_gameState.statusMessage = L"당신의 턴입니다.";
    } else {
        g_gameState.statusMessage = L"상대방의 턴입니다.";
    }

    InvalidateRect(hwnd, nullptr, FALSE);
}

void OnGameEnd(HWND hwnd, GameEndPacket* packet)
{
    std::lock_guard<std::mutex> lock(g_gameState.mtx);

    g_gameState.isGameOver = true;

    if (packet->winner == g_gameState.myStone) {
        g_gameState.statusMessage = L"승리했습니다!";
    } else if (packet->winner == 0) {
        g_gameState.statusMessage = L"무승부입니다.";
    } else {
        g_gameState.statusMessage = L"패배했습니다.";
    }

    InvalidateRect(hwnd, nullptr, FALSE);

    MessageBox(hwnd, g_gameState.statusMessage.c_str(), L"게임 종료", MB_OK);
}

bool SendPacket(const void* data, int size)
{
    std::lock_guard<std::mutex> lock(g_sendMutex);

    if (g_socket == INVALID_SOCKET) {
        return false;
    }

    int totalSent = 0;
    const char* ptr = (const char*)data;

    while (totalSent < size) {
        int sent = send(g_socket, ptr + totalSent, size - totalSent, 0);
        if (sent == SOCKET_ERROR) {
            return false;
        }
        totalSent += sent;
    }

    return true;
}

void SendPlaceStonePacket(int row, int col)
{
    PlaceStonePacket packet;
    packet.header.type = PacketType::PlaceStone;
    packet.header.size = sizeof(PlaceStonePacket);
    packet.row = row;
    packet.col = col;

    SendPacket(&packet, sizeof(packet));
}
