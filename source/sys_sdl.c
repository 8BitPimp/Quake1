/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// sys_win.c -- Win32 system interface code

#define _CRT_SECURE_NO_WARNINGS

#define _SDL_main_h
#include <SDL.h>

#include <direct.h>

#include "quakedef.h"
#include "errno.h"
#include "resource.h"

#define MINIMUM_WIN_MEMORY 0x0880000
#define MAXIMUM_WIN_MEMORY 0x2000000 //johnfitz -- 32 mb, was 16 mb

#define CONSOLE_ERROR_TIMEOUT 60.0 // # of seconds to wait on Sys_Error running
//  dedicated before exiting
#define PAUSE_SLEEP 50 // sleep time on pause or minimization
#define NOT_FOCUS_SLEEP 20 // sleep time when not focus

int starttime;
bool ActiveApp, Minimized;

static double pfreq;
static double curtime = 0.0;
static double lastcurtime = 0.0;
static int lowshift;
bool isDedicated;
static bool sc_return_on_enter = false;
HANDLE hinput, houtput;

static char* tracking_tag = "Clams & Mooses";

//static HANDLE tevent;
//static HANDLE hFile;
//static HANDLE heventParent;
//static HANDLE heventChild;

void Sys_InitFloatTime(void);

volatile int sys_checksum;

HINSTANCE global_hInstance;
int global_nCmdShow;
char* argv[MAX_NUM_ARGVS];
static char* empty_string = "";
HWND hwnd_dialog;

/*
===============================================================================

FILE IO

===============================================================================
*/

#define MAX_HANDLES 100 //johnfitz -- was 10
FILE* sys_handles[MAX_HANDLES];

static int findhandle(void)
{
    for (int i = 1; i < MAX_HANDLES; i++)
        if (!sys_handles[i])
            return i;
    Sys_Error("out of handles");
    return -1;
}

static long filelength(FILE* f)
{
//    const int t = VID_ForceUnlockedAndReturnState();
    const long pos = ftell(f);
    fseek(f, 0, SEEK_END);
    const long end = ftell(f);
    fseek(f, pos, SEEK_SET);
//    VID_ForceLockState(t);
    return end;
}

int Sys_FileOpenRead(const char* path, int* hndl)
{
    *hndl = -1;
    int retval = -1;
//    const int t = VID_ForceUnlockedAndReturnState();
    {
        const int i = findhandle();
        FILE* f = fopen(path, "rb");
        if (f)
        {
            sys_handles[i] = f;
            *hndl = i;
            retval = filelength(f);
        }
    }
//    VID_ForceLockState(t);
    return retval;
}

int Sys_FileOpenWrite(const char* path)
{
//    const int t = VID_ForceUnlockedAndReturnState();
    const int i = findhandle();
    FILE* f = fopen(path, "wb");
    if (!f)
        Sys_Error("Error opening %s: %s", path, strerror(errno));
    sys_handles[i] = f;
//    VID_ForceLockState(t);
    return i;
}

void Sys_FileClose(int handle)
{
//    const int t = VID_ForceUnlockedAndReturnState();
    fclose(sys_handles[handle]);
    sys_handles[handle] = NULL;
//    VID_ForceLockState(t);
}

void Sys_FileSeek(int handle, int position)
{
//    const int t = VID_ForceUnlockedAndReturnState();
    fseek(sys_handles[handle], position, SEEK_SET);
//    VID_ForceLockState(t);
}

int Sys_FileRead(int handle, void* dest, int count)
{
//    const int t = VID_ForceUnlockedAndReturnState();
    const int x = fread(dest, 1, count, sys_handles[handle]);
//    VID_ForceLockState(t);
    return x;
}

int Sys_FileWrite(int handle, void* data, int count)
{
//    const int t = VID_ForceUnlockedAndReturnState();
    const int x = fwrite(data, 1, count, sys_handles[handle]);
//    VID_ForceLockState(t);
    return x;
}

int Sys_FileTime(const char* path)
{
//    const int t = VID_ForceUnlockedAndReturnState();
    FILE* f = fopen(path, "rb");
    int retval = -1;
    if (f)
    {
        fclose(f);
        retval = 1;
    }
//    VID_ForceLockState(t);
    return retval;
}

