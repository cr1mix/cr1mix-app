import os, sys, struct, re

ROOT = sys.argv[1]
CAT = os.path.join(ROOT, 'catalog')
RES = os.path.join(ROOT, 'res')
SRC = os.path.join(ROOT, 'src')
os.makedirs(SRC, exist_ok=True)

ORDER = ['cpu', 'gpu', 'memory', 'storage', 'network', 'gaming', 'privacy', 'windows', 'apps', 'look']
parts, cat_icons = [], set()
for name in ORDER:
    text = open(os.path.join(CAT, name + '.cat'), encoding='utf-8-sig').read()
    keep = []
    for line in text.splitlines():
        s = line.strip()
        if s.startswith('#') or s.startswith('src '):
            continue
        m = re.match(r'(tweak|tab)\s+\S+\s*\|\s*(\S+)', s) if s.startswith('tweak') else re.match(r'tab\s+\S+\s*\|[^|]*\|\s*(\S+)', s)
        if m: cat_icons.add(m.groups()[-1])
        keep.append(line)
    parts.append('\n'.join(keep))
raw = ('\n'.join(parts) + '\n').encode('utf-8')
print('catalog', len(raw), 'bytes')

KEY = bytes([0x9E, 0x37, 0x79, 0xB9, 0x7F, 0x4A, 0x7C, 0x15, 0xC2, 0xB3, 0x56, 0x88, 0x3D, 0x11, 0xE6, 0xA5])
import ctypes
from ctypes import wintypes
cab = ctypes.WinDLL('cabinet.dll')
h = ctypes.c_void_p()
assert cab.CreateCompressor(5, None, ctypes.byref(h)), 'CreateCompressor failed'
outsz = ctypes.c_size_t(0)
cap = len(raw) + 65536
cbuf = ctypes.create_string_buffer(cap)
cab.Compress.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
assert cab.Compress(h, raw, len(raw), cbuf, cap, ctypes.byref(outsz)), 'Compress failed'
cab.CloseCompressor(h)
print('lzms', len(raw), '->', outsz.value)
raw = cbuf.raw[:outsz.value]
out = bytearray(len(raw))
k = 0x12345678
for i, b in enumerate(raw):
    k = (k * 1103515245 + 12345) & 0xFFFFFFFF
    out[i] = b ^ KEY[i & 15] ^ ((k >> 16) & 0xFF)
open(os.path.join(RES, 'pack.dat'), 'wb').write(out)
print('pack.dat', len(out))

png = open(os.path.join(ROOT, 'logo_cr1mix.png'), 'rb').read()
ico = struct.pack('<HHH', 0, 1, 1) + struct.pack('<BBBBHHII', 0, 0, 0, 0, 1, 32, len(png), 22)
open(os.path.join(RES, 'app.ico'), 'wb').write(ico + png)
print('app.ico', len(ico) + len(png))

cps = {}
for line in open(os.environ.get('CPFILE', os.path.join(os.environ.get('TEMP', '.'), 'ms_codepoints.txt')), encoding='utf-8', errors='replace'):
    p = line.split()
    if len(p) == 2:
        cps[p[0]] = int(p[1], 16)
UI_ICONS = '''home memory developer_board memory_alt hard_drive lan sports_esports shield window apps palette settings search close remove crop_square filter_none
person check check_circle error warning info chevron_right chevron_left expand_more expand_less arrow_back arrow_forward bolt restart_alt undo tune
auto_awesome backup history logout login dark_mode light_mode contrast speed thermostat storage network_check download upload schedule timer
play_arrow pause stop done_all select_all deselect delete_sweep lock lock_open verified verified_user new_releases star favorite magic_button
rocket_launch cloud_off wifi monitor_heart insights bar_chart show_chart donut_large data_usage visibility tips_and_updates open_in_new link
grid_view view_list format_paint brush gradient blur_on blur_off animation motion_photos_on circle radio_button_checked radio_button_unchecked
check_box check_box_outline_blank toggle_on toggle_off more_vert more_horiz menu filter_list sort refresh sync power_settings_new restart_alt
pending task_alt rule playlist_add_check dashboard space_dashboard computer laptop_windows desktop_windows battery_full battery_charging_full
device_thermostat electric_bolt local_fire_department ac_unit trending_up trending_down equalizer graphic_eq waves forum account_circle
work checklist help storefront public cloud print bluetooth extension terminal cleaning_services window laptop_windows desktop_windows
magic_button person_remove verified_user dns code apps_outage arrow_upward arrow_downward'''.split()
want = sorted(set(cat_icons) | set(UI_ICONS))
rows = [(n, cps[n]) for n in want if n in cps]
with open(os.path.join(SRC, 'gen_icons.h'), 'w', encoding='utf-8') as f:
    f.write('#pragma once\nstruct IconRow{const char*n;int cp;};\n')
    f.write('static const IconRow ICONS[]={\n')
    for n, cp in rows:
        f.write('{"%s",0x%X},\n' % (n, cp))
    f.write('};\nstatic const int ICONS_N=%d;\n' % len(rows))
