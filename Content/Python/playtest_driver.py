"""Lure playtest driver (T-025): play the game in PIE through vibegame_tools run_python, one short call at a time.

Quick start (each block = one run_python call; `result = ...` is what you get back):

    import importlib, playtest_driver as pd
    importlib.reload(pd)                       # safe any time: live state (input, tick callback, log) survives reloads
    result = pd.begin_session("T026-swim")     # report folder Saved/AgentLogs/playtest/<stamp>-T026-swim/ + session.log
    result = pd.start_pie()                    # start_pie(players=2) = listen server + 1 client; settings restored later

    import playtest_driver as pd
    result = pd.wait_pie()                     # {"ready": true, ...} once every player has a pawn; call again if false

    result = pd.teleport("tp_T3")              # marker id or tag, via the C++ command Lure.Teleport (server side)
    result = pd.move(forward=1.0, frames=90)   # walks for 90 frames AFTER this call returns

    result = pd.state()                        # next call: where am I, stance, speed, swimming, fishing ...
    result = pd.screenshot("walked")           # <session folder>/walked.png WITH the HUD/prompts (ui=False: 3D only)
    result = pd.stop_pie()                     # also stops all input and restores the play settings

Rules
  - Never sleep inside a call: the game cannot tick while Python runs. Everything that takes time (holds, looks,
    after/script steps) runs from ONE slate post-tick callback after your call returns; check results next call.
  - player=N on every player helper: 0 = the host (or the only player), 1 = the first client, ... Input, views,
    stance and screenshots act in that player's own world; teleport and give_fish run in the server world for that
    player's PlayerId (server-authoritative).
  - Button actions (Jump, Sprint, Crouch, Prone, Cast, Hook, ...) are held with x=1; Move is (x=right, y=forward);
    Look is degrees per frame (x=yaw right, y=pitch up). Action names come from LureInputSubsystem (see actions()).
  - Every helper appends a line to the session log (session.log in the session folder).
  - Enum values in state() are Python enum names (e.g. stance "CROUCH").

Helpers (every name here exists; self_check() verifies it)
  Session   begin_session(topic, folder=None)    new report folder + session.log; returns the paths
            set_folder(path) / folder()          change / get the output folder (screenshots, session.log)
            session_log(text)                    add your own note line to session.log
            log_path()                           path of session.log
            editor_log(pattern="LogLureDev", lines=20)  last matching lines of the editor log (dev command output)
  PIE       start_pie(players=1, level=None)     request PIE (in the level viewport); players>=2 = listen server
            wait_pie()                           {"ready": bool, ...}; call until ready
            pie_status()                         running, players (role, pawn, PlayerId), frame
            stop_pie()                           stop input, end PIE, restore play settings
            restore_play_settings()              put back the play settings start_pie changed (stop_pie does this)
  Players   worlds(), world(player=0), server_world(), pc(player=0), pawn(player=0), server_pc(player=0),
            server_pawn(player=0), player_id(player=0), input_subsystem(player=0)   (these return unreal objects)
  Input     actions()                            the action names you can use
            hold(action, x=1.0, y=0.0, frames=None, seconds=None, player=0)   inject every frame until released
            release(action=None, player=0)       stop one hold, or all of that player's (player=None: everyone's)
            tap(action, frames=3, player=0)      press for a few frames (one Started/Completed)
            move(forward=1.0, right=0.0, frames=None, seconds=None, player=0)
            look(yaw, pitch=0.0, frames=10, player=0)   turn by yaw/pitch degrees, spread over N frames
            set_view(yaw=None, pitch=None, player=0)    set the control rotation directly
            holds()                              what is injected right now
            stop_input()                         drop all holds, looks and scheduled steps
  Time      frame()                              frames counted by the driver's tick
            wait_frames(n)                       returns at once: {"now", "until"}; check with done(until)
            done(target_frame)                   True once that frame has passed
            after(frames, fn, *args, **kwargs)   run fn N frames from now (from the tick); result in results()
            script(steps)                        steps = [(frame_offset, "helper_name", arg, ...), ...]
            results(ids=None) / pending()        results of after/script steps / steps not run yet
                                                 (results are cleared by begin_session and start_pie)
  Move      teleport(marker, index=0, player=0)  Lure.Teleport: "tp_T3"/"T3", "Lure.FishingSpot", "dock_end", ...
            teleport_to(x, y, z, yaw=None, player=0)   feet location in cm
            markers(tag=None, all=False)         tagged markers (Lure.* tags, or one tag) with location and tags;
                                                 without a tag, labels, lights and patrol points are left out
                                                 unless all=True
  State     state(player=0, server=False)        location, velocity, stance, sprinting, swimming, eye height,
                                                 capsule, view, fishing (if the fishing component exists), holds
            states()                             state() of every player
  Commands  console(command, player=0)           run a console command in that player's world
            set_stance(stance, player=0, retry_frames=60)   Lure.SetStance Stand|Crouch|Prone (like the stance
                                                 keys); a refused request (e.g. Prone while still falling right
                                                 after a teleport) is re-sent every frame until it sticks
            give_fish(species, rarity=None, seed=None, player=0)   Lure.GiveFish: lands it like a catch
                                                 (cooler + XP); read editor_log for the result
  Pictures  screenshot(name, player=0, width=1280, height=720, ui=True)   PNG in the session folder.
                                                 ui=True: Lure.Screenshot in that player's viewport, with the
                                                 HUD text and prompts, viewport size, written at once.
                                                 ui=False: 3D view only at width x height, after the next frame.
                                                 exists(name) to check
            exists(name_or_path)                 True once a screenshot file is written
  Checks    self_check(report_path=None, play_settings_roundtrip=False)   every unreal API used exists (tests)
"""

import functools
import json
import math
import os
import re
import sys
import time

import unreal

DRIVER_VERSION = "1.1 (T-025 follow-ups)"