void Sys_mkdir(const char* path)
{
    _mkdir(path);
}

/*
===============================================================================

SYSTEM IO

===============================================================================
*/

/*
================
Sys_Init
================
*/
void Sys_Init(void)
{
    LARGE_INTEGER PerformanceFreq;
    unsigned int lowpart, highpart;
    OSVERSIONINFO vinfo;

    if (!QueryPerformanceFrequency(&PerformanceFreq))
        Sys_Error("No hardware timer available");

    // get 32 out of the 64 time bits such that we have around
    // 1 microsecond resolution
    lowpart = (unsigned int)PerformanceFreq.LowPart;
    highpart = (unsigned int)PerformanceFreq.HighPart;
    lowshift = 0;

    while (highpart || (lowpart > 2000000.0))
    {
        lowshift++;
        lowpart >>= 1;
        lowpart |= (highpart & 1) << 31;
        highpart >>= 1;
    }

    pfreq = 1.0 / (double)lowpart;

    Sys_InitFloatTime();

    vinfo.dwOSVersionInfoSize = sizeof(vinfo);
}

void Sys_Error(const char* error, ...)
{
    va_list argptr;
    char text[1024], text2[1024];
    char* text3 = "Press Enter to exit\n";
    char* text4 = "***********************************\n";
    char* text5 = "\n";
    DWORD dummy;
    double starttime;
    static int in_sys_error0 = 0;
    static int in_sys_error1 = 0;
    static int in_sys_error2 = 0;
    static int in_sys_error3 = 0;

    if (!in_sys_error3)
    {
        in_sys_error3 = 1;
    }

    va_start(argptr, error);
    vsprintf(text, error, argptr);
    va_end(argptr);

    if (isDedicated)
    {
        va_start(argptr, error);
        vsprintf(text, error, argptr);
        va_end(argptr);

        sprintf(text2, "ERROR: %s\n", text);
        WriteFile(houtput, text5, strlen(text5), &dummy, NULL);
        WriteFile(houtput, text4, strlen(text4), &dummy, NULL);
        WriteFile(houtput, text2, strlen(text2), &dummy, NULL);
        WriteFile(houtput, text3, strlen(text3), &dummy, NULL);
        WriteFile(houtput, text4, strlen(text4), &dummy, NULL);

        starttime = Sys_FloatTime();
        sc_return_on_enter = true; // so Enter will get us out of here

        while (!Sys_ConsoleInput() && ((Sys_FloatTime() - starttime) < CONSOLE_ERROR_TIMEOUT))
        {
        }
    }
    else
    {
        // switch to windowed so the message box is visible, unless we already
        // tried that and failed
        if (!in_sys_error0)
        {
            in_sys_error0 = 1;
            VID_SetDefaultMode();
            MessageBox(NULL, text, "Quake Error",
                MB_OK | MB_SETFOREGROUND | MB_ICONSTOP);
        }
        else
        {
            MessageBox(NULL, text, "Double Quake Error",
                MB_OK | MB_SETFOREGROUND | MB_ICONSTOP);
        }
    }

    if (!in_sys_error1)
    {
        in_sys_error1 = 1;
        Host_Shutdown();
    }

    // shut down QHOST hooks if necessary
    if (!in_sys_error2)
    {
        in_sys_error2 = 1;
    }

    exit(1);
}

void Sys_Printf(const char* fmt, ...)
{
  // XXX: Called but no body!
}

void Sys_Quit(void)
{
    Host_Shutdown();
    //    if (tevent)
    //        CloseHandle(tevent);
    if (isDedicated)
        FreeConsole();
    // shut down QHOST hooks if necessary
    exit(0);
}

