"""Reel fight balance model (T-007 + T-028 rod steering and reel speed).

A line-by-line Python transcription of FLureFight::Step (Source/VibeGame/Fishing/FishFight.cpp) and of the fish roll's stat
pipeline, used to tune DT_FishFight / DT_FightPattern before the C++ tests pin the result. Keep it in step with the C++:
when a formula changes there, change it here, re-run, and update the targets table in docs/specs/reel-fight-rules.md.

Usage (repo root):
  python tools/balance/reel_fight_model.py                 # the report (every scripted player, every fish group)
  python tools/balance/reel_fight_model.py overrides.json  # the same with tuning / pattern overrides:
      {"tuning": {"SideDrain": 1.0}, "patterns": {"Run": {"Run": {"Side": 0.6}}}}

Scripted players (reaction time 0.3 s, like the C++ balance tests):
  hold      reels the whole fight, rod level and centered, default reel speed (the T-007 "hold" player)
  careful   T-007 tension watcher: reels below 70 % of the line, eases off above 90 % (rod neutral)
  runaware  T-007: lets every aggressive move run, reels the rest (rod neutral)
  side      holds reel, steers the rod against every sideways run (nothing else)
  wrongside holds reel, rod on the SAME side as the run (should lose ground)
  back      holds reel with the rod pulled fully back (more pressure, more risk)
  fast      holds reel at the fastest reel step
  carefulside  careful + steering against every sideways run
  skilled   the intended play (Project.Fishing.Fight.Rod.SkilledPlayBeatsHolding scripts the same player): steers against
            every sideways run; on hard moves eases (rod dipped half, slowest reel); dips fully and slows when the bar passes
            85 %; pumps (rod 60 % back, fastest reel) while the fish rests or is tired, and reels fast with the rod level through
            a gentle swim (bar under 50 %) - both only until this fish has once overpowered the line; the careful watcher on the
            reel button

QA's players (T-028 senior QA, Project.Fishing.Fight.Rod.QA.Balance.* and Rod.T028b.*; they read the HUD like play_qa below):
  advice       GAME_DESIGN's advice: rod against the run and a little back during a run, ease off at 90 % of the bar, reel
               again under 60 %
  advicewheel  advice + the wheel: slowest step above 75 %, fastest below 50 % when the fish doesn't run
  dippedfast   the O1 posture (no watching): rod fully dipped, fastest reel, reel held, rod against the run. T-028b adds
               PitchDipPower so this loses clearly to advice (it matched it before).
  dippedslow   rod fully dipped, slowest reel, reel held (O2: reeling is never slack, so it never throws the hook)
The QA section of the report prints lost / median time for 200 rolled bonefish and 200 rolled snappers.
"""
import csv
import json
import math
import os
import random
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
TABLES = os.path.join(ROOT, "data", "tables")
FLOAT_SKIP = ("Name", "StrengthStat", "StaminaStat", "SpeedStat", "AggressionStat", "ApplyLevelScaling")
INT_COLUMNS = ("SimRate", "ReelSteps", "ReelDefaultStep")


# Used only when DT_FishFight.csv lacks a column (they are optional columns in C++ too, with these struct defaults).
T028_DEFAULTS = {"PitchBackPressure": 0.3, "PitchDipPressure": 0.5, "PitchDipPower": 0.8, "SideMinShare": 0.15, "SideLeverage": 0.5,
                 "SideTurnRate": 1.0, "SideTurnPull": 0.2, "SideDrain": 1.5, "ReelSteps": 3, "ReelDefaultStep": 2,
                 "ReelSpeedMin": 0.5, "ReelSpeedMax": 1.5, "ReelLoadPerSpeed": 1.5}


def load():
    with open(os.path.join(TABLES, "DT_FishFight.csv")) as f:
        row = next(csv.DictReader(f))
    tun = dict(T028_DEFAULTS)
    tun.update({k: (v if k in FLOAT_SKIP else float(v)) for k, v in row.items()})
    for k in INT_COLUMNS:
        if k in tun:
            tun[k] = int(tun[k])
    pats = {p["Name"]: p for p in json.load(open(os.path.join(TABLES, "DT_FightPattern.json")))}
    gear = {}
    with open(os.path.join(TABLES, "DT_Gear.csv")) as f:
        for r in csv.DictReader(f):
            gear[r["Name"]] = {k: float(r[k]) for k in ("RodPower", "ReelSpeed", "Drag", "LineStrength", "SpoolLength", "HookSecurity")}
    species = {s["Name"]: s for s in json.load(open(os.path.join(TABLES, "DT_FishSpecies.json")))}
    rar = {r["Name"]: r for r in json.load(open(os.path.join(TABLES, "DT_FishRarity.json")))}
    mods = {m["Name"]: m for m in json.load(open(os.path.join(TABLES, "DT_FishModifier.json")))}
    return tun, pats, gear, species, rar, mods