HELPERS = [
    "begin_session", "set_folder", "folder", "session_log", "log_path", "editor_log",
    "start_pie", "wait_pie", "pie_status", "stop_pie", "restore_play_settings",
    "worlds", "world", "server_world", "pc", "pawn", "server_pc", "server_pawn", "player_id", "input_subsystem",
    "actions", "hold", "release", "tap", "move", "look", "set_view", "holds", "stop_input",
    "frame", "wait_frames", "done", "after", "script", "results", "pending",
    "teleport", "teleport_to", "markers",
    "state", "states",
    "console", "set_stance", "give_fish",
    "screenshot", "exists",
    "self_check",
]

_STATE_KEY = "_lure_playtest_driver_state"
_PROJECT_DIR = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
_PLAY_SETTINGS_PATH = "/Script/UnrealEd.Default__LevelEditorPlaySettings"
_PLAY_SETTING_NAMES = ["PlayNetMode", "PlayNumberOfClients", "RunUnderOneProcess"]
_LOG_RESULT_CHARS = 300
_MAX_RESULTS = 200
_MAX_ERRORS = 20


def _new_state():
    return {
        "cb": None,                    # slate post-tick callback handle
        "frame": 0,                    # ticks seen by the callback
        "holds": {},                   # (player, action) -> hold
        "looks": [],                   # spread-out Look injections
        "scheduled": [],               # after()/script() steps
        "results": {},                 # step id -> result
        "next_id": 1,
        "errors": [],                  # last tick errors
        "folder": None,                # output folder (screenshots, session.log)
        "log": None,                   # session.log path
        "saved_play_settings": None,   # what start_pie changed (restored by stop_pie)
        "players": 1,                  # players the last start_pie asked for
        "sub_cache": {},               # player -> (world path, EnhancedInputLocalPlayerSubsystem)
    }


if not hasattr(unreal, _STATE_KEY):
    setattr(unreal, _STATE_KEY, _new_state())
S = getattr(unreal, _STATE_KEY)
for _key, _value in _new_state().items():  # keys added by a newer driver after a reload
    S.setdefault(_key, _value)


# ----------------------------------------------------------------------------------------------------------------------
# Small utilities
# ----------------------------------------------------------------------------------------------------------------------

def _r(value, digits=1):
    try:
        return round(float(value), digits)
    except Exception:
        return None


def _vec(v, digits=1):
    try:
        return [_r(v.x, digits), _r(v.y, digits), _r(v.z, digits)]
    except Exception:
        return None


def _enum(e):
    if e is None:
        return None
    name = getattr(e, "name", None)
    return name if isinstance(name, str) else str(e).split(".")[-1].split(":")[0].strip("<> ")


def _get(obj, name):
    """obj.name() if it is a method, obj.name if it is a property, None if missing or failing."""
    if obj is None:
        return None
    try:
        attr = getattr(obj, name)
    except Exception:
        try:
            return obj.get_editor_property(name)
        except Exception:
            return None
    try:
        return attr() if callable(attr) else attr
    except Exception:
        return None


def _plain(value, depth=0):
    """JSON-friendly copy of a result (unreal vectors -> lists, enums -> names, objects -> names)."""
    if depth > 6:
        return str(value)
    if value is None or isinstance(value, (bool, int, str)):
        return value
    if isinstance(value, float):
        return _r(value, 3)
    if isinstance(value, dict):
        return {str(k): _plain(v, depth + 1) for k, v in value.items()}
    if isinstance(value, (list, tuple, set)):
        return [_plain(v, depth + 1) for v in value]
    if isinstance(value, unreal.Vector):
        return _vec(value, 2)
    if isinstance(value, unreal.Rotator):
        return {"pitch": _r(value.pitch, 2), "yaw": _r(value.yaw, 2), "roll": _r(value.roll, 2)}
    if isinstance(value, unreal.EnumBase):
        return _enum(value)
    if isinstance(value, unreal.Object):
        try:
            return value.get_name()
        except Exception:
            return str(value)
    return str(value)


def _short(value):
    try:
        text = json.dumps(_plain(value), default=str)
    except Exception:
        text = str(value)
    return text if len(text) <= _LOG_RESULT_CHARS else text[:_LOG_RESULT_CHARS] + "..."


def _args_text(args, kwargs):
    parts = [_short(a) for a in args] + ["%s=%s" % (k, _short(v)) for k, v in kwargs.items()]
    return ", ".join(parts)


def _stamp():
    return time.strftime("%Y%m%d-%H%M%S")


def _log_line(text):
    try:
        path = log_path()
        with open(path, "a", encoding="utf-8") as fh:
            fh.write("%s.%03d f=%d %s\n" % (time.strftime("%H:%M:%S"), int((time.time() % 1) * 1000), S["frame"], text))
    except Exception:
        pass


_CALL_DEPTH = [0]


def _logged(fn):
    """Every public helper writes one line to session.log (call, result or error); helpers it calls do not."""
    @functools.wraps(fn)
    def wrapper(*args, **kwargs):
        top = _CALL_DEPTH[0] == 0
        _CALL_DEPTH[0] += 1
        try:
            result = fn(*args, **kwargs)
        except Exception as exc:
            if top:
                _log_line("%s(%s) !! %r" % (fn.__name__, _args_text(args, kwargs), exc))
            raise
        finally:
            _CALL_DEPTH[0] -= 1
        if top:
            _log_line("%s(%s) -> %s" % (fn.__name__, _args_text(args, kwargs), _short(result)))
        return result
    return wrapper


def _editor_subsystem(cls):
    return unreal.get_editor_subsystem(cls)


def _level_editor():
    return _editor_subsystem(unreal.LevelEditorSubsystem)


def _pie_running():
    try:
        return bool(_level_editor().is_in_play_in_editor())
    except Exception:
        return False


# ----------------------------------------------------------------------------------------------------------------------
# Session
# ----------------------------------------------------------------------------------------------------------------------

def _default_folder():
    return os.path.join(_PROJECT_DIR, "Saved", "AgentLogs", "playtest", _stamp() + "-session").replace("\\", "/")


