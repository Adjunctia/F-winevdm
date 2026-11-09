#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "conf.h"

static BOOL init;

static CHAR filename[MAX_PATH];
static CRITICAL_SECTION critical_section;

/* ------------------------------------------------------------------------
 * Config report ([otvdm] ConfigReport=1, or OTVDM_CONFIG_REPORT=1 in the
 * environment when the ini itself is the problem)
 *
 * Appends to otvdm_config_report.log next to otvdm.exe (or in the current
 * directory if that is not writable): the otvdm.ini path that is used, every
 * setting lookup with its value or the default, and diagnostic notes from
 * other modules (config_report_note, e.g. window geometry from user.exe16).
 * Each distinct line is written once, so per-call lookups and notes from hot
 * paths cannot flood the log.  Lines carry the process id because every
 * win16 task normally runs in its own otvdm process, all sharing the file.
 * When disabled, nothing is opened and lookups cost nothing extra.
 * ------------------------------------------------------------------------ */
static BOOL report_enabled;
static FILE *report_file;
static CRITICAL_SECTION report_section;
static DWORD report_seen[2048];   /* hashes of the lines written so far */
static int report_seen_count;

static void report_write(const char *fmt, ...)
{
    char line[512];
    va_list ap;
    DWORD hash = 2166136261u;
    const char *p;
    int i;

    va_start(ap, fmt);
    _vsnprintf(line, sizeof(line) - 1, fmt, ap);
    va_end(ap);
    line[sizeof(line) - 1] = 0;

    for (p = line; *p; p++)
        hash = (hash ^ (BYTE)*p) * 16777619u;   /* FNV-1a */

    EnterCriticalSection(&report_section);
    for (i = 0; i < report_seen_count; i++)
        if (report_seen[i] == hash)
            break;
    if (i == report_seen_count)
    {
        if (report_seen_count < ARRAYSIZE(report_seen))
            report_seen[report_seen_count++] = hash;
        fprintf(report_file, "otvdm[%lu]: %s\n", GetCurrentProcessId(), line);
        fflush(report_file);
    }
    LeaveCriticalSection(&report_section);
}

static BOOL is_true(const char *value)
{
    return atoi(value) || !lstrcmpiA(value, "true") || !lstrcmpiA(value, "yes");
}

