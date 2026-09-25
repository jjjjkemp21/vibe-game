"""Unit tests for tools/board.py (run: python tools/board.py selftest). Every test uses a temp DB via BOARD_DB."""
import contextlib
import io
import json
import os
import sqlite3
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

TOOLS = Path(__file__).resolve().parents[1]
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))
import board  # noqa: E402


class BoardCase(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)
        self._env = {k: os.environ.get(k) for k in ('BOARD_DB', 'BOARD_ROOT', 'BOARD_AS')}
        os.environ['BOARD_DB'] = str(self.tmp / 'db' / 'board.db')
        os.environ['BOARD_ROOT'] = str(self.tmp / 'root')
        os.environ.pop('BOARD_AS', None)

    def tearDown(self):
        for k, v in self._env.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
        self._tmp.cleanup()

    def run_cmd(self, *argv, stdin=None):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = board.main(list(argv), io.StringIO(stdin) if stdin is not None else None)
        return code, out.getvalue(), err.getvalue()

    def ok(self, *argv, stdin=None):
        code, out, err = self.run_cmd(*argv, stdin=stdin)
        self.assertEqual(code, 0, f'{argv} failed: {err}')
        self.assertEqual(err, '')
        return out

    def fail_hint(self, *argv, hint=None, stdin=None):
        """Assert a refused command: exit 1, nothing on stdout, one stderr line ending in a working command."""
        code, out, err = self.run_cmd(*argv, stdin=stdin)
        self.assertEqual(code, 1, f'{argv} should fail; out={out!r}')
        self.assertEqual(out, '')
        self.assertTrue(err.startswith('board: ') and err.endswith('\n') and err.count('\n') == 1, err)
        self.assertIn('; fix: python ', err)
        if hint:
            self.assertTrue(err.rstrip('\n').endswith(hint), f'hint {hint!r} not at end of {err!r}')
        return err

    def new(self, kind='task', dept='eng', title='Thing', *extra):
        return int(self.ok('new', '--kind', kind, '--dept', dept, '--title', title, *extra).splitlines()[0][1:])

    def db(self):
        con = sqlite3.connect(os.environ['BOARD_DB'])
        con.row_factory = sqlite3.Row
        return con

    def q(self, sql, args=()):
        con = self.db()
        try:
            return con.execute(sql, args).fetchall()
        finally:
            con.close()

    def events(self, item=None):
        if item is None:
            return len(self.q('SELECT id FROM events'))
        return len(self.q('SELECT id FROM events WHERE item=?', (item,)))

    def status(self, iid):
        return self.q('SELECT status FROM items WHERE id=?', (iid,))[0][0]

    def to_doing(self, iid, agent='a771470abcdef', lane='eng5'):
        self.ok('set', str(iid), 'status=ready')
        self.ok('set', str(iid), f'agent={agent}', f'lane={lane}', 'status=doing')

    def to_review(self, iid):
        self.to_doing(iid)
        self.ok('set', str(iid), 'status=review', 'note=finished')


