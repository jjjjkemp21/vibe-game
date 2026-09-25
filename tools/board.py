#!/usr/bin/env python3
"""board: the studio's agent-first work-item tracker. Spec: docs/tools/board.md.

Always run the main copy: python C:/GameDev/VibeGame/tools/board.py <cmd>
Env: BOARD_DB overrides the DB path (tests), BOARD_ROOT the root used by export and packet paths (tests),
BOARD_AS the default actor (else --as, else "lead").
Python 3.12 standard library only.
"""
from __future__ import annotations

import argparse
import glob
import io
import json
import os
import re
import shlex
import sqlite3
import sys
import time
from datetime import datetime
from pathlib import Path

SCHEMA_VERSION = 1
MAIN_ROOT = Path('C:/GameDev/VibeGame')
HERE = Path(__file__).resolve().parent


def _root() -> Path:
    """The main checkout: this script's repo when it is the main copy (.git is a folder), else MAIN_ROOT."""
    r = HERE.parent
    return r if (r / '.git').is_dir() and (r / 'VibeGame.uproject').is_file() else MAIN_ROOT


ROOT = _root()
CMD = f'python {ROOT.as_posix()}/tools/board.py'

STATUSES = ['todo', 'ready', 'doing', 'review', 'blocked', 'merge', 'done', 'dropped']
OPEN = ['todo', 'ready', 'doing', 'review', 'blocked', 'merge']
ACTIVE = ['doing', 'review', 'blocked', 'merge']
DEPTS = ['lead', 'eng', 'art', 'qa', 'design']
KINDS = ['obj', 'task', 'bug', 'q']
LEVELS = ['junior', 'mid', 'senior']
PRIS = ['P0', 'P1', 'P2', 'P3']
SEVS = ['blocker', 'major', 'minor', 'trivial']
VOCAB = {'status': STATUSES, 'dept': DEPTS, 'kind': KINDS, 'level': LEVELS, 'pri': PRIS, 'sev': SEVS}

COLS = ['id', 'key', 'kind', 'dept', 'title', 'parent', 'status', 'prev', 'pri', 'sev', 'level', 'pct',
        'owner', 'agent', 'lane', 'milestone', 'packet', 'handoff', 'commit', 'evidence', 'created', 'updated']
SETTABLE = ['key', 'kind', 'dept', 'title', 'parent', 'status', 'pri', 'sev', 'level', 'pct', 'owner', 'agent',
            'lane', 'milestone', 'packet', 'handoff', 'commit', 'evidence']
REQUIRED = ['kind', 'dept', 'title', 'status', 'pri']
NO_SPACE = ['key', 'owner', 'agent', 'lane']
ALIASES = {'ms': 'milestone'}
EXAMPLE = {'kind': 'task', 'dept': 'eng', 'level': 'mid', 'sev': 'major', 'status': 'ready', 'pri': 'P1',
           'pct': '50', 'parent': '<obj-id>', 'key': 'T-001', 'title': '"<title>"', 'owner': 'eng-mgr',
           'agent': '<agent-id>', 'lane': '<lane>', 'milestone': '<milestone>'}
# Normal forward flow for task/bug items (blocked, dropped and rework are handled separately).
FLOW = {'todo': ['ready'], 'ready': ['doing', 'todo'], 'doing': ['review'], 'review': ['merge', 'done', 'doing'],
        'merge': ['done', 'doing']}
NEXT = {'todo': 'ready', 'ready': 'doing', 'doing': 'review', 'review': 'done', 'merge': 'done'}
NOT_IN_BATCH = ['batch', 'import', 'export', 'selftest']
ACTIVE_MINUTES = 20  # same window as tools/lead-check.ps1

SCHEMA = """
CREATE TABLE items(
  id INTEGER PRIMARY KEY, key TEXT UNIQUE COLLATE NOCASE, kind TEXT NOT NULL, dept TEXT NOT NULL,
  title TEXT NOT NULL, parent INTEGER, status TEXT NOT NULL, prev TEXT, pri TEXT NOT NULL, sev TEXT,
  level TEXT, pct INTEGER, owner TEXT, agent TEXT, lane TEXT, milestone TEXT, packet TEXT, handoff TEXT,
  "commit" TEXT, evidence TEXT, created TEXT NOT NULL, updated TEXT NOT NULL) STRICT;
CREATE TABLE deps(item INTEGER NOT NULL, needs INTEGER NOT NULL, PRIMARY KEY(item, needs)) STRICT;
CREATE TABLE claims(item INTEGER NOT NULL, path TEXT NOT NULL, symbol TEXT NOT NULL DEFAULT '',
  PRIMARY KEY(item, path, symbol)) STRICT;
CREATE TABLE events(id INTEGER PRIMARY KEY, item INTEGER, ts TEXT NOT NULL, actor TEXT NOT NULL,
  verb TEXT NOT NULL, "from" TEXT, "to" TEXT, note TEXT) STRICT;
CREATE INDEX items_parent ON items(parent);
CREATE INDEX events_item ON events(item, id);
"""


class BoardError(Exception):
    """One-line error for stderr, always ending in a command that works."""

    def __init__(self, msg: str, fix: str, raw: bool = False):
        super().__init__(msg)
        self.msg = msg
        self.fix = fix if raw else f'{CMD} {fix}'

    def line(self) -> str:
        return ' '.join(f'board: {self.msg}; fix: {self.fix}'.split())


class HelpExit(Exception):
    pass


class Parser(argparse.ArgumentParser):
    def __init__(self, name: str, fix: str, **kw):
        super().__init__(prog=f'board.py {name}', allow_abbrev=False, **kw)
        self.name, self.fix = name, fix

    def error(self, message):
        raise BoardError(f'{self.name}: {message}', self.fix)

    def exit(self, status=0, message=None):
        if status:
            raise BoardError(f'{self.name}: {message or "bad arguments"}', self.fix)
        raise HelpExit()