@_logged
def begin_session(topic, folder=None):
    """Start a report folder (default Saved/AgentLogs/playtest/<yyyyMMdd-HHmmss>-<topic>/) with a fresh session.log."""
    safe = re.sub(r"[^A-Za-z0-9_.-]+", "-", str(topic)).strip("-") or "session"
    path = folder or os.path.join(_PROJECT_DIR, "Saved", "AgentLogs", "playtest", "%s-%s" % (_stamp(), safe))
    set_folder(path)
    S["results"] = {}
    _log_line("session '%s' started (driver %s, project %s)" % (topic, DRIVER_VERSION, _PROJECT_DIR))
    return {"folder": S["folder"], "log": S["log"]}


def set_folder(path):
    """Use path for screenshots and session.log (created if missing)."""
    path = os.path.abspath(path).replace("\\", "/")
    os.makedirs(path, exist_ok=True)
    S["folder"] = path
    S["log"] = path + "/session.log"
    return path


def folder():
    """The output folder (a default Saved/AgentLogs/playtest/<stamp>-session/ is made on first use)."""
    if not S["folder"]:
        set_folder(_default_folder())
    return S["folder"]


def log_path():
    """Path of session.log."""
    if not S["log"]:
        folder()
    return S["log"]


def session_log(text):
    """Append your own note (e.g. the scenario step) to session.log."""
    _log_line("NOTE " + str(text))
    return log_path()


def _editor_log_file():
    logs = os.path.join(_PROJECT_DIR, "Saved", "Logs")
    names = [n for n in os.listdir(logs) if n.endswith(".log") and "backup" not in n.lower()] if os.path.isdir(logs) else []
    if not names:
        return None
    names.sort(key=lambda n: os.path.getmtime(os.path.join(logs, n)), reverse=True)
    return os.path.join(logs, names[0])


def editor_log(pattern="LogLureDev", lines=20):
    """The last `lines` lines of the editor log containing `pattern` (e.g. Lure.* command results)."""
    path = _editor_log_file()
    if not path:
        return {"log": None, "lines": []}
    with open(path, "rb") as fh:
        fh.seek(0, os.SEEK_END)
        size = fh.tell()
        fh.seek(max(0, size - 512 * 1024))
        text = fh.read().decode("utf-8", errors="replace")
    hits = [line.rstrip() for line in text.splitlines() if pattern in line]
    return {"log": path.replace("\\", "/"), "lines": hits[-int(lines):]}


# ----------------------------------------------------------------------------------------------------------------------
# Tick: holds, looks and scheduled steps run from one slate post-tick callback
# ----------------------------------------------------------------------------------------------------------------------

def _tick_trampoline(delta_seconds):
    # Registered once; looks up _tick_impl on every call so importlib.reload() swaps in new code.
    impl = globals().get("_tick_impl")
    if impl is not None:
        impl(delta_seconds)


def _tick_error(where, exc):
    S["errors"].append({"frame": S["frame"], "where": where, "error": repr(exc)})
    del S["errors"][:-_MAX_ERRORS]


def _tick_impl(delta_seconds):
    S["frame"] += 1
    if (S["holds"] or S["looks"]) and not _pie_running():
        S["holds"].clear()
        S["looks"] = []
        _log_line("PIE is not running: input stopped")
    now = time.time()
    for key, h in list(S["holds"].items()):
        try:
            if (h["frames"] is not None and h["frames"] <= 0) or (h["until"] is not None and now >= h["until"]):
                S["holds"].pop(key, None)
                continue
            sub = _subsystem_for(h["player"])
            if sub is None:
                continue
            sub.inject_input_vector_for_action(h["action"], unreal.Vector(h["x"], h["y"], 0.0), [], [])
            if h["frames"] is not None:
                h["frames"] -= 1
        except Exception as exc:
            S["holds"].pop(key, None)
            _tick_error("hold %s" % (key,), exc)
    keep = []
    for lk in S["looks"]:
        try:
            if lk["frames"] <= 0:
                continue
            sub = _subsystem_for(lk["player"])
            if sub is not None:
                sub.inject_input_vector_for_action(lk["action"], unreal.Vector(lk["yaw"], lk["pitch"], 0.0), [], [])
                lk["frames"] -= 1
            keep.append(lk)
        except Exception as exc:
            _tick_error("look", exc)
    S["looks"] = keep
    if S["scheduled"]:
        due = [step for step in S["scheduled"] if step["at"] <= S["frame"]]
        if due:
            S["scheduled"] = [step for step in S["scheduled"] if step["at"] > S["frame"]]
            for step in due:
                try:
                    value = step["fn"](*step["args"], **step["kwargs"])
                    if step["id"] is not None:
                        S["results"][step["id"]] = {"step": step["name"], "frame": S["frame"], "result": _plain(value)}
                except Exception as exc:
                    if step["id"] is not None:
                        S["results"][step["id"]] = {"step": step["name"], "frame": S["frame"], "error": repr(exc)}
                    _tick_error("step %s" % step["name"], exc)
            for old in sorted(S["results"])[:-_MAX_RESULTS]:
                S["results"].pop(old, None)


def _ensure_tick():
    if S["cb"] is None:
        S["cb"] = unreal.register_slate_post_tick_callback(_tick_trampoline)
    return True


def _stop_tick():
    if S["cb"] is not None:
        try:
            unreal.unregister_slate_post_tick_callback(S["cb"])
        except Exception:
            pass
        S["cb"] = None


# ----------------------------------------------------------------------------------------------------------------------
# PIE
# ----------------------------------------------------------------------------------------------------------------------

def _play_settings_object():
    obj = unreal.find_object(None, _PLAY_SETTINGS_PATH)
    if obj is None:
        raise RuntimeError("LevelEditorPlaySettings not found (%s)" % _PLAY_SETTINGS_PATH)
    return obj