class TestBasics(BoardCase):
    def test_db_folder_created_schema_and_wal(self):
        self.assertFalse(Path(os.environ['BOARD_DB']).parent.exists())
        self.ok('ls')
        con = self.db()
        try:
            self.assertEqual(con.execute('PRAGMA user_version').fetchone()[0], board.SCHEMA_VERSION)
            self.assertEqual(con.execute('PRAGMA journal_mode').fetchone()[0], 'wal')
            sql = con.execute("SELECT sql FROM sqlite_master WHERE name='items'").fetchone()[0]
            self.assertIn('STRICT', sql)
        finally:
            con.close()

    def test_new_prints_id_and_event(self):
        self.assertEqual(self.ok('new', '--kind', 'task', '--dept', 'eng', '--title', 'A'), '#1\n')
        self.assertEqual(self.ok('new', '--kind', 'bug', '--dept', 'art', '--title', 'B', '--sev', 'major'), '#2\n')
        self.assertEqual(self.events(1), 1)
        self.assertEqual(tuple(self.q('SELECT pri, status FROM items WHERE id=1')[0]), ('P2', 'todo'))

    def test_new_validation_hints(self):
        self.fail_hint('new', '--kind', 'epic', '--dept', 'eng', '--title', 'A',
                       hint='new --kind task --dept eng --title "<title>"')
        self.fail_hint('new', '--kind', 'task', '--dept', 'eng', '--title', 'A', '--level', 'god',
                       hint='--level mid')
        self.fail_hint('new', '--kind', 'task', '--dept', 'eng', hint='new --kind task --dept eng --title "<title>"')
        self.fail_hint('new', '--kind', 'task', '--dept', 'eng', '--title', 'A', '--key', '12', hint='--key T-001')
        self.fail_hint('new', '--kind', 'task', '--dept', 'eng', '--title', 'A', '--needs', '99', hint='ls --all')
        t = self.new()
        self.fail_hint('new', '--kind', 'task', '--dept', 'eng', '--title', 'A', '--parent', str(t),
                       hint='--parent <obj-id>')

    def test_new_key_retry_is_noop(self):
        a = self.ok('new', '--kind', 'task', '--dept', 'eng', '--title', 'A', '--key', 'T-1')
        n = self.events()
        b = self.ok('new', '--kind', 'task', '--dept', 'eng', '--title', 'A', '--key', 't-1')
        self.assertEqual(a, b)
        self.assertEqual(self.events(), n)
        self.fail_hint('new', '--kind', 'task', '--dept', 'eng', '--title', 'Other', '--key', 'T-1', hint='show 1')

    def test_ls_line_format_exact(self):
        self.new('task', 'eng', 'Line collides with dock planks', '--key', 'T-040a', '--level', 'mid', '--pri', 'P1')
        self.to_doing(1)
        self.ok('set', '1', 'pct=60')
        self.assertEqual(self.ok('ls'), '1 task eng mid doing P1 eng5 a771470 60% T-040a Line collides with dock planks\n')
        self.new('q', 'lead', 'Bare')
        self.assertEqual(self.ok('ls', '--kind', 'q'), '2 q lead - todo P2 - - - - Bare\n')

    def test_ls_title_cut_limit_and_more(self):
        self.new('task', 'eng', 'x' * 59 + ' ' + 'y' * 30)
        for i in range(24):
            self.new('task', 'eng', f'T{i}')
        out = self.ok('ls').splitlines()
        self.assertEqual(len(out), 21)
        self.assertEqual(out[-1], '+5 more')
        self.assertTrue(out[0].endswith(' ' + 'x' * 59))
        self.assertEqual(len(self.ok('ls', '-n', '0').splitlines()), 25)
        self.assertEqual(self.ok('ls', '-n', '3').splitlines()[-1], '+22 more')
        self.assertFalse(self.ok('ls').endswith('\n\n'))

    def test_ls_sort_and_filters(self):
        o = self.new('obj', 'lead', 'Objective', '--ms', 'M1')
        a = self.new('task', 'eng', 'A', '--parent', str(o), '--pri', 'P3', '--ms', 'M1', '--level', 'junior')
        b = self.new('task', 'art', 'B', '--pri', 'P0')
        c = self.new('task', 'eng', 'C', '--pri', 'P1')
        self.to_doing(c, agent='deadbeef99', lane='eng7')
        d = self.new('task', 'art', 'D')
        self.ok('set', str(d), 'status=dropped', 'note=nope')
        ids = [int(s.split()[0]) for s in self.ok('ls').splitlines()]
        self.assertEqual(ids, [b, o, a, c])  # todo (P0, P2, P3) then doing
        def only(*args):
            return [int(s.split()[0]) for s in self.ok('ls', *args).splitlines()]
        self.assertEqual(only('--dept', 'art'), [b])
        self.assertEqual(only('--status', 'doing'), [c])
        self.assertEqual(only('--status', 'dropped'), [d])
        self.assertEqual(only('--kind', 'obj'), [o])
        self.assertEqual(only('--parent', str(o)), [a])
        self.assertEqual(only('--ms', 'M1'), [o, a])
        self.assertEqual(only('--agent', 'deadbee'), [c])
        self.assertEqual(only('--lane', 'eng7'), [c])
        self.assertEqual(only('--level', 'junior'), [a])
        self.assertEqual(sorted(only('--all')), [o, a, b, c, d])
        self.fail_hint('ls', '--status', 'wip', hint='ls --status todo')
        rows = json.loads(self.ok('ls', '--json', '--dept', 'art'))
        self.assertEqual([r['id'] for r in rows], [b])
        self.assertEqual(rows[0]['title'], 'B')

    def test_show(self):
        n1 = self.new()
        t = self.new('task', 'eng', 'Show me', '--key', 'T-9', '--needs', str(n1),
                     '--files', 'Source/A.cpp:Foo,Source/B.h')
        for i in range(6):
            self.ok('note', str(t), f'n{i}')
        out = self.ok('show', 'T-9').splitlines()
        self.assertEqual(out[0], f'id: {t}')
        self.assertIn('key: T-9', out)
        self.assertIn('level: -', out)
        self.assertIn(f'needs: {n1} todo', out)
        self.assertIn('claims: Source/A.cpp:Foo, Source/B.h', out)
        evs = [s for s in out if s.startswith('event: ')]
        self.assertEqual(len(evs), 5)
        self.assertTrue(evs[-1].endswith('lead note n5'))
        self.assertTrue(all(': ' in s for s in out))
        self.assertEqual(self.ok('show', f'#{t}'), self.ok('show', 'T-9'))
        js = json.loads(self.ok('show', str(t), '--json'))
        self.assertEqual(js['needs'], [{'id': n1, 'status': 'todo'}])
        self.fail_hint('show', '404', hint='ls --all')

    def test_set_noop_and_events(self):
        t = self.new()
        n = self.events(t)
        self.assertEqual(self.ok('set', str(t), 'pri=P1', 'level=mid'), '')
        self.assertEqual(self.events(t), n + 2)
        self.ok('set', str(t), 'pri=p1', 'level=mid')
        self.assertEqual(self.events(t), n + 2)
        self.ok('set', str(t), 'ms=M1', 'pct=40%')
        self.assertEqual(tuple(self.q('SELECT milestone, pct FROM items WHERE id=?', (t,))[0]), ('M1', 40))
        self.ok('set', str(t), 'level=-')
        self.assertIsNone(self.q('SELECT level FROM items WHERE id=?', (t,))[0][0])
        self.fail_hint('set', str(t), 'colour=red', hint=f'set {t} status=ready')
        self.fail_hint('set', str(t), 'pct=140', hint=f'set {t} pct=50')
        self.fail_hint('set', str(t), 'title=', hint=f'set {t} title="<title>"')
        self.fail_hint('set', str(t), 'nonsense', hint=f'set {t} status=ready')

    def test_set_needs_and_cycle(self):
        a, b = self.new(), self.new()
        self.ok('set', str(b), f'needs={a}')
        self.fail_hint('set', str(a), f'needs={b}', hint=f'show {b}')
        n = self.events(b)
        self.ok('set', str(b), f'needs={a}')
        self.assertEqual(self.events(b), n)
        self.ok('set', str(b), 'needs=-')
        self.assertEqual(self.q('SELECT COUNT(*) FROM deps')[0][0], 0)

    def test_as_actor_recorded(self):
        t = self.new()
        self.ok('--as', 'eng-mgr', 'set', str(t), 'pri=P0')
        self.assertEqual(self.q('SELECT actor FROM events ORDER BY id DESC LIMIT 1')[0][0], 'eng-mgr')
        os.environ['BOARD_AS'] = 'qa-mgr'
        self.ok('note', str(t), 'hi')
        self.assertEqual(self.q('SELECT actor FROM events ORDER BY id DESC LIMIT 1')[0][0], 'qa-mgr')

    def test_note_and_dedupe(self):
        t = self.new()
        self.assertEqual(self.ok('note', str(t), 'first', 'words'), '')
        self.ok('note', str(t), 'first words')
        self.assertEqual(self.events(t), 2)
        self.assertEqual(self.q('SELECT note FROM events ORDER BY id DESC LIMIT 1')[0][0], 'first words')

    def test_tree_and_log(self):
        o = self.new('obj', 'lead', 'Goal')
        k1 = self.new('task', 'eng', 'Kid1', '--parent', str(o))
        k2 = self.new('task', 'art', 'Kid2', '--parent', str(o))
        out = self.ok('tree', str(o)).splitlines()
        self.assertEqual(len(out), 3)
        self.assertTrue(out[0].startswith(f'{o} obj lead'))
        self.assertTrue(out[1].startswith(f'  {k1} task eng'))
        self.assertTrue(out[2].startswith(f'  {k2} task art'))
        self.ok('note', str(k1), 'hello')
        log = self.ok('log').splitlines()
        self.assertEqual(len(log), 4)
        self.assertRegex(log[-1], rf'^\d\d-\d\d \d\d:\d\d #{k1} lead note hello$')
        self.assertEqual(len(self.ok('log', '-n', '2').splitlines()), 2)
        self.assertEqual(len(self.ok('log', str(k1)).splitlines()), 2)
        self.assertIn(' new todo', self.ok('log', str(o)))
        self.assertEqual(json.loads(self.ok('log', str(k1), '-n', '1', '--json'))[0]['note'], 'hello')


