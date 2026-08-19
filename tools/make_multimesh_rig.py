#!/usr/bin/env python3
"""Emit assets/models/multimesh_rig.gltf - three skinned meshes, one skin.

Every rigged model the engine has is one mesh, so nothing exercised the case
1.6's design turns on: importModelIntoScene spawns a sub-entity per aiMesh, so
a real character arrives as body + clothes + hair sharing ONE rig. This writes
the smallest file that is that case.

  Character                     (the rig frame: parent of the root joint)
    Root -- Spine -+- Head      four joints, one branch at Spine
                   +- ArmL
    body    weights Root, Spine, Head
    tunic   weights Root, Spine, ArmL
    hair    weights Head

No mesh names every joint, and no joint is named by every mesh, which is what a
real character looks like and what the union-of-bones rig builder has to get
right. One clip rotates Spine, Head and ArmL so a posed bone is unmistakably
not its bind pose.
"""

import base64
import json
import struct
import sys

# Joints as (name, parent, local translation), in the order the skin lists
# them - which is the order the engine's bone indices end up in.
JOINTS = [
    ("Root",  None,    (0.0,  0.0,  0.0)),
    ("Spine", "Root",  (0.0,  1.0,  0.0)),
    ("Head",  "Spine", (0.0,  1.0,  0.0)),
    ("ArmL",  "Spine", (-0.5, 0.75, 0.0)),
]

# Meshes: name, box extents, and the influences each of the 8 corners carries,
# chosen by which half of the box the corner sits in (low y / high y).
MESHES = [
    ("body",  (-0.40, 0.40, 0.00, 2.00, -0.25, 0.25),
     {"low": [(0, 1.0)], "high": [(1, 0.6), (2, 0.4)]}),
    ("tunic", (-0.45, 0.45, 0.20, 1.20, -0.30, 0.30),
     {"low": [(0, 0.5), (1, 0.5)], "high": [(1, 0.7), (3, 0.3)]}),
    ("hair",  (-0.30, 0.30, 2.00, 2.50, -0.30, 0.30),
     {"low": [(2, 1.0)], "high": [(2, 1.0)]}),
]

# One clip, two keys a second apart, rotating three joints well away from bind.
CLIP = [
    ("Spine", (0.0, 0.0, 0.3826834, 0.9238795)),   # +45 deg about Z
    ("Head",  (-0.3826834, 0.0, 0.0, 0.9238795)),  # -45 deg about X
    ("ArmL",  (0.0, 0.0, 0.7071068, 0.7071068)),   # +90 deg about Z
]

FACES = [
    (0, 3, 2), (0, 2, 1), (4, 5, 6), (4, 6, 7),
    (0, 4, 7), (0, 7, 3), (1, 2, 6), (1, 6, 5),
    (0, 1, 5), (0, 5, 4), (3, 7, 6), (3, 6, 2),
]

FLOAT, UBYTE, USHORT = 5126, 5121, 5123


