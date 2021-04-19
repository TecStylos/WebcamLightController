#include <iostream>
#include <thread>
#include <mutex>

#include <WinSock2.h>
#include <Windows.h>
#include <CommCtrl.h>

#include <boost/asio.hpp>
#include <boost/array.hpp>

#include "../resource.h"

#include "WindowsMutex.h"

#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#pragma comment(lib, "Comctl32.lib")

enum ICON_ENUM
{
	ICON_ON,
	ICON_OFF,
	ICON_NO_CON
} currIcon;

enum class LightState
{
	none, no_con, on, off
};

#define TRAY_ICON_ID 1111
#define WM_TRAY_ICON (WM_APP + 1)
#define ID_ICON_MENU_CLOSE (WM_APP + 2)

constexpr int MSG_LIGHT_ON = 1;
constexpr int MSG_LIGHT_OFF = 2;
constexpr int MSG_LIGHT_STATE = 3;

HICON icons[3];
HMENU hMenu;
NOTIFYICONDATA iconData;
std::thread conManThread;
bool conManRunning;

boost::asio::io_service ioService;
boost::asio::ip::tcp::socket webcamSocket(ioService);
std::mutex socketMutex;

int main();
LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void showContextMenu(HWND hWnd);
void loadIcons(HINSTANCE hInstance);
void freeIcons();
void initMenu();
void initIcon(HWND hWnd);
void modifyIcon();
void setIcon(ICON_ENUM icon);
void setInfo(const std::string& info);
void destroyIcon();
HWND createWindow(HINSTANCE hInstance);
void destroyWindow(HWND hWnd, HINSTANCE hInstance);
void turnLightOn();
void turnLightOff();
LightState getLightState();
bool connectToWebcam();
void disconnectFromWebcam();
bool sendData(const char* buff, size_t nBytes);
bool recvData(char* buff, size_t nBytes);
void connectionManager();
void startConMan();
void stopConMan();
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow);

int main()
{
	return WinMain(GetModuleHandle(NULL), NULL, GetCommandLineA(), SW_SHOW);
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_TRAY_ICON:
		switch (lParam)
		{
		case WM_LBUTTONDOWN:
			switch (currIcon)
			{
			case ICON_OFF:
				turnLightOn();
				break;
			case ICON_ON:
				turnLightOff();
				break;
			case ICON_NO_CON:
				turnLightOff();
				break;
			}
			break;
		case WM_RBUTTONDOWN:
		case WM_CONTEXTMENU:
			showContextMenu(hWnd);
			break;
		}
		break;
	case WM_COMMAND:
	{
		int wmID = LOWORD(wParam);
		int wmEvent = HIWORD(wParam);

		switch (wmID)
		{
		case ID_ICON_MENU_CLOSE:
			PostQuitMessage(0);
			break;
		}
		break;
	}
	}
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

void showContextMenu(HWND hWnd)
{
	POINT curPoint;
	GetCursorPos(&curPoint);

	SetForegroundWindow(hWnd);

	UINT clicked = TrackPopupMenu(
		hMenu, 0,
		curPoint.x,
		curPoint.y,
		0, hWnd,
		NULL
	);
}

void loadIcons(HINSTANCE hInstance)
{
	LoadIconMetric(
		hInstance,
		MAKEINTRESOURCEW(IDI_ICON1),
		LIM_SMALL,
		&icons[ICON_ON]
	);
	LoadIconMetric(
		hInstance,
		MAKEINTRESOURCEW(IDI_ICON2),
		LIM_SMALL,
		&icons[ICON_OFF]
	);
	LoadIconMetric(
		hInstance,
		MAKEINTRESOURCEW(IDI_ICON3),
		LIM_SMALL,
		&icons[ICON_NO_CON]
	);
}

void freeIcons()
{
	for (int i = 0; i < 3; ++i)
	{
		DestroyIcon(icons[i]);
	}
}

void initMenu()
{
	hMenu = CreatePopupMenu();
	AppendMenu(hMenu, MF_STRING, ID_ICON_MENU_CLOSE, "Close");
}

void initIcon(HWND hWnd)
{
	currIcon = ICON_NO_CON;

	iconData.cbSize = sizeof(NOTIFYICONDATA);
	iconData.hWnd = hWnd;
	iconData.uID = TRAY_ICON_ID;
	iconData.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
	iconData.uCallbackMessage = WM_TRAY_ICON;
	iconData.hIcon = icons[currIcon];
	strcpy_s(iconData.szTip, "Webcam light controller.");
	iconData.dwState = NIS_SHAREDICON;
	iconData.dwStateMask = NIS_SHAREDICON;
	iconData.uVersion = NOTIFYICON_VERSION_4;
	iconData.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND | NIIF_LARGE_ICON;

	Shell_NotifyIcon(NIM_SETVERSION, &iconData);
	Shell_NotifyIcon(NIM_ADD, &iconData);
}

void modifyIcon()
{
	Shell_NotifyIcon(NIM_MODIFY, &iconData);
}

