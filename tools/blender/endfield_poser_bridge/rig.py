"""Exact matrix conversion; game transform axes need not match Blender bone axes."""
import json
import math
import copy
import bpy
from mathutils import Matrix, Quaternion, Vector

# A reflection swaps Unity Y-up and Blender Z-up. Conjugating rotations by
# this matrix preserves handedness without ad-hoc Euler negation.
C = Matrix(((1, 0, 0, 0), (0, 0, 1, 0), (0, 1, 0, 0), (0, 0, 0, 1)))
CAM = Matrix.Diagonal((1, 1, -1, 1))


def matrix(values):
    return Matrix(tuple(tuple(values[c * 4 + r] for c in range(4)) for r in range(4)))


def flatten(m):
    return [m[r][c] for c in range(4) for r in range(4)]


def trs(p, q, scale=(1, 1, 1)):
    return Matrix.LocRotScale(Vector(p), Quaternion((q[3], *q[:3])), Vector(scale))


def qlist(q):
    q.normalize()
    return [q.x, q.y, q.z, q.w]


def bone_key(index):
    return f"epb_face_{index:03d}"


def principal(b):
    role = b.get('role', -1)
    return b['editable'] and (0 <= role <= 20 or 24 <= role <= 54)


def apply_display(arm, show_fingers=True):
    """Keep native helper transforms for conversion, out of the editing view."""
    if not arm or 'epb_scene' not in arm:
        return
    for b in schema(arm)['bones']:
        pb = arm.pose.bones.get(b['blender_name'])
        if pb is None:
            continue
        visible = principal(b) and (show_fingers or not 24 <= b['role'] <= 53)
        # Blender 5 separates Pose/Object visibility from Edit visibility.
        pb.hide = not visible
        pb.bone.hide = not visible
        pb.bone.hide_select = not visible
        if not visible:
            pb.select = False


def display_tail(b, data, children, roles):
    w = C @ matrix(b['world']) @ C
    head = w.translation
    role = b.get('role', -1)
    targets = {0: (7,), 1: (3,), 2: (4,), 3: (5,), 4: (6,), 5: (19,), 6: (20,),
               7: (8,), 8: (54, 9), 54: (9,), 9: (10,), 11: (13,), 12: (14,),
               13: (15,), 14: (16,), 15: (17,), 16: (18,), 17: (30,), 18: (45,)}
    if 24 <= role <= 53 and (role-24) % 3 != 2:
        candidates = (role+1,)
    else:
        candidates = targets.get(role, ())
    for target in candidates:
        if target in roles:
            tip = C.to_3x3() @ matrix(roles[target]['world']).translation
            if (tip-head).length > .001:
                return tip
    # Authored tips are useful endpoints, but cloth/corrective children must
    # never determine the drawn direction of a body joint.
    for i in children.get(b['i'], []):
        child = data['bones'][i]
        if principal(child) or child['name'].lower().endswith(('nub', 'tip', '_end')):
            tip = C.to_3x3() @ matrix(child['world']).translation
            if (tip-head).length > .001:
                return tip
    if 24 <= role <= 53 and b['parent'] >= 0:
        parent = C.to_3x3() @ matrix(data['bones'][b['parent']]['world']).translation
        direction = head-parent
        if direction.length > .001:
            return head+direction*.65
    return head + (w.to_quaternion() @ Vector((0, .04, 0)))


def create_rig(data, scene):
    if bpy.context.object and bpy.context.object.mode != "OBJECT":
        bpy.ops.object.mode_set(mode="OBJECT")
    for obj in bpy.context.selected_objects:
        obj.select_set(False)
    arm = bpy.data.objects.new("Endfield · " + data["model"], bpy.data.armatures.new("Endfield 骨架"))
    scene.collection.objects.link(arm)
    arm.show_in_front = True
    arm.lock_rotation = (True, True, True)
    arm.lock_scale = (True, True, True)
    arm.data.display_type = "OCTAHEDRAL"
    arm["epb_scene"] = json.dumps(data, ensure_ascii=False)
    arm["epb_managed"] = True
    arm["epb_visible"] = 1.0
    arm.id_properties_ui("epb_visible").update(min=0, max=1, description="模型显示开关")
    bpy.context.view_layer.objects.active = arm
    arm.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    created = []
    children = {}
    for b in data["bones"]:
        children.setdefault(b["parent"], []).append(b["i"])
    roles = {b['role']: b for b in data['bones'] if b.get('role', -1) >= 0}
    for b in data["bones"]:
        eb = arm.data.edit_bones.new(b["name"])
        w = C @ matrix(b["world"]) @ C
        head = w.translation
        eb.head, eb.tail = head, display_tail(b, data, children, roles)
        eb.align_roll(w.to_quaternion() @ Vector((0, 0, 1)))
        if 0 <= b["parent"] < len(created):
            eb.parent = created[b["parent"]]
        created.append(eb)
    bpy.ops.object.mode_set(mode="OBJECT")
    for b, pb in zip(data["bones"], arm.pose.bones):
        # Index by the actual returned Blender name (long/duplicate game names).
        b["blender_name"] = pb.name
        pb.rotation_mode = "QUATERNION"
        pb["game_index"] = b["i"]
        pb["game_name"] = b["name"]
        native = C @ matrix(b["world"]) @ C
        b["compensation"] = flatten(pb.bone.matrix_local.inverted() @ native)
    for i, face in enumerate(data["faces"]):
        key = bone_key(i)
        arm[key] = 0.0
        arm.id_properties_ui(key).update(min=0.0, max=1.0, description=face["label"] + ' · ' + face['name'])
    arm["epb_scene"] = json.dumps(data, ensure_ascii=False)
    bpy.ops.object.mode_set(mode="POSE")
    apply_display(arm, getattr(scene, 'epb_show_fingers', True))
    return arm


