#ifndef __WINE_KRNL386_CONF_H
#define __WINE_KRNL386_CONF_H

#include <windows.h>

void init_config(void);
DWORD WINAPI krnl386_get_config_int(LPCSTR appname, LPCSTR keyname, INT def);
DWORD WINAPI krnl386_get_config_string(LPCSTR appname, LPCSTR keyname, LPCSTR def, LPSTR ret, DWORD size);

/* [otvdm] ConfigReport: diagnostic log (see conf.c) */
BOOL WINAPI krnl386_config_report_enabled(void);
void WINAPI config_report_note(LPCSTR text);

/* [otvdm] PreferredMonitor (see conf.c) */
BOOL WINAPI krnl386_get_preferred_monitor(HMONITOR *mon, RECT *bounds, RECT *work);

#endif /* __WINE_KRNL386_CONF_H */