void setIcon(ICON_ENUM icon)
{
	currIcon = icon;
	iconData.hIcon = icons[currIcon];
	modifyIcon();
}

void setInfo(const std::string& info)
{
	std::cout << info << std::endl;
	strcpy_s(iconData.szTip, info.c_str());
	modifyIcon();
}

void destroyIcon()
{
	Shell_NotifyIcon(NIM_DELETE, &iconData);
}

HWND createWindow(HINSTANCE hInstance)
{
	WNDCLASS wc = {};
	wc.lpfnWndProc = WindowProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = "WLCWindowClass";
	auto ret = RegisterClass(&wc);

	HWND hWnd = CreateWindowEx(
		0,
		wc.lpszClassName,
		"WLCWindow",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		NULL, NULL,
		hInstance, NULL
	);

	return hWnd;
}

void destroyWindow(HWND hWnd, HINSTANCE hInstance)
{
	DestroyWindow(hWnd);

	UnregisterClass("WLCWindowClass", hInstance);
}

void turnLightOn()
{
	std::lock_guard lock(socketMutex);

	setInfo("Turning light on...");

	if (sendData((const char*)&MSG_LIGHT_ON, 4))
	{
		setIcon(ICON_ON);
		setInfo("Webcam light is on.");
	}
	else
	{
		setInfo("Unable to toggle light.");
	}
}

void turnLightOff()
{
	std::lock_guard lock(socketMutex);

	setInfo("Turning light off...");

	if (sendData((const char*)&MSG_LIGHT_OFF, 4))
	{
		setIcon(ICON_OFF);
		setInfo("Webcam light is off.");
	}
	else
	{
		setInfo("Unable to toggle light.");
	}
}

LightState getLightState()
{
	std::lock_guard lock(socketMutex);

	if (sendData((const char*)&MSG_LIGHT_STATE, 4))
	{
		int state;
		if (recvData((char*)&state, 4))
		{
			switch (state)
			{
			case MSG_LIGHT_ON:
				return LightState::on;
			case MSG_LIGHT_OFF:
				return LightState::off;
			}
		}
	}

	return LightState::no_con;
}

bool connectToWebcam()
{
	std::lock_guard lock(socketMutex);

	setInfo("Connecting to webcam...");

	boost::asio::ip::tcp::resolver::query query("raspi-webcam.local", "1000");
	boost::asio::ip::tcp::resolver resolver(ioService);

	boost::asio::ip::tcp::resolver::iterator endpointIterator = resolver.resolve(query);
	boost::asio::ip::tcp::resolver::iterator end;
	boost::system::error_code error = boost::asio::error::host_not_found;
	while (error && endpointIterator != end)
	{
		webcamSocket.close();
		webcamSocket.connect(*endpointIterator++, error);
	}

	if (error)
		setInfo("Unable to connect to webcam.");
	else
		setInfo("Webcam connected.");

	return !error;
}

void disconnectFromWebcam()
{
	std::lock_guard lock(socketMutex);

	setInfo("Disconnecting from webcam...");

	webcamSocket.close();

	setInfo("Webcam not connected.");
}

bool sendData(const char* buff, size_t nBytes)
{
	boost::system::error_code error;
	boost::asio::write(webcamSocket, boost::asio::buffer(buff, nBytes), error);

	return !error;
}
bool recvData(char* buff, size_t nBytes)
{
	boost::system::error_code error;
	boost::asio::read(webcamSocket, boost::asio::buffer(buff, nBytes), error);

	return !error;
}

void connectionManager()
{
	LightState lastState = LightState::none;

	while (conManRunning)
	{
		auto lightState = getLightState();

		if (lightState != lastState)
		{
			switch (lightState)
			{
			case LightState::no_con:
				setIcon(ICON_NO_CON);
				break;
			case LightState::on:
				setIcon(ICON_ON);
				break;
			case LightState::off:
				setIcon(ICON_OFF);
				break;
			}

			lastState = lightState;
		}
		else
		{
			std::this_thread::sleep_for(std::chrono::seconds(5));
		}

		if (lightState == LightState::no_con)
			connectToWebcam();
	}
}

void startConMan()
{
	conManRunning = true;
	conManThread = std::thread(connectionManager);
}

void stopConMan()
{
	conManRunning = false;
	conManThread.join();
}

int WINAPI WinMain(
	HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPSTR lpCmdLine,
	int nShowCmd)
{
	WindowsMutex winMutex("WebcamLightControllerMutex");
	if (!winMutex)
	{
		MessageBox(NULL, "WebcamLightController is already running.", "Cannot run", MB_ICONEXCLAMATION | MB_OK);
		exit(1);
	}

	loadIcons(hInstance);

	HWND hWnd = createWindow(hInstance);

	initMenu();
	initIcon(hWnd);

	startConMan();

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	setInfo("Closing webcam light controller...");

	stopConMan();

	destroyIcon();

	destroyWindow(hWnd, hInstance);

	freeIcons();
}