# ---------------------------------------------------------------- helpers
def now() -> str:
    return datetime.now().isoformat(timespec='seconds')


def short_ts(ts: str) -> str:
    return ts[5:16].replace('T', ' ')


def dumps(x) -> str:
    return json.dumps(x, separators=(',', ':'), ensure_ascii=False)


def q(col: str) -> str:
    return f'"{col}"'


def sval(v):
    return None if v is None else str(v)


def db_path() -> Path:
    p = os.environ.get('BOARD_DB')
    return Path(p) if p else ROOT / 'Saved' / 'Studio' / 'board.db'


def work_root() -> Path:
    p = os.environ.get('BOARD_ROOT')
    return Path(p) if p else ROOT


def line(r) -> str:
    """List line: <id> <kind> <dept> <level|-> <status> <pri> <lane|-> <agent7|-> <pct|-> <key|-> <title>"""
    pct = '-' if r['pct'] is None else f"{r['pct']}%"
    return ' '.join([str(r['id']), r['kind'], r['dept'], r['level'] or '-', r['status'], r['pri'],
                     r['lane'] or '-', (r['agent'] or '-')[:7], pct, r['key'] or '-', r['title'][:60].rstrip()])


def sort_key(r):
    return (STATUSES.index(r['status']), PRIS.index(r['pri']), r['id'])


def age(ts: str) -> str:
    try:
        s = max(0, (datetime.now() - datetime.fromisoformat(ts)).total_seconds())
    except ValueError:
        return '-'
    if s < 3600:
        return f'{int(s // 60)}m'
    if s < 48 * 3600:
        return f'{int(s // 3600)}h'
    return f'{int(s // 86400)}d'


def fmt_event(e, with_item: bool) -> str:
    parts = [short_ts(e['ts'])]
    if with_item:
        parts.append('#-' if e['item'] is None else f"#{e['item']}")
    parts += [e['actor'], e['verb']]
    f, t = e['from'], e['to']
    if f and (e['verb'] in SETTABLE or e['verb'] == 'needs'):
        parts.append(f"{f}->{t or '-'}")
    elif t or f:
        parts.append(t or f)
    if e['note']:
        parts.append(' '.join(e['note'].split()))
    return ' '.join(parts)


def report_exists(packet) -> bool:
    if not packet:
        return False
    p = Path(packet)
    if not p.is_absolute():
        p = work_root() / p
    return (p / 'report.md').is_file()


def is_code(rec) -> bool:
    return rec['dept'] in ('eng', 'qa') and rec['kind'] in ('task', 'bug')


# ---------------------------------------------------------------- claims
def norm_path(p: str) -> str:
    p = p.strip().replace('\\', '/')
    for base in {ROOT.as_posix().rstrip('/') + '/', work_root().as_posix().rstrip('/') + '/'}:
        if p.lower().startswith(base.lower()):
            p = p[len(base):]
            break
    else:
        m = re.match(re.escape(ROOT.as_posix()) + r'-lanes/[^/]+/', p, re.I)
        if m:
            p = p[m.end():]
    while p.startswith('./'):
        p = p[2:]
    return p


def split_spec(s: str):
    """'path[:Symbol]' -> (path, symbol). The symbol starts at the first ':' after the last '/' (keeps C:/ intact)."""
    s = s.strip().replace('\\', '/')
    colon = s.find(':', s.rfind('/') + 1)
    path, sym = (s[:colon], s[colon + 1:].strip()) if colon > 1 else (s, '')
    return norm_path(path), sym


def spec_str(path: str, sym: str) -> str:
    return f'{path}:{sym}' if sym else path


def overlaps(p1: str, s1: str, p2: str, s2: str) -> bool:
    a, b = p1.lower(), p2.lower()
    if a.endswith('/') or b.endswith('/'):  # folder claims cover everything below them
        return (a.endswith('/') and b.startswith(a)) or (b.endswith('/') and a.startswith(b))
    return a == b and (not s1 or not s2 or s1 == s2)


# ---------------------------------------------------------------- orphans (isolated so tests can stub it)
def projects_dir() -> Path:
    """~/.claude/projects/<main repo path with :, \\ and / replaced by '-'>, as tools/lead-check.ps1 computes it."""
    home = os.environ.get('USERPROFILE') or str(Path.home())
    return Path(home) / '.claude' / 'projects' / re.sub(r'[:\\/]', '-', str(ROOT))


def transcript_running(path, now_ts: float | None = None) -> bool:
    """lead-check.ps1 rule: running = written in the last ACTIVE_MINUTES and the last stop_reason is not end_turn."""
    now_ts = time.time() if now_ts is None else now_ts
    try:
        st = os.stat(path)
    except OSError:
        return False
    if now_ts - st.st_mtime > ACTIVE_MINUTES * 60:
        return False
    n = min(st.st_size, 4 * 1024 * 1024)
    with open(path, 'rb') as f:
        if n:
            f.seek(-n, 2)
        data = f.read(n)
    stops = re.findall(rb'"stop_reason":"(\w+)"', data)
    return not (stops and stops[-1] == b'end_turn')


def agent_running(agent: str) -> bool:
    pat = str(projects_dir() / '*' / 'subagents' / f'agent-{glob.escape(agent)}*.jsonl')
    return any(transcript_running(p) for p in glob.glob(pat))


