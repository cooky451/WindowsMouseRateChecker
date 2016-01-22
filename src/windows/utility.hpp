#pragma once

#include "base.hpp"
#include "utf.hpp"
#include "error.hpp"

namespace
{
	struct GlobalMemoryDeleter
	{
		void operator () (HGLOBAL globalMemory)
		{
			GlobalFree(globalMemory);
		}
	};

	struct GDIObjectDeleter
	{
		void operator () (HGDIOBJ object)
		{
			DeleteObject(object);
		}
	};

	typedef std::unique_ptr<std::remove_pointer<HGLOBAL>::type, GlobalMemoryDeleter> GlobalMemoryPtr;
	typedef std::unique_ptr<std::remove_pointer<HBRUSH>::type, GDIObjectDeleter> BrushPtr;
	typedef std::unique_ptr<std::remove_pointer<HFONT>::type, GDIObjectDeleter> FontPtr;
	typedef std::unique_ptr<std::remove_pointer<HDC>::type, GDIObjectDeleter> DeviceContextPtr;
	typedef std::unique_ptr<std::remove_pointer<HBITMAP>::type, GDIObjectDeleter> BitmapPtr;

	class GlobalMemoryLock
	{
		HGLOBAL _globalMemory;

	public:
		void* const ptr;

		GlobalMemoryLock(HGLOBAL globalMemory)
			: _globalMemory(globalMemory)
			, ptr(GlobalLock(globalMemory))
		{
			if (ptr == nullptr)
			{
				throw WindowsError("GlobalLock");
			}
		}

		~GlobalMemoryLock()
		{
			GlobalUnlock(_globalMemory);
		}
	};

	class ClipboardLock
	{
	public:
		ClipboardLock()
		{
			OpenClipboard(nullptr);
		}

		~ClipboardLock()
		{
			CloseClipboard();
		}
	};

	struct PaintLock
	{
		HWND hwnd;
		PAINTSTRUCT ps;
		HDC hdc;

		PaintLock(HWND hwnd)
			: hwnd(hwnd)
		{
			hdc = BeginPaint(hwnd, &ps);
		}

		~PaintLock()
		{
			EndPaint(hwnd, &ps);
		}
	};

	struct SelectionLock
	{
		HDC hdc;
		HGDIOBJ oldObject;

		SelectionLock(HDC hdc, HGDIOBJ newObject)
			: hdc(hdc)
			, oldObject(SelectObject(hdc, newObject))
		{}

		~SelectionLock()
		{
			SelectObject(hdc, oldObject);
		}
	};

	void showMessageBox(const char* title, const char* text, HWND owner = nullptr)
	{
		MessageBoxW(owner, toWideString(text).c_str(), toWideString(title).c_str(), MB_OK);
	}

	std::string getWindowText(HWND hwnd)
	{
		wchar_t windowText[0x1000];
		auto windowTextSize = GetWindowTextW(hwnd, windowText, sizeof windowText / sizeof *windowText);
		return toUtf8(windowText, windowTextSize);
	}

	void setWindowText(HWND hwnd, const std::string& windowText)
	{
		SetWindowTextW(hwnd, toWideString(windowText).c_str());
	}

	void copyToClipboard(const std::string& clipboardString)
	{
		auto wideString = toWideString(clipboardString);
		auto wideStringBytes = (wideString.size() + 1) * sizeof wideString[0];
		auto globalMemory = GlobalMemoryPtr(GlobalAlloc(GMEM_MOVEABLE, wideStringBytes));

		if (globalMemory == nullptr)
		{
			throw WindowsError("GlobalAlloc");
		}

		{
			GlobalMemoryLock memoryLock(globalMemory.get());
			std::memcpy(memoryLock.ptr, &wideString[0], wideStringBytes);
		}

		ClipboardLock clipboardLock;
		EmptyClipboard();
		SetClipboardData(CF_UNICODETEXT, globalMemory.release());
	}

	std::string copyFromClipboard()
	{
		ClipboardLock clipboardLock;
		auto clipboardData = GetClipboardData(CF_UNICODETEXT);
		GlobalMemoryLock memoryLock(clipboardData);
		std::string clipboardString = toUtf8(static_cast<wchar_t*>(memoryLock.ptr));
		return clipboardString;
	}
}