def _get_play_settings():
    raw = unreal.ToolsetLibrary.get_object_properties(_play_settings_object(), [unreal.Name(n) for n in _PLAY_SETTING_NAMES])
    return json.loads(raw) if raw else {}


def _set_play_settings(values):
    ok = unreal.ToolsetLibrary.set_object_properties(_play_settings_object(), json.dumps(values))
    if not ok:
        raise RuntimeError("could not set the play settings %s" % values)
    return _get_play_settings()


@_logged
def start_pie(players=1, level=None):
    """Request PIE in the level viewport. players>=2: listen server + (players-1) clients in one process.
    Loads `level` first if given (e.g. "/Game/Maps/Dev/L_Dev_Movement"). PIE starts on the next editor tick."""
    les = _level_editor()
    if les.is_in_play_in_editor():
        return {"requested": False, "error": "PIE is already running (pd.stop_pie() first)", "status": pie_status()}
    if level:
        if not les.load_level(level):
            raise RuntimeError("could not load level %s" % level)
    players = max(1, int(players))
    if players == 1:
        want = {"PlayNetMode": "PIE_Standalone", "PlayNumberOfClients": 1}
    else:
        want = {"PlayNetMode": "PIE_ListenServer", "PlayNumberOfClients": players, "RunUnderOneProcess": True}
    if S["saved_play_settings"] is None:
        S["saved_play_settings"] = _get_play_settings()
    now = _set_play_settings(want)
    S["players"] = players
    S["sub_cache"] = {}
    S["results"] = {}
    stop_input()
    _ensure_tick()
    les.editor_request_begin_play()
    return {"requested": True, "players": players, "play_settings": now, "restore_on_stop": S["saved_play_settings"],
            "next": "call pd.wait_pie() in your next step"}


def pie_status():
    """PIE running?, each player's world role, pawn and PlayerId, the driver frame and recent tick errors."""
    out = {"running": _pie_running(), "frame": S["frame"], "players": [], "errors": S["errors"][-3:]}
    if not out["running"]:
        return out
    for index, w in enumerate(worlds()):
        entry = {"player": index, "role": _role(w), "world": w.get_name(), "pawn": None, "player_id": None}
        try:
            controller = _local_pc(w)
            p = controller.get_controlled_pawn() if controller else None
            entry["pawn"] = p.get_name() if p else None
            entry["player_id"] = _pc_player_id(controller)
        except Exception as exc:
            entry["error"] = repr(exc)
        out["players"].append(entry)
    return out


@_logged
def wait_pie():
    """{"ready": true} once PIE runs and every expected player has a pawn. Call again in a later step if false."""
    status = pie_status()
    players = status["players"]
    status["ready"] = bool(status["running"] and len(players) >= S["players"] and all(p["pawn"] for p in players))
    status["expected_players"] = S["players"]
    return status


@_logged
def restore_play_settings():
    """Put back the play settings start_pie changed (no-op if none were changed)."""
    saved = S["saved_play_settings"]
    if saved is None:
        return {"restored": None}
    now = _set_play_settings(saved)
    S["saved_play_settings"] = None
    return {"restored": now}


@_logged
def stop_pie():
    """Stop all injected input, end PIE and restore the play settings."""
    stop_input()
    was_running = _pie_running()
    if was_running:
        _level_editor().editor_request_end_play()
    restored = restore_play_settings()
    S["sub_cache"] = {}
    _stop_tick()
    return {"stop_requested": was_running, "play_settings": restored}


# ----------------------------------------------------------------------------------------------------------------------
# Players and worlds
# ----------------------------------------------------------------------------------------------------------------------

def _is_server(w):
    try:
        return bool(unreal.SystemLibrary.is_server(w))
    except Exception:
        return True


def _role(w):
    try:
        if unreal.SystemLibrary.is_standalone(w):
            return "standalone"
    except Exception:
        pass
    return "server" if _is_server(w) else "client"


def _pie_instance(w):
    match = re.search(r"UEDPIE_(\d+)_", w.get_path_name())
    return int(match.group(1)) if match else 0


def worlds():
    """PIE worlds (dedicated servers excluded): the server/standalone world first, then clients by PIE instance."""
    found = []
    try:
        found = [w for w in unreal.EditorLevelLibrary.get_pie_worlds(False) if w is not None]
    except Exception:
        found = []
    if not found:
        game = _editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        found = [game] if game is not None else []
    found.sort(key=lambda w: (0 if _is_server(w) else 1, _pie_instance(w)))
    return found


def world(player=0):
    """The world where `player` is the local player."""
    found = worlds()
    if not found:
        raise RuntimeError("PIE is not running (pd.start_pie(), then pd.wait_pie())")
    if player < 0 or player >= len(found):
        raise IndexError("player %d: there are %d PIE world(s)" % (player, len(found)))
    return found[player]


def server_world():
    """The authoritative world (listen server or standalone)."""
    for w in worlds():
        if _is_server(w):
            return w
    raise RuntimeError("no server/standalone PIE world")


def _local_pc(w):
    for actor in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.PlayerController):
        try:
            if actor.is_local_controller():
                return actor
        except Exception:
            pass
    return None


def _pc_player_id(controller):
    ps = _get(controller, "player_state") if controller is not None else None
    if ps is None:
        return None
    value = _get(ps, "player_id")
    if value is None:
        value = _get(ps, "get_player_id")
    return int(value) if value is not None else None


def pc(player=0):
    """The local PlayerController of `player` (in that player's own world)."""
    controller = _local_pc(world(player))
    if controller is None:
        raise RuntimeError("player %d has no local PlayerController yet" % player)
    return controller


def pawn(player=0):
    """The pawn `player` controls, as seen in that player's own world (a client's autonomous proxy)."""
    return pc(player).get_controlled_pawn()


def player_id(player=0):
    """The PlayerId of `player` (PlayerState), or None."""
    return _pc_player_id(pc(player))


