/****************************************************************************
* Copyright (C) 2014-2015 Intel Corporation.   All Rights Reserved.
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
#include <stdarg.h>
#include <stdio.h>
#include <assert.h>

#if SWR_ENABLE_ASSERTS || SWR_ENABLE_REL_ASSERTS

#if defined(_WIN32)
#pragma comment(lib, "user32.lib")
#endif // _WIN32

enum TextColor
{
    TEXT_BLACK      = 0,
    TEXT_RED        = 1,
    TEXT_GREEN      = 2,
    TEXT_BLUE       = 4,
    TEXT_PURPLE     = TEXT_RED | TEXT_BLUE,
    TEXT_CYAN       = TEXT_GREEN | TEXT_BLUE,
    TEXT_YELLOW     = TEXT_RED | TEXT_GREEN,
    TEXT_WHITE      = TEXT_RED | TEXT_GREEN | TEXT_BLUE,
};

enum TextStyle
{
    TEXT_NORMAL     = 0,
    TEXT_INTENSITY  = 1,
};

void SetTextColor(FILE* stream, TextColor color = TEXT_WHITE, TextStyle style = TEXT_NORMAL)
{
#if defined(_WIN32)

    HANDLE hConsoleHandle = nullptr;
    if (stream == stderr)
    {
        hConsoleHandle = GetStdHandle(STD_ERROR_HANDLE);
    }
    else if (stream == stdout)
    {
        hConsoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    }
    else
    {
        // Not a console stream, do nothing
        return;
    }

    WORD textAttributes = 0;
    if (color & TEXT_RED)
    {
        textAttributes |= FOREGROUND_RED;
    }
    if (color & TEXT_GREEN)
    {
        textAttributes |= FOREGROUND_GREEN;
    }
    if (color & TEXT_BLUE)
    {
        textAttributes |= FOREGROUND_BLUE;
    }
    if (style & TEXT_INTENSITY)
    {
        textAttributes |= FOREGROUND_INTENSITY;
    }
    SetConsoleTextAttribute(hConsoleHandle, textAttributes);

#else // !_WIN32

    // Print ANSI codes
    uint32_t cc = 30 + (style ? 60 : 0) + color;
    fprintf(stream, "\033[0m\033[%d;%dm", style, cc);

#endif
}

void ResetTextColor(FILE* stream)
{
#if defined(_WIN32)

    SetTextColor(stream);

#else // !_WIN32

    // Print ANSI codes
    fprintf(stream, "\033[0m");

#endif
}

bool SwrAssert(
    bool        chkDebugger,
    bool&       enabled,
    const char* pExpression,
    const char* pFileName,
    uint32_t    lineNum,
    const char* pFunction,
    const char* pFmtString /* = nullptr */,
    ...)
{
    if (!enabled) return false;

    SetTextColor(stderr, TEXT_CYAN, TEXT_NORMAL);

    fprintf(stderr, "%s(%d): ", pFileName, lineNum);

    SetTextColor(stderr, TEXT_RED, TEXT_INTENSITY);

    fprintf(stderr, "ASSERT: %s\n", pExpression);

    SetTextColor(stderr, TEXT_CYAN, TEXT_INTENSITY);
    fprintf(stderr, "\t%s\n", pFunction);

    if (pFmtString)
    {
        SetTextColor(stderr, TEXT_YELLOW, TEXT_INTENSITY);
        fprintf(stderr, "\t");
        va_list args;
        va_start(args, pFmtString);
        vfprintf(stderr, pFmtString, args);
        va_end(args);
        fprintf(stderr, "\n");
    }
    ResetTextColor(stderr);
    fflush(stderr);

#if defined(_WIN32)
    static const int MAX_MESSAGE_LEN = 2048;
    char msgBuf[MAX_MESSAGE_LEN];

    sprintf_s(msgBuf, "%s(%d): ASSERT: %s\n", pFileName, lineNum, pExpression);
    msgBuf[MAX_MESSAGE_LEN - 2] = '\n';
    msgBuf[MAX_MESSAGE_LEN - 1] = 0;
    OutputDebugStringA(msgBuf);

    sprintf_s(msgBuf, "\t%s\n", pFunction);
    msgBuf[MAX_MESSAGE_LEN - 2] = '\n';
    msgBuf[MAX_MESSAGE_LEN - 1] = 0;
    OutputDebugStringA(msgBuf);

    int offset = 0;

    if (pFmtString)
    {
        va_list args;
        va_start(args, pFmtString);
        offset = _vsnprintf_s(
            msgBuf,
            sizeof(msgBuf),
            sizeof(msgBuf),
            pFmtString,
            args);
        va_end(args);

        if (offset < 0) { return true; }

        OutputDebugStringA("\t");
        OutputDebugStringA(msgBuf);
        OutputDebugStringA("\n");
    }

    if (KNOB_ENABLE_ASSERT_DIALOGS)
    {
        int retval = sprintf_s(
            &msgBuf[offset],
            MAX_MESSAGE_LEN - offset,
            "\n\n"
            "File: %s\n"
            "Line: %d\n"
            "\n"
            "Expression: %s\n\n"
            "Cancel: Disable this assert for the remainder of the process\n"
            "Try Again: Break into the debugger\n"
            "Continue: Continue execution (but leave assert enabled)",
            pFileName,
            lineNum,
            pExpression);

        if (retval < 0) { return true; }

        offset += retval;

        if (!IsDebuggerPresent())
        {
            sprintf_s(
                &msgBuf[offset],
                MAX_MESSAGE_LEN - offset,
                "\n\n*** NO DEBUGGER DETECTED ***\n\nPressing \"Try Again\" will cause a program crash!");
        }

        retval = MessageBoxA(nullptr, msgBuf, "Assert Failed", MB_CANCELTRYCONTINUE | MB_ICONEXCLAMATION | MB_SETFOREGROUND);

        switch (retval)
        {
        case IDCANCEL:
            enabled = false;
            return false;

        case IDTRYAGAIN:
            return true;

        case IDCONTINUE:
            return false;
        }
    }
    else
    {
        return IsDebuggerPresent() || !chkDebugger;
    }
#endif // _WIN32

    return true;
}

#endif // SWR_ENABLE_ASSERTS
