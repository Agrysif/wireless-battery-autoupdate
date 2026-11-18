#!/usr/bin/env python3
"""Adds silent auto-update to an upstream Wireless Battery source tree.

usage: apply.py <upstream-dir> <version>      version = X.Y.Z.R (upstream X.Y.Z + our patch revision R)

Every edit is anchored on a line of upstream code. If upstream changes that line, the script stops with
"PATCH FAILED" and the build fails, so a release without the updater is never published.
"""
import io
import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def fail(msg):
    sys.exit('PATCH FAILED: ' + msg)


def read(up, rel):
    with io.open(os.path.join(up, rel), encoding='utf-8', newline='') as f:
        return f.read()


def write(up, rel, text):
    with io.open(os.path.join(up, rel), 'w', encoding='utf-8', newline='') as f:
        f.write(text)


def one(pattern, text, what):
    found = list(re.finditer(pattern, text, re.M))
    if len(found) != 1:
        fail('%s: expected 1 match, found %d' % (what, len(found)))
    return found[0]


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    up, version = sys.argv[1], sys.argv[2]
    if not re.fullmatch(r'\d+\.\d+\.\d+\.\d+', version):
        fail('bad version ' + version)

    # 1. Our files.
    for name in ('tk_update.h', 'tk_update.cpp'):
        shutil.copyfile(os.path.join(HERE, name), os.path.join(up, 'src', name))
    write(up, 'src/tk_version.h', '#pragma once\n#define TK_VERSION_W L"%s"\n' % version)
    shutil.copyfile(os.path.join(HERE, 'TK_MODIFICATIONS.txt'), os.path.join(up, 'TK_MODIFICATIONS.txt'))

    # 2. src/main.cpp: include, menu item, menu command, start.
    s = read(up, 'src/main.cpp')
    if 'tkupdate::' in s:
        fail('src/main.cpp is already patched')
    nl = '\r\n' if '\r\n' in s else '\n'
    if re.search(r'\b950\b', s):
        fail('menu id 950 is used upstream, pick another TK_ID_AUTOUPDATE')

    head = s[:s.index('namespace {')]
    inc = list(re.finditer(r'^#include "[^"\n]+"[ \t]*(?=\r?$)', head, re.M))
    if not inc:
        fail('no local #include in src/main.cpp')
    p = inc[-1].end()
    s = s[:p] + nl + '#include "tk_update.h"  // mytoolkit.ru build: silent auto-update' + s[p:]

    m = one(r'^([ \t]*)AppendMenuW\((\w+),[^\n]*T\(L"Start with Windows"\)\);[ \t]*(?=\r?$)', s, 'menu "Start with Windows"')
    indent, menu = m.group(1), m.group(2)
    line = ('%sif (tkupdate::Available()) AppendMenuW(%s, MF_STRING | (tkupdate::Enabled() ? MF_CHECKED : 0), '
            'tkupdate::TK_ID_AUTOUPDATE, T(L"Update automatically"));' % (indent, menu))
    s = s[:m.end()] + nl + line + s[m.end():]

    m = one(r'^([ \t]*)case ID_EXIT:', s, 'case ID_EXIT')
    line = '%scase tkupdate::TK_ID_AUTOUPDATE: tkupdate::SetEnabled(!tkupdate::Enabled()); break;' % m.group(1)
    s = s[:m.start()] + line + nl + s[m.start():]

    m = one(r'^([ \t]*)g_worker = CreateThread\([^\n]*\);[ \t]*(?=\r?$)', s, 'worker thread start')
    s = s[:m.end()] + nl + m.group(1) + 'tkupdate::Start();' + s[m.end():]
    write(up, 'src/main.cpp', s)

    # 3. Makefile: BCrypt for SHA-256 and the signature check.
    mk = read(up, 'Makefile')
    m = one(r'^LIBS[ \t]*:?=[^\n]*?(?=\r?$)', mk, 'Makefile LIBS')
    if '-lbcrypt' not in m.group(0):
        mk = mk[:m.end()] + ' -lbcrypt' + mk[m.end():]
    write(up, 'Makefile', mk)

    # 4. Installer: our notice file, restart after a silent update.
    iss = read(up, 'installer/WirelessBattery.iss')
    nl = '\r\n' if '\r\n' in iss else '\n'
    m = one(r'^Source: "\{#Root\}\\LICENSE";[^\n]*?(?=\r?$)', iss, 'installer LICENSE line')
    iss = iss[:m.end()] + nl + 'Source: "{#Root}\\TK_MODIFICATIONS.txt"; DestDir: "{app}"; Flags: ignoreversion' + iss[m.end():]
    m = one(r'^\[Run\][ \t]*(?=\r?$)', iss, 'installer [Run]')
    iss = (iss[:m.end()] + nl + 'Filename: "{app}\\WirelessBattery.exe"; Flags: nowait; Check: TkAfterUpdate' + iss[m.end():])
    one(r'^\[Code\][ \t]*(?=\r?$)', iss, 'installer [Code]')
    if not iss.endswith(nl):
        iss += nl
    iss += nl.join([
        '',
        '{ mytoolkit.ru build: the app runs this installer with /TKUPDATE=1 to update itself; start it again afterwards. }',
        'function TkAfterUpdate(): Boolean;',
        'begin',
        "  Result := ExpandConstant('{param:TKUPDATE|0}') = '1';",
        'end;',
        '',
    ])
    write(up, 'installer/WirelessBattery.iss', iss)

    # 5. Russian menu text (other languages show the English one until translated).
    ru = read(up, 'lang/ru.txt')
    nl = '\r\n' if '\r\n' in ru else '\n'
    if not re.search(r'^Update automatically\s*=', ru, re.M):
        if not ru.endswith(nl):
            ru += nl
        ru += 'Update automatically = Обновлять автоматически' + nl
    write(up, 'lang/ru.txt', ru)

    print('patched %s as %s' % (up, version))


if __name__ == '__main__':
    main()