/*
================
Sys_FloatTime
================
*/
double Sys_FloatTime(void)
{
    static int sametimecount;
    static unsigned int oldtime;
    static int first = 1;
    LARGE_INTEGER PerformanceCount;
    unsigned int temp, t2;
    double time;

    QueryPerformanceCounter(&PerformanceCount);

    temp = ((unsigned int)PerformanceCount.LowPart >> lowshift) | ((unsigned int)PerformanceCount.HighPart << (32 - lowshift));

    if (first)
    {
        oldtime = temp;
        first = 0;
    }
    else
    {
        // check for turnover or backward time
        if ((temp <= oldtime) && ((oldtime - temp) < 0x10000000))
        {
            oldtime = temp; // so we can't get stuck
        }
        else
        {
            t2 = temp - oldtime;

            time = (double)t2 * pfreq;
            oldtime = temp;

            curtime += time;

            if (curtime == lastcurtime)
            {
                sametimecount++;

                if (sametimecount > 100000)
                {
                    curtime += 1.0;
                    sametimecount = 0;
                }
            }
            else
            {
                sametimecount = 0;
            }

            lastcurtime = curtime;
        }
    }

    return curtime;
}

/*
================
Sys_InitFloatTime
================
*/
void Sys_InitFloatTime(void)
{
    Sys_FloatTime();
    const int j = COM_CheckParm("-starttime");
    if (j)
    {
        curtime = (double)(Q_atof(com_argv[j + 1]));
    }
    else
    {
        curtime = 0.0;
    }
    lastcurtime = curtime;
}

char* Sys_ConsoleInput(void)
{
    static char text[256];
    static int len;
    INPUT_RECORD recs[1024];
    int dummy;
    int ch, numread, numevents;

    if (!isDedicated)
        return NULL;

    for (;;)
    {
        if (!GetNumberOfConsoleInputEvents(hinput, &numevents))
            Sys_Error("Error getting # of console events");

        if (numevents <= 0)
            break;

        if (!ReadConsoleInput(hinput, recs, 1, &numread))
            Sys_Error("Error reading console input");

        if (numread != 1)
            Sys_Error("Couldn't read console input");

        if (recs[0].EventType == KEY_EVENT)
        {
            if (!recs[0].Event.KeyEvent.bKeyDown)
            {
                ch = recs[0].Event.KeyEvent.uChar.AsciiChar;

                switch (ch)
                {
                case '\r':
                    WriteFile(houtput, "\r\n", 2, &dummy, NULL);

                    if (len)
                    {
                        text[len] = 0;
                        len = 0;
                        return text;
                    }
                    else if (sc_return_on_enter)
                    {
                        // special case to allow exiting from the error handler on Enter
                        text[0] = '\r';
                        len = 0;
                        return text;
                    }

                    break;

                case '\b':
                    WriteFile(houtput, "\b \b", 3, &dummy, NULL);
                    if (len)
                    {
                        len--;
                    }
                    break;

                default:
                    if (ch >= ' ')
                    {
                        WriteFile(houtput, &ch, 1, &dummy, NULL);
                        text[len] = ch;
                        len = (len + 1) & 0xff;
                    }

                    break;
                }
            }
        }
    }

    return NULL;
}

void Sys_Sleep(void)
{
    SDL_Delay(1);
}

void Sys_PumpEvents(void)
{
    // SDL input event handler prototype
    void IN_SDLEvent(const SDL_Event* event);

    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
        case SDL_KEYDOWN:
        case SDL_KEYUP:
        case SDL_MOUSEMOTION:
            IN_SDLEvent(&event);
            break;
        case SDL_QUIT:
            Sys_Quit();
            return;
        }
    }
}