# ---------------------------------------------------------------- DB
def connect(argv) -> sqlite3.Connection:
    p = db_path()
    p.parent.mkdir(parents=True, exist_ok=True)
    con = sqlite3.connect(str(p), timeout=5, isolation_level=None)
    try:
        con.row_factory = sqlite3.Row
        con.execute('PRAGMA busy_timeout=5000')
        v = con.execute('PRAGMA user_version').fetchone()[0]
        if v > SCHEMA_VERSION:
            main = f'python {MAIN_ROOT.as_posix()}/tools/board.py'
            raise BoardError(f'DB schema v{v} is newer than this tool (v{SCHEMA_VERSION}); use the main copy',
                             ' '.join([main] + [shlex.quote(a) for a in argv]), raw=True)
        if v < SCHEMA_VERSION:
            con.execute('PRAGMA journal_mode=WAL')
            con.execute('BEGIN IMMEDIATE')
            try:
                if con.execute('PRAGMA user_version').fetchone()[0] == 0:
                    for stmt in SCHEMA.split(';'):
                        if stmt.strip():
                            con.execute(stmt)
                con.execute(f'PRAGMA user_version={SCHEMA_VERSION}')
                con.execute('COMMIT')
            except BaseException:
                con.execute('ROLLBACK')
                raise
    except BaseException:
        con.close()
        raise
    return con


def split_globals(argv):
    """Pull --json and --as <actor> out of argv (anywhere before a literal --)."""
    rest, js, actor, i = [], False, None, 0
    while i < len(argv):
        a = argv[i]
        if a == '--':
            rest += argv[i:]
            break
        if a == '--json':
            js = True
        elif a == '--as' and i + 1 < len(argv):
            actor = argv[i + 1]
            i += 1
        elif a.startswith('--as='):
            actor = a[5:]
        else:
            rest.append(a)
        i += 1
    return rest, js, actor


