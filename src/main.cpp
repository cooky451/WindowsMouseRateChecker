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
	static const std::uint32_t BACKGROUND_COLOR = 0x0003030F;

	std::deque<MouseData> mouseData;
	std::uint64_t highFrequencyPackets = 0;

	FontPtr consolasFont;
	std::unique_ptr<MemoryCanvas> backBuffer;

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
	{}	break;

	case WM_CREATE:
	{
		windowData = new WindowData;
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(windowData));

		windowData->consolasFont.reset(CreateFontW(16, 0, 0, 0, FW_NORMAL, 0, 0, 0, 
			ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FF_DONTCARE, L"Consolas"));

		SetTimer(hwnd, 0, 100, nullptr);
	}	return 0;

	case WM_CLOSE:
	{
		delete windowData;
		DestroyWindow(hwnd);
		PostQuitMessage(0);
	}	return 0;

	case WM_TIMER:
	{
		if (windowData->isWindowInvalidated)
		{
			InvalidateRect(hwnd, nullptr, true);
		}
	}	return 0;

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
	}	return 0;

	case WM_PAINT:
	{
		if (windowData->isWindowInvalidated)
		{
			windowData->isWindowInvalidated = false;

			RECT clientRect;
			GetClientRect(hwnd, &clientRect);
			auto clientWidth = std::uint16_t(clientRect.right - clientRect.left);
			auto clientHeight = std::uint16_t(clientRect.bottom - clientRect.top);

			if (windowData->backBuffer == nullptr)
			{
				windowData->backBuffer = std::make_unique<MemoryCanvas>(GetDC(hwnd), clientWidth, clientHeight);
				windowData->backBuffer->select(windowData->consolasFont.get());
			}

			auto backBufferContext = windowData->backBuffer->deviceContext();

			const auto nElementsToFill = std::size_t(windowData->backBuffer->width() * windowData->backBuffer->height());
			std::fill_n(windowData->backBuffer->memoryPtr(), nElementsToFill, WindowData::BACKGROUND_COLOR); // Clear background

			SetBkColor(backBufferContext, RGB(3, 3, 15));
			SetTextColor(backBufferContext, RGB(255, 255, 255));

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

			PaintLock paintLock(hwnd);
			BitBlt(paintLock.deviceContext(), 0, 0, clientWidth, clientHeight, backBufferContext, 0, 0, SRCCOPY);
		}
	}	return 0;

	case WM_ERASEBKGND:
	{}	return 1;

	}

	return DefWindowProcW(hwnd, message, wparam, lparam);
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
		windowClassEx.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		windowClassEx.hIcon = LoadIconW(nullptr, IDI_APPLICATION);;
		windowClassEx.hIconSm = nullptr;
		windowClassEx.hInstance = hinstance;
		windowClassEx.lpfnWndProc = windowProc;
		windowClassEx.lpszClassName = windowClassName;
		windowClassEx.lpszMenuName = nullptr;
		windowClassEx.style = CS_OWNDC;

		if (!RegisterClassExW(&windowClassEx))
		{
			throw WindowsError("RegisterClassEx");
		}

		auto hwnd = CreateWindowExW(0, windowClassName, windowTitle, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
			CW_USEDEFAULT, CW_USEDEFAULT, 240, 480, nullptr, nullptr, hinstance, nullptr);

		if (hwnd == nullptr)
		{
			throw WindowsError("CreateWindowEx");
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
			throw WindowsError("RegisterRawInputDevices");
		}

		ShowWindow(hwnd, show);
		UpdateWindow(hwnd);

		MSG message;

		while (GetMessageW(&message, nullptr, 0, 0) > 0)
		{
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}

		return static_cast<int>(message.wParam);
	}
	catch (std::exception& e)
	{
		showMessageBox("Fatal Error", e.what());
	}
	catch (...)
	{
		showMessageBox("Fatal Error", "Fatal Error");
	}

	return 0;
}
