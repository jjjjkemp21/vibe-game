# Art style

Status: v1 approved by Jimmy on 2026-09-22 (direction). Still to do in T-002: mood-board renders in art/reference/ and art/lib/style.py.

## Direction (a few words + reference images in art/reference/)
Stylized low-poly, atmosphere-first. Inspired by Dredge (moody water, strong silhouettes, fog as a storytelling tool, a readable UI), but our own look: brighter and chunkier in the tropics, with a first-person view.
- Clean, chunky shapes with a slight bevel; no fine detail that would read as noise at a distance.
- Flat or gradient colors with simple materials; texture only where it helps readability (wood grain strips, fish markings).
- Mood comes from lighting, fog, sky and water color, which change by region and time of day.
- Readability first: fish silhouettes, bobber, line and danger must read instantly. Measured: a real-size bobber (5 cm) is ~3 px wide at 15 m at 1080p/90 deg FOV (invisible); at 3x it's ~12 px and reads at 10-20 m. Gameplay objects that must be tracked at distance get a readability scale (bobber: ~3x, or scaled with distance).
- Reference images: we don't copy Dredge art. Our own mood boards (renders from our recipes plus color studies) go in `art/reference/` as part of T-002.

## Palette (hex values)
Tropical / sunny (beginner zone, vertical slice):
- Shallow water `#3ED1C4`, deep water `#0E6F7A`, reef `#F28F6B`
- Sand `#F2D6A2`, palm green `#3F8F4A`, leaf dark `#2A5E36`
- Weathered wood `#8A5A3B`, rope `#C9A66B`
- Sky day `#8FD3F0`, sunset `#FF9A5A`, night `#1B2440`
- Accent (bobber, UI highlights) `#FF4D3D`

Foggy / eerie: fog `#8A9A9C`, rock `#3E4A4F`, water `#2C3E40`, lantern `#E8C46A`, wrong-light accent `#9DB36B`
Frozen / cold: snow `#E6F2F5`, ice `#9CC7D8`, deep ice `#2F4A5E`, accent `#FFB347`
Murky / gloomy: water `#3B3A24`, mud `#4E4330`, moss `#5E6B3A`, accent `#C7A43B`

UI: parchment `#F3E9D2`, ink `#2B2A26`, danger `#C0392B`, safe `#3FA34D`
Character (placeholder first-person arms, 7cd2157): sleeve `#7C8A63` (faded olive: palm green washed toward sand, so it doesn't fight the red accent or the turquoise water), skin `#B98563`. Rod: cork `#C9A66B`, blank `#3A2A20`, fittings `#3E4A4F`, trim/line `#F5F1E6`.

## Shapes and proportions (bevel sizes, silhouette rules)
- Bevel: about 2-3% of the object's smallest dimension (a 1 m crate gets about 2-3 cm), 1-2 segments.
- Props: chunky, slightly exaggerated proportions (thick dock posts, fat ropes, big bobber).
- Fish: simple, bold body shapes, readable in silhouette from 5 m; each species has one strong identifying feature (fin, jaw, stripe, glow).
- Creatures (shark, shadow): readable as silhouettes first; danger shows through shape and motion, not detail.
- Terrain and islands: soft, rounded landforms; rocks are faceted low-poly.

## Materials (roughness ranges, use of textures vs flat colors)
- Default: flat base color from the palette, roughness 0.6-0.9, metallic 0.
- Wet or glossy: fish, wet rocks and the bobber at roughness 0.25-0.45.
- Textures: only small shared ones (gradient, wood strip, fish pattern masks); no photo textures.
- Water, fog and sky are Unreal materials and systems, tuned per region and time of day in data.

## Budgets
- Small prop <= 2k triangles, large prop <= 10k, hero prop <= 30k.
- Fish <= 3k triangles each; boat <= 8k; island module pieces <= 10k each.
- Textures: 512 px default, 1024 px for hero items.

## Orientation rule
CONFIRMED in the editor 2026-09-23 (SM_Rod_Basic on SK_FPArms `hand_r_rod`, PIE screenshot Saved/AgentLogs/editor/20260923-t004-import/pie_fp_rod.png): Blender +X = Unreal +X (forward), Blender +Y = Unreal -Y, Z up; Unreal location = (100x, -100y, 100z) cm. Model directional props facing Blender +X. Skeletal FBX must be exported in centimeters with no armature scale (see the animation-artist's export settings); a meter-declared FBX imports with a 100x `root` bone scale.

## Characters and animation
- First person: the player sees their own arms and the rod. Placeholder arms first, taken from Epic's first-person content or a simple blockout; stylized custom arms later.
- Rod and line: the rod bends under tension; the line is a simulated or spline cable; the bobber bobs on the waves.
- Fish: simple procedural swim animation (bone chain or vertex wiggle) made in Blender or in material; a fight animation while hooked.
- Other players (co-op, later): a simple stylized fisher body with Unreal mannequin animations retargeted.
- Custom characters only after the vertical slice is fun.