# ---------------------------------------------------------------- commands
class Board:
    def __init__(self, con: sqlite3.Connection, actor: str, js: bool):
        self.con, self.actor, self.js = con, actor, js
        self.out: list[str] = []
        self.in_batch = False
        self.parsers = build_parsers()

    # -- plumbing
    def x(self, sql, args=()):
        return self.con.execute(sql, args)

    def one(self, sql, args=()):
        return self.x(sql, args).fetchone()

    def emit(self, *lines):
        self.out.extend(lines)

    def event(self, item, verb, frm=None, to=None, note=None):
        self.x('INSERT INTO events(item, ts, actor, verb, "from", "to", note) VALUES(?,?,?,?,?,?,?)',
               (item, now(), self.actor, verb, sval(frm), sval(to), note))

    def get(self, ref):
        r = str(ref).strip()
        row = None
        if r.lstrip('#').isdigit():
            row = self.one('SELECT * FROM items WHERE id=?', (int(r.lstrip('#')),))
        elif r:
            row = self.one('SELECT * FROM items WHERE key=?', (r,))
        if not row:
            raise BoardError(f'no item {ref}', 'ls --all')
        return row

    def dispatch(self, argv, stdin=None):
        argv, js, actor = split_globals(argv)
        saved = (self.js, self.actor)
        self.js = self.js or js
        self.actor = actor or self.actor
        try:
            if not argv:
                raise BoardError('no command', 'ls')
            name = argv[0]
            if name not in self.parsers:
                raise BoardError(f'unknown command {name} (commands: {" ".join(self.parsers)})', 'ls')
            if self.in_batch and name in NOT_IN_BATCH:
                raise BoardError(f'{name} cannot run inside batch', 'ls')
            parser, write = self.parsers[name]
            a = parser.parse_args(argv[1:])
            fn = getattr(self, 'cmd_' + name)
            if name == 'batch':
                fn(a, stdin)
            elif write and not self.in_batch:
                self.x('BEGIN IMMEDIATE')
                try:
                    fn(a)
                    self.x('COMMIT')
                except BaseException:
                    self.x('ROLLBACK')
                    raise
            else:
                fn(a)
        finally:
            self.js, self.actor = saved

    def list_out(self, rows, n):
        rows = sorted(rows, key=sort_key)
        shown = rows[:n] if n else rows
        if self.js:
            self.emit(dumps([dict(r) for r in shown]))
            return
        self.emit(*[line(r) for r in shown])
        if len(rows) > len(shown):
            self.emit(f'+{len(rows) - len(shown)} more')

    def norm(self, f, v, ctx, item_id=None):
        """Validate and normalise one field value; ctx is 'new' or 'set <id>' (for the fix hint)."""
        ex = EXAMPLE.get(f, '<value>')
        if ctx == 'new':
            flag = {'milestone': 'ms'}.get(f, f)
            fix = 'new --kind task --dept eng --title "<title>"' + ('' if f in ('kind', 'dept', 'title')
                                                                   else f' --{flag} {ex}')
        else:
            fix = f'{ctx} {f}={ex}'
        v = v.strip() if isinstance(v, str) else v
        if v is None or v in ('', '-'):
            if f in REQUIRED:
                raise BoardError(f'{f} cannot be empty', fix)
            return None
        if f in VOCAB:
            v = v.upper() if f == 'pri' else v.lower()
            if v not in VOCAB[f]:
                raise BoardError(f'{f} must be one of: {" ".join(VOCAB[f])}', fix)
        elif f == 'pct':
            try:
                v = int(v.rstrip('%'))
            except ValueError:
                raise BoardError('pct must be a number 0-100', fix)
            if not 0 <= v <= 100:
                raise BoardError('pct must be 0-100', fix)
        elif f == 'parent':
            p = self.get(v)
            if p['kind'] != 'obj':
                raise BoardError(f"parent #{p['id']} is a {p['kind']}, not an obj", fix)
            if p['id'] == item_id:
                raise BoardError('an item cannot be its own parent', fix)
            v = p['id']
        elif f == 'title':
            v = ' '.join(v.split())
        elif f in NO_SPACE:
            if len(v.split()) != 1:
                raise BoardError(f'{f} cannot contain spaces', fix)
            if f == 'key':
                if v.lstrip('#').isdigit():
                    raise BoardError('key cannot be a number (numbers are ids)', fix)
                other = self.one('SELECT id FROM items WHERE key=?', (v,))
                if other and other['id'] != item_id:
                    raise BoardError(f"key {v} is already #{other['id']}", f"show {other['id']}")
        return v

    def parse_needs(self, v, item_id, ctx):
        ids = []
        for ref in [s for s in (v or '').replace(' ', ',').split(',') if s.strip() and s.strip() != '-']:
            n = self.get(ref)['id']
            if n == item_id:
                raise BoardError('an item cannot need itself', f'{ctx} needs=-')
            if item_id is not None and self.reaches(n, item_id):
                raise BoardError(f'#{item_id} needs #{n} would make a cycle', f'show {n}')
            if n not in ids:
                ids.append(n)
        return ids

    def reaches(self, start, target) -> bool:
        seen, todo = set(), [start]
        while todo:
            cur = todo.pop()
            for (n,) in self.x('SELECT needs FROM deps WHERE item=?', (cur,)).fetchall():
                if n == target:
                    return True
                if n not in seen:
                    seen.add(n)
                    todo.append(n)
        return False

    def add_claims(self, iid, specs):
        for s in specs:
            path, sym = split_spec(s)
            if not path:
                raise BoardError(f'empty claim path in "{s}"', f'claim {iid} Source/VibeGame/File.cpp:Symbol')
            if self.one('SELECT 1 FROM claims WHERE item=? AND lower(path)=lower(?) AND symbol=?', (iid, path, sym)):
                continue
            self.x('INSERT INTO claims(item, path, symbol) VALUES(?,?,?)', (iid, path, sym))
            self.event(iid, 'claim', to=spec_str(path, sym))
            rows = self.x("SELECT c.item, c.path, c.symbol, i.status, i.agent FROM claims c JOIN items i ON "
                          "i.id=c.item WHERE c.item!=? AND i.status NOT IN ('done','dropped') ORDER BY c.item",
                          (iid,)).fetchall()
            for o in rows:
                if overlaps(path, sym, o['path'], o['symbol']):
                    self.emit(f"warn: #{iid} {spec_str(path, sym)} overlaps #{o['item']} "
                              f"{spec_str(o['path'], o['symbol'])} ({o['status']} {(o['agent'] or '-')[:7]})")

    def last_note_is(self, iid, note) -> bool:
        r = self.one('SELECT note FROM events WHERE item=? ORDER BY id DESC LIMIT 1', (iid,))
        return bool(r) and r['note'] == note

    def note_since_doing(self, iid) -> bool:
        return bool(self.one(
            "SELECT 1 FROM events WHERE item=? AND verb='note' AND id > COALESCE((SELECT MAX(id) FROM events "
            "WHERE item=? AND verb='status' AND \"to\"='doing'), 0)", (iid, iid)))

    def hint(self, rec, target) -> str:
        parts = [f"set {rec['id']}"]
        if target == 'doing':
            if not rec['agent']:
                parts.append('agent=<agent-id>')
            if is_code(rec) and not rec['lane']:
                parts.append('lane=<lane>')
        if target == 'merge' and not rec['lane']:
            parts.append('lane=<lane>')
        parts.append(f'status={target}')
        if target == 'review' and not report_exists(rec['packet']):
            parts.append('note="<summary>"')
        if target == 'done' and not (rec['commit'] or rec['evidence']):
            parts.append('commit=<hash>')
        return ' '.join(parts)

    def check_children(self, rec, target):
        if rec['kind'] != 'obj':
            return
        kids = [str(r['id']) for r in self.x(
            "SELECT id FROM items WHERE parent=? AND status NOT IN ('done','dropped') ORDER BY id", (rec['id'],))]
        if kids:
            raise BoardError(f"obj #{rec['id']} ->{target} needs every child done or dropped; open: {','.join(kids)}",
                             f"ls --parent {rec['id']}")

    def guard(self, old, new, note):
        a, b, i, kind = old['status'], new['status'], old['id'], new['kind']
        if b == 'dropped':
            if not note:
                raise BoardError(f'#{i} ->dropped needs a note', f'set {i} status=dropped note="<reason>"')
            self.check_children(new, 'dropped')
            return
        if b == 'blocked':
            if a not in OPEN:
                back = 'doing' if a == 'done' else 'todo'
                raise BoardError(f'#{i} is {a}; only open items can be blocked',
                                 f'set {i} status={back} note="<why reopen>"')
            if not note:
                raise BoardError(f'#{i} ->blocked needs a note with the reason',
                                 f'set {i} status=blocked note="<reason>"')
            return
        if a == 'blocked':
            prev = old['prev'] or 'todo'
            if b != prev:
                raise BoardError(f'#{i} leaves blocked only back to {prev}', f'set {i} status={prev}')
            return
        if a == 'dropped':
            if b != 'todo' or not note:
                raise BoardError(f'#{i} is dropped; reopen it to todo with a note',
                                 f'set {i} status=todo note="<why>"')
            return
        if a == 'done':
            if not note or (b != 'doing' and kind not in ('obj', 'q')):
                raise BoardError(f'#{i} done->{b}: rework goes to doing with a note',
                                 f'set {i} status=doing note="<why>"')
            return
        if kind in ('obj', 'q'):
            if b == 'done' and kind == 'obj':
                self.check_children(new, 'done')
            elif b == 'done' and not (note or new['evidence']):
                raise BoardError(f'#{i} q ->done needs the answer as a note or evidence',
                                 f'set {i} status=done note="<answer>"')
            return
        if b not in FLOW[a]:
            raise BoardError(f'#{i} {a}->{b} not allowed ({a}->{"|".join(FLOW[a])})', self.hint(new, NEXT[a]))
        if b == 'doing' and a == 'ready':
            missing = [f for f in ('agent', 'lane') if not new[f] and (f == 'agent' or is_code(new))]
            if missing:
                raise BoardError(f'#{i} ->doing needs {" and ".join(missing)}', self.hint(new, 'doing'))
        elif b == 'doing':
            if not note:
                raise BoardError(f'#{i} {a}->doing (rework) needs a note', f'set {i} status=doing note="<why>"')
        elif b == 'review':
            if not (note or self.note_since_doing(i) or report_exists(new['packet'])):
                why = f"{new['packet']}/report.md is missing" if new['packet'] else 'needs packet (with report.md) or a note'
                raise BoardError(f'#{i} ->review {why}', f'set {i} status=review note="<summary>"')
        elif b == 'merge':
            if not new['lane']:
                raise BoardError(f'#{i} ->merge needs lane', self.hint(new, 'merge'))
        elif b == 'done':
            if not (new['commit'] or new['evidence']):
                raise BoardError(f'#{i} ->done needs commit or evidence', self.hint(new, 'done'))

    # -- reads
    def cmd_ls(self, a):
        where, args = [], []

        def multi(col, val, vocab=None):
            vals = [v.strip() for v in val.split(',') if v.strip()]
            if vocab:
                vals = [v.upper() if col == 'pri' else v.lower() for v in vals]
                bad = [v for v in vals if v not in vocab]
                if bad:
                    raise BoardError(f'{col} must be one of: {" ".join(vocab)}', f'ls --{col} {vocab[0]}')
            where.append(f'{q(col)} IN ({",".join("?" * len(vals))})')
            args.extend(vals)

        if a.status:
            multi('status', a.status, STATUSES)
        elif not a.all:
            where.append("status NOT IN ('done','dropped')")
        for col in ('dept', 'kind', 'level'):
            if getattr(a, col):
                multi(col, getattr(a, col), VOCAB[col])
        if a.lane:
            multi('lane', a.lane)
        if a.ms:
            multi('milestone', a.ms)
        if a.parent:
            where.append('parent=?')
            args.append(self.get(a.parent)['id'])
        if a.agent:
            where.append("agent LIKE ? ESCAPE '\\'")
            args.append(re.sub(r'([%_\\])', r'\\\1', a.agent) + '%')
        sql = 'SELECT * FROM items' + (' WHERE ' + ' AND '.join(where) if where else '')
        self.list_out(self.x(sql, args).fetchall(), check_n(a.n, 'ls'))

    def cmd_next(self, a):
        where, args = ["status='ready'", "kind IN ('task','bug')",
                       "NOT EXISTS (SELECT 1 FROM deps d JOIN items n ON n.id=d.needs "
                       "WHERE d.item=items.id AND n.status!='done')"], []
        for col in ('dept', 'level'):
            v = getattr(a, col)
            if v:
                if v.lower() not in VOCAB[col]:
                    raise BoardError(f'{col} must be one of: {" ".join(VOCAB[col])}', f'next --{col} {VOCAB[col][1]}')
                where.append(f'{col}=?')
                args.append(v.lower())
        rows = self.x('SELECT * FROM items WHERE ' + ' AND '.join(where), args).fetchall()
        self.list_out(rows, check_n(a.n, 'next'))

    def cmd_show(self, a):
        r = self.get(a.id)
        iid = r['id']
        needs = self.x('SELECT n.id, n.status FROM deps d JOIN items n ON n.id=d.needs WHERE d.item=? ORDER BY n.id',
                       (iid,)).fetchall()
        claims = [spec_str(c['path'], c['symbol']) for c in
                  self.x('SELECT path, symbol FROM claims WHERE item=? ORDER BY path, symbol', (iid,))]
        evs = self.x('SELECT * FROM events WHERE item=? ORDER BY id DESC LIMIT 5', (iid,)).fetchall()[::-1]
        kids = self.x("SELECT status FROM items WHERE parent=?", (iid,)).fetchall()
        if self.js:
            d = dict(r)
            d.update(needs=[{'id': n['id'], 'status': n['status']} for n in needs], claims=claims,
                     events=[dict(e) for e in evs])
            self.emit(dumps(d))
            return
        for c in COLS:
            if c == 'prev':
                if r['status'] == 'blocked':
                    self.emit(f"was: {r['prev'] or 'todo'}")
                continue
            self.emit(f"{c}: {'-' if r[c] is None else r[c]}")
        if r['kind'] == 'obj':
            self.emit(f"children: {len(kids)} ({sum(k['status'] in OPEN for k in kids)} open)")
        self.emit('needs: ' + (', '.join(f"{n['id']} {n['status']}" for n in needs) or '-'))
        self.emit('claims: ' + (', '.join(claims) or '-'))
        self.emit(*[f'event: {fmt_event(e, False)}' for e in evs])

    def cmd_conflicts(self, a):
        rows = self.x("SELECT c.item, c.path, c.symbol FROM claims c JOIN items i ON i.id=c.item "
                      "WHERE i.status NOT IN ('done','dropped') ORDER BY c.item, c.path, c.symbol").fetchall()
        pairs = []
        for i, c1 in enumerate(rows):
            for c2 in rows[i + 1:]:
                if c1['item'] != c2['item'] and overlaps(c1['path'], c1['symbol'], c2['path'], c2['symbol']):
                    pairs.append((c1, c2))
        if self.js:
            self.emit(dumps([{'a': p['item'], 'a_claim': spec_str(p['path'], p['symbol']), 'b': o['item'],
                              'b_claim': spec_str(o['path'], o['symbol'])} for p, o in pairs]))
            return
        self.emit(*[f"#{p['item']} {spec_str(p['path'], p['symbol'])} | #{o['item']} {spec_str(o['path'], o['symbol'])}"
                    for p, o in pairs])

    def cmd_resume(self, a):
        rows = sorted(self.x(f"SELECT * FROM items WHERE status IN ({','.join('?' * len(ACTIVE))})",
                             ACTIVE).fetchall(), key=sort_key)
        items, orphans = [], []
        for r in rows:
            since = self.one("SELECT MAX(ts) AS ts FROM events WHERE item=? AND verb='status'", (r['id'],))['ts']
            last = self.one('SELECT * FROM events WHERE item=? ORDER BY id DESC LIMIT 1', (r['id'],))
            items.append((r, age(since or r['updated']), last))
            if r['status'] == 'doing' and not (r['agent'] and agent_running(r['agent'])):
                orphans.append(r)
        if self.js:
            self.emit(dumps({'items': [dict(r, age=ag, last=dict(e) if e else None) for r, ag, e in items],
                             'orphans': [r['id'] for r in orphans]}))
            return
        for r, ag, e in items:
            self.emit(f"{r['id']} {r['status']} {r['lane'] or '-'} {(r['agent'] or '-')[:7]} {ag} {r['key'] or '-'} "
                      f"handoff={r['handoff'] or '-'} | {r['title'][:60].rstrip()} | "
                      f"{fmt_event(e, False)[:100] if e else '-'}")
        for r in orphans:
            self.emit(f"orphan {r['id']} {(r['agent'] or '-')[:7]} {r['key'] or '-'} "
                      f"{'no agent' if not r['agent'] else 'no running transcript'}")

    def cmd_tree(self, a):
        root = self.get(a.id)
        out, seen = [], set()

        def walk(r, depth):
            seen.add(r['id'])
            out.append((depth, r))
            for c in self.x('SELECT * FROM items WHERE parent=? ORDER BY id', (r['id'],)).fetchall():
                if c['id'] not in seen:
                    walk(c, depth + 1)

        walk(root, 0)
        if self.js:
            self.emit(dumps([dict(r, depth=d) for d, r in out]))
            return
        self.emit(*['  ' * d + line(r) for d, r in out])

    def cmd_log(self, a):
        n = check_n(a.n, 'log')
        lim = f' LIMIT {n}' if n else ''
        if a.id:
            evs = self.x(f'SELECT * FROM events WHERE item=? ORDER BY id DESC{lim}', (self.get(a.id)['id'],))
        else:
            evs = self.x(f'SELECT * FROM events ORDER BY id DESC{lim}')
        evs = evs.fetchall()[::-1]
        if self.js:
            self.emit(dumps([dict(e) for e in evs]))
            return
        self.emit(*[fmt_event(e, True) for e in evs])

    # -- writes
    def cmd_new(self, a):
        vals = {f: self.norm(f, getattr(a, f), 'new') for f in ('kind', 'dept', 'title')}
        if a.key:
            ex = self.one('SELECT * FROM items WHERE key=?', (a.key.strip(),))
            if ex:
                if (ex['kind'], ex['dept'], ex['title']) == (vals['kind'], vals['dept'], vals['title']):
                    self.emit(f"#{ex['id']}")
                    return
                raise BoardError(f"key {a.key} is already #{ex['id']} ({ex['title'][:60]})", f"show {ex['id']}")
        vals['pri'] = self.norm('pri', a.pri or 'P2', 'new')
        for f, v in (('sev', a.sev), ('level', a.level), ('key', a.key), ('milestone', a.ms), ('owner', a.owner),
                     ('parent', a.parent)):
            vals[f] = self.norm(f, v, 'new')
        needs = self.parse_needs(a.needs, None, 'new')
        ts = now()
        vals.update(status='todo', created=ts, updated=ts)
        cols = list(vals)
        iid = self.x(f"INSERT INTO items({','.join(q(c) for c in cols)}) VALUES({','.join('?' * len(cols))})",
                     [vals[c] for c in cols]).lastrowid
        self.event(iid, 'new', to='todo')
        for n in needs:
            self.x('INSERT INTO deps(item, needs) VALUES(?,?)', (iid, n))
        self.emit(f'#{iid}')
        if a.files:
            self.add_claims(iid, [s for s in a.files.split(',') if s.strip()])

    def cmd_set(self, a):
        r = self.get(a.id)
        iid = r['id']
        ctx = f'set {iid}'
        changes, note, needs_new = {}, None, None
        for pair in a.pairs:
            if '=' not in pair:
                raise BoardError(f'expected k=v, got "{pair}"', f'set {iid} status=ready')
            k, v = pair.split('=', 1)
            k = ALIASES.get(k.strip().lower(), k.strip().lower())
            if k == 'note':
                note = ' '.join(v.split()) or None
            elif k == 'needs':
                needs_new = self.parse_needs(v, iid, ctx)
            elif k in SETTABLE:
                changes[k] = self.norm(k, v, ctx, iid)
            else:
                raise BoardError(f'unknown field {k} (fields: {" ".join(SETTABLE)} needs note)',
                                 f'set {iid} status=ready')
        diff = {k: v for k, v in changes.items() if r[k] != v}
        old_needs = [n for (n,) in self.x('SELECT needs FROM deps WHERE item=? ORDER BY needs', (iid,))]
        needs_changed = needs_new is not None and sorted(needs_new) != old_needs
        if not diff and not needs_changed:
            if note and not self.last_note_is(iid, note):
                self.event(iid, 'note', note=note)
            return
        new = dict(r)
        new.update(diff)
        if 'status' in diff:
            self.guard(r, new, note)
            if diff['status'] == 'blocked':
                diff['prev'] = r['status']
            elif r['status'] == 'blocked':
                diff['prev'] = None
        diff['updated'] = now()
        self.x(f"UPDATE items SET {', '.join(f'{q(k)}=?' for k in diff)} WHERE id=?", [*diff.values(), iid])
        evs = [(k, r[k], diff[k]) for k in SETTABLE if k in diff and k != 'status']
        if needs_changed:
            self.x('DELETE FROM deps WHERE item=?', (iid,))
            for n in needs_new:
                self.x('INSERT INTO deps(item, needs) VALUES(?,?)', (iid, n))
            evs.append(('needs', ','.join(map(str, old_needs)) or None, ','.join(map(str, sorted(needs_new))) or None))
        if 'status' in diff:
            evs.append(('status', r['status'], diff['status']))
        for j, (k, frm, to) in enumerate(evs):
            self.event(iid, k, frm, to, note if j == len(evs) - 1 else None)

    def cmd_note(self, a):
        iid = self.get(a.id)['id']
        text = ' '.join(' '.join(a.text).split())
        if not text:
            raise BoardError('empty note', f'note {iid} "<text>"')
        if not self.last_note_is(iid, text):
            self.event(iid, 'note', note=text)

    def cmd_claim(self, a):
        self.add_claims(self.get(a.id)['id'], a.paths)

    def cmd_unclaim(self, a):
        iid = self.get(a.id)['id']
        rows = self.x('SELECT path, symbol FROM claims WHERE item=? ORDER BY path, symbol', (iid,)).fetchall()
        if a.path:
            path, sym = split_spec(a.path)
            has_sym = ':' in a.path.replace('\\', '/')[a.path.replace('\\', '/').rfind('/') + 1:]
            rows = [c for c in rows if c['path'].lower() == path.lower() and (not has_sym or c['symbol'] == sym)]
        for c in rows:
            self.x('DELETE FROM claims WHERE item=? AND path=? AND symbol=?', (iid, c['path'], c['symbol']))
            self.event(iid, 'unclaim', frm=spec_str(c['path'], c['symbol']))

    def cmd_batch(self, a, stdin):
        text = (stdin if stdin is not None else sys.stdin).read()
        self.x('BEGIN IMMEDIATE')
        self.in_batch = True
        try:
            for n, raw in enumerate(text.splitlines(), 1):
                s = raw.strip()
                if not s or s.startswith('#'):
                    continue
                try:
                    argv = shlex.split(re.sub(r'\\(?=[A-Za-z0-9_.])', '/', s))
                    if len(argv) > 1 and argv[0] == 'python' and argv[1].endswith('board.py'):
                        argv = argv[2:]
                    elif argv and (argv[0].endswith('board.py') or argv[0] == 'board'):
                        argv = argv[1:]
                    self.dispatch(argv)
                except ValueError as e:
                    raise BoardError(f'batch line {n} (nothing applied): {e}', 'batch')
                except BoardError as e:
                    e.msg = f'batch line {n} (nothing applied): {e.msg}'
                    raise
                except HelpExit:
                    raise BoardError(f'batch line {n} (nothing applied): help is not a command', 'ls')
            self.x('COMMIT')
        except BaseException:
            self.x('ROLLBACK')
            raise
        finally:
            self.in_batch = False

    def cmd_export(self, a):
        root = work_root()
        items = [dict(r) for r in self.x('SELECT * FROM items ORDER BY id')]
        deps, claims = {}, {}
        for d in self.x('SELECT item, needs FROM deps ORDER BY item, needs'):
            deps.setdefault(d['item'], []).append(d['needs'])
        for c in self.x('SELECT item, path, symbol FROM claims ORDER BY item, path, symbol'):
            claims.setdefault(c['item'], []).append(spec_str(c['path'], c['symbol']))
        lines = [dumps({'t': 'meta', 'schema': SCHEMA_VERSION})]
        for it in items:
            lines.append(dumps({'t': 'item', **it, 'needs': deps.get(it['id'], []), 'claims': claims.get(it['id'], [])}))
        lines += [dumps({'t': 'event', **dict(e)}) for e in self.x('SELECT * FROM events ORDER BY id')]
        jl = root / 'docs' / 'board' / 'items.jsonl'
        jl.parent.mkdir(parents=True, exist_ok=True)
        jl.write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')

        md = ['# Board', '',
              'Generated by `python tools/board.py export`; do not edit (the DB is the truth). '
              'Line: id kind dept level status pri lane agent pct key title.']
        groups = {}
        for it in items:
            groups.setdefault(it['milestone'], []).append(it)
        for ms in sorted(groups, key=lambda m: (m is None, m or '')):
            its = sorted(groups[ms], key=sort_key)
            counts = ', '.join(f'{s} {n}' for s in STATUSES if (n := sum(i['status'] == s for i in its)))
            md += ['', f"## {ms or '(no milestone)'}", '', f'Counts: {counts}.', '']
            md += [f'- {line(i)}' for i in its if i['status'] in OPEN] or ['- (nothing open)']
        out = root / 'docs' / 'BOARD.md'
        out.write_text('\n'.join(md) + '\n', encoding='utf-8', newline='\n')

    def cmd_import(self, a):
        src = Path(a.file)
        try:
            recs = [json.loads(s) for s in src.read_text(encoding='utf-8').splitlines() if s.strip()]
        except (OSError, ValueError) as e:
            raise BoardError(f'cannot read {a.file}: {e}', 'import docs/board/items.jsonl')
        meta = next((r for r in recs if r.get('t') == 'meta'), {})
        if meta.get('schema', 1) > SCHEMA_VERSION:
            raise BoardError(f"dump schema v{meta['schema']} is newer than this tool; use the main copy",
                             f'python {MAIN_ROOT.as_posix()}/tools/board.py import {a.file}', raw=True)
        n = self.one('SELECT COUNT(*) AS n FROM items')['n']
        if n and not a.replace:
            raise BoardError(f'DB already has {n} items; import replaces everything', f'import {a.file} --replace')
        for t in ('items', 'deps', 'claims', 'events'):
            self.x(f'DELETE FROM {t}')
        for r in recs:
            if r.get('t') == 'item':
                missing = [c for c in ('id', 'kind', 'dept', 'title', 'status', 'pri') if r.get(c) in (None, '')]
                if missing:
                    raise BoardError(f"item {r.get('id')} in {a.file} lacks {','.join(missing)}", f'show {r.get("id")}')
                ts = r.get('created') or now()
                row = {c: r.get(c) for c in COLS}
                row['created'], row['updated'] = ts, r.get('updated') or ts
                self.x(f"INSERT INTO items({','.join(q(c) for c in COLS)}) VALUES({','.join('?' * len(COLS))})",
                       [row[c] for c in COLS])
                for d in r.get('needs', []):
                    self.x('INSERT OR IGNORE INTO deps(item, needs) VALUES(?,?)', (r['id'], d))
                for s in r.get('claims', []):
                    path, sym = split_spec(s)
                    self.x('INSERT OR IGNORE INTO claims(item, path, symbol) VALUES(?,?,?)', (r['id'], path, sym))
            elif r.get('t') == 'event':
                self.x('INSERT INTO events(id, item, ts, actor, verb, "from", "to", note) VALUES(?,?,?,?,?,?,?,?)',
                       (r['id'], r.get('item'), r['ts'], r['actor'], r['verb'], r.get('from'), r.get('to'),
                        r.get('note')))
        self.event(None, 'import', to=src.as_posix())