void SYS_MainLoop()
{
    double time, oldtime, newtime;
    oldtime = Sys_FloatTime();

    /* main window message loop */
    while (true)
    {
        Sys_PumpEvents();

        if (isDedicated)
        {
            newtime = Sys_FloatTime();
            time = newtime - oldtime;

            while (time < sys_ticrate.value)
            {
                Sys_Sleep();
                newtime = Sys_FloatTime();
                time = newtime - oldtime;
            }
        }
        else
        {
            // yield the CPU for a little while when paused, minimized, or not the focus
            if ((cl.paused && (!ActiveApp)) || Minimized || block_drawing)
            {
                SDL_Delay(PAUSE_SLEEP);
                scr_skipupdate = 1; // no point in bothering to draw
            }
            else if (!ActiveApp)
            {
                SDL_Delay(NOT_FOCUS_SLEEP);
            }

            newtime = Sys_FloatTime();
            time = newtime - oldtime;
        }

        Host_Frame(time);
        oldtime = newtime;
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    quakeparms_t parms;
    static char cwd[1024] = { 0 };
    int t;

    /* previous instances do not exist in Win32 */
    if (hPrevInstance)
        return 0;

    global_hInstance = hInstance;
    global_nCmdShow = nCmdShow;

    if (!GetCurrentDirectory(sizeof(cwd), cwd))
        Sys_Error("Couldn't determine current directory");

    if (cwd[Q_strlen(cwd) - 1] == '/')
        cwd[Q_strlen(cwd) - 1] = 0;

    parms.basedir = cwd;
    parms.cachedir = NULL;

    parms.argc = 1;
    argv[0] = empty_string;

    while (*lpCmdLine && (parms.argc < MAX_NUM_ARGVS))
    {
        while (*lpCmdLine && ((*lpCmdLine <= 32) || (*lpCmdLine > 126)))
            lpCmdLine++;

        if (*lpCmdLine)
        {
            argv[parms.argc] = lpCmdLine;
            parms.argc++;

            while (*lpCmdLine && ((*lpCmdLine > 32) && (*lpCmdLine <= 126)))
                lpCmdLine++;

            if (*lpCmdLine)
            {
                *lpCmdLine = 0;
                lpCmdLine++;
            }
        }
    }

    parms.argv = argv;

    COM_InitArgv(parms.argc, parms.argv);

    parms.argc = com_argc;
    parms.argv = com_argv;

    isDedicated = (COM_CheckParm("-dedicated") != 0);

#if 0 //johnfitz -- 0 to supress the 'starting quake' dialog
    if (!isDedicated)
    {
        hwnd_dialog = CreateDialog(hInstance, MAKEINTRESOURCE(IDD_DIALOG1), NULL, NULL);

        if (hwnd_dialog)
        {
            if (GetWindowRect(hwnd_dialog, &rect))
            {
                if (rect.left > (rect.top * 2))
                {
                    SetWindowPos(hwnd_dialog, 0,
                        (rect.left / 2) - ((rect.right - rect.left) / 2),
                        rect.top, 0, 0,
                        SWP_NOZORDER | SWP_NOSIZE);
                }
            }

            ShowWindow(hwnd_dialog, SW_SHOWDEFAULT);
            UpdateWindow(hwnd_dialog);
            SetForegroundWindow(hwnd_dialog);
        }
    }
#endif

    // take the greater of all the available memory or half the total memory,
    // but at least 8 Mb and no more than 16 Mb, unless they explicitly
    // request otherwise
    parms.memsize = MAXIMUM_WIN_MEMORY;

    if (COM_CheckParm("-heapsize"))
    {
        t = COM_CheckParm("-heapsize") + 1;

        if (t < com_argc)
            parms.memsize = Q_atoi(com_argv[t]) * 1024;
    }

    parms.membase = malloc(parms.memsize);

    if (!parms.membase)
        Sys_Error("Not enough memory free; check disk space\n");

    if (isDedicated)
    {
        if (!AllocConsole())
        {
            Sys_Error("Couldn't create dedicated server console");
        }

        hinput = GetStdHandle(STD_INPUT_HANDLE);
        houtput = GetStdHandle(STD_OUTPUT_HANDLE);

        // give QHOST a chance to hook into the console
        if ((t = COM_CheckParm("-HFILE")) > 0)
        {
            //            if (t < com_argc)
            //                hFile = (HANDLE)Q_atoi(com_argv[t + 1]);
        }

        if ((t = COM_CheckParm("-HPARENT")) > 0)
        {
            //            if (t < com_argc)
            //                heventParent = (HANDLE)Q_atoi(com_argv[t + 1]);
        }

        if ((t = COM_CheckParm("-HCHILD")) > 0)
        {
            //            if (t < com_argc)
            //                heventChild = (HANDLE)Q_atoi(com_argv[t + 1]);
        }
    }

    Sys_Init();

    // because sound is off until we become active
//    S_BlockSound();

    Sys_Printf("Host_Init\n");
    Host_Init(&parms);

    // execute the main game loop
    SYS_MainLoop();

    /* return success of application */
    return TRUE;
}
