#!/usr/bin/env python3
"""Chooses the upstream release to build: the newest one that has been out for DELAY_HOURS.

Writes tag=vX.Y.Z and version=X.Y.Z.R (R = PATCH_REV) to $GITHUB_OUTPUT, or empty values when there is
nothing new. FORCE_TAG builds that exact upstream tag; FORCE=true rebuilds even if the release exists.
"""
import datetime
import json
import os
import re
import urllib.request

UPSTREAM = 'krisgfx9/Wireless-Device-Battery-Monitor'
ME = os.environ['GITHUB_REPOSITORY']
TOKEN = os.environ.get('GH_TOKEN', '')
DELAY_HOURS = float(os.environ.get('DELAY_HOURS', '12'))
FORCE_TAG = os.environ.get('FORCE_TAG', '').strip()
FORCE = os.environ.get('FORCE', 'false') == 'true'


def api(path):
    headers = {'Accept': 'application/vnd.github+json', 'User-Agent': 'wireless-battery-autoupdate'}
    if TOKEN:
        headers['Authorization'] = 'Bearer ' + TOKEN
    req = urllib.request.Request('https://api.github.com' + path, headers=headers)
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.load(r)


def out(tag='', version=''):
    print('tag=%r version=%r' % (tag, version))
    with open(os.environ['GITHUB_OUTPUT'], 'a') as f:
        f.write('tag=%s\nversion=%s\n' % (tag, version))


def vt(s):
    return tuple(int(x) for x in s.lstrip('v').split('.'))


def main():
    rev = open('PATCH_REV').read().strip()
    assert rev.isdigit(), 'PATCH_REV must be a number'
    now = datetime.datetime.now(datetime.timezone.utc)
    rels = [r for r in api('/repos/%s/releases?per_page=50' % UPSTREAM)
            if not r['draft'] and not r['prerelease'] and re.fullmatch(r'v\d+\.\d+\.\d+', r['tag_name'])]
    if FORCE_TAG:
        cand = [r for r in rels if r['tag_name'] == FORCE_TAG]
    else:
        def age_h(r):
            t = datetime.datetime.fromisoformat(r['published_at'].replace('Z', '+00:00'))
            return (now - t).total_seconds() / 3600
        cand = [r for r in rels if age_h(r) >= DELAY_HOURS]
    if not cand:
        print('no upstream release old enough')
        return out()
    best = max(cand, key=lambda r: vt(r['tag_name']))
    version = best['tag_name'][1:] + '.' + rev

    mine = [r['tag_name'] for r in api('/repos/%s/releases?per_page=100' % ME)
            if re.fullmatch(r'v\d+\.\d+\.\d+\.\d+', r['tag_name'])]
    if not FORCE:
        if 'v' + version in mine:
            print('already released v' + version)
            return out()
        newest = max((vt(t) for t in mine), default=None)
        if newest and vt(version) <= newest and not FORCE_TAG:
            print('v%s is not newer than our v%s' % (version, '.'.join(map(str, newest))))
            return out()
    out(best['tag_name'], version)


if __name__ == '__main__':
    main()