def kit(gear, tun, rod, line, hook):
    g = {"RodPower": gear[rod]["RodPower"], "ReelSpeed": gear[rod]["ReelSpeed"], "Drag": gear[rod]["Drag"],
         "LineStrength": gear[line]["LineStrength"], "SpoolLength": gear[line]["SpoolLength"], "HookSecurity": gear[hook]["HookSecurity"]}
    g["Drag"] = min(g["Drag"], g["LineStrength"] * tun.get("DragLineCap", 0.9))
    return g


DIFF = ["Fish.Stat.Strength", "Fish.Stat.Stamina", "Fish.Stat.Speed", "Fish.Stat.Aggression"]


def roll_stats(sp, rarity, modifiers, weight):
    base = {s["Tag"]: float(s["Value"]) for s in sp["BaseStats"]}
    f = (weight / sp["ReferenceWeight"]) ** sp["WeightStatExponent"]
    st = {k: v * f for k, v in base.items()}

    def apply(modlist):
        for phase in ("Add", "Multiply"):
            for m in modlist:
                if m["Op"] != phase or m["StatTag"] not in st:
                    continue
                st[m["StatTag"]] = st[m["StatTag"]] + m["Value"] if phase == "Add" else st[m["StatTag"]] * m["Value"]
    apply(rarity["StatMods"])
    ml = []
    for m in modifiers:
        ml += m["StatMods"]
    apply(ml)
    ratios = [st[t] / base[t] for t in DIFF if base.get(t, 0) > 0]
    rating = sum(ratios) / len(ratios) if ratios else 1.0
    level = sp["BaseLevel"] + rarity["LevelBonus"]
    return st, rating, level


def level_mult(fish_level, player_level=1, over=0.35, under=0.1, lo=0.5, hi=5.0):
    d = fish_level - player_level
    return max(min(lo, hi), min(max(lo, hi), 1 + max(0, d) * over - max(0, -d) * under))


def make_fish(st, rating, level, tun, player_level=1):
    lm = level_mult(level, player_level) if tun["ApplyLevelScaling"] in ("True", True) else 1.0
    fish = {
        "BasePull": max(0.05, st["Fish.Stat.Strength"] * tun["PullPerStrength"] * lm),
        "BaseSpeed": st["Fish.Stat.Speed"] * tun["SpeedPerStat"],
        "Pool": max(0.5, st["Fish.Stat.Stamina"] * tun["StaminaPerStat"]),
        "Aggr": st["Fish.Stat.Aggression"],
    }
    rating = rating if rating > 0 else 1.0
    fish["RestScale"] = min(4, max(0.25, max(0.25, rating) ** (-tun["RestDifficultyExponent"])))
    return fish


# ---- T-028 rod factors (FLureFight::PitchPressure, RunDirection, SideScore, ReelStepSpeed, ReelStepLoad, RodFactors) ----

def clamp(x, lo, hi):
    return max(lo, min(hi, x))


def pitch_pressure(t, pitch):
    p = clamp(pitch, -1.0, 1.0)
    return 1.0 + p * t["PitchBackPressure"] if p >= 0 else 1.0 + p * t["PitchDipPressure"]


def pitch_power(t, pitch):
    """T-028b (O1): the rod's power. Pulled back = PitchBackPressure (like the tension); dipped = PitchDipPower (more than the
    tension's PitchDipPressure: a rod pointed at the fish relieves the line but barely works the fish)."""
    p = clamp(pitch, -1.0, 1.0)
    return 1.0 + p * t["PitchBackPressure"] if p >= 0 else 1.0 + p * t["PitchDipPower"]


def run_direction(t, move, side_sign):
    if move is None:
        return 0
    lateral = clamp(move["Side"], -1.0, 1.0) * side_sign
    if abs(lateral) < t["SideMinShare"] or lateral == 0:
        return 0
    return 1 if lateral > 0 else -1


def side_score(yaw, run_dir):
    return clamp(-clamp(yaw, -1.0, 1.0) * run_dir, -1.0, 1.0)