def schema(arm):
    return json.loads(arm["epb_scene"])


def reconnect_schema(previous, incoming):
    """Keep saved bone names/basis and map new runtime indices by hierarchy.

    Clothing helpers may appear/disappear after physics preparation. They must
    not make a saved body rig incompatible merely by changing array indices.
    """
    if not previous or previous['model'] != incoming['model']:
        return None
    def paths(bones):
        result = []
        for b in bones:
            result.append((result[b['parent']] if b['parent'] >= 0 else ()) + (b['name'],))
        return result
    old_paths, new_paths = paths(previous['bones']), paths(incoming['bones'])
    if len(set(old_paths)) != len(old_paths) or len(set(new_paths)) != len(new_paths):
        return None
    indices = {p: i for i, p in enumerate(new_paths)}
    required = set()
    for b in incoming['bones']:
        if b['editable']:
            i = b['i']
            while i >= 0 and i not in required:
                required.add(i)
                i = incoming['bones'][i]['parent']
    if any(new_paths[i] not in old_paths for i in required):
        return None
    result = copy.deepcopy(previous)
    for b, path in zip(result['bones'], old_paths):
        i = indices.get(path, -1)
        b['game_index'] = i
        b['editable'] = i >= 0 and incoming['bones'][i]['editable']
    result['game_bones'] = [{'name': b['name'], 'parent': b['parent']} for b in incoming['bones']]
    return result


def rebase_sample(data, sample):
    """Convert newly sampled world offsets to the saved project's anchor.

    Local joint transforms are independent of the connection's world heading.
    Camera and root offsets are not: a reconnect may face another direction.
    """
    editing = data.get('anchor_rotation', [0, 0, 0, 1])
    incoming = sample.get('anchor_rotation', data.get('sample_anchor_rotation', editing))
    edit_q = Quaternion((editing[3], *editing[:3])).normalized()
    source_q = Quaternion((incoming[3], *incoming[:3])).normalized()
    basis = edit_q @ source_q.conjugated()
    result = dict(sample)
    result['root'] = list(basis @ Vector(sample['root']))
    c = sample.get('camera')
    if c:
        camera = dict(c)
        camera['p'] = list(basis @ Vector(c['p']))
        camera['target'] = list(basis @ Vector(c['target']))
        camera['q'] = qlist(basis @ Quaternion((c['q'][3], *c['q'][:3])))
        result['camera'] = camera
    return result


def basis_matrices(data, sample):
    """Returns Blender matrix_basis values without touching scene state."""
    updates = {b["i"]: b for b in sample["bones"]}
    worlds, poses, basis = [], [], []
    for b in data["bones"]:
        local = updates.get(b.get('game_index', b['i']), b)
        native_local = trs(local["p"], local["q"], b["scale"])
        if b["parent"] >= 0:
            native_world = worlds[b["parent"]] @ native_local
        else:
            parent = matrix(b["world"]) @ trs(b["p"], b["q"], b["scale"]).inverted()
            native_world = parent @ native_local
        worlds.append(native_world)
        correction = matrix(b["compensation"])
        rest = C @ matrix(b["world"]) @ C @ correction.inverted()
        pose = C @ native_world @ C @ correction.inverted()
        if b["parent"] >= 0:
            parent_b = data["bones"][b["parent"]]
            parent_rest = C @ matrix(parent_b["world"]) @ C @ matrix(parent_b["compensation"]).inverted()
            value = rest.inverted() @ parent_rest @ poses[b["parent"]].inverted() @ pose
        else:
            value = rest.inverted() @ pose
        poses.append(pose)
        basis.append(value)
    return basis