def server_pc(player=0):
    """The server's PlayerController for `player` (matched by PlayerId)."""
    sw = server_world()
    if player == 0 or len(worlds()) == 1:
        return _local_pc(sw)
    wanted = player_id(player)
    for actor in unreal.GameplayStatics.get_all_actors_of_class(sw, unreal.PlayerController):
        if _pc_player_id(actor) == wanted:
            return actor
    raise RuntimeError("no server PlayerController with PlayerId %s" % wanted)


def server_pawn(player=0):
    """The authoritative copy of `player`'s pawn (server world)."""
    return server_pc(player).get_controlled_pawn()


def input_subsystem(player=0):
    """The EnhancedInputLocalPlayerSubsystem of `player` (what the injection uses)."""
    return _subsystem_for(player, strict=True)


def _subsystem_for(player, strict=False):
    try:
        w = world(player)
    except Exception:
        if strict:
            raise
        return None
    key = w.get_path_name()
    cached = S["sub_cache"].get(player)
    if cached and cached[0] == key:
        return cached[1]
    for sub in unreal.ObjectIterator(unreal.EnhancedInputLocalPlayerSubsystem):
        local_player = sub.get_outer()
        if not isinstance(local_player, unreal.LocalPlayer):
            continue
        try:
            if local_player.get_world() == w:
                S["sub_cache"][player] = (key, sub)
                return sub
        except Exception:
            continue
    if strict:
        raise RuntimeError("no EnhancedInputLocalPlayerSubsystem for player %d" % player)
    return None


# ----------------------------------------------------------------------------------------------------------------------
# Input
# ----------------------------------------------------------------------------------------------------------------------

def actions():
    """Action names LureInputSubsystem knows (Move, Look, Jump, Sprint, Crouch, Prone, and the fishing ones)."""
    return [str(n) for n in unreal.LureInputSubsystem.get_input_action_names()]


def _action(name):
    clean = str(name)
    for prefix in ("IA_Lure_", "IA_"):
        if clean.startswith(prefix):
            clean = clean[len(prefix):]
    action = unreal.LureInputSubsystem.get_input_action_by_name(clean)
    if action is None:
        raise ValueError("unknown action %r (known: %s)" % (name, ", ".join(actions())))
    return clean, action


@_logged
def hold(action, x=1.0, y=0.0, frames=None, seconds=None, player=0):
    """Inject (x, y) into `action` every frame until release(), or for `frames` frames / `seconds` seconds."""
    name, act = _action(action)
    _subsystem_for(player, strict=True)
    _ensure_tick()
    S["holds"][(player, name)] = {
        "action": act, "name": name, "player": player, "x": float(x), "y": float(y),
        "frames": int(frames) if frames is not None else None,
        "until": (time.time() + float(seconds)) if seconds is not None else None,
    }
    return {"holding": name, "player": player, "x": x, "y": y, "frames": frames, "seconds": seconds, "frame": S["frame"]}


@_logged
def release(action=None, player=0):
    """Stop injecting `action` for `player`; action=None stops all of that player's holds; player=None everyone's."""
    name = _action(action)[0] if action is not None else None
    for key in list(S["holds"]):
        if (player is None or key[0] == player) and (name is None or key[1] == name):
            S["holds"].pop(key, None)
    return holds()


@_logged
def tap(action, frames=3, player=0):
    """Press a button for a few frames (one Started, one Completed). Tap again only after it finished."""
    return hold(action, 1.0, 0.0, frames=frames, player=player)


@_logged
def move(forward=1.0, right=0.0, frames=None, seconds=None, player=0):
    """Hold Move (forward/right in -1..1) until release("Move") or for frames/seconds."""
    return hold("Move", x=right, y=forward, frames=frames, seconds=seconds, player=player)


@_logged
def look(yaw, pitch=0.0, frames=10, player=0):
    """Turn by `yaw` degrees right and `pitch` degrees up through the Look action, spread over `frames` frames."""
    frames = max(1, int(frames))
    _subsystem_for(player, strict=True)
    _ensure_tick()
    S["looks"].append({"action": _action("Look")[1], "player": player, "yaw": float(yaw) / frames,
                       "pitch": float(pitch) / frames, "frames": frames})
    return {"look": [yaw, pitch], "frames": frames, "player": player}


@_logged
def set_view(yaw=None, pitch=None, player=0):
    """Set the control rotation directly (None keeps that axis)."""
    controller = pc(player)
    r = controller.get_control_rotation()
    controller.set_control_rotation(unreal.Rotator(roll=0.0, pitch=r.pitch if pitch is None else float(pitch),
                                                   yaw=r.yaw if yaw is None else float(yaw)))
    r = controller.get_control_rotation()
    return {"yaw": _r(r.yaw), "pitch": _r(r.pitch)}


def holds():
    """What is being injected now."""
    out = [{"player": h["player"], "action": h["name"], "x": h["x"], "y": h["y"], "frames_left": h["frames"],
            "seconds_left": _r(h["until"] - time.time(), 2) if h["until"] is not None else None} for h in S["holds"].values()]
    out += [{"player": lk["player"], "action": "Look", "yaw_per_frame": _r(lk["yaw"], 3), "pitch_per_frame": _r(lk["pitch"], 3),
             "frames_left": lk["frames"]} for lk in S["looks"]]
    return out


@_logged
def stop_input():
    """Drop every hold, look and scheduled step."""
    S["holds"].clear()
    S["looks"] = []
    dropped = len(S["scheduled"])
    S["scheduled"] = []
    return {"stopped": True, "dropped_steps": dropped}


# ----------------------------------------------------------------------------------------------------------------------
# Time
# ----------------------------------------------------------------------------------------------------------------------

def frame():
    """Frames counted by the driver's tick callback (starts counting once any input/step/PIE helper ran)."""
    return S["frame"]


def wait_frames(n):
    """Returns at once with {"now", "until"}; the game runs n frames after your call. Check done(until) later."""
    _ensure_tick()
    return {"now": S["frame"], "until": S["frame"] + max(0, int(n))}