def corners(box):
    x0, x1, y0, y1, z0, z1 = box
    return [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
            (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]


def normal_of(point, box):
    x0, x1, y0, y1, z0, z1 = box
    centre = ((x0 + x1) / 2.0, (y0 + y1) / 2.0, (z0 + z1) / 2.0)
    d = [point[i] - centre[i] for i in range(3)]
    length = sum(c * c for c in d) ** 0.5
    return tuple(c / length for c in d)


class Buffer:
    """The file's one binary blob, plus the views and accessors that index it.

    add() appends a payload and returns the accessor index naming it, which is
    what the rest of the document refers to data by.
    """

    def __init__(self):
        self.blob = bytearray()
        self.views = []
        self.accessors = []

    def _view(self, payload, target=None):
        while len(self.blob) % 4:
            self.blob.append(0)
        view = {"buffer": 0, "byteOffset": len(self.blob), "byteLength": len(payload)}
        if target is not None:
            view["target"] = target
        self.blob.extend(payload)
        self.views.append(view)
        return len(self.views) - 1

    def add(self, values, kind, component, target=None, minmax=False):
        fmt = {FLOAT: "f", UBYTE: "B", USHORT: "H"}[component]
        flat = [c for v in values for c in v] if isinstance(values[0], tuple) else values
        payload = struct.pack("<%d%s" % (len(flat), fmt), *flat)
        accessor = {
            "bufferView": self._view(payload, target),
            "componentType": component,
            "count": len(values),
            "type": kind,
        }
        if minmax:
            width = len(values[0])
            accessor["min"] = [min(v[i] for v in values) for i in range(width)]
            accessor["max"] = [max(v[i] for v in values) for i in range(width)]
        self.accessors.append(accessor)
        return len(self.accessors) - 1


def inverse_bind(index):
    """Inverse of the joint's bind matrix, in the rig root's space.

    Every joint here is a pure translation, so the inverse is the negated sum
    of the chain - no matrix library needed, and the file stays checkable by
    eye.
    """
    offset = [0.0, 0.0, 0.0]
    name = JOINTS[index][0]
    while name is not None:
        joint = next(j for j in JOINTS if j[0] == name)
        offset = [offset[i] + joint[2][i] for i in range(3)]
        name = joint[1]
    return (1.0, 0.0, 0.0, 0.0,
            0.0, 1.0, 0.0, 0.0,
            0.0, 0.0, 1.0, 0.0,
            -offset[0], -offset[1], -offset[2], 1.0)


def build():
    buf = Buffer()

    # Nodes: the rig frame first, then the joints, then a node per mesh. The
    # frame sits off the origin so that a pose composed in the wrong node's
    # space is a visible displacement rather than a coincidence.
    nodes = [{"name": "Character", "translation": [2.0, 0.0, -3.0], "children": []}]
    joint_nodes = {}
    for name, parent, translation in JOINTS:
        node = {"name": name, "translation": list(translation)}
        nodes.append(node)
        joint_nodes[name] = len(nodes) - 1
        owner = nodes[0] if parent is None else nodes[joint_nodes[parent]]
        owner.setdefault("children", []).append(len(nodes) - 1)

    ibm = buf.add([inverse_bind(i) for i in range(len(JOINTS))], "MAT4", FLOAT)
    skin = {
        "name": "rig",
        "joints": [joint_nodes[name] for name, _, _ in JOINTS],
        "skeleton": joint_nodes["Root"],
        "inverseBindMatrices": ibm,
    }

    meshes = []
    for name, box, weighting in MESHES:
        points = corners(box)
        mid = (box[2] + box[3]) / 2.0
        joints, weights = [], []
        for point in points:
            influences = weighting["high" if point[1] > mid else "low"]
            joints.append(tuple([j for j, _ in influences] + [0] * (4 - len(influences))))
            weights.append(tuple([w for _, w in influences] + [0.0] * (4 - len(influences))))

        primitive = {
            "attributes": {
                "POSITION": buf.add(points, "VEC3", FLOAT, 34962, minmax=True),
                "NORMAL": buf.add([normal_of(p, box) for p in points], "VEC3", FLOAT, 34962),
                "JOINTS_0": buf.add(joints, "VEC4", UBYTE, 34962),
                "WEIGHTS_0": buf.add(weights, "VEC4", FLOAT, 34962),
            },
            "indices": buf.add([i for face in FACES for i in face], "SCALAR", USHORT, 34963),
            "material": 0,
        }
        meshes.append({"name": name, "primitives": [primitive]})
        nodes.append({"name": name, "mesh": len(meshes) - 1, "skin": 0})
        nodes[0]["children"].append(len(nodes) - 1)

    times = buf.add([0.0, 1.0], "SCALAR", FLOAT)
    channels, samplers = [], []
    for name, rotation in CLIP:
        output = buf.add([(0.0, 0.0, 0.0, 1.0), rotation], "VEC4", FLOAT)
        samplers.append({"input": times, "output": output, "interpolation": "LINEAR"})
        channels.append({
            "sampler": len(samplers) - 1,
            "target": {"node": joint_nodes[name], "path": "rotation"},
        })

    uri = "data:application/octet-stream;base64," + base64.b64encode(bytes(buf.blob)).decode()
    return {
        "asset": {
            "version": "2.0",
            "generator": "vkmEngine tools/make_multimesh_rig.py",
            "extras": {
                "purpose": "Test fixture, not art: three skinned meshes sharing "
                           "one skin, one skeleton and one clip.",
                "proves": "importModelIntoScene spawns a sub-entity per mesh, so "
                          "a rigged character is body + clothes + hair under ONE "
                          "Animator. Small enough that the bind matrices can be "
                          "read by eye; BrainStem.glb is the same shape at scale.",
                "regenerate": "python3 tools/make_multimesh_rig.py "
                              "assets/models/multimesh_rig.gltf",
            },
        },
        "scene": 0,
        "scenes": [{"name": "multimesh_rig", "nodes": [0]}],
        "nodes": nodes,
        "meshes": meshes,
        "skins": [skin],
        "materials": [{"name": "fixture",
                       "pbrMetallicRoughness": {"baseColorFactor": [0.8, 0.8, 0.8, 1.0],
                                                "metallicFactor": 0.0,
                                                "roughnessFactor": 0.8}}],
        "animations": [{"name": "bend", "channels": channels, "samplers": samplers}],
        "accessors": buf.accessors,
        "bufferViews": buf.views,
        "buffers": [{"byteLength": len(buf.blob), "uri": uri}],
    }


def main():
    if len(sys.argv) != 2:
        print("usage: make_multimesh_rig.py <output.gltf>", file=sys.stderr)
        return 2
    with open(sys.argv[1], "w") as out:
        json.dump(build(), out, indent=2)
        out.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