class TestGuards(BoardCase):
    def test_todo_to_doing_refused(self):
        t = self.new()
        self.fail_hint('set', str(t), 'status=doing', hint=f'set {t} status=ready')
        self.assertEqual(self.status(t), 'todo')

    def test_ready_to_doing_needs_agent_and_lane_for_code(self):
        t = self.new('task', 'eng')
        self.ok('set', str(t), 'status=ready')
        self.fail_hint('set', str(t), 'status=doing', hint=f'set {t} agent=<agent-id> lane=<lane> status=doing')
        self.fail_hint('set', str(t), 'status=doing', 'agent=abc', hint=f'set {t} lane=<lane> status=doing')
        qa = self.new('task', 'qa')
        self.ok('set', str(qa), 'status=ready')
        self.fail_hint('set', str(qa), 'agent=abc', 'status=doing', hint=f'set {qa} lane=<lane> status=doing')
        art = self.new('task', 'art')
        self.ok('set', str(art), 'status=ready')
        self.fail_hint('set', str(art), 'status=doing', hint=f'set {art} agent=<agent-id> status=doing')
        self.ok('set', str(art), 'agent=abc', 'status=doing')
        self.assertEqual(self.status(art), 'doing')

    def test_doing_to_review_needs_packet_report_or_note(self):
        t = self.new()
        self.to_doing(t)
        self.fail_hint('set', str(t), 'status=review', hint=f'set {t} status=review note="<summary>"')
        pk = self.tmp / 'root' / 'Saved' / 'AgentLogs' / 'tasks' / str(t)
        pk.mkdir(parents=True)
        err = self.fail_hint('set', str(t), 'packet=Saved/AgentLogs/tasks/1', 'status=review')
        self.assertIn('report.md is missing', err)
        (pk / 'report.md').write_text('done')
        self.ok('set', str(t), f'packet=Saved/AgentLogs/tasks/{t}', 'status=review')
        u = self.new()
        self.to_doing(u)
        self.ok('note', str(u), 'all done, see commit')
        self.ok('set', str(u), 'status=review')
        v = self.new()
        self.to_doing(v)
        self.ok('set', str(v), 'status=review', 'note=done')
        self.assertEqual([self.status(i) for i in (t, u, v)], ['review'] * 3)

    def test_review_merge_done(self):
        t = self.new()
        self.to_review(t)
        self.ok('set', str(t), 'status=merge')
        self.fail_hint('set', str(t), 'status=done', hint=f'set {t} status=done commit=<hash>')
        self.ok('set', str(t), 'status=done', 'commit=abc1234')
        art = self.new('task', 'art')
        self.ok('set', str(art), 'status=ready')
        self.ok('set', str(art), 'agent=x', 'status=doing')
        self.ok('set', str(art), 'status=review', 'note=ok')
        self.fail_hint('set', str(art), 'status=merge', hint=f'set {art} lane=<lane> status=merge')
        self.ok('set', str(art), 'status=done', 'evidence=Saved/AgentLogs/x.png')
        n = self.new()
        self.fail_hint('set', str(n), 'status=done', hint=f'set {n} status=done commit=<hash> note="<where it landed>"')

    def test_todo_ready_to_done_needs_commit_and_note_in_same_set(self):
        t = self.new()
        hint = f'set {t} status=done commit=<hash> note="<where it landed>"'
        self.fail_hint('set', str(t), 'status=done', 'note=merged earlier', hint=hint)
        self.fail_hint('set', str(t), 'status=done', 'commit=abc1234', hint=hint)
        self.ok('set', str(t), 'commit=abc1234')
        self.fail_hint('set', str(t), 'status=done', 'note=merged earlier', hint=hint)
        self.ok('set', str(t), 'status=done', 'commit=abc1234', 'note=merged in abc1234 before the item existed')
        r = self.new()
        self.ok('set', str(r), 'status=ready')
        self.fail_hint('set', str(r), 'status=done', 'evidence=x.png', 'note=x', hint=f'set {r} status=done commit=<hash> note="<where it landed>"')
        self.ok('set', str(r), 'status=done', 'commit=def5678', 'note=already on main')
        self.assertEqual([self.status(i) for i in (t, r)], ['done', 'done'])
        d = self.new()
        self.ok('set', str(d), 'status=ready')
        self.assertIn('ready->review not allowed', self.fail_hint('set', str(d), 'status=review', 'note=x'))

    def test_blocked_needs_note_and_restores_previous(self):
        t = self.new()
        self.to_doing(t)
        self.fail_hint('set', str(t), 'status=blocked', hint=f'set {t} status=blocked note="<reason>"')
        self.ok('set', str(t), 'status=blocked', 'note=waiting on art')
        self.assertIn('was: doing', self.ok('show', str(t)).splitlines())
        n = self.events(t)
        self.ok('set', str(t), 'status=blocked', 'note=waiting on art')
        self.assertEqual(self.events(t), n)
        self.fail_hint('set', str(t), 'status=review', hint=f'set {t} status=doing')
        self.ok('set', str(t), 'status=doing')
        self.assertIsNone(self.q('SELECT prev FROM items WHERE id=?', (t,))[0][0])
        d = self.new()
        self.ok('set', str(d), 'status=dropped', 'note=x')
        self.fail_hint('set', str(d), 'status=blocked', 'note=y', hint=f'set {d} status=todo note="<why reopen>"')

    def test_dropped_needs_note_and_reopen(self):
        t = self.new()
        self.fail_hint('set', str(t), 'status=dropped', hint=f'set {t} status=dropped note="<reason>"')
        self.ok('set', str(t), 'status=dropped', 'note=out of scope')
        self.fail_hint('set', str(t), 'status=todo', hint=f'set {t} status=todo note="<why>"')
        self.ok('set', str(t), 'status=todo', 'note=back in scope')

    def test_done_to_doing_rework_needs_note(self):
        t = self.new()
        self.to_review(t)
        self.ok('set', str(t), 'status=done', 'commit=abc')
        self.fail_hint('set', str(t), 'status=doing', hint=f'set {t} status=doing note="<why>"')
        self.fail_hint('set', str(t), 'status=review', 'note=x', hint=f'set {t} status=doing note="<why>"')
        self.ok('set', str(t), 'status=doing', 'note=regressed')
        self.fail_hint('set', str(t), 'status=todo', hint=f'set {t} status=review note="<summary>"')

    def test_review_back_to_doing_needs_note(self):
        t = self.new()
        self.to_review(t)
        self.fail_hint('set', str(t), 'status=doing', hint=f'set {t} status=doing note="<why>"')
        self.ok('set', str(t), 'status=doing', 'note=changes requested')

    def test_obj_closes_only_when_children_closed(self):
        o = self.new('obj', 'lead', 'Goal')
        k = self.new('task', 'art', 'Kid', '--parent', str(o))
        self.fail_hint('set', str(o), 'status=done', hint=f'ls --parent {o}')
        self.fail_hint('set', str(o), 'status=dropped', 'note=x', hint=f'ls --parent {o}')
        self.ok('set', str(o), 'status=doing')
        self.ok('set', str(k), 'status=dropped', 'note=not needed')
        self.ok('set', str(o), 'status=done')
        self.assertIn('children: 1 (0 open)', self.ok('show', str(o)))

    def test_question_done_needs_answer(self):
        q = self.new('q', 'lead', 'Blue or green?')
        self.fail_hint('set', str(q), 'status=done', hint=f'set {q} status=done note="<answer>"')
        self.ok('set', str(q), 'status=done', 'note=green')