def done(target_frame):
    """True once the driver's frame counter reached target_frame (a wait_frames()["until"])."""
    target = target_frame["until"] if isinstance(target_frame, dict) else int(target_frame)
    return S["frame"] >= target


@_logged
def after(frames, fn, *args, **kwargs):
    """Run fn(*args, **kwargs) `frames` frames from now (inside the tick). fn may be a helper name. Result: results()."""
    if isinstance(fn, str):
        name = fn
        fn = globals().get(fn)
        if name not in HELPERS or fn is None:
            raise ValueError("unknown helper %r" % name)
    _ensure_tick()
    step_id = S["next_id"]
    S["next_id"] += 1
    S["scheduled"].append({"id": step_id, "at": S["frame"] + max(0, int(frames)), "fn": fn, "args": args,
                           "kwargs": kwargs, "name": getattr(fn, "__name__", str(fn))})
    return {"id": step_id, "at_frame": S["frame"] + max(0, int(frames))}


@_logged
def script(steps):
    """Schedule several steps: [(frame_offset, "helper_name", arg, ...), ...] or (offset, "name", {kwargs}) as the
    last item. Example: pd.script([(0, "move", 1.0), (30, "tap", "Jump"), (42, "screenshot", "jump_apex"),
    (90, "release", "Move")])."""
    ids = []
    for step in steps:
        offset, name, rest = step[0], step[1], list(step[2:])
        kwargs = rest.pop() if rest and isinstance(rest[-1], dict) else {}
        ids.append(after(offset, name, *rest, **kwargs)["id"])
    return {"ids": ids, "now": S["frame"]}


def results(ids=None):
    """Results of after()/script() steps (all kept ones, or the given ids)."""
    if ids is None:
        return {str(k): v for k, v in sorted(S["results"].items())}
    if isinstance(ids, int):
        ids = [ids]
    return {str(i): S["results"].get(i) for i in ids}


def pending():
    """Scheduled steps that have not run yet."""
    return [{"id": s["id"], "step": s["name"], "at_frame": s["at"]} for s in S["scheduled"]]


# ----------------------------------------------------------------------------------------------------------------------
# Teleport and markers
# ----------------------------------------------------------------------------------------------------------------------

def _run_on_server(command, player):
    pid = player_id(player)
    if pid is not None:
        command += " Player=%d" % pid
    elif player != 0:
        raise RuntimeError("player %d has no PlayerId yet (PlayerState not replicated)" % player)
    unreal.SystemLibrary.execute_console_command(server_world(), command)
    return command


@_logged
def teleport(marker, index=0, player=0):
    """Lure.Teleport <marker> [index] for `player` (server side). Markers: tag Teleport=<id> (or tp_<id>), an exact
    tag (Lure.FishingSpot, Respawn=Dock), a Key=<id> tag value (dock_end), or an actor name. A client's own view
    catches up after replication (read state() next call)."""
    command = _run_on_server("Lure.Teleport %s %d" % (marker, int(index)), player)
    return {"command": command, "state": state(player)}


@_logged
def teleport_to(x, y, z, yaw=None, player=0):
    """Lure.Teleport X Y Z [Yaw]: feet at (x, y, z) cm, snapped to the floor below (within 5 m)."""
    command = "Lure.Teleport %.2f %.2f %.2f" % (float(x), float(y), float(z))
    if yaw is not None:
        command += " %.2f" % float(yaw)
    command = _run_on_server(command, player)
    return {"command": command, "state": state(player)}


_MARKER_NOISE_TAGS = ("lure.editorlabel", "lure.light", "lure.patrolpoint")


def markers(tag=None, all=False):
    """Actors with Lure.* tags (or with `tag` exactly / as a Key=... tag): name, label, location, yaw, tags.
    Without `tag`, editor labels, lights and patrol points (Lure.EditorLabel / Lure.Light / Lure.PatrolPoint) are left
    out unless all=True."""
    w = server_world() if _pie_running() else _editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    wanted = tag.lower() if tag else None
    out = []
    for actor in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.Actor):
        tags = [str(t) for t in actor.tags]
        if not tags:
            continue
        low = [t.lower() for t in tags]
        if wanted:
            hit = any(t == wanted or t.startswith(wanted + "=") for t in low)
        else:
            hit = any(t.startswith("lure.") for t in low)
            if hit and not all and any(t in _MARKER_NOISE_TAGS for t in low):
                hit = False
        if not hit:
            continue
        try:
            label = actor.get_actor_label()
        except Exception:
            label = actor.get_name()
        out.append({"name": actor.get_name(), "label": label, "loc": _vec(actor.get_actor_location()),
                    "yaw": _r(actor.get_actor_rotation().yaw), "tags": [t for t in tags if not t.startswith("LureLayout")]})
    out.sort(key=lambda m: m["label"].lower())
    return out


# ----------------------------------------------------------------------------------------------------------------------
# State
# ----------------------------------------------------------------------------------------------------------------------

_FISHING_GETTERS = ("get_fishing_state", "is_line_out", "is_charging", "get_charge", "is_rod_in_hand", "get_cast_block",
                    "get_status_text", "get_bobber_location")


def _fishing_state(character):
    try:
        components = character.get_components_by_class(unreal.ActorComponent)
    except Exception:
        return None
    for comp in components:
        cls = comp.get_class().get_name()
        if "Fishing" in cls and "Line" not in cls:
            out = {"component": cls}
            for getter in _FISHING_GETTERS:
                if hasattr(comp, getter):
                    key = getter[4:] if getter.startswith("get_") else getter
                    out[key] = _plain(_get(comp, getter))
            hooked = _get(comp, "get_hooked_fish")
            if hooked is not None:
                out["hooked_fish"] = str(_get(hooked, "species_id") or "")
            return out
    return None