def apply_sample(arm, sample, camera=None):
    data = schema(arm)
    sample = rebase_sample(data, sample)
    for b, value in zip(data["bones"], basis_matrices(data, sample)):
        arm.pose.bones[b["blender_name"]].matrix_basis = value
    arm.location = C.to_3x3() @ Vector(sample["root"])
    for i, face in enumerate(data["faces"]):
        arm[bone_key(i)] = sample["faces"].get(face["name"], 0.)
    arm["epb_visible"] = float(sample.get("visible", True))
    if camera and sample.get("camera"):
        apply_camera(camera, sample["camera"])


def apply_camera(obj, value):
    obj.rotation_mode = "QUATERNION"
    obj.matrix_world = C @ trs(value["p"], value["q"]) @ CAM
    obj.data.type = "PERSP" if value.get("perspective", True) else "ORTHO"
    obj.data.sensor_fit = "VERTICAL"
    obj.data.sensor_height = 24
    obj.data.lens = 12 / math.tan(math.radians(value["fov"]) / 2)
    obj.data.ortho_scale = value.get("size", 5) * 2
    forward = obj.rotation_quaternion @ Vector((0, 0, -1))
    obj.data.dof.focus_distance = max(.1, (C.to_3x3() @ Vector(value["target"]) - obj.location).dot(forward))


def create_camera(data, scene):
    obj = bpy.data.objects.new("Endfield 镜头", bpy.data.cameras.new("Endfield 镜头"))
    obj['epb_managed_camera'] = True
    scene.collection.objects.link(obj)
    scene.camera = obj
    obj["epb_managed"] = True
    value = data.get("camera") or {"p": [0, 1.3, -4], "q": [0, 0, 0, 1], "target": [0, 1.3, 0], "fov": 45}
    apply_camera(obj, value)
    return obj


def packet(arm, camera, scene, session, sequence, data=None, depsgraph=None):
    data = data or schema(arm)
    # The update handler already owns a fully evaluated graph. Re-entering its
    # evaluation here can miss modal edits or recursively trigger the handler.
    deps = depsgraph if depsgraph is not None else bpy.context.evaluated_depsgraph_get()
    evaluated = arm.evaluated_get(deps)
    root = C.to_3x3() @ evaluated.matrix_world.translation
    worlds, bones = [], []
    for b in data["bones"]:
        pb = evaluated.pose.bones[b["blender_name"]]
        world = C @ pb.matrix @ matrix(b["compensation"]) @ C
        if b["parent"] >= 0:
            parent = worlds[b["parent"]]
        else:
            parent = matrix(b["world"]) @ trs(b["p"], b["q"], b["scale"]).inverted()
        local = parent.inverted() @ world
        worlds.append(world)
        if b["editable"]:
            p, q, _ = local.decompose()
            bones.append({"i": b.get('game_index', b['i']), "p": list(p), "q": qlist(q)})
    view = None
    if camera:
        cam = camera.evaluated_get(deps)
        transform = C @ cam.matrix_world @ CAM
        p, q, _ = transform.decompose()
        target = p + q @ Vector((0, 0, max(.1, cam.data.dof.focus_distance)))
        # Use the actual render aspect and Blender's sensor-fit convention.
        aspect = scene.render.resolution_x * scene.render.pixel_aspect_x / max(1, scene.render.resolution_y * scene.render.pixel_aspect_y)
        vertical = cam.data.sensor_fit == "VERTICAL" or (cam.data.sensor_fit == "AUTO" and aspect < 1)
        sensor_y = cam.data.sensor_height if vertical else cam.data.sensor_width / aspect
        view = {"p": list(p), "q": qlist(q), "target": list(target),
                "fov": math.degrees(2 * math.atan(sensor_y / (2 * cam.data.lens))),
                "size": cam.data.ortho_scale / 2, "perspective": cam.data.type != "ORTHO"}
    fps = scene.render.fps / scene.render.fps_base
    return {"session": session, "sequence": sequence, "time": max(0, (scene.frame_current_final - 1) / fps),
            "root": list(root), "bones": bones, "faces": {f["name"]: max(0., min(1., evaluated.get(bone_key(i), 0.))) for i, f in enumerate(data["faces"]) if f.get('available', True)},
            "camera": view, "anchor_rotation": data.get('anchor_rotation', [0, 0, 0, 1]),
            "visible": evaluated.get("epb_visible", 1) >= .5}