class TestNextClaimsBatch(BoardCase):
    def test_next_with_deps(self):
        a = self.new('task', 'eng', 'A', '--pri', 'P2')
        c = self.new('task', 'eng', 'C')
        b = self.new('task', 'eng', 'B', '--pri', 'P0', '--needs', str(c))
        art = self.new('task', 'art', 'Art', '--level', 'junior')
        o = self.new('obj', 'lead', 'Obj')
        for i in (a, b, art, o):
            self.ok('set', str(i), 'status=ready')
        ids = lambda *x: [int(s.split()[0]) for s in self.ok('next', *x).splitlines()]  # noqa: E731
        self.assertEqual(ids(), [a, art])
        self.assertEqual(ids('--dept', 'art'), [art])
        self.assertEqual(ids('--level', 'junior'), [art])
        self.ok('set', str(c), 'status=ready')
        self.ok('set', str(c), 'agent=z', 'lane=eng1', 'status=doing')
        self.ok('set', str(c), 'status=review', 'note=x')
        self.assertNotIn(b, ids())
        self.ok('set', str(c), 'status=done', 'commit=abc')
        self.assertEqual(ids(), [b, a, art])
        self.assertEqual(self.ok('next', '-n', '1').splitlines()[-1], '+2 more')

    def test_claim_overlap_file_and_symbol(self):
        a = self.new('task', 'eng', 'A', '--files', 'Source/VibeGame/Fish.cpp:Roll')
        b = self.new('task', 'eng', 'B')
        self.assertEqual(self.ok('claim', str(b), 'Source/VibeGame/Fish.cpp:Cast'), '')  # other symbol: no overlap
        out = self.ok('claim', str(b), 'Source/VibeGame/Fish.cpp:Roll')
        self.assertEqual(out, f'warn: #{b} Source/VibeGame/Fish.cpp:Roll overlaps #{a} Source/VibeGame/Fish.cpp:Roll '
                              '(todo -)\n')
        c = self.new('task', 'eng', 'C')
        out = self.ok('claim', str(c), 'Source\\VibeGame\\Fish.cpp')  # whole file: overlaps every symbol
        self.assertEqual(out.count('warn: '), 3)
        self.assertIn(f'#{c} Source/VibeGame/Fish.cpp overlaps', out)
        d = self.new('task', 'eng', 'D')
        out = self.ok('claim', str(d), board.ROOT.as_posix() + '/Source/VibeGame/Fish.cpp:Cast',
                      board.ROOT.as_posix() + '-lanes/eng5/Source/Other.h')
        self.assertIn(f'#{d} Source/VibeGame/Fish.cpp:Cast overlaps #{b}', out)
        self.assertIn('claims: Source/Other.h, Source/VibeGame/Fish.cpp:Cast', self.ok('show', str(d)))
        e = self.new('task', 'eng', 'E')
        self.assertIn('overlaps', self.ok('claim', str(e), 'Source/Other.h'))
        n = self.events(e)
        self.assertEqual(self.ok('claim', str(e), 'Source/Other.h'), '')  # retry: no-op
        self.assertEqual(self.events(e), n)
        f = self.new('task', 'eng', 'F')
        self.assertIn('overlaps', self.ok('claim', str(f), 'Source/VibeGame/'))  # folder claim
        conf = self.ok('conflicts').splitlines()
        self.assertIn(f'#{a} Source/VibeGame/Fish.cpp:Roll | #{b} Source/VibeGame/Fish.cpp:Roll', conf)
        self.assertIn(f'#{d} Source/Other.h | #{e} Source/Other.h', conf)
        self.assertEqual(len(json.loads(self.ok('conflicts', '--json'))), len(conf))
        # closed items don't collide
        self.ok('set', str(e), 'status=dropped', 'note=x')
        self.assertNotIn(f'#{e} ', self.ok('conflicts'))
        self.ok('set', str(e), 'status=todo', 'note=back')
        # unclaim one path, then everything
        self.ok('unclaim', str(e), 'Source/Other.h')
        self.assertNotIn(f'#{e} ', self.ok('conflicts'))
        self.ok('unclaim', str(b), 'Source/VibeGame/Fish.cpp:Cast')
        self.assertIn('claims: Source/VibeGame/Fish.cpp:Roll', self.ok('show', str(b)))
        self.ok('unclaim', str(c))
        self.assertIn('claims: -', self.ok('show', str(c)))
        self.assertEqual(self.ok('unclaim', str(c)), '')

    def test_batch_all_or_nothing(self):
        script = '''# plan
new --kind obj --dept lead --title "Palm Key" --key O-1

new --kind task --dept eng --title "Cast line" --key T-1 --parent O-1
set T-1 status=ready pri=P1
python C:/GameDev/VibeGame/tools/board.py claim T-1 Source\\VibeGame\\Cast.cpp
'''
        out = self.ok('batch', stdin=script)
        self.assertEqual(out, '#1\n#2\n')
        self.assertEqual(self.status(2), 'ready')
        self.assertIn('claims: Source/VibeGame/Cast.cpp', self.ok('show', 'T-1'))
        before = (self.q('SELECT COUNT(*) FROM items')[0][0], self.events())
        bad = '''new --kind task --dept eng --title "Will vanish" --key T-2
set T-1 pri=P0
set T-2 status=doing
'''
        err = self.fail_hint('batch', stdin=bad, hint='set 3 status=ready')
        self.assertIn('batch line 3 (nothing applied)', err)
        self.assertEqual((self.q('SELECT COUNT(*) FROM items')[0][0], self.events()), before)
        self.assertEqual(self.q('SELECT pri FROM items WHERE id=2')[0][0], 'P1')
        self.fail_hint('batch', stdin='batch\n', hint='ls')
        self.fail_hint('batch', stdin='set 1 title="unterminated\n', hint='batch')