def reel_steps(t):
    return max(1, int(t["ReelSteps"]))


def default_step(t):
    return clamp(int(t["ReelDefaultStep"]) - 1, 0, reel_steps(t) - 1)


def step_speed(t, step):
    n = reel_steps(t)
    if n == 1:
        return 1.0
    i = clamp(step, 0, n - 1)
    return t["ReelSpeedMin"] + (t["ReelSpeedMax"] - t["ReelSpeedMin"]) * i / (n - 1)


def step_load(t, step):
    return max(0.0, 1.0 + (step_speed(t, step) - 1.0) * t["ReelLoadPerSpeed"])


class Fight:
    def __init__(s, fish, pattern, g, tun, seed, line_out=1000.0):
        s.f, s.p, s.g, s.t = fish, pattern, g, tun
        s.rng = random.Random(seed)
        s.dt = 1.0 / max(10, min(240, tun["SimRate"]))
        s.line = line_out
        s.stam = 1.0
        s.tension = 0.0
        s.over = 0
        s.slack = 0
        s.exh = False
        s.elapsed = 0.0
        s.outcome = None
        s.peak = 0.0
        s.side_sign = 1.0
        s.run_dir = 0
        s.side = 0.0
        s.side_deg = 0.0
        moves = pattern["Moves"]
        op = next((i for i, m in enumerate(moves) if m["Id"] == pattern.get("OpeningMove")), None)
        s.start(op if op is not None else s.pick())

    def pick(s):
        w = [max(0.0, m["Weight"] + max(0.0, s.f["Aggr"]) * m["AggressionWeight"]) for m in s.p["Moves"]]
        tot = sum(w)
        u = s.rng.random() * tot
        for i, x in enumerate(w):
            if u < x:
                return i
            u -= x
        return len(w) - 1

    def start(s, i):
        s.mi = i
        m = s.p["Moves"][i]
        lo = max(s.dt, m["DurationMin"])
        hi = max(lo, m["DurationMax"])
        d = lo + (hi - lo) * s.rng.random()
        if m["Rest"]:
            d *= s.f["RestScale"]
        s.left = max(s.dt, d)
        s.side_sign = (-1.0 if s.rng.random() < 0.5 else 1.0) if m["RandomSide"] else 1.0

    def move(s):
        return None if s.exh else s.p["Moves"][s.mi]

    def sf(s):
        tp = min(1, max(0, s.t["TiredPull"]))
        return tp + (1 - tp) * min(1, max(0, s.stam))

    def step(s, reel, pitch=0.0, yaw=0.0, reel_step=None):
        t, g, dt = s.t, s.g, s.dt
        rstep = default_step(t) if reel_step is None else clamp(reel_step, 0, reel_steps(t) - 1)
        s.elapsed += dt
        # 1. Move (+ turning: opposite side pressure runs the move's clock faster).
        if not s.exh:
            s_old = side_score(yaw, run_direction(t, s.move(), s.side_sign))
            s.left -= dt * (1.0 + max(0.0, s_old) * t["SideTurnRate"])
            if s.left <= 0:
                s.start(s.pick())
        m = s.move()
        s.run_dir = run_direction(t, m, s.side_sign)
        side = side_score(yaw, s.run_dir)
        s.side = side
        opp = max(0.0, side)
        # 2. Fish.
        if m is None:
            pull = s.f["BasePull"] * min(1, max(0, t["TiredPull"]))
            speed = 0.0
        else:
            pull = s.f["BasePull"] * max(0, m["Pull"]) * s.sf()
            speed = s.f["BaseSpeed"] * max(0, m["Speed"]) * s.sf()
        pull *= 1.0 - opp * t["SideTurnPull"]
        away = speed * min(1, max(-1, m["Away"])) if m else 0.0
        # Rod factors.
        pressure = pitch_pressure(t, pitch)
        power = g["RodPower"] * pitch_power(t, pitch) * (1.0 + side * t["SideLeverage"])
        reel_speed = g["ReelSpeed"] * step_speed(t, rstep)
        # 3. Line.
        gain = reel_speed * clamp(1 - pull / power, 0, 1) if (reel and power > 0 and reel_speed > 0) else 0.0
        if away <= 0:
            taken = away
        elif reel:
            taken = away * clamp(pull / power - 1, 0, 1) if power > 0 else away
        elif not g["Drag"] > 0:
            taken = away
        else:
            hold = min(0.99, max(0, t["DragHold"]))
            taken = away * clamp((pull / g["Drag"] - hold) / (1 - hold), 0, 1)
        s.line = max(0.0, s.line + (taken - gain) * dt)
        # 4. Tension.
        if reel:
            target = (pull * t["ReelStrain"] + g["RodPower"] * t["ReelLoad"] * step_load(t, rstep)) * pressure
        else:
            target = min(pull * pressure, g["Drag"])
        tau = t["TensionRiseTime"] if target > s.tension else t["TensionFallTime"]
        s.tension = max(0.0, s.tension + (target - s.tension) * (1 - math.exp(-dt / tau)) if tau > 0 else target)
        s.peak = max(s.peak, s.tension / g["LineStrength"])
        # 5. Stamina (opposite side pressure tires it faster).
        slack_t = max(0.01, t["SlackShare"] * s.f["BasePull"])
        pool = max(0.01, s.f["Pool"])
        e = s.stam * pool - s.tension * dt * (1.0 + opp * t["SideDrain"])
        # T-028b (O2): one slack rule. The line is slack only while you let it run and the tension is under the slack line:
        # reeling always takes up slack (a dipped rod at the slowest step is not slack while you crank).
        slack_now = (not reel) and s.tension < slack_t
        if slack_now:
            e += pool * t["StaminaRecovery"] * dt
        s.stam = min(1, max(0, e / pool))
        if not s.exh and s.stam <= t["ExhaustedStamina"]:
            s.exh = True
        # Cosmetic swing (a fish under opposite pressure swings back).
        if m is not None:
            radius = max(100.0, s.line)
            s.side_deg += math.degrees(speed * clamp(m["Side"], -1, 1) * s.side_sign * dt / radius) * (1.0 - 2.0 * opp)
            s.side_deg = clamp(s.side_deg, -t["MaxSideDeg"], t["MaxSideDeg"])
        # 6. Outcome.
        rate = 1 / dt
        if s.line <= t["LandDistance"]:
            s.outcome = "Landed"; return
        if g["SpoolLength"] > 0 and s.line > g["SpoolLength"]:
            s.outcome = "Spooled"; return
        if s.tension > g["LineStrength"]:
            s.over += 1
            if s.over > t["SnapGraceTime"] * rate + 1e-4:
                s.outcome = "Snapped"; return
        else:
            s.over = 0
        if slack_now:
            s.slack += 1
            if s.slack > t["SlackGraceTime"] * g["HookSecurity"] * rate + 1e-4:
                s.outcome = "ThrewHook"; return
        else:
            s.slack = 0


