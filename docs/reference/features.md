# Features

What the engine has, area by area. A row says what a feature does in one line; how it works is on
the page it links to, and the detail is in the code. What is missing or planned is not listed here.

**Status** says what it takes to get the feature:

- **Works** - on by default, or always there: it acts once its component is added or its call is
  made, with nothing else to switch on.
- **Opt-in** - off until a project sets something that defaults off; the cell names that switch.
- **Partial** - exists, but limited; the cell states the limit.

## Rendering

| Feature | What it does | Status | Page |
|---|---|---|---|
| PBR shading | GGX specular and Lambert diffuse with multiple-scattering compensation, in one forward shader built in a fixed set of variants | Works | [lighting](lighting.md#shading-model) |
| Material types | Opaque, AlphaMask (cut-out), Transparent (blended, sorted) and Unlit | Works | [resources](resources.md#materialasset) |
| Material maps | Albedo, normal, metallic, roughness, AO and emission, separate or packed | Works | [resources](resources.md#materialasset) |
| UV tiling | `uvScale` repeats a mesh's maps and `uvOffset` slides them | Works | [resources](resources.md#materialasset) |
| Clearcoat | A second glossy layer over the base | Opt-in: `MaterialAsset::clearcoat` | [lighting](lighting.md#shading-model) |
| Sheen | A cloth lobe at grazing angles | Opt-in: `MaterialAsset::sheenColor` | [lighting](lighting.md#shading-model) |
| Subsurface | A wrapped back-light for foliage and wax | Opt-in: `MaterialAsset::subsurface` | [lighting](lighting.md#shading-model) |
| Transmission | Glass refracting a copy of the opaques behind it, with volume absorption | Opt-in: `MaterialAsset::transmission` | [lighting](lighting.md#shading-model) |
| Double-sided materials | Both faces drawn, a back face lit as its own | Opt-in: `MaterialAsset::doubleSided` | [lighting](lighting.md#shading-model) |
| Box mapping | Maps read off the object's own box, so a stretched cube keeps its texture square | Opt-in: `MaterialAsset::boxMapping` | [resources](resources.md#materialasset) |
| Layered materials | Up to four materials blended by a weight map, far repeats broken up | Partial: built in code, never saved | [resources](resources.md#materialasset) |
| Mesh pool and multi-draw | Every mesh shares one pool; sorted runs go out as one indirect multi-draw per program, winding, material and layout | Works | [rendering](rendering.md#glassetmirror---gpu-sync) |
| MSAA | 1, 2, 4 or 8 samples (4 by default), alpha-to-coverage on cut-outs, a tonemap-weighted resolve | Works | [rendering](rendering.md#opengl-backend) |
| Specular antialiasing | Raises roughness where normals vary within a pixel | Works | [decisions](decisions.md#what-has-been-decided) |
| Texture filtering | Trilinear with anisotropy (16x by default, 4x at most on normals and data); a texture may state nearest | Works | [resources](resources.md#textureasset) |
| Exposure | The frame scaled by a number of stops before the tonemap | Works | [rendering](rendering.md#rendersettings-and-rendermode) |
| Tonemapping | ACES by default, or Hable | Works | [rendering](rendering.md#rendersettings-and-rendermode) |
| Auto-exposure | Adapts the exposure from a luminance histogram read back without a stall | Opt-in: `RenderSettings::autoExposure` | [rendering](rendering.md#rendersettings-and-rendermode) |
| Bloom | A bright-pass mip chain in compute, added before the tonemap | Works | [rendering](rendering.md#the-passes-fixed-order) |
| Color grading | A 3D table made from an image strip, applied to display values after the tonemap | Opt-in: `RenderSettings::gradingLut` | [rendering](rendering.md#color-grading) |
| Render scale | The scene drawn smaller and widened by an edge-adaptive upscale and a sharpen; the UI stays at full size | Opt-in: `RenderSettings::renderScale` | [rendering](rendering.md#rendersettings-and-rendermode) |
| Quality tiers | `Graphics::setQuality` applies Performance, Low, Medium, High or Max over the project's settings, and a game's changes never turn on an effect the project has off | Works | [rendering](rendering.md#rendersettings-and-rendermode) |
| Screen-space reflections | Traced with no temporal filter on opaque materials smoother than `GLReflectionPass::MAX_ROUGHNESS` | Works | [lighting](lighting.md#where-the-ambient-comes-from) |
| Half-resolution reflections | One ray per two by two pixels, gathered back to each | Opt-in: `RenderSettings::ssrHalfResolution` | [rendering](rendering.md#rendersettings-and-rendermode) |
| Decals | A box projected onto the depth buffer and lit diffusely, culled by the view | Partial: albedo only; each a draw of its own every frame | [rendering](rendering.md#the-passes-fixed-order) |
| Outlines | A one-color rim around a mesh, optionally through walls | Works | [rendering](rendering.md#outlines) |
| Cameras | Perspective or orthographic; the lowest-slot active camera is the eye, and stays so until it goes | Partial: one view a frame, no split screen or render to texture | [visibility](visibility.md#where-the-view-comes-from) |
| First-person meshes | A body that casts but is not seen, arms seen alone, and a viewmodel with its own field of view drawn in front of the world | Opt-in: `Mesh::seenBy` | [rendering](rendering.md#the-viewmodel) |
| View culling | Frustum and screen-size tests with a fade at the size limit; posed meshes by their pose | Works | [visibility](visibility.md#the-cull) |
| Level of detail | `LOD` picks a mesh by distance and cross-fades between levels | Works | [visibility](visibility.md#level-of-detail) |
| Fixed meshes | A mesh promises never to move, so its matrix, box and draw are kept | Opt-in: `Mesh::movable` off | [visibility](visibility.md#fixed-meshes) |
| GPU occlusion cull | Two phases against a depth pyramid, in the depth prepass | Opt-in: `RenderSettings::occlusionCulling` | [visibility](visibility.md#occlusion-culling-on-the-gpu-as-an-option) |
| Debug views | Wireframe, lighting only, GI only, direct only, albedo, roughness, metalness, normals, depth, AO, clusters, bloom, shadow atlas and fog (`RenderMode`), in the editor's viewport or a run's `vkm_runtime --view <name>` | Works | [rendering](rendering.md#rendersettings-and-rendermode) |
| Debug drawing | `DebugDraw`: lines and shapes with a lifetime and depth mode; a shipping engine draws none | Works | [rendering](rendering.md#debug-drawing) |
| Scene debug drawings | Colliders, contacts, sound reach, acoustics, lights, probes, cameras, rigs, effects and bounds drawn from the scene | Opt-in: `DebugDraw::setShown` | [rendering](rendering.md#debug-drawing) |
| Screenshots | `Graphics::saveScreenshot` writes the next frame's viewport to a `.png`, or its bare rows to a `.rgb`, so one a frame streams raw video down a pipe; read back and written off the frame, which never waits on it | Works | - |
| Reversed depth | A 32-bit float depth buffer, 1 at the near plane; shadow maps keep near 0, far 1 | Works | [rendering](rendering.md#opengl-backend) |
| Cold-start compile | Programs compile on the driver's threads where it offers that; only the splash and loading screen draw until they are done | Works | [rendering](rendering.md#bakes) |

## Lighting and shadows

| Feature | What it does | Status | Page |
|---|---|---|---|
| Punctual lights | Directional, point and spot, with color, intensity, reach and cones | Works | [lighting](lighting.md#lights) |
| Forward+ clustering | Lights listed per screen and depth cluster; up to 256 a frame and 64 a cluster | Works | [lighting](lighting.md#forward-clustering) |
| Light source size | `Light::sourceRadius` widens the highlight and a baked shadow's penumbra | Works | [lighting](lighting.md#lights) |
| Flicker | A point or spot light wavers by the world's clock, captures taking it steady | Opt-in: `Light::flicker` | [lighting](lighting.md#lights) |
| Sun cascades | Four cascades around the eye over `Light::shadowDistance`, blended at each split, fading out at the reach | Works | [lighting](lighting.md#shadow-atlas) |
| Spot shadows | Atlas tiles sized by how much screen the cone covers; very wide spots go through a cube | Partial: two with a cascaded sun, six without | [lighting](lighting.md#shadow-atlas) |
| Point shadows | A cube-map array, one layer a light | Partial: two at a time | [lighting](lighting.md#shadow-atlas) |
| Shadow budget | `Light::shadowPriority`, then nearness, decides who holds a shadow; a light the view misses takes none | Works | [lighting](lighting.md#shadow-atlas) |
| Soft shadows | A fixed-width PCF filter, three texels for every light; the sun's movers three of their half-resolution texels or one compare | Works | [lighting](lighting.md#shadow-atlas) |
| Alpha-cut shadows | Cut-out and see-through casters cut their shadow by alpha | Works | [lighting](lighting.md#shadow-atlas) |
| Shadow caching | An unchanged tile or cube face is kept; the sun's fixed casters are held in layers that draw only what the eye uncovers | Works | [lighting](lighting.md#shadow-atlas) |
| Baked shadows | Fixed casters' shadows from fixed lights go into lightmaps, baked on the GPU at load; up to four lights, nothing saved | Opt-in: `Light::movable` off | [lighting](lighting.md#baked-shadows) |
| Baked sky occlusion | Each lightmap texel holds the share of sky it sees past the fixed meshes | Opt-in: `Mesh::movable` off | [lighting](lighting.md#baked-shadows) |
| GTAO | Ambient occlusion and a bent normal at half size, spatially denoised, no history | Works | [lighting](lighting.md#shading-model) |
| Procedural sky | A Rayleigh, Mie and ozone atmosphere from four tables, on by default | Works | [lighting](lighting.md#image-based-lighting-ibl) |
| Day and night | The sun's angle aims and tints the key light; below the horizon, skyglow, a moon, moonlight and stars | Works | [lighting](lighting.md#the-sun-and-the-sky) |
| Aerial perspective | Surfaces fade into the air with distance | Works | [lighting](lighting.md#image-based-lighting-ibl) |
| HDR sky | An equirect `.hdr` baked into the skybox and IBL, with the procedural sky off | Opt-in: `SkySettings::hdrPath` | [lighting](lighting.md#the-sun-and-the-sky) |
| Image-based lighting | Irradiance and prefilter cubes and a BRDF table, re-baked a step a frame while the sun drifts | Works | [lighting](lighting.md#image-based-lighting-ibl) |
| Reflection probes | Up to 32 parallax-corrected box probes, re-baked when moved and refreshed when their light changes | Works | [lighting](lighting.md#where-the-ambient-comes-from) |
| Irradiance volume | A baked SH-L1 grid for indirect diffuse, relit when the lights or sky change, probes in walls repaired | Partial: one a scene, up to 4096 probes | [rendering](rendering.md#bakes) |
| Light left out of captures | A carried or moving light that would keep probes re-baking | Opt-in: `Light::inCaptures` off | [lighting](lighting.md#what-a-capture-is-lit-by) |
| Volumetric fog | A froxel volume with height falloff, scattering every clustered light through its shadow | Opt-in: `FogSettings::enabled` | [rendering](rendering.md#fog) |

## Particles

| Feature | What it does | Status | Page |
|---|---|---|---|
| CPU particles | `ParticleEmitter` steps world-space particles on the CPU; they cast no shadow and no probe sees them | Works | [rendering](rendering.md#particles) |
| Emission | From a point, sphere, hemisphere, cone, box or disc, inside or on the surface; a rate a second, a rate a meter traveled and bursts, under `ParticleEmitter::maxParticles` | Works | [rendering](rendering.md#particles) |
| Motion | Per-particle life, speed, size, turn and spin; a size curve and color gradient over a life; acceleration, drag, turbulence and inherited velocity | Works | [rendering](rendering.md#particles) |
| Blend modes | Additive emits, Alpha is lit by clustered lights through their shadows, Distortion bends the frame behind | Works | [rendering](rendering.md#particles) |
| Draw shapes | Billboard, stretched card, trail behind each | Works | [rendering](rendering.md#particles) |
| Soft particles | Faded against scene depth | Works | [rendering](rendering.md#the-passes-fixed-order) |
| Half-resolution smoke | Alpha particles move to a half-size target once they cover the scene many times over | Works | [rendering](rendering.md#particles) |
| Flipbooks | A texture read as a sheet of frames, looped or over a life, cross-faded if asked | Opt-in: `ParticleEmitter::sheetColumns` | [rendering](rendering.md#particles) |
| Normal-mapped smoke | A second sheet that lights each puff by its own folds | Opt-in: `ParticleEmitter::normalMap` | [rendering](rendering.md#particles) |
| Collision | A ray a step against physics bodies, with bounce and life lost | Opt-in: `ParticleEmitter::collide` | [rendering](rendering.md#particles) |
| Emitter light | One unshadowed point light at the live particles' middle, in no capture | Opt-in: `ParticleEmitter::lightIntensity` | [rendering](rendering.md#particles) |
| One-off effects | The entity and its subtree go once the last particle dies | Opt-in: `ParticleEmitter::destroyWhenSpent` | [rendering](rendering.md#particles) |
| Effects pack | An explosion, a campfire, a torch and a smoke plume in `content/effects`, copied in by Create > Effect | Works | [editor](editor.md#what-create--effect-brings-into-the-project) |

## Water

| Feature | What it does | Status | Page |
|---|---|---|---|
| Water | A `Water` makes any mesh water - a pond, a river, a pool or the sea - at its own level; a scene with none pays nothing for it | Partial: with waves it must lie level and casts no shadow; flat in captures | [water](water.md) |
| Waves | A JONSWAP spectrum in the environment (`Environment::waves`) transformed by FFT on up to three repeating grids; each water takes its share | Works | [water](water.md#waves) |
| Ripples | Three repeating fields read per pixel for normal, roughness and foam; each water shows its share | Works | [water](water.md#waves) |
| Foam | Lace on crests, and at the waterline from a shore capture ahead of the eye | Partial: waterlines at one level a frame | [water](water.md#waves) |
| Shallow water | Waves slowed and flattened by the water's depth (TMA) | Opt-in: `WaveShape::depth` | [water](water.md#waves) |
| Calm radius | Waves fade out with distance from a point of the scene | Opt-in: `WaveSettings::calmRadius` | [water](water.md#waves) |
| Surges | Up to four ridges gameplay raises (`WaveSurge`); session state, not saved | Works | [water](water.md#waves) |
| Flow | Ripples run along the mesh's +U and carry what floats | Opt-in: `Water::flow` | [water](water.md#making-water) |
| Water queries | `WaterData::at` says which water a point is in, its surface, normal and flow, from the same grids the GPU draws, on a server too | Works | [water](water.md#where-the-water-is) |
| Buoyancy | `Buoyancy` floats a dynamic body, taken as its collider's box, on any water | Works | [water](water.md#floating-things) |

## Terrain

| Feature | What it does | Status | Page |
|---|---|---|---|
| Terrain asset | A square height grid of 33 to 1025 vertices a side, kept in a `.terrain` file | Works | [terrain](terrain.md#the-asset) |
| Chunked drawing | Chunks at four levels, coarser with distance, skirts hiding the seams | Works | [terrain](terrain.md#how-it-works-inside) |
| Layer painting | Four materials shared per vertex, each with its own tiling, far repeats broken up | Works | [terrain](terrain.md#how-it-works-inside) |
| Plants | Up to eight layers of multi-part plants scattered by painted density, the same on every end, each part taking coarser levels of its mesh with distance | Works | [terrain](terrain.md#plants) |
| Terrain collider | `ColliderShape::Terrain` collides the surface as drawn, rebuilt as it is edited | Works | [physics](physics.md#components) |
| Baked light on terrain | Chunks, and plants whose mesh is unwrapped, are lightmap receivers | Works | [lighting](lighting.md#baked-shadows) |
| Editor brushes | Raise, lower, smooth, flatten, paint and plant, with radius, strength and hardness; a stroke is one undo step | Works | [terrain](terrain.md#making-one) |
| Resampling | `TerrainAsset::resample` regrids, keeping shape, paint and plants | Works | [terrain](terrain.md#making-one) |
| Terrain from gameplay | `TerrainBrush` makes the same edits at runtime, and `TerrainBrush::hit` finds where a ray meets the surface | Works | [terrain](terrain.md#the-asset) |

## Physics

| Feature | What it does | Status | Page |
|---|---|---|---|
| Rigid bodies | Dynamic, kinematic or static, with mass, damping, restitution, friction and gravity scale | Works | [physics](physics.md#components) |
| Colliders | One or more box or capsule parts; sizes are absolute, the `Transform` scale is ignored, and the editor writes a scale into the parts as it is given | Works | [physics](physics.md#components) |
| Mesh colliders | A mesh's triangles under a bounding tree | Partial: mesh never meets mesh; one mesh part a collider | [physics](physics.md#the-narrowphase) |
| Triggers | Report overlaps with no push | Opt-in: `Collider::isTrigger` | [physics](physics.md#events) |
| Collision layers | Layer and mask bits on each body, named per project (`Project::physicsLayers`) | Works | [physics](physics.md#components) |
| Solver | Soft-constraint Gauss-Seidel over sort-and-sweep pairs on a fixed step, warm started; gravity and iterations per scene | Works | [physics](physics.md#the-solver) |
| Determinism | The same world run twice ends bit-identical on any thread count | Works | [physics](physics.md#determinism) |
| Sleeping | Islands of bodies rest and wake together | Works | [physics](physics.md#sleeping) |
| Drawn between ticks | Every moving body is drawn carried from its last tick to the frame by its velocities, and put back for the next tick but for what the frame wrote; a rig a clip plays is posed at the frame's moment | Partial: a body hanging from another moves with it | [physics](physics.md#drawn-between-ticks) |
| Fast-body stop | A body that moved past its half-thickness sweeps its path and stops at a face | Partial: only against what no contact pushes - static, kinematic and massless parts, and bodies another end moves | [physics](physics.md#the-tick) |
| Joints | Tie two bodies at a point, or at a set distance, solved with the contacts | Partial: point and distance only; no limits or motors | [physics](physics.md#components) |
| Joint muscles | A joint holds its angle up to a torque, so a limb sags under a weight | Opt-in: `Joint::holdTorque` | [physics](physics.md#components) |
| Ragdolls | `buildRagdoll` makes capsule bodies per bone; kinematic hitboxes under a clip, solver-driven once `Ragdoll::active` is set | Partial: ball joints with no angle limits, so a hard fall can fold a knee backwards | [physics](physics.md#ragdolls) |
| Character controller | A capsule steered by velocity: slope limit, jump, sliding along walls, held to the ground | Works | [physics](physics.md#the-character-controller) |
| Steps | Mounts an obstacle under `CharacterController::stepHeight` after probing clearance and landing | Works | [physics](physics.md#steps) |
| Moving ground | Carries a character on what it stands on, spin included | Works | [physics](physics.md#riding-what-it-stands-on) |
| Crouch | Shortens the capsule; stands up only when there is room | Opt-in: `CharacterController::crouching` | [physics](physics.md#crouching) |
| Climb | Holds a character to a face gameplay names, each tick it holds on | Opt-in: `CharacterController::climbing` | [physics](physics.md#the-character-controller) |
| Knockback | A shove's speed is carried and bleeds off | Opt-in: `CharacterController::knockbackDrag` | [physics](physics.md#the-character-controller) |
| Queries | `raycast` and `spherecast` return the nearest hit under a filter; `QueryParts` answers many rays at one moment | Works | [physics](physics.md#queries) |
| Contact events | `CollisionEvent` and `TriggerEvent`: Began, Stayed, Ended, in a deterministic order | Works | [physics](physics.md#events) |
| Collider fitting | `fitBoxesToMesh` approximates a mesh with a grid of boxes | Works | [physics](physics.md#components) |

## Animation

| Feature | What it does | Status | Page |
|---|---|---|---|
| Keyframe tracks | `Animation` drives an entity's `Transform` from position, rotation and scale tracks, looped or one-shot | Works | [animation](animation.md#animation-component) |
| Easing | Linear, and In, Out and InOut forms of ten curves, one per track | Works | [animation](animation.md#easing-functions) |
| Clip-driven bodies | A clip on a kinematic body gives it velocities, so it carries what stands on it | Works | [physics](physics.md#riding-what-it-stands-on) |
| Skeletal clips | `Animator` plays a baked clip on a rig on the tick and publishes the pose | Works | [animation](animation.md#animator) |
| GPU skinning | Four bone influences a vertex, one palette per rig, shared with shadows and the prepass | Works | [animation](animation.md#the-gpu-path) |
| Crossfades | `Animator::crossFadeTo` blends local poses, and a fade can turn round | Partial: two clips at a time | [animation](animation.md#crossfading) |
| Bone adjustments | `Animator::adjust` adds rotation, offset and scale after the clip | Works | [animation](animation.md#adjusting-a-bone-from-gameplay) |
| Markers | Named instants in a clip that fire an `AnimationEvent` | Partial: written by hand in the recipe | [animation](animation.md#animation-events) |
| Bone sockets | `BoneSocket` places a direct child of the rig on a named bone every frame | Works | [animation](animation.md#bone-sockets) |
| Ragdoll takeover | An active ragdoll's bodies drive the pose the skin draws | Works | [animation](animation.md#how-a-rig-is-posed) |

## Audio

| Feature | What it does | Status | Page |
|---|---|---|---|
| Sound clips | wav, mp3 and flac imported as 16-bit PCM | Partial: decoded whole and shipped uncompressed; no streaming | [audio](audio.md#the-clip-asset) |
| Sources | `AudioSource`: volume, pitch, loop and play-on-start, one voice each | Works | [audio](audio.md#the-component-model) |
| Spatial sound | Linear or inverse rolloff between two distances; a mono clip pans, a stereo clip only leans | Works | [audio](audio.md#spatialization) |
| Listener | `AudioListener` is the ear and the master gain, apart from the camera; with none, the active camera hears | Works | [audio](audio.md#two-cameras-one-ear) |
| One-shot requests | `playSoundAt`, `playSound` or a `PlaySoundEvent` start a sound with no entity | Works | [audio](audio.md#starting-a-sound-with-no-entity) |
| Occlusion | Rays against static and kinematic colliders dim and muffle what stands behind them | Works | [audio](audio.md#acoustics) |
| Room reverb | Rolling rays estimate the room's size and enclosure, tuning a reverb | Partial: one reverb, the ear's room | [audio](audio.md#acoustics) |
| Voice budget | Up to 128 voices; a new sound takes the place of one of lower priority or quieter | Works | [audio](audio.md#the-voice-budget) |
| Click-free stops | A stop ramps to silence, whoever caused it | Works | [audio](audio.md#starting-and-stopping-a-sound-from-gameplay) |
| No device | With no audio device the engine runs silent; a lost device is reopened | Works | [audio](audio.md#no-device-is-a-normal-state) |
| Offline mixing | `Audio::Device::openOffline` renders the same graph to a buffer; the tests use it | Works | [audio](audio.md#no-device-is-a-normal-state) |

## Input

| Feature | What it does | Status | Page |
|---|---|---|---|
| Named actions | `project.json` lists each action and the keys and buttons that feed it, read by name | Partial: keyboard and mouse only | [input](input.md#listing-actions) |
| Axes | Opposing bindings summed and clamped to -1..1 | Works | [input](input.md#listing-actions) |
| Rebinding | `InputMap::rebind` and `InputMap::struck` for a controls screen; the runtime keeps a player's choice in their own file | Works | [input](input.md#rebinding) |
| Frame queries | Held, pressed, released, axis, pointer, delta and wheel | Works | [input](input.md#reading-it-on-the-frame-clock) |
| Tick commands | `InputCommand` carries axes and every edge since the last tick, and is what a replay re-runs | Partial: 32 actions carry to a tick | [input](input.md#reading-it-on-the-tick) |
| Command payload | `InputMap::setPayload` holds up to 16 bytes every command carries | Works | [input](input.md#game-state-in-the-command) |
| Camera-relative moves | Every command carries the active camera's view; `alongView` turns an axis pair into a level move by it | Works | [input](input.md#camera-relative-movement) |
| Typed text | `InputMap::text` gives characters typed, `InputMap::typed` key repeats | Works | [ui](ui.md#text-input) |
| Cursor modes | Normal, hidden, disabled for mouse-look, or captured | Works | [input](input.md#reading-it-on-the-frame-clock) |

## Networking

Every row but the first needs a project that has the first.

| Feature | What it does | Status | Page |
|---|---|---|---|
| A networked project | The module says what a joining player is given; without it the project runs offline | Opt-in: `vkmSetupNetwork` | [networking](networking.md#what-a-player-is) |
| Dedicated server | `vkm_server` runs the client's systems with no window or renderer; it owns no entity and does not play | Works | [networking](networking.md#running-one) |
| Joining | `vkm_runtime --connect`, up to `Project::maxPlayers` seats on `Project::netPort` | Works | [networking](networking.md#running-one) |
| One code path offline | The same behavior runs with or without a session; offline every question answers yes | Works | [networking](networking.md#the-five-questions) |
| Snapshots | Per connection, the components that differ from what it confirmed, under a byte budget with rising priority | Works | [networking](networking.md#snapshots) |
| Late join | A connection that confirmed nothing gets the whole world by the same path | Works | [networking](networking.md#snapshots) |
| Quantization | Each field at a set precision, with an exact zero | Works | [networking](networking.md#snapshots) |
| Project components | `NetSchema::replicate` adds a type with a reflect block, or its own `netEncode` and `netDecode` | Works | [networking](networking.md#what-replicates) |
| Staying off the wire | `NetLocal`, static bodies, fixed meshes, inactive ragdoll bones and prefab interiors are not sent | Works | [networking](networking.md#what-does-not-replicate) |
| Client prediction | A client runs its character at once and re-runs the ticks the server disagrees with | Works | [networking](networking.md#prediction-and-correction) |
| Correction smoothing | The simulation takes a correction at once; the picture eases to it | Works | [networking](networking.md#prediction-and-correction) |
| Body leases | A client also predicts the bodies its character touches | Works | [networking](networking.md#leasing) |
| Interpolation | What the client does not own is drawn between two server poses, behind a jitter-sized delay | Works | [networking](networking.md#interpolation) |
| Tick pacing | A client bends its tick clock, not the step, to hold the server's command queue | Works | [networking](networking.md#pacing) |
| Command redundancy | Each packet repeats the newest unacknowledged commands instead of retransmitting | Works | [networking](networking.md#commands) |
| Lag compensation | `NetSession::Rewind` puts other players back for one query | Partial: player roots only | [networking](networking.md#lag-compensation) |
| Player messages | `NetSession::sendMessage`: reliable and ordered, up to 255 bytes | Partial: client to server only | [networking](networking.md#messages) |
| Shared spawns | `NetSession::spawn` builds a prefab on the server and every client, later joiners too | Works | [networking](networking.md#spawning) |
| Changing scene | The server takes its players along; a numbered world drops stale packets | Works | [networking](networking.md#changing-scene) |
| Join checks | Schema, action, scene and tick-rate mismatches are refused with a reason a player can read | Works | [networking](networking.md#the-handshake) |
| Join tokens | A keyed cookie stops floods and spoofed joins | Partial: packets are neither encrypted nor signed | [networking](networking.md#the-handshake) |
| World clock | `NetSession::worldTime`: the authority's seconds, alike on every end | Works | [networking](networking.md#what-does-not-replicate) |
| Lost connection | The session holds the world still and reports what happened | Works | [networking](networking.md#limits) |
| UDP transport | Sequenced datagrams with acknowledgements, nothing retransmitted | Partial: no fragmentation, 1200 bytes at most | [networking](networking.md#the-connection-and-rates) |
| The editor's view | Shows the schema and fingerprint, and each entity's wire slot | Partial: the editor never hosts or joins | [networking](networking.md#running-one) |

## Scripting

| Feature | What it does | Status | Page |
|---|---|---|---|
| Behaviors | C++ classes attached to an entity, with start, update, fixed, realtime, collision, trigger and destroy hooks | Works | [scripting](scripting.md#behavior) |
| Reflected fields | A reflect block makes fields saved, shown in the inspector and kept across a reload | Works | [scripting](scripting.md#fields) |
| Asset and prefab fields | `AssetRef` and `PrefabRef` name what a behavior uses, so a scene lists it | Works | [scripting](scripting.md#naming-an-asset) |
| Gameplay module | The project's code built into `game.dll` or `libgame.so`, which every host loads | Works | [scripting](scripting.md#the-gameplay-module) |
| Version guard | A host refuses a module built against another engine version | Works | [scripting](scripting.md#the-gameplay-module) |
| Hot reload | Swaps a rebuilt module, keeping each behavior's fields; `onStart` runs again | Partial: editor only; during Play it restarts the session | [scripting](scripting.md#hot-reload) |
| Time and pause | Hooks on simulation time or real time; a game can pause, step and scale itself | Works | [scripting](scripting.md#time-and-pause) |
| Behavior lookups | `findBehavior`, `findInScene` and `forEachBehavior` | Works | [scripting](scripting.md#your-first-behavior) |
| World verbs | `spawn`, `instantiate`, `destroy`, `loadScene` and `restartScene`; destroying and loading wait for the hook pass to end | Works | [scripting](scripting.md#behavior) |
| Listeners | `subscribe` reads the event type off a lambda and drops the listener with the behavior | Works | [events](events.md#subscribing-and-publishing) |
| Unknown behaviors kept | A behavior no module registered stays as text and is written back unread | Works | [scripting](scripting.md#scriptcomponent) |
| A throwing hook | Reported through `reportError`, and that behavior is disabled | Works | [scripting](scripting.md#behaviorsystem) |

## UI

| Feature | What it does | Status | Page |
|---|---|---|---|
| Canvases | Several `UICanvas` roots, drawn and hit-tested in sort order | Works | [ui](ui.md#the-component-model) |
| Anchor layout | Anchor, pivot, position, size and a share of the parent, scaled with the canvas | Works | [ui](ui.md#layout-anchor--pivot) |
| Images and shapes | `UIImage`: a tint and a stretched picture, with its `UIShape`'s rounded corners, border and gradient | Works | [ui](ui.md#the-component-model) |
| SDF text | One atlas at many sizes: wrapping, alignment, kerning, optional shadow | Partial: ASCII, Latin-1 and a few marks | [ui](ui.md#text) |
| Project fonts | A project's fonts, baked as it starts | Opt-in: `Project::fonts` | [ui](ui.md#fonts) |
| Buttons | Press and release fire `UIClickEvent`; `UIButton::held` makes drags | Works | [ui](ui.md#interaction) |
| Pointer blocking | The topmost blocking element takes the pointer, so a click on a menu fires no weapon | Works | [ui](ui.md#interaction) |
| Clipping | An element bounds what its descendants draw and hit | Opt-in: `UIElement::clipChildren` | [ui](ui.md#clipping-and-scrolling) |
| Scroll views | `UIScroll` measures its content and takes the wheel | Works | [ui](ui.md#clipping-and-scrolling) |
| Stacks | `UIStack` lays its children out in a column or a row, a hidden one closing up | Works | [ui](ui.md#layout-anchor--pivot) |
| Menus that pause | A canvas that holds the world and frees the cursor while it shows, toggled by an action | Opt-in: `UICanvas::pausesGame` | [ui](ui.md#menus-that-pause-the-game) |
| Sliders, toggles, choices | `UISlider`, `UIToggle` and `UIChoice` ride `UIButton`s and enqueue `UIValueEvent` | Works | [ui](ui.md#sliders-toggles-and-choices) |
| Opacity and visibility | Both pass down a whole subtree | Works | [ui](ui.md#the-component-model) |
| Text fields | Composed by a project from typed text, UTF-8 helpers and `measureText` | Partial: pieces only, no field component | [ui](ui.md#text-input) |
| World to screen | `ScreenView` gives a ray through a window point and a world point on a canvas | Works | [ui](ui.md#the-world-on-the-screen) |
| UI as entities | Elements are saved with the scene, edited with cards and undo | Works | [ui](ui.md#the-component-model) |
| Splash | The engine's ident and notice at startup, on any host with a window | Works | [rendering](rendering.md#the-passes-fixed-order) |
| Project logos | Logos shown after the engine's, each with its own hold | Opt-in: `Project::splash` | [io](io.md#projects-and-their-roots) |
| Loading screen | Covers a world until it is ready: water backdrop, progress bar, a line naming the step | Works | [ui](ui.md#the-loading-screen) |
| Custom loading screen | A project's tips, a prefab of UI over the water, or black in place of it | Opt-in: `Project::loading` | [ui](ui.md#the-loading-screen) |
| Frame-rate readout | At the viewport's top right, while no host authors | Opt-in: `RenderSettings::showFrameRate` | [ui](ui.md#how-it-works-inside) |

## World and runtime

| Feature | What it does | Status | Page |
|---|---|---|---|
| Project hosts | `vkm_editor`, `vkm_runtime`, `vkm_cook` and `vkm_server` each open a project directory | Works | [architecture](architecture.md#the-engine-runs-projects) |
| ECS | Open sparse-set registry: any plain struct is a component, with no registration | Works | [ecs](ecs.md) |
| Generational handles | Entities and assets detect a stale reference by generation | Works | [ecs](ecs.md#entities) |
| Hierarchy | Parent and child links resolved into world matrices once a frame, with no dirty flags | Works | [hierarchy](hierarchy.md) |
| Staged frame | Input, Simulation, Transform, Visibility, Render and Editor, each a fixed sequence of systems | Works | [architecture](architecture.md#stages) |
| Fixed step | An accumulator at the project's `Project::tickRate` (64 by default), capped against a spiral | Works | [architecture](architecture.md#the-fixed-step) |
| Clock | Real and simulation time, pause, step, time scale and a host's speed | Works | [architecture](architecture.md#framecontext) |
| Event bus | Typed pub/sub, delivered now or at the next flush | Works | [events](events.md) |
| Thread pool | `parallelFor` over a loop and background tasks; workers one fewer than the physical cores | Works | [threading](threading.md) |
| Math helpers | Rotation and axes, bounds, frustum, curves, gradients and seeded random | Works | [architecture](architecture.md#directory-layout) |
| Reflection | `VKM_REFLECT_BEGIN` lists a type's fields for serialization, and `VKM_BEHAVIOR` a behavior's for the inspector | Works | [architecture](architecture.md#conventions) |
| Scene files | JSON, staged and swapped so a failed load changes nothing; a name that will not resolve is kept for the next save | Works | [io](io.md#scene-files) |
| Prefabs | One entity and its subtree in a file, instantiated by reference | Partial: no nesting | [io](io.md#prefabs) |
| Per-instance overrides | One field of one component of one entity, stored by the instance | Works | [io](io.md#per-instance-overrides) |
| Project file | `project.json`, read leniently: what is missing or malformed keeps its default | Works | [io](io.md#projects-and-their-roots) |
| Player files | `ReflectJson` and `writeOwnFile` keep a game's settings and saves under `Roots::playerRoot`; the runtime keeps a player's graphics choices there itself (`graphics.json`) as it keeps their rebinds | Works | [io](io.md#a-projects-own-files) |
| Logging and reports | Categorized logs into the project's `logs/`, and `reportError` for what the project author caused | Works | [architecture](architecture.md#when-something-fails) |

## Assets and cooking

| Feature | What it does | Status | Page |
|---|---|---|---|
| ResourceManager | Owns every asset behind typed handles, a name its identity and a version for GPU sync | Works | [resources](resources.md) |
| Model import | glTF, GLB, FBX and OBJ into meshes, materials, a rig, clips and embedded images, converted to the engine's axes and meters | Partial: one rig a file; morph targets dropped | [resources](resources.md#importing-a-model) |
| Image import | jpg, png, tga and bmp, as color, data or normal | Works | [resources](resources.md#textureasset) |
| Texture import settings | Wrap and nearest filter on a texture | Partial: in the recipe file, no panel | [resources](resources.md#textureasset) |
| PBR folder import | A folder of `*Color*`, `*Normal*` and like files becomes a material | Works | [resources](resources.md#key-files) |
| Generators | Triangle, plane, cube, sphere, pyramid, cone, cylinder and ring meshes; solid textures; default, water and sea materials | Works | [resources](resources.md#importing-and-cooking) |
| Recipes and library | Each asset's source descriptor is version-controlled and named by project-relative path, so a re-import relinks | Works | [io](io.md#assets-recipes-and-the-cooked-cache) |
| Cooker | `vkm_cook`, with no window, bakes each recipe into a `.vkmc` file keyed by its content | Works | [io](io.md#cooking-and-the-cache-key) |
| Stale detection | Recipe bytes and source-file bytes are hashed, so a re-export re-bakes | Works | [io](io.md#cooking-and-the-cache-key) |
| Texture bake | A full mip chain holding color, coverage, normals and roughness at range, then BC4, BC5 or BC7 | Works | [resources](resources.md#what-the-cook-makes-of-a-texture) |
| Mesh bake | Welded, tangents from MikkTSpace where the file has none, reordered for the GPU | Works | [resources](resources.md#importing-a-model) |
| LOD generation | `generateLOD` simplifies a mesh into levels kept as recipes, each handing over at about a pixel of error | Works | [visibility](visibility.md#level-of-detail) |
| Lightmap unwrap | xatlas gives an imported or generated unskinned mesh its second UV set | Works | [resources](resources.md#meshasset) |
| Async loads | Mesh and texture decodes run on the pool and land on the main thread | Works | [io](io.md#loading-an-asset) |
| Fonts | An SDF atlas baked at startup at a size that follows the display, never cooked | Works | [ui](ui.md#fonts) |
| Content packs | Effects and a neutral grading table the editor copies into a project | Works | [editor](editor.md#what-create--effect-brings-into-the-project) |

## The editor

| Feature | What it does | Status | Page |
|---|---|---|---|
| Scene and project files | New, Open, Save, Save As and recents, with an unsaved-changes guard on anything that throws the scene away; a start screen of recent projects and examples to copy | Works | [editor](editor.md#actions-that-throw-the-live-scene-away) |
| Workspace | Dear ImGui docking and multi-viewport, the layout saved per person; F5 leaves the scene the whole window | Works | [editor](editor.md#where-settings-live) |
| Inspector | A card of rows per component, each row its field's range and tip; reset, copy and paste values; a prefab's overrides marked on their rows; a card names what its component is waiting for | Works | [editor](editor.md#inspector-cards) |
| Multi-select edits | An edit reaches every selected entity holding the component, as one step | Partial: not lists or entity fields | [editor](editor.md#a-card-is-rows) |
| Create menu | Entities, lights, primitives, a character, UI kits, terrain, water, a sea, effects and prefabs, standing where the view looks | Works | [editor](editor.md#where-create-puts-a-new-entity) |
| Copy, paste, duplicate | Subtrees as a scene document, pasted across scenes; Alt-drag copies in place | Works | [editor](editor.md#what-the-viewport-shows) |
| Undo and redo | Scene, material and terrain edits on a stack of 200 steps, with a history menu | Partial: not render or project settings, or asset deletion | [editor](editor.md#undo--redo) |
| Play session | Play, Pause, Step, Stop and a speed in the viewport; Eject flies the editor's view while the game hears nothing | Works | [editor](editor.md#a-play-session-owns-the-scene) |
| Stop restores | The scene, materials and terrains return as Play found them, with their undo history | Partial: other assets keep edits made in play | [editor](editor.md#a-play-session-owns-the-scene) |
| Viewport | The transform gizmo, with snapping and handles on UI rects; picking by triangle, a meshless entity by its marker; View > Show per kind of marker and shape | Works | [editor](editor.md#markers-and-picking) |
| Editor view | Its own viewpoint, per scene: fly, orbit, orthographic and axis views; any scene camera looked through, read-only; a world grid on the axes picked | Works | [editor](editor.md#what-the-viewport-shows) |
| Asset browser | One library for every asset kind but fonts: rename, delete, in-use, Select Users; a tile dragged onto the viewport, a row or a slot assigns or places it, and a file dropped on the window imports | Works | [editor](editor.md#asset-browser) |
| Asset thumbnails | Materials and meshes rendered and cached, textures from their GPU copy | Partial: sRGB thumbnails draw too dark | [editor](editor.md#asset-browser) |
| Asset windows | Mesh, material, clip and texture each open in a window of their own, with a preview; a material's optional lobes added as cards | Works | [editor](editor.md#asset-editors) |
| Animation panel | Transport and a keyframe timeline for `Animation`, scrubbing posed through its system | Works | [editor](editor.md#inspector-cards) |
| Terrain window | Tools, brush, layers and plants, and the grid | Works | [terrain](terrain.md#making-one) |
| Settings windows | Render Settings, written to `project.json` as each edit ends, with a tier preview; Project Settings; Preferences and rebindable keys, per person | Works | [editor](editor.md#where-settings-live) |
| Console and status bar | This run's log, filtered; errors with repeats counted; the build's output; the selection, errors, warnings and build state, each a click from the Console | Works | [editor](editor.md) |
| Builds and the module | New Project, Build Scripts and Package Game run `vkm`; a new build reloads the moment it is linked, and in a session plays again on the new code | Works | [editor](editor.md#building-from-the-editor) |
| Shader and table reload | Polls shader and grading-strip files once a second | Partial: editor only, never the runtime | [io](io.md#shader-hot-reload) |
| Prefab workflow | Save as Prefab, Apply, Revert All, Unpack and a card per instance; gestures that cannot save are refused | Works | [editor](editor.md#prefabs) |
| Component tools | Fit to Mesh on a collider, Generate Levels on an LOD, Build on a Ragdoll; particle effects previewed and clips auditioned with no scene edit | Works | [editor](editor.md#inspector-cards) |
| Crash recovery | A copy of unsaved changes in the user's folder, offered back on open | Works | [editor](editor.md#unsaved-changes-outlive-a-crash) |

## Tools and packaging

| Feature | What it does | Status | Page |
|---|---|---|---|
| `vkm` | One command for a project's life, from `new` to `clean`; `vkm -h` lists them | Works | [getting started](../getting-started.md#the-commands) |
| `vkm package` | Assemble the game a player gets under `dist/`, on the shipping engine, leaving source art behind | Works | [getting started](../getting-started.md#shipping) |
| `vkm bench` | Time the packaged game at fixed views and tiers: fps, 1% and 0.1% lows, worst frame, a picture of each view; fails over budget | Works | [building](building.md#measuring-a-change) |
| Debug symbols | Split into a folder beside the package, for reading a player's crash | Works | [getting started](../getting-started.md#shipping) |
| Windows executable stamp | The game's name, version and icon written into its resources | Partial: Windows only | [getting started](../getting-started.md#shipping) |
| Pinned toolchain | Compiler, CMake and Ninja fetched by hash on the first build; configure refuses another compiler | Works | [getting started](../getting-started.md#the-toolchain-pin) |
| Project template | `templates/default`: a spinning orange cube, a gray floor and a dropped ball, each material its own | Works | [getting started](../getting-started.md#your-first-project) |
| Example projects | The lab, a runner, a stress arena and a street-level city, each a complete project | Works | [building](building.md#the-examples-build-the-way-a-game-does) |
| Installers | `install.sh` and `install.ps1` install for one user, check the download's hash and add an app-menu entry | Works | [getting started](../getting-started.md#install) |
| SDK and archives | `cmake --install` lays out an SDK, CPack makes the archive | Works | [building](building.md#installing-an-sdk) |
| Shipping engine | `VKM_SHIPPING`, the engine a packaged game runs on and what `vkm package` builds on | Works | [building](building.md#the-shipping-engine) |
| Tracy profiling | CPU and GPU zones per stage and pass; on unless the build type is Release | Works | [building](building.md#profiling-with-tracy) |
| Sanitizer builds | Address and undefined-behavior sanitizers in a tree of their own | Opt-in: `VKM_SANITIZE` | [building](building.md#sanitizers) |
| Engine tests | `vkm_engine_tests`, a test per promise in a process of its own, needing no GPU or display | Works | [building](building.md#tests) |
| GPU tests | `vkm_render_tests` renders scenes off-screen and compares them with reference pictures and with each other | Partial: needs an EGL device | [building](building.md#tests) |
| vkm tests | `tests/cli/` runs the command as a user does | Works | [building](building.md#tests) |
| The `docs` suite | Holds the manual to the tree, and the code to its style rules | Works | [building](building.md#tests) |
| CI | GCC and Clang on Linux and Windows, run by hand, nothing on a push; a version tag releases | Works | [building](building.md#ci-and-releases) |
| API reference | A Doxygen site, from a `docs` target offered where Doxygen is found | Opt-in: `cmake --build build --target docs` | - |

## Platforms

| Feature | What it does | Status | Page |
|---|---|---|---|
| Linux | x86-64, built with GCC or Clang | Works | [getting started](../getting-started.md#what-you-need) |
| Windows | x86-64, built with GCC or Clang; the game and editor are windowed programs with no console | Works | [getting started](../getting-started.md#what-you-need) |
| OpenGL 4.5 | The renderer's backend, behind `RenderBackend`, `EditorRenderHooks` and `WindowSurface` | Partial: the only backend | [rendering](rendering.md#renderbackend---the-seam) |
| Window control | Fullscreen or windowed, vsync, a frame cap, a window icon, render resolutions listed from the monitor; a packaged game opens fullscreen unless its player chose a window | Works | [rendering](rendering.md#rendersettings-and-rendermode) |
| Display scale | Editor UI and font bake follow the display | Works | [editor](editor.md#where-settings-live) |
| Discrete GPU | The game and editor ask the driver for the discrete GPU (`VKM_PLATFORM_PREFER_DISCRETE_GPU()`) | Partial: Windows only | [getting started](../getting-started.md#shipping) |
| Headless hosts | `vkm_server` and `vkm_cook` need no display | Works | [building](building.md#cmake-targets) |
| Clean exit | SIGINT and SIGTERM, and on Windows Ctrl+C or a parent's stop, shut the main loop down in order (`Platform::stopRequested`) | Works | [building](building.md#build-commands) |