class TestExportResumeMisc(BoardCase):
    def test_export_import_round_trip(self):
        o = self.new('obj', 'lead', 'Goal', '--ms', 'M1')
        t = self.new('task', 'eng', 'Tâsk ünïcode', '--parent', str(o), '--ms', 'M1', '--files', 'Source/A.cpp:Foo')
        u = self.new('task', 'art', 'Other', '--needs', str(t))
        self.to_doing(t)
        self.ok('set', str(u), 'status=blocked', 'note=needs t')
        self.ok('export')
        root = self.tmp / 'root'
        jl = (root / 'docs' / 'board' / 'items.jsonl').read_text(encoding='utf-8')
        md = (root / 'docs' / 'BOARD.md').read_text(encoding='utf-8')
        self.assertIn('## M1', md)
        self.assertIn(f'- {t} task eng - doing P2 eng5 a771470 - - Tâsk ünïcode', md)
        self.assertTrue(md.endswith('\n') and not md.endswith('\n\n'))
        os.environ['BOARD_DB'] = str(self.tmp / 'db2' / 'board.db')
        self.ok('import', str(root / 'docs' / 'board' / 'items.jsonl'))
        self.ok('export')
        jl2 = (root / 'docs' / 'board' / 'items.jsonl').read_text(encoding='utf-8')
        l1, l2 = jl.splitlines(), jl2.splitlines()
        self.assertEqual(l2[:-1], l1)  # identical, plus one import event
        self.assertEqual(json.loads(l2[-1])['verb'], 'import')
        self.assertIn('was: todo', self.ok('show', str(u)))
        src = str(root / 'docs' / 'board' / 'items.jsonl')
        self.fail_hint('import', src, hint=f'import {src} --replace')
        self.ok('import', str(root / 'docs' / 'board' / 'items.jsonl'), '--replace')
        self.assertEqual(self.q('SELECT COUNT(*) FROM items')[0][0], 3)
        self.assertEqual(self.new(), 4)

    def test_resume_and_orphans(self):
        live = self.new('task', 'eng', 'Live', '--key', 'T-L')
        dead = self.new('task', 'eng', 'Dead', '--key', 'T-D')
        rev = self.new('task', 'art', 'Rev')
        todo = self.new('task', 'eng', 'Todo')
        self.to_doing(live, agent='a11ce1234567')
        self.to_doing(dead, agent='adead7654321')
        self.ok('set', str(rev), 'status=ready')
        self.ok('set', str(rev), 'agent=zzz', 'status=doing', 'handoff=Saved/AgentLogs/handoff/x.md')
        self.ok('set', str(rev), 'status=review', 'note=see packet')
        with mock.patch.object(board, 'agent_running', side_effect=lambda a: a.startswith('a11ce')) as m:
            out = self.ok('resume').splitlines()
        self.assertEqual(sorted(c.args[0] for c in m.call_args_list), ['a11ce1234567', 'adead7654321'])
        self.assertEqual(len(out), 4)
        self.assertRegex(out[0], rf'^{live} doing eng5 a11ce12 \d+m T-L handoff=- \| Live \| .* lead status ready->doing$')
        self.assertTrue(out[2].startswith(f'{rev} review - zzz '))
        self.assertIn('handoff=Saved/AgentLogs/handoff/x.md', out[2])
        self.assertTrue(out[2].endswith('lead status doing->review see packet'))
        self.assertEqual(out[3], f'orphan {dead} adead76 T-D no running transcript')
        self.assertFalse(any(f'{todo} ' == s[:len(str(todo)) + 1] for s in out))
        with mock.patch.object(board, 'agent_running', return_value=True):
            js = json.loads(self.ok('resume', '--json'))
        self.assertEqual(js['orphans'], [])
        self.assertEqual(len(js['items']), 3)

    def test_transcript_running_rule(self):
        base = self.tmp / 'projects'
        sub = base / 'session-1' / 'subagents'
        sub.mkdir(parents=True)
        busy = sub / 'agent-busy123abc.jsonl'
        busy.write_text('{"stop_reason":"end_turn"}\n{"message":{"stop_reason":"tool_use"}}\n'.replace(' ', ''))
        fin = sub / 'agent-fin456.jsonl'
        fin.write_text('{"stop_reason":"tool_use"}\n{"stop_reason":"end_turn"}\n')
        stale = sub / 'agent-old789.jsonl'
        stale.write_text('{"stop_reason":"tool_use"}\n')
        old = time.time() - (board.ACTIVE_MINUTES + 5) * 60
        os.utime(stale, (old, old))
        self.assertTrue(board.transcript_running(busy))
        self.assertFalse(board.transcript_running(fin))
        self.assertFalse(board.transcript_running(stale))
        self.assertFalse(board.transcript_running(sub / 'missing.jsonl'))
        with mock.patch.object(board, 'projects_dir', return_value=base):
            self.assertTrue(board.agent_running('busy123'))
            self.assertFalse(board.agent_running('fin456'))
            self.assertFalse(board.agent_running('old789'))
            self.assertFalse(board.agent_running('nobody'))
        self.assertTrue(str(board.projects_dir()).endswith(os.sep.join(['.claude', 'projects', 'C--GameDev-VibeGame'])))

    def test_schema_guard(self):
        self.ok('ls')
        con = sqlite3.connect(os.environ['BOARD_DB'])
        con.execute('PRAGMA user_version=99')
        con.commit()
        con.close()
        err = self.fail_hint('ls', '--dept', 'eng', hint='C:/GameDev/VibeGame/tools/board.py ls --dept eng')
        self.assertIn('use the main copy', err)

    def test_unknown_command_and_help(self):
        self.fail_hint('frobnicate', hint='ls')
        self.fail_hint(hint='ls')
        self.assertIn('usage', self.ok('help'))

    def test_selftest_command(self):
        class One(unittest.TestCase):
            def test_x(self):
                pass
        suite = unittest.TestSuite([One('test_x')])
        with mock.patch.object(board, 'load_suite', return_value=suite):
            self.assertEqual(self.ok('selftest'), 'ok 1 tests\n')

        class Bad(unittest.TestCase):
            def test_y(self):
                self.fail('boom')
        with mock.patch.object(board, 'load_suite', return_value=unittest.TestSuite([Bad('test_y')])):
            code, out, err = self.run_cmd('selftest')
        self.assertEqual(code, 1)
        self.assertIn('1 of 1 tests failed', err)
        self.assertTrue(err.rstrip().endswith('-p test_board.py -v'))

    def test_cli_process_exit_code_and_streams(self):
        env = dict(os.environ)
        exe = [sys.executable, str(TOOLS / 'board.py')]
        r = subprocess.run(exe + ['set', '1', 'status=done'], env=env, capture_output=True)
        self.assertEqual(r.returncode, 1)
        self.assertEqual(r.stdout, b'')
        self.assertRegex(r.stderr.decode(), r'^board: no item 1; fix: python .*/tools/board\.py ls --all\n$')
        r = subprocess.run(exe + ['new', '--kind', 'task', '--dept', 'eng', '--title', 'Ünï'], env=env,
                           capture_output=True)
        self.assertEqual((r.returncode, r.stdout), (0, b'#1\n'))
        r = subprocess.run(exe + ['ls'], env=env, capture_output=True)
        self.assertEqual(r.stdout, '1 task eng - todo P2 - - - - Ünï\n'.encode('utf-8'))


if __name__ == '__main__':
    unittest.main()