def aggressive(m):
    return m is not None and m.get("AggressionWeight", 0) > 0


def play(fight, policy, react=0.3, max_s=180.0):
    """Runs one fight with a scripted player; returns the outcome (Timeout after max_s)."""
    t = fight.t
    fastest = reel_steps(t) - 1
    slowest = 0
    reel = policy != "careful"
    pitch, yaw, step = 0.0, 0.0, None
    since_tension = 1000.0
    since_move = 0.0
    seen = None
    # What the player last "saw" (after the reaction time): the move and its run direction.
    seen_move, seen_dir = None, 0
    watch_reel = True
    while fight.outcome is None and fight.elapsed < max_s:
        m = fight.move()
        key = (fight.mi if m else -1, fight.run_dir)
        if key != seen:
            seen = key
            since_move = 0.0
        since_move += fight.dt
        if since_move >= react - 1e-4:
            seen_move, seen_dir = m, fight.run_dir
        since_tension += fight.dt
        t01 = fight.tension / fight.g["LineStrength"]
        if since_tension >= react - 1e-4:
            since_tension = 0.0
            watch_reel = (t01 < 0.9) if watch_reel else (t01 < 0.7)
        if policy == "hold":
            pass
        elif policy == "careful":
            reel = watch_reel
        elif policy == "runaware":
            reel = not aggressive(seen_move)
        elif policy == "side":
            yaw = -float(seen_dir)
        elif policy == "wrongside":
            yaw = float(seen_dir)
        elif policy == "back":
            pitch = 1.0
        elif policy == "fast":
            step = fastest
        elif policy == "carefulside":
            reel = watch_reel
            yaw = -float(seen_dir)
        elif policy == "skilled":
            # Opposite pressure on every sideways run; ease on hard moves (rod dipped, slow reel); the rod is the first
            # tension control (dip it hard when the bar passes 85 %); pump (rod back, fast reel) while the bar is low and
            # the fish rests, swims gently or is tired, unless this fish has already overpowered the line once (then only
            # when it is tired); the reel button's tension watcher as the safety net.
            yaw = -float(seen_dir)
            hard = aggressive(seen_move)
            tired = fight.exh
            rest = seen_move is not None and seen_move.get("Rest", False)
            respect = fight.peak > 1.0
            pump = tired or (rest and not respect)
            if since_tension == 0.0:
                if t01 > 0.85:
                    pitch, step = -1.0, slowest
                elif t01 < 0.6:
                    if hard:
                        pitch, step = -0.5, slowest
                    elif pump:
                        pitch, step = 0.6, fastest
                    elif t01 < 0.5 and not respect:
                        pitch, step = 0.0, fastest   # a gentle swim: reel fast, rod level
                    else:
                        pitch, step = 0.0, None
            elif hard and pitch > -0.5:
                pitch, step = -0.5, slowest
            reel = watch_reel
        fight.step(reel, pitch, yaw, step)
    return fight.outcome or "Timeout"


