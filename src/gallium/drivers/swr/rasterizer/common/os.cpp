/****************************************************************************
* Copyright (C) 2017 Intel Corporation.   All Rights Reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice (including the next
* paragraph) shall be included in all copies or substantial portions of the
* Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
* IN THE SOFTWARE.
****************************************************************************/

#include "common/os.h"
#include <vector>
#include <sstream>

#if defined(_WIN32)
#include <shlobj.h>
#endif // Windows

#if defined(__APPLE__) || defined(FORCE_LINUX) || defined(__linux__) || defined(__gnu_linux__)
#include <pthread.h>
#endif // Linux



#if defined(_WIN32)
static const DWORD MS_VC_EXCEPTION = 0x406D1388;

#pragma pack(push,8)  
typedef struct tagTHREADNAME_INFO
{
    DWORD dwType; // Must be 0x1000.  
    LPCSTR szName; // Pointer to name (in user addr space).  
    DWORD dwThreadID; // Thread ID (-1=caller thread).  
    DWORD dwFlags; // Reserved for future use, must be zero.  
} THREADNAME_INFO;
#pragma pack(pop)

void LegacySetThreadName(const char* pThreadName)
{
    THREADNAME_INFO info;
    info.dwType = 0x1000;
    info.szName = pThreadName;
    info.dwThreadID = GetCurrentThreadId();
    info.dwFlags = 0;

    if (!IsDebuggerPresent())
    {
        // No debugger attached to interpret exception, no need to actually do it
        return;
    }

#pragma warning(push)  
#pragma warning(disable: 6320 6322)  
    __try {
        RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
#pragma warning(pop)  
}
#endif // _WIN32

void SWR_API SetCurrentThreadName(const char* pThreadName)
{
#if defined(_WIN32)
    // The SetThreadDescription API was brought in version 1607 of Windows 10.
    typedef HRESULT(WINAPI* PFNSetThreadDescription)(HANDLE hThread, PCWSTR lpThreadDescription);
    // The SetThreadDescription API works even if no debugger is attached.
    auto pfnSetThreadDescription =
        reinterpret_cast<PFNSetThreadDescription>(
            GetProcAddress(GetModuleHandleA("Kernel32.dll"), "SetThreadDescription"));

    if (!pfnSetThreadDescription)
    {
        // try KernelBase.dll
        pfnSetThreadDescription =
            reinterpret_cast<PFNSetThreadDescription>(
                GetProcAddress(GetModuleHandleA("KernelBase.dll"), "SetThreadDescription"));
    }

    if (pfnSetThreadDescription)
    {
        std::string utf8Name = pThreadName;
        std::wstring wideName;
        wideName.resize(utf8Name.size() + 1);
        swprintf_s(&(wideName.front()), wideName.size(), L"%S", utf8Name.c_str());
        HRESULT hr = pfnSetThreadDescription(GetCurrentThread(), wideName.c_str());
        SWR_ASSERT(SUCCEEDED(hr), "Failed to set thread name to %s", pThreadName);

        // Fall through - it seems like some debuggers only recognize the exception
    }

    // Fall back to exception based hack
    LegacySetThreadName(pThreadName);
#endif // _WIN32

#if defined(FORCE_LINUX) || defined(__linux__) || defined(__gnu_linux__)
    pthread_setname_np(pthread_self(), pThreadName);
#endif // Linux
}

static void SplitString(std::vector<std::string>& out_segments, const std::string& input, char splitToken)
{
    out_segments.clear();

    std::istringstream f(input);
    std::string s;
    while (std::getline(f, s, splitToken))
    {
        if (s.size())
        {
            out_segments.push_back(s);
        }
    }
}

void SWR_API CreateDirectoryPath(const std::string& path)
{
#if defined(_WIN32)
    SHCreateDirectoryExA(nullptr, path.c_str(), nullptr);
#endif // Windows

#if defined(__APPLE__) || defined(FORCE_LINUX) || defined(__linux__) || defined(__gnu_linux__)
    std::vector<std::string> pathSegments;
    SplitString(pathSegments, path, '/');

    std::string tmpPath;
    for (auto const& segment : pathSegments)
    {
        tmpPath.push_back('/');
        tmpPath += segment;

        int result = mkdir(tmpPath.c_str(), 0777);
        if (result == -1 && errno != EEXIST)
        {
            break;
        }
    }
#endif // Unix
}