static void report_init(BOOL ini_found, BOOL ini_from_env)
{
    CHAR value[16], path[MAX_PATH];
    LPSTR slash;
    SYSTEMTIME now;
    DWORD len;

    if (GetPrivateProfileStringA("otvdm", "ConfigReport", "0", value, sizeof(value), filename) && is_true(value))
        report_enabled = TRUE;
    len = GetEnvironmentVariableA("OTVDM_CONFIG_REPORT", value, sizeof(value));
    if (len && len < sizeof(value) && is_true(value))
        report_enabled = TRUE;
    if (!report_enabled)
        return;

    len = GetModuleFileNameA(NULL, path, MAX_PATH);
    slash = len ? strrchr(path, '\\') : NULL;
    if (slash && (slash + 1 - path) + sizeof("otvdm_config_report.log") <= MAX_PATH)
    {
        strcpy(slash + 1, "otvdm_config_report.log");
        report_file = fopen(path, "a");
    }
    if (!report_file)
        report_file = fopen("otvdm_config_report.log", "a");
    if (!report_file)
    {
        report_enabled = FALSE;
        return;
    }
    InitializeCriticalSection(&report_section);

    GetLocalTime(&now);
    report_write("==== config report %04u-%02u-%02u %02u:%02u:%02u ====",
                 now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    report_write("ini file: '%s'%s, %s", filename, ini_from_env ? " (from OTVDM_INI)" : "",
                 ini_found ? "found" : "NOT FOUND - every setting uses its default");
    {
        /* DPI awareness decides which coordinates Windows hands us; a manifest
         * or shim that makes otvdm DPI-aware changes all screen metrics */
        HDC dc = GetDC(NULL);
        report_write("desktop DC %dx%d, physical height %d",
                     GetDeviceCaps(dc, HORZRES), GetDeviceCaps(dc, VERTRES), GetDeviceCaps(dc, DESKTOPVERTRES));
        ReleaseDC(NULL, dc);
    }
}

static void report_lookup(LPCSTR appname, LPCSTR keyname, LPCSTR value)
{
    CHAR probe[2];

    /* GetPrivateProfileString can't tell "missing" from "set to the
     * default", so probe for the key with a default no ini value can have */
    GetPrivateProfileStringA(appname, keyname, "\x01", probe, sizeof(probe), filename);
    report_write("setting [%s] %s = %s%s", appname ? appname : "", keyname ? keyname : "",
                 value, probe[0] == '\x01' ? " (default, not in ini)" : "");
}

/* free-form diagnostic line from other modules (exported) */
void WINAPI config_report_note(LPCSTR text)
{
    if (!init)
        init_config();
    if (report_enabled && text)
        report_write("%s", text);
}

/* lets other modules skip formatting a note when the report is off (exported) */
BOOL WINAPI krnl386_config_report_enabled(void)
{
    if (!init)
        init_config();
    return report_enabled;
}

void init_config(void)
{
    LPSTR last;
    DWORD len;
    BOOL from_env = FALSE;

    if (init)
        return;
    init = TRUE;
    InitializeCriticalSection(&critical_section);

    /* OTVDM_INI points at another ini, e.g. to test a configuration */
    len = GetEnvironmentVariableA("OTVDM_INI", filename, MAX_PATH);
    if (len && len < MAX_PATH)
        from_env = TRUE;
    else
    {
        filename[0] = 0;
        len = GetModuleFileNameA(NULL, filename, MAX_PATH);
        last = (len && len < MAX_PATH) ? strrchr(filename, '\\') : NULL;
        if (last && (last + 1 - filename) + sizeof("otvdm.ini") <= MAX_PATH)
            strcpy(last + 1, "otvdm.ini");
        else
            filename[0] = 0;
    }
    report_init(filename[0] && GetFileAttributesA(filename) != INVALID_FILE_ATTRIBUTES, from_env);
}

DWORD WINAPI krnl386_get_config_string(LPCSTR appname, LPCSTR keyname, LPCSTR def, LPSTR ret, DWORD size)
{
    DWORD result;

    if (!init)
        init_config();
    EnterCriticalSection(&critical_section);
    result = GetPrivateProfileStringA(appname, keyname, def, ret, size, filename);
    if (report_enabled)
        report_lookup(appname, keyname, size ? ret : "");
    LeaveCriticalSection(&critical_section);
    return result;
}

DWORD WINAPI krnl386_get_config_int(LPCSTR appname, LPCSTR keyname, INT def)
{
    DWORD result;

    if (!init)
        init_config();
    EnterCriticalSection(&critical_section);
    result = GetPrivateProfileIntA(appname, keyname, def, filename);
    if (report_enabled)
    {
        CHAR value[16];
        sprintf(value, "%ld", (LONG)result);
        report_lookup(appname, keyname, value);
    }
    LeaveCriticalSection(&critical_section);
    return result;
}

/* ------------------------------------------------------------------------
 * [otvdm] PreferredMonitor
 *
 * Makes one monitor look like the primary display to win16 apps without
 * touching the Windows display settings.  Shared by user.exe16 (metrics,
 * work area, desktop window rect, window placement) and gdi.exe16
 * (GetDeviceCaps HORZRES/VERTRES), so every screen-size channel agrees.
 *
 *   PreferredMonitor=N      0-based index in EnumDisplayMonitors order (the
 *                           "monitor[N]:" lines of the ConfigReport log)
 *   PreferredMonitor=text   substring of the device name, e.g. DISPLAY3
 *
 * The rects are returned exactly as GetMonitorInfo reports them inside this
 * (DPI-unaware) process.  Windows already virtualizes them per monitor: e.g.
 * a 3440x1440 primary at 125% reads 2752x1152 and a 100% secondary reads
 * 1:1.  Those are the same units SetWindowPos/GetWindowRect use in this
 * process, so no DPI scaling may be applied on top of them.
 * ------------------------------------------------------------------------ */
typedef struct
{
    int index;           /* >= 0: match by enumeration position */
    const char *name;    /* else: device name substring */
    int seen;
    HMONITOR found;
} prefmon_enum_ctx;

static BOOL CALLBACK prefmon_enum_proc(HMONITOR hmon, HDC hdc, LPRECT rc, LPARAM lparam)
{
    prefmon_enum_ctx *ctx = (prefmon_enum_ctx *)lparam;
    if (ctx->index >= 0)
    {
        if (ctx->seen++ != ctx->index)
            return TRUE;
    }
    else
    {
        MONITORINFOEXA mi;
        char dev[CCHDEVICENAME + 1], want[64];
        mi.cbSize = sizeof(mi);
        if (!GetMonitorInfoA(hmon, (MONITORINFO *)&mi))
            return TRUE;
        lstrcpynA(dev, mi.szDevice, sizeof(dev));
        lstrcpynA(want, ctx->name, sizeof(want));
        CharUpperA(dev);
        CharUpperA(want);
        if (!strstr(dev, want))
            return TRUE;
    }
    ctx->found = hmon;
    return FALSE;
}

BOOL WINAPI krnl386_get_preferred_monitor(HMONITOR *mon, RECT *bounds, RECT *work)
{
    static int state; /* 0 = ini not read, 1 = disabled, 2 = enabled */
    static int want_index = -1;
    static char want_name[64];
    static HMONITOR cached;
    static BOOL reported;
    MONITORINFOEXA mi;

    if (!state)
    {
        char raw[64], *end;
        long n;
        raw[0] = 0;
        krnl386_get_config_string("otvdm", "PreferredMonitor", "", raw, sizeof(raw));
        state = 1;
        if (raw[0])
        {
            n = strtol(raw, &end, 10);
            if (end != raw && !*end && n >= 0)
                want_index = (int)n;
            else
                lstrcpynA(want_name, raw, sizeof(want_name));
            state = 2;
        }
    }
    if (state != 2)
        return FALSE;

    mi.cbSize = sizeof(mi);
    /* the cached handle goes stale when the display layout changes */
    if (!cached || !GetMonitorInfoA(cached, (MONITORINFO *)&mi))
    {
        prefmon_enum_ctx ctx;
        ctx.index = want_index;
        ctx.name = want_name;
        ctx.seen = 0;
        ctx.found = NULL;
        EnumDisplayMonitors(NULL, NULL, prefmon_enum_proc, (LPARAM)&ctx);
        cached = ctx.found;
        if (!cached || !GetMonitorInfoA(cached, (MONITORINFO *)&mi))
        {
            if (!reported)
            {
                char buf[128];
                reported = TRUE;
                snprintf(buf, sizeof(buf), "PreferredMonitor: no monitor matches '%s' -- option ignored",
                         want_index >= 0 ? "(index)" : want_name);
                config_report_note(buf);
            }
            cached = NULL;
            return FALSE;
        }
        {
            char buf[256];
            snprintf(buf, sizeof(buf),
                     "PreferredMonitor: using '%s' bounds L%ld T%ld R%ld B%ld (%ldx%ld) work L%ld T%ld R%ld B%ld (%ldx%ld)%s",
                     mi.szDevice,
                     mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right, mi.rcMonitor.bottom,
                     mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                     mi.rcWork.left, mi.rcWork.top, mi.rcWork.right, mi.rcWork.bottom,
                     mi.rcWork.right - mi.rcWork.left, mi.rcWork.bottom - mi.rcWork.top,
                     (mi.dwFlags & MONITORINFOF_PRIMARY) ? " [PRIMARY]" : "");
            config_report_note(buf);
        }
    }
    if (mon) *mon = cached;
    if (bounds) *bounds = mi.rcMonitor;
    if (work) *work = mi.rcWork;
    return TRUE;
}