def play_qa(fight, player, react=0.3, max_s=180.0):
    """QA's scripted players (the same as RodQABalanceTest.cpp / RodFollowUpTest.cpp); returns the outcome."""
    t = fight.t
    fastest = reel_steps(t) - 1
    reel, pitch, yaw, step = True, 0.0, 0.0, None
    seen_key, since_change, seen_run = None, 0.0, 0
    since_bar, bar, easing = 1000.0, 0.0, False
    while fight.outcome is None and fight.elapsed < max_s:
        key = ((-1 if fight.exh else fight.mi), fight.run_dir)
        if key != seen_key:
            seen_key, since_change = key, 0.0
        since_change += fight.dt
        if since_change >= react - 1e-4:
            seen_run = fight.run_dir
        since_bar += fight.dt
        if since_bar >= react - 1e-4:
            since_bar = 0.0
            bar = fight.tension / max(1e-3, fight.g["LineStrength"])
        if player in ("advice", "advicewheel"):
            easing = (bar >= 0.6) if easing else (bar >= 0.9)
            reel = not easing
            yaw = -float(seen_run)
            pitch = 0.5 if seen_run != 0 else 0.0
            if player == "advicewheel":
                step = 0 if bar >= 0.75 else (fastest if (bar < 0.5 and seen_run == 0) else None)
        elif player == "dippedfast":
            pitch, step, yaw = -1.0, fastest, -float(seen_run)
        elif player == "dippedslow":
            pitch, step = -1.0, 0
        fight.step(reel, pitch, yaw, step)
    return fight.outcome or "Timeout"


QA_PLAYERS = ("hold", "advice", "advicewheel", "dippedfast", "dippedslow")