@_logged
def state(player=0, server=False):
    """Player state (as seen in the player's own world; server=True reads the authoritative copy)."""
    w = server_world() if server else world(player)
    controller = server_pc(player) if server else pc(player)
    character = controller.get_controlled_pawn() if controller else None
    out = {"player": player, "side": "server" if server else "local", "role": _role(w), "player_id": _pc_player_id(controller),
           "frame": S["frame"], "pawn": character.get_name() if character else None}
    if controller is not None:
        r = controller.get_control_rotation()
        out["yaw"], out["pitch"] = _r(r.yaw), _r(r.pitch)
    if character is None:
        return out
    out["loc"] = _vec(character.get_actor_location())
    movement = _get(character, "get_lure_movement") or _get(character, "get_movement_component")
    if movement is not None:
        v = _get(movement, "velocity")
        if v is not None:
            out["vel"] = _vec(v)
            out["speed2d"] = _r(math.hypot(v.x, v.y))
        out["feet_z"] = _r(_get(movement, "get_feet_height"))
        out["movement_state"] = _enum(_get(movement, "get_movement_state"))
        out["movement_mode"] = _enum(_get(movement, "movement_mode"))
        out["swimming"] = _get(movement, "is_swimming")
        out["falling"] = _get(movement, "is_falling")
        out["on_ground"] = _get(movement, "is_moving_on_ground")
        out["max_speed"] = _r(_get(movement, "max_walk_speed"))
    out["stance"] = _enum(_get(character, "get_stance"))
    out["requested_stance"] = _enum(_get(character, "get_requested_stance"))
    out["sprinting"] = _get(character, "is_sprinting")
    out["prone"] = _get(character, "is_prone")
    out["eye_height"] = _r(_get(character, "get_current_eye_height"))
    out["target_eye_height"] = _r(_get(character, "get_target_eye_height"))
    camera = _get(character, "get_first_person_camera")
    if camera is not None:
        out["camera_z"] = _r(camera.get_world_location().z)
    capsule = _get(character, "capsule_component")
    if capsule is not None:
        out["capsule"] = [_r(capsule.get_scaled_capsule_half_height()), _r(capsule.get_scaled_capsule_radius())]
    out["holding_rod"] = _get(character, "is_holding_rod")
    fishing = _fishing_state(character)
    if fishing is not None:
        out["fishing"] = fishing
    mine = [h for h in holds() if h["player"] == player]
    if mine:
        out["holds"] = mine
    return out


def states():
    """state() of every player."""
    return [state(i) for i in range(len(worlds()))]


# ----------------------------------------------------------------------------------------------------------------------
# Commands
# ----------------------------------------------------------------------------------------------------------------------

@_logged
def console(command, player=0):
    """Run a console command in `player`'s world (Lure.* dev commands, stat fps, ...)."""
    unreal.SystemLibrary.execute_console_command(world(player), command, pc(player))
    return {"command": command, "player": player}


def _requested_stance(player):
    return str(_enum(_get(pawn(player), "get_requested_stance")) or "").upper()


def _schedule(frames, fn, *args):
    """Internal after(): runs fn from the tick without a results() entry or a session.log line."""
    _ensure_tick()
    S["scheduled"].append({"id": None, "at": S["frame"] + max(0, int(frames)), "fn": fn, "args": args, "kwargs": {},
                           "name": fn.__name__})


def _stance_retry(stance, player, frames_left):
    # Runs from the tick: re-send the request until the character holds it (Prone is refused, not queued, while the
    # pawn is falling, e.g. the few frames after a teleport drops it onto the floor).
    wanted = str(stance).upper()
    if _requested_stance(player) != wanted:
        unreal.SystemLibrary.execute_console_command(world(player), "Lure.SetStance %s" % stance, pc(player))
    if _requested_stance(player) == wanted:
        _log_line("set_stance(%s, player=%d): applied on a retry" % (stance, player))
        return True
    if frames_left <= 0:
        _log_line("set_stance(%s, player=%d): still refused after the retries (not on the ground?)" % (stance, player))
        return False
    _schedule(1, _stance_retry, stance, player, frames_left - 1)
    return None


@_logged
def set_stance(stance, player=0, retry_frames=60):
    """Lure.SetStance <Stand|Crouch|Prone> for `player` (same path as the stance keys; changes over a few frames).
    If the character refuses the request right now (Prone while falling, e.g. just after a teleport), it is re-sent
    every frame for up to retry_frames frames, so set_stance in the same call as a teleport still applies before a
    later screenshot. Check state()["requested_stance"] / ["stance"] next call."""
    console("Lure.SetStance %s" % stance, player)
    out = {"command": "Lure.SetStance %s" % stance, "stance_now": _enum(_get(pawn(player), "get_stance"))}
    if _requested_stance(player) != str(stance).upper() and int(retry_frames) > 0:
        _schedule(1, _stance_retry, stance, player, int(retry_frames) - 1)
        out["retrying"] = "refused now (falling?); re-sent every frame for up to %d frames" % int(retry_frames)
    return out


@_logged
def give_fish(species, rarity=None, seed=None, player=0):
    """Lure.GiveFish on the server: rolls a fish with the real pipeline and lands it like a real catch (into the
    player's cooler, XP added). Read the result with editor_log("Lure.GiveFish") in your next step."""
    command = "Lure.GiveFish %s %s" % (species, rarity if rarity else "-")
    if seed is not None:
        command += " %d" % int(seed)
    command = _run_on_server(command, player)
    return {"command": command, "next": "pd.editor_log('Lure.GiveFish')"}


# ----------------------------------------------------------------------------------------------------------------------
# Screenshots
# ----------------------------------------------------------------------------------------------------------------------

def _shot_path(name):
    path = name if os.path.isabs(name) else os.path.join(folder(), name)
    if not path.lower().endswith(".png"):
        path += ".png"
    return path.replace("\\", "/")


