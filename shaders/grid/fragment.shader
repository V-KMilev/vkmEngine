/*
 * The world grid, one fullscreen draw after the tonemap: the grids on the XZ, XY and ZY
 * planes and the X, Y and Z axis lines, each on its own switch. Each pixel's view ray finds
 * its point on every plane and its nearest point on every axis; each is drawn only where the
 * scene's depth does not hide it, and they blend far to near.
 *
 * A plane's lines follow the zoom in powers of ten from a metre up: each pixel takes the
 * level whose cells are at least MIN_CELL_PX wide there, so a grid reads the same from any
 * distance, and the finer of two levels fades as its parent takes over. A plane fades where
 * it is seen nearly edge-on, which no spacing keeps from turning to noise. Colours are
 * display values, blended as the gizmos are.
 */
in vec2 vUV;
out vec4 FragColor;

layout(binding = POST_SLOT_SCENE_DEPTH) uniform sampler2D u_sceneDepth;  // geometry target depth

#include "../camera.glsl"
#include "../depth.glsl"

uniform vec3  u_axisColor[3];  // X, Y, Z as Math::AXIS_COLORS gives them, display values
uniform ivec3 u_axisShown;
uniform ivec3 u_planeShown;    // by the plane's normal: ZY, XZ, XY

const float BASE_EXTENT  = 100.0;  // reach in world units, grown with the log of the distance
const float MIN_CELL_PX  = 24.0;   // a level fades out as its cells shrink to this
const float GRAZING      = 0.12;   // sine of the view angle below which a plane fades
const float MINOR_ALPHA  = 0.45;
const float MAJOR_ALPHA  = 0.75;
const vec3  LINE_COLOR   = vec3(0.70);
const float WALL_ALPHA   = 0.6;    // the XY and ZY planes against the ground's lines
const float AXIS_ALPHA   = 0.9;
const float AXIS_WIDTH   = 1.5;    // pixels
const float PAST_PLANE   = 0.35;   // an axis's alpha past the plane it pierces, from the camera
const float DEPTH_SLACK  = 1.0e-3; // relative; a floor at y = 0 does not hide the grid on it

const int LAYERS = 6;  // three planes, three axes

// Coverage of the lines every `spacing` units, about one pixel wide however far away.
float lines(vec2 coord, vec2 perPixel, float spacing) {
    vec2 cell = coord / spacing;
    vec2 g = abs(fract(cell - 0.5) - 0.5) / (perPixel / spacing);
    return 1.0 - min(min(g.x, g.y), 1.0);
}

// Coverage of a line `distPx` pixels away, AXIS_WIDTH pixels wide.
float axisCoverage(float distPx) {
    return 1.0 - clamp(distPx / (AXIS_WIDTH * 0.5) - 0.5, 0.0, 1.0);
}

// Linear view depth of a world point.
float viewDepth(vec3 p) {
    return -(u_camera.view * vec4(p, 1.0)).z;
}

// World units across one pixel at view depth `depth`.
float worldPerPixel(float depth) {
    float scale = cameraIsPerspective() ? depth : 1.0;
    return scale * 2.0 / (u_camera.projection[1][1] * u_camera.viewport.y);
}

// Fades out toward the reach a camera `away` units from a plane or axis sees.
float reachFade(float dist, float away) {
    float extent = BASE_EXTENT * max(1.0, log(max(away, 1.0)));
    return 1.0 - smoothstep(extent * 0.6, extent, dist);
}

// The two axes spanning the plane whose normal is axis `n`.
ivec2 planeAxes(int n) {
    return n == 0 ? ivec2(2, 1) : (n == 1 ? ivec2(0, 2) : ivec2(0, 1));
}