print('icons', len(rows))

rc = r'''#include <windows.h>
1 ICON "app.ico"
1 24 "app.manifest"
PACK   RCDATA "pack.dat"
FONTGS4 RCDATA "gsans400.woff"
FONTGS5 RCDATA "gsans500.woff"
FONTGS7 RCDATA "gsans700.woff"
FONTIC0 RCDATA "icons0.woff"
FONTIC1 RCDATA "icons1.woff"
LOGO    RCDATA "logo.png"
AUTHOR  RCDATA "author.jpg"
'''
_common = open(os.path.join(SRC, 'common.h'), encoding='utf-8').read()
_ver = re.search(r'CR1MIX_VERSION L"([^"]+)"', _common).group(1)
_build = int(re.search(r'CR1MIX_BUILD (\d+)', _common).group(1))
_nums = [int(x) for x in re.findall(r'\d+', _ver)][:3]
_nums += [0] * (3 - len(_nums))
_fv = '%d,%d,%d,%d' % (_nums[0], _nums[1], _nums[2], _build)
rc += r'''
1 VERSIONINFO
FILEVERSION %(fv)s
PRODUCTVERSION %(fv)s
FILEFLAGSMASK 0x3fL
FILEFLAGS 0x0L
FILEOS 0x40004L
FILETYPE 0x1L
FILESUBTYPE 0x0L
BEGIN
  BLOCK "StringFileInfo"
  BEGIN
    BLOCK "040904b0"
    BEGIN
      VALUE "CompanyName", "cr1mix"
      VALUE "FileDescription", "cr1mix app"
      VALUE "FileVersion", "%(ver)s"
      VALUE "InternalName", "cr1mix app"
      VALUE "LegalCopyright", "Copyright (C) 2026 cr1mix"
      VALUE "OriginalFilename", "cr1mix app.exe"
      VALUE "ProductName", "cr1mix app"
      VALUE "ProductVersion", "%(ver)s"
      VALUE "Comments", "Windows tweaking utility - https://cr1mix.pages.dev"
    END
  END
  BLOCK "VarFileInfo"
  BEGIN
    VALUE "Translation", 0x409, 1200
  END
END
''' % {'fv': _fv, 'ver': _ver}
open(os.path.join(RES, 'cr1mix.rc'), 'w', encoding='utf-8').write(rc)
open(os.path.join(RES, 'logo.png'), 'wb').write(png)
man = r'''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
 <trustInfo xmlns="urn:schemas-microsoft-com:asm.v3"><security><requestedPrivileges>
   <requestedExecutionLevel level="requireAdministrator" uiAccess="false"/>
 </requestedPrivileges></security></trustInfo>
 <application xmlns="urn:schemas-microsoft-com:asm.v3"><windowsSettings>
   <dpiAware xmlns="http://schemas.microsoft.com/SMI/2005/WindowsSettings">true/pm</dpiAware>
   <dpiAwareness xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">permonitorv2,permonitor</dpiAwareness>
   <activeCodePage xmlns="http://schemas.microsoft.com/SMI/2019/WindowsSettings">UTF-8</activeCodePage>
 </windowsSettings></application>
 <compatibility xmlns="urn:schemas-microsoft-com:compatibility.v1"><application>
   <supportedOS Id="{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}"/>
 </application></compatibility>
</assembly>
'''
open(os.path.join(RES, 'app.manifest'), 'w', encoding='utf-8').write(man)
print('rc + manifest written')