def check_n(n, cmd):
    if n is None or n < 0:
        raise BoardError('-n must be 0 (all) or more', f'{cmd} -n 20')
    return n


def build_parsers():
    p = {}

    def mk(name, fix, write=False):
        parser = Parser(name, fix)
        p[name] = (parser, write)
        return parser

    ls = mk('ls', 'ls --dept eng')
    for f in ('dept', 'status', 'kind', 'parent', 'ms', 'agent', 'lane', 'level'):
        ls.add_argument(f'--{f}')
    ls.add_argument('--all', action='store_true')
    ls.add_argument('-n', type=int, default=20)
    nx = mk('next', 'next --dept eng')
    nx.add_argument('--dept')
    nx.add_argument('--level')
    nx.add_argument('-n', type=int, default=5)
    mk('show', 'show 1').add_argument('id')
    new = mk('new', 'new --kind task --dept eng --title "<title>"', True)
    for f in ('kind', 'dept', 'title'):
        new.add_argument(f'--{f}', required=True)
    for f in ('parent', 'pri', 'sev', 'level', 'key', 'ms', 'needs', 'files', 'owner'):
        new.add_argument(f'--{f}')
    st = mk('set', 'set 1 status=ready', True)
    st.add_argument('id')
    st.add_argument('pairs', nargs='+')
    nt = mk('note', 'note 1 "<text>"', True)
    nt.add_argument('id')
    nt.add_argument('text', nargs='+')
    cl = mk('claim', 'claim 1 Source/VibeGame/File.cpp:Symbol', True)
    cl.add_argument('id')
    cl.add_argument('paths', nargs='+')
    uc = mk('unclaim', 'unclaim 1 Source/VibeGame/File.cpp', True)
    uc.add_argument('id')
    uc.add_argument('path', nargs='?')
    mk('conflicts', 'conflicts')
    mk('resume', 'resume')
    mk('tree', 'tree 1').add_argument('id')
    lg = mk('log', 'log 1 -n 10')
    lg.add_argument('id', nargs='?')
    lg.add_argument('-n', type=int, default=10)
    mk('batch', 'batch < commands.txt', True)
    mk('export', 'export')
    im = mk('import', 'import docs/board/items.jsonl --replace', True)
    im.add_argument('file')
    im.add_argument('--replace', action='store_true')
    mk('selftest', 'selftest')
    return p


