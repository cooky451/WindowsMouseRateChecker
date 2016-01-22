/* 
 * 
 * 
 */

#include <chrono>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "windows/utility.hpp"

struct WindowPoint
{
	std::uint16_t x, y;
};

struct MouseData
{
	std::chrono::high_resolution_clock::time_point time;
	RAWINPUT input;
};

struct WindowData
{
	std::deque<MouseData> mouseData;
	std::uint64_t highFrequencyPackets = 0;
	BrushPtr blueBrush;
	FontPtr consolasFont;
	DeviceContextPtr backBufferContext;
	BitmapPtr backBuffer;

	bool isWindowInvalidated = true;
};

bool isDuplicatedPacket(const RAWINPUT& lhs, const RAWINPUT& rhs)
{
	return lhs.data.mouse.lLastX == rhs.data.mouse.lLastX
		&& lhs.data.mouse.lLastY == rhs.data.mouse.lLastY;
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
	auto windowData = reinterpret_cast<WindowData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

	switch (message)
	{
	default:
	{}	return DefWindowProcW(hwnd, message, wparam, lparam);

	case WM_CREATE:
	{
		auto createStruct = reinterpret_cast<LPCREATESTRUCT>(lparam);
		windowData = reinterpret_cast<WindowData*>(createStruct->lpCreateParams);
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(windowData));

		windowData->blueBrush.reset(CreateSolidBrush(RGB(4, 4, 16)));

		windowData->consolasFont.reset(CreateFontW(16, 0, 0, 0, FW_NORMAL, 0, 0, 0, 
			ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FF_DONTCARE, L"Consolas"));

		SetTimer(hwnd, 0, 100, nullptr);
	}	break;

	case WM_TIMER:
	{
		if (windowData->isWindowInvalidated)
		{
			InvalidateRect(hwnd, nullptr, true);
		}
	}	break;

	case WM_CLOSE:
	{
		delete windowData;
		DestroyWindow(hwnd);
		PostQuitMessage(0);
	}	break;

	case WM_INPUT:
	{
		HRAWINPUT hrawinput = reinterpret_cast<HRAWINPUT>(lparam);
		UINT size;
		GetRawInputData(hrawinput, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));

		if (size <= sizeof(RAWINPUT))
		{
			RAWINPUT input;
			GetRawInputData(hrawinput, RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER));

			if (input.header.dwType == RIM_TYPEMOUSE)
			{
				windowData->isWindowInvalidated = true;

				MouseData data{ std::chrono::high_resolution_clock::now(), input };

				if (windowData->mouseData.size() == 0)
				{
					windowData->mouseData.push_front(data);
				}
				else if (data.time - windowData->mouseData.front().time < std::chrono::microseconds(100))
				{
					windowData->highFrequencyPackets += 1;
				}
				else
				{
					windowData->mouseData.push_front(data);

					if (windowData->mouseData.size() > 1024)
					{
						windowData->mouseData.resize(1024);
					}
				}
			}
		}
		else
		{
			showMessageBox("Warning", "Raw input data too wide.");
		}
	}	break;

	case WM_PAINT:
	{
		if (windowData->isWindowInvalidated)
		{
			windowData->isWindowInvalidated = false;

			RECT clientRect;
			GetClientRect(hwnd, &clientRect);
			auto clientWidth = std::uint16_t(clientRect.right - clientRect.left);
			auto clientHeight = std::uint16_t(clientRect.top - clientRect.bottom);

			PaintLock paintLock(hwnd);

			if (windowData->backBufferContext == nullptr)
			{
				windowData->backBufferContext.reset(CreateCompatibleDC(paintLock.hdc));
				windowData->backBuffer.reset(CreateCompatibleBitmap(paintLock.hdc, clientWidth, clientHeight));
			}

			SelectionLock backBufferLock(windowData->backBufferContext.get(), windowData->backBuffer.get());

			auto backBufferContext = windowData->backBufferContext.get();

			SetBkColor(backBufferContext, RGB(4, 4, 16));
			SetTextColor(backBufferContext, RGB(255, 255, 255));

			FillRect(backBufferContext, &clientRect, windowData->blueBrush.get());

			SelectionLock fontLock(backBufferContext, windowData->consolasFont.get());

			std::string tempString;

			for (std::size_t i = 1; i < windowData->mouseData.size() && (i + 1) * 16 < clientHeight; ++i)
			{
				auto time = windowData->mouseData[i - 1].time - windowData->mouseData[i].time;
				auto hz = 1000000.0 / std::chrono::duration_cast<std::chrono::microseconds>(time).count();
				tempString = std::to_string(std::uint32_t(std::round(hz)));
				TextOutA(backBufferContext, 8, int(i * 16 + 16 - 8), tempString.c_str(), int(tempString.size()));
			}

			tempString = "Filtered packets: " + std::to_string(windowData->highFrequencyPackets);
			TextOutA(backBufferContext, 8, 8, tempString.c_str(), int(tempString.size()));

			BitBlt(paintLock.hdc, 0, 0, clientWidth, clientHeight, backBufferContext, 0, 0, SRCCOPY);
		}
	}

	case WM_ERASEBKGND:
	{}	return 1;

	}

	return 0;
}

int WINAPI wWinMain(HINSTANCE hinstance, HINSTANCE, LPWSTR, int show)
{
	try
	{
		const auto windowClassName = L"MainWindowClass";
		const auto windowTitle = L"Mouse Rate";

		WNDCLASSEXW windowClassEx = {};
		windowClassEx.cbClsExtra = 0;
		windowClassEx.cbSize = sizeof windowClassEx;
		windowClassEx.cbWndExtra = 0;
		windowClassEx.hbrBackground = HBRUSH(GetStockObject(BLACK_BRUSH));
		windowClassEx.hCursor = LoadCursor(nullptr, IDC_ARROW);
		windowClassEx.hIcon = LoadIcon(nullptr, IDI_APPLICATION);;
		windowClassEx.hIconSm = nullptr;
		windowClassEx.hInstance = hinstance;
		windowClassEx.lpfnWndProc = windowProc;
		windowClassEx.lpszClassName = windowClassName;
		windowClassEx.lpszMenuName = nullptr;
		windowClassEx.style = CS_OWNDC;

		if (!RegisterClassExW(&windowClassEx))
		{
			throw WindowsError("RegisterClassEx()");
		}

		auto hwnd = CreateWindowExW(0, windowClassName, windowTitle, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
			CW_USEDEFAULT, CW_USEDEFAULT, 256, 512, nullptr, nullptr, hinstance, new WindowData);

		if (hwnd == nullptr)
		{
			throw WindowsError("CreateWindowEx()");
		}

		RAWINPUTDEVICE devices[2] = {};
		devices[0].usUsagePage = 0x01;
		devices[0].usUsage = 0x02;
		devices[0].dwFlags = 0x00;
		devices[0].hwndTarget = hwnd;
		devices[1].usUsagePage = 0x01;
		devices[1].usUsage = 0x06;
		devices[1].dwFlags = 0x00;
		devices[1].hwndTarget = hwnd;

		if (!RegisterRawInputDevices(devices, sizeof devices / sizeof *devices, sizeof *devices))
		{
			throw WindowsError("RegisterRawInputDevices()");
		}

		ShowWindow(hwnd, show);
		UpdateWindow(hwnd);

		MSG message;

		while (GetMessageW(&message, nullptr, 0, 0) > 0)
		{
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}

		return int(message.wParam);
	}
	catch (std::exception& e)
	{
		showMessageBox("Fatal Error", e.what());
	}

	return 0;
}