// The grid on the plane through the origin normal to axis `n`, straight alpha, and its depth.
vec4 plane(int n, vec3 origin, vec3 dir, float sceneView, out float depth) {
    // Taken whether or not the ray meets the plane, so the derivatives are defined; a miss
    // is masked at the end, by selection, since its lines are not numbers.
    float t = -origin[n] / dir[n];
    vec3  p = origin + dir * t;
    ivec2 ij = planeAxes(n);
    vec2  c = vec2(p[ij.x], p[ij.y]);
    vec2  perPixel = fwidth(c);
    depth = viewDepth(p);

    // The level: world units across MIN_CELL_PX pixels, as a power of ten no finer than a
    // metre, and how far past it this pixel is - the share by which the finer level has faded.
    float lod   = max(log(max(perPixel.x, perPixel.y) * MIN_CELL_PX) / log(10.0), 0.0);
    float level = floor(lod);
    float fade  = lod - level;
    float minorSpacing = pow(10.0, level);

    // The finer level fades out; the coarser one dims from major to minor as it takes over,
    // so a level change never pops.
    float minor = lines(c, perPixel, minorSpacing) * MINOR_ALPHA * (1.0 - fade);
    float major = lines(c, perPixel, minorSpacing * 10.0) * mix(MAJOR_ALPHA, MINOR_ALPHA, fade);
    float alpha = max(minor, major) * (n == 1 ? 1.0 : WALL_ALPHA);

    // Seen edge-on the plane goes before its lines merge.
    alpha *= smoothstep(0.0, GRAZING, abs(dir[n]));
    // Distance within the plane, so the fade is a disc on it however far the camera stands off.
    vec3 eye = u_camera.cameraPosition.xyz;
    alpha *= reachFade(distance(c, vec2(eye[ij.x], eye[ij.y])), abs(eye[n]));

    bool shown = t > 0.0 && depth <= sceneView * (1.0 + DEPTH_SLACK);
    return shown ? vec4(LINE_COLOR, alpha) : vec4(0.0);
}

// Axis `a` at the ray's nearest approach to it, straight alpha, and its depth.
vec4 axis(int a, vec3 origin, vec3 dir, float sceneView, out float depth) {
    // The closest points of the ray origin + s * dir and the axis h * e_a.
    float along = dot(dir, origin);
    float denom = 1.0 - dir[a] * dir[a];
    depth = 0.0;
    if (denom < 1.0e-6) return vec4(0.0);  // looking straight along it
    float s = (dir[a] * origin[a] - along) / denom;
    float h = (origin[a] - dir[a] * along) / denom;

    vec3 onAxis = vec3(0.0);
    onAxis[a] = h;
    depth = viewDepth(onAxis);
    // A ray aimed away from the axis meets it nearest behind the camera, at no pixel.
    if (s <= 0.0 || depth <= 0.0 || depth > sceneView * (1.0 + DEPTH_SLACK)) return vec4(0.0);

    float distPx = distance(origin + dir * s, onAxis) / worldPerPixel(depth);
    float alpha  = axisCoverage(distPx) * AXIS_ALPHA;

    vec3 eye = u_camera.cameraPosition.xyz;
    vec3 off = eye;
    off[a] = 0.0;
    alpha *= reachFade(distance(eye, onAxis), length(off));
    if (u_planeShown[a] != 0 && h * eye[a] < 0.0) alpha *= PAST_PLANE;
    return vec4(u_axisColor[a], alpha);
}

void main() {
    float sceneDepth = texelFetch(u_sceneDepth, ivec2(vUV * u_camera.viewport), 0).r;
    float sceneView  = linearSceneDepth(sceneDepth, u_camera.invProjection);

    // The pixel's ray, from the near plane: an orthographic camera's start where the pixel is.
    vec2 ndc  = vUV * 2.0 - 1.0;
    vec4 near = u_camera.invViewProjection * vec4(ndc, -1.0, 1.0);
    vec4 far  = u_camera.invViewProjection * vec4(ndc,  1.0, 1.0);
    vec3 origin = near.xyz / near.w;
    vec3 dir    = normalize(far.xyz / far.w - origin);

    vec4  layer[LAYERS];
    float depth[LAYERS];
    for (int i = 0; i < 3; ++i) {
        // The switches are uniform, so the derivatives inside stay defined.
        layer[i] = vec4(0.0);
        depth[i] = 0.0;
        if (u_planeShown[i] != 0) layer[i] = plane(i, origin, dir, sceneView, depth[i]);
        layer[3 + i] = vec4(0.0);
        depth[3 + i] = 0.0;
        if (u_axisShown[i] != 0) layer[3 + i] = axis(i, origin, dir, sceneView, depth[3 + i]);
        // An axis lies in two planes; at a tie it is drawn over them.
        depth[3 + i] *= 1.0 - 1.0e-4;
    }

    // Far to near, each over what is behind it.
    for (int i = 1; i < LAYERS; ++i) {
        for (int j = i; j > 0 && depth[j] > depth[j - 1]; --j) {
            vec4  l = layer[j]; layer[j] = layer[j - 1]; layer[j - 1] = l;
            float d = depth[j]; depth[j] = depth[j - 1]; depth[j - 1] = d;
        }
    }
    vec3  color = vec3(0.0);
    float alpha = 0.0;
    for (int i = 0; i < LAYERS; ++i) {
        vec4 l = layer[i];
        color = l.rgb * l.a + color * (1.0 - l.a);
        alpha = l.a + alpha * (1.0 - l.a);
    }
    if (alpha < 0.002) discard;
    FragColor = vec4(color / alpha, alpha);
}