@_logged
def screenshot(name, player=0, width=1280, height=720, ui=True):
    """Screenshot of `player`'s view to <folder>/<name>.png.
    ui=True (default): the C++ command Lure.Screenshot runs in that player's own world and captures its game viewport
    through Slate, WITH the HUD text, prompts and widgets, at the viewport's size (width/height are ignored). The file
    is written during this call.
    ui=False: the 3D view only at width x height. Player 0 uses AutomationLibrary.take_high_res_screenshot, other
    players run HighResShot in their own viewport. The file appears after the next rendered frame."""
    path = _shot_path(name)
    if os.path.exists(path):
        os.remove(path)
    if ui:
        console("Lure.Screenshot %s" % path, player)
        method = "Lure.Screenshot with UI (player %d viewport)" % player
        _ensure_tick()
        if os.path.exists(path):
            return {"path": path, "method": method, "written": True, "next": "Read the file"}
        return {"path": path, "method": method, "written": False,
                "next": "not written: read pd.editor_log('Lure.Screenshot') for the reason (is PIE running and the "
                        "viewport visible?), or use ui=False"}
    if player == 0:
        unreal.AutomationLibrary.take_high_res_screenshot(int(width), int(height), path, force_game_view=True)
        method = "take_high_res_screenshot"
    else:
        console("HighResShot %dx%d filename=%s" % (int(width), int(height), path), player)
        method = "HighResShot (player %d viewport)" % player
    _ensure_tick()
    return {"path": path, "method": method, "next": "check pd.exists(%r) next step, then Read the file" % os.path.basename(path)}


def exists(name_or_path):
    """True once the screenshot file exists (name in the session folder, or a full path)."""
    return os.path.exists(_shot_path(name_or_path))


# ----------------------------------------------------------------------------------------------------------------------
# Self check (run by the C++ test Project.Dev.PlaytestDriver.ImportsAndApisExist, no PIE needed)
# ----------------------------------------------------------------------------------------------------------------------

_REQUIRED_API = {
    "": ["register_slate_post_tick_callback", "unregister_slate_post_tick_callback", "find_object", "ObjectIterator",
         "get_editor_subsystem", "Vector", "Rotator", "Name", "EnumBase", "Object", "LocalPlayer", "Actor",
         "ActorComponent", "PlayerController"],
    "LevelEditorSubsystem": ["editor_request_begin_play", "editor_request_end_play", "is_in_play_in_editor", "load_level"],
    "UnrealEditorSubsystem": ["get_game_world", "get_editor_world"],
    "EditorLevelLibrary": ["get_pie_worlds"],
    "SystemLibrary": ["execute_console_command", "is_server", "is_standalone"],
    "GameplayStatics": ["get_all_actors_of_class"],
    "EnhancedInputLocalPlayerSubsystem": ["inject_input_vector_for_action"],
    "LureInputSubsystem": ["get_input_action_by_name", "get_input_action_names"],
    "LurePlayerCharacter": ["get_lure_movement", "get_stance", "get_requested_stance", "is_sprinting",
                            "get_current_eye_height", "get_target_eye_height", "get_first_person_camera", "is_holding_rod"],
    "LureCharacterMovementComponent": ["get_feet_height", "get_movement_state", "is_swimming", "is_falling",
                                       "is_moving_on_ground"],
    "PlayerController": ["get_control_rotation", "set_control_rotation", "is_local_controller", "get_controlled_pawn"],
    "Actor": ["get_actor_location", "get_actor_rotation", "get_components_by_class", "get_actor_label"],
    "SceneComponent": ["get_world_location"],
    "CapsuleComponent": ["get_scaled_capsule_half_height", "get_scaled_capsule_radius"],
    "AutomationLibrary": ["take_high_res_screenshot"],
    "ToolsetLibrary": ["get_object_properties", "set_object_properties"],
}


def self_check(report_path=None, play_settings_roundtrip=False):
    """Checks every unreal API this driver uses exists, the Lure actions resolve, every helper is documented, and the
    play settings can be read (and, with play_settings_roundtrip, set to a listen server and restored).
    Writes JSON to report_path if given; returns the same dict."""
    missing = []
    for owner, names in _REQUIRED_API.items():
        target = unreal if not owner else getattr(unreal, owner, None)
        if target is None:
            missing.append("unreal.%s" % owner)
            continue
        for name in names:
            if not hasattr(target, name):
                missing.append("unreal.%s%s" % (owner + "." if owner else "", name))
    doc = __doc__ or ""
    for name in HELPERS:
        if not callable(globals().get(name)):
            missing.append("helper %s is not defined" % name)
        if not re.search(r"\b%s\(" % re.escape(name), doc):
            missing.append("helper %s is not in the docstring" % name)
    report = {"driver": DRIVER_VERSION, "python": sys.version.split()[0], "helpers": list(HELPERS), "missing": missing}
    try:
        names = actions()
        report["actions"] = names
        for needed in ("Move", "Look", "Jump", "Sprint", "Crouch", "Prone"):
            if needed not in names or unreal.LureInputSubsystem.get_input_action_by_name(needed) is None:
                missing.append("action %s" % needed)
    except Exception as exc:
        missing.append("actions(): %r" % exc)
    try:
        before = _get_play_settings()
        report["play_settings"] = before
        for name in _PLAY_SETTING_NAMES:
            if name not in before:
                missing.append("play setting %s" % name)
        if play_settings_roundtrip and not missing:
            listen = _set_play_settings({"PlayNetMode": "PIE_ListenServer", "PlayNumberOfClients": 2})
            restored = _set_play_settings(before)
            report["roundtrip"] = {"listen": listen, "restored": restored}
            if "ListenServer" not in str(listen.get("PlayNetMode")) or int(listen.get("PlayNumberOfClients", 0)) != 2:
                missing.append("play settings: could not set a listen server (%s)" % listen)
            if restored != before:
                missing.append("play settings: not restored (%s != %s)" % (restored, before))
    except Exception as exc:
        missing.append("play settings: %r" % exc)
    report["ok"] = not missing
    if report_path:
        directory = os.path.dirname(report_path)
        if directory:
            os.makedirs(directory, exist_ok=True)
        with open(report_path, "w", encoding="utf-8") as fh:
            json.dump(report, fh, indent=2, default=str)
    return report
