#pragma once

#include <Windows.h>
#include <string>

class WindowsMutex
{
public:
	WindowsMutex(const std::string& name)
		: m_name(name)
	{
		m_handle = CreateMutex(NULL, TRUE, name.c_str());
		m_success = (GetLastError() != ERROR_ALREADY_EXISTS);
	}
	WindowsMutex() = delete;
	WindowsMutex(const WindowsMutex&) = delete;
	WindowsMutex(WindowsMutex&&) = delete;
	WindowsMutex& operator=(const WindowsMutex&) = delete;
	~WindowsMutex()
	{
		ReleaseMutex(m_handle);
		CloseHandle(m_handle);
	}
public:
	operator bool() const
	{
		return m_success;
	}
private:
	HANDLE m_handle;
	bool m_success;
	std::string m_name;
};