def qa_group(label, fishes, pattern, g, tun, players=QA_PLAYERS):
    """Lost / median landing time per QA player (hold = play()'s hold)."""
    parts = []
    for pol in players:
        lost, times = 0, []
        for i, (st, rating, lvl, w, name) in enumerate(fishes):
            f = Fight(make_fish(st, rating, lvl, tun), pattern, g, tun, seed=1000 * i + 7)
            o = play(f, pol) if pol == "hold" else play_qa(f, pol)
            if o == "Landed":
                times.append(f.elapsed)
            else:
                lost += 1
        times.sort()
        parts.append("%s %d/%d %.1fs" % (pol, lost, len(fishes), times[len(times) // 2] if times else float("nan")))
    print("  %s: %s" % (label, "  ".join(parts)))


def population(sp, rar, mods, n, rng):
    """(stats, rating, level, weight, label) sampled like the roll: rarity by weight, weight by skew, modifiers by chance."""
    names = [r for r in rar if rar[r]["RollWeight"] > 0]
    ws = [rar[r]["RollWeight"] for r in names]
    out = []
    for _ in range(n):
        r = rng.choices(names, ws)[0]
        u = rng.random()
        w = sp["WeightMin"] + (sp["WeightMax"] - sp["WeightMin"]) * u ** sp["SizeSkew"]
        hit = [m for m in mods.values() if rng.random() < m["RollChance"]]
        size = [m for m in hit if m["ExclusivityGroup"] == "Size"]
        hit = [m for m in hit if m["ExclusivityGroup"] != "Size"] + (size[:1])
        hit = hit[: sp["MaxModifiers"]]
        st, rating, lvl = roll_stats(sp, rar[r], hit, w)
        out.append((st, rating, lvl, w, r + ("+" + ",".join(m["Name"] for m in hit) if hit else "")))
    return out


POLICIES = ("hold", "careful", "runaware", "side", "carefulside", "wrongside", "back", "fast", "skilled")


def run_group(label, fishes, pattern, g, tun, policies=POLICIES, line_out=1000.0):
    res = {}
    per_fish = {}
    print("  " + label)
    for pol in policies:
        oc = {}
        times = []
        peaks = []
        for i, (st, rating, lvl, w, name) in enumerate(fishes):
            f = Fight(make_fish(st, rating, lvl, tun), pattern, g, tun, seed=1000 * i + 7, line_out=line_out)
            o = play(f, pol)
            per_fish[(pol, i)] = f.elapsed if o == "Landed" else None
            oc[o] = oc.get(o, 0) + 1
            peaks.append(f.peak)
            if o == "Landed":
                times.append(f.elapsed)
        n = sum(oc.values())
        times.sort()
        peaks.sort()
        med = times[len(times) // 2] if times else float("nan")
        res[pol] = {"land": oc.get("Landed", 0) / n, "snap": oc.get("Snapped", 0) / n, "med": med,
                    "p10": times[len(times) // 10] if times else float("nan"), "p90": times[len(times) * 9 // 10] if times else float("nan"),
                    "peak": peaks[len(peaks) // 2]}
        r = res[pol]
        print("    %-9s land %5.1f%% snap %5.1f%% other %5.1f%%  time med %5.1f (p10 %4.1f p90 %4.1f)  peak med %4.2f" % (
            pol, 100 * r["land"], 100 * r["snap"], 100 * (1 - r["land"] - r["snap"]), med, r["p10"], r["p90"], r["peak"]))
    if "hold" in policies and "skilled" in policies:
        ratios = sorted(per_fish[("skilled", i)] / per_fish[("hold", i)] for i in range(len(fishes))
                        if per_fish[("hold", i)] and per_fish[("skilled", i)])
        if ratios:
            print("    skilled / hold time on the %d fish holding lands: median %.2f (p90 %.2f)" % (
                len(ratios), ratios[len(ratios) // 2], ratios[len(ratios) * 9 // 10]))
    return res


def main():
    tun, pats, gear, species, rar, mods = load()
    if len(sys.argv) > 1:
        ov = json.load(open(sys.argv[1]))
        tun.update(ov.get("tuning", {}))
        for pname, moves in ov.get("patterns", {}).items():
            for mid, vals in moves.items():
                for m in pats[pname]["Moves"]:
                    if m["Id"] == mid:
                        m.update(vals)
    starter = kit(gear, tun, "Rod_Starter", "Line_Mono", "Hook_Shrimp")
    reef = kit(gear, tun, "Rod_Reef", "Line_Braid", "Hook_Squid")
    bone = species["Bonefish"]
    snap = species["CoralSnapper"]
    ref = lambda sp, r, w: [roll_stats(sp, rar[r], [], w) + (w, r)]
    print("Starter kit, the fight starts 10 m out")
    run_group("bonefish, 300 rolled like the pipeline", population(bone, rar, mods, 300, random.Random(42)), pats[bone["FightPatternId"]], starter, tun)
    for (r, w) in (("Common", 1.5), ("Rare", 2.04), ("Common", 3.0)):
        run_group("bonefish %s %.2f kg x40" % (r, w), ref(bone, r, w) * 40, pats["Run"], starter, tun)
    run_group("snapper, 300 rolled", population(snap, rar, mods, 300, random.Random(7)), pats["Dive"], starter, tun)
    run_group("snapper Common 2.5 kg (reference) x40", ref(snap, "Common", 2.5) * 40, pats["Dive"], starter, tun)
    run_group("snapper Common 7 kg x40", ref(snap, "Common", 7.0) * 40, pats["Dive"], starter, tun)
    print("Reef kit")
    run_group("snapper 7 kg reef x40", ref(snap, "Common", 7.0) * 40, pats["Dive"], reef, tun)
    run_group("bonefish reef, 300 rolled", population(bone, rar, mods, 300, random.Random(9)), pats["Run"], reef, tun)
    print("QA's players (T-028b O1/O2), starter kit: lost / median time")
    qa_group("bonefish, 200 rolled", population(bone, rar, mods, 200, random.Random(42)), pats["Run"], starter, tun)
    qa_group("snapper, 200 rolled", population(snap, rar, mods, 200, random.Random(7)), pats["Dive"], starter, tun)


if __name__ == "__main__":
    main()