# ---------------------------------------------------------------- selftest
def load_suite():
    import unittest
    d = HERE / 'tests'
    return unittest.defaultTestLoader.discover(str(d), pattern='test_board.py', top_level_dir=str(d))


def run_selftest(out):
    import unittest
    stream = io.StringIO()
    res = unittest.TextTestRunner(stream=stream, verbosity=0).run(load_suite())
    if res.wasSuccessful() and res.testsRun:
        out.append(f'ok {res.testsRun} tests')
        return
    for test, tb in res.failures + res.errors:
        sys.stderr.write(f'FAIL {test.id()}: {tb.strip().splitlines()[-1]}\n')
    bad = len(res.failures) + len(res.errors)
    raise BoardError(f'{bad} of {res.testsRun} tests failed',
                     f'python -m unittest discover -s {HERE.as_posix()}/tests -p test_board.py -v', raw=True)


USAGE = ('usage: board.py <cmd> [--json] [--as actor]; cmds: ls next show new set note claim unclaim conflicts '
         'resume tree log batch export import selftest (spec: docs/tools/board.md; <cmd> -h for flags)')


def main(argv=None, stdin=None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    rest, js, actor = split_globals(argv)
    actor = actor or os.environ.get('BOARD_AS') or 'lead'
    out: list[str] = []
    try:
        if not rest or rest[0] in ('-h', '--help', 'help'):
            if not rest:
                raise BoardError('no command', 'ls')
            out.append(USAGE)
        elif rest[0] == 'selftest':
            build_parsers()['selftest'][0].parse_args(rest[1:])
            run_selftest(out)
        else:
            con = connect(argv)
            try:
                b = Board(con, actor, js)
                b.dispatch(rest, stdin)
                out = b.out
            finally:
                con.close()
    except BoardError as e:
        sys.stderr.write(e.line() + '\n')
        return 1
    except HelpExit:
        return 0
    except sqlite3.OperationalError as e:
        sys.stderr.write(BoardError(f'database error: {e}', 'ls').line() + '\n')
        return 1
    if out:
        sys.stdout.write('\n'.join(out) + '\n')
    return 0


if __name__ == '__main__':
    for s in (sys.stdout, sys.stderr):
        try:
            s.reconfigure(encoding='utf-8', newline='\n')
        except (AttributeError, ValueError):
            pass
    sys.exit(main())
