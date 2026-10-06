"""One shared scene/time/camera, independent actor rigs and correction stacks."""
import math
import bpy
from . import rig, animation

active = False
schemas = {}
identities = {}
baseline = None
face_untouched = {}


def members(scene):
    return {i: getattr(scene, f'epb_member_{i}', None) for i in range(4)
            if getattr(scene, f'epb_member_{i}', None) is not None}


def valid(scene):
    return {i: arm.session_uid for i, arm in members(scene).items()} == identities


def clear():
    global active, baseline
    active, baseline = False, None
    schemas.clear(); identities.clear(); face_untouched.clear()


def touch_face(scene):
    if active:
        for slot, arm in members(scene).items():
            if arm == scene.epb_armature: face_untouched[slot] = False


def connect(owner, result):
    global active, baseline
    incoming = result['members']
    if not 1 <= len(incoming) <= 4 or len({m['slot'] for m in incoming}) != len(incoming):
        raise ValueError('游戏返回了无效的小队成员列表')
    clear()
    scene = bpy.context.scene
    fresh = not scene.get('epb_squad')
    if not scene.get('epb_squad'):
        scene = bpy.data.scenes.new('Endfield 小队动作编辑')
        bpy.context.window.scene = scene
        scene.render.fps, scene.render.fps_base = 30, 1.
        scene.render.resolution_x, scene.render.resolution_y = 1920, 1080
        scene['epb_squad'] = True
    scene.epb_mode = 'squad'
    old = members(scene)
    for slot in range(4): setattr(scene, f'epb_member_{slot}', None)
    camera = scene.epb_camera
    for member in incoming:
        slot = member['slot']
        scene.epb_armature = old.get(slot)
        owner.connect_current(member, scene, camera)
        arm = scene.epb_armature; camera = scene.epb_camera
        arm['epb_squad_slot'] = slot
        if old.get(slot) and old[slot] != arm: old[slot].hide_set(True)
        arm.hide_set(False)
        setattr(scene, f'epb_member_{slot}', arm)
        schemas[slot] = rig.schema(arm)
        identities[slot] = arm.session_uid
        face_untouched[slot] = True
    for slot, arm in old.items():
        if slot not in identities: arm.hide_set(True)
    first = min(identities)
    if fresh:
        scene.frame_start = 1
        scene.frame_end = max(2, math.ceil(result['duration'] * owner.scene_fps(scene)) + 1)
        scene.epb_import_end = scene.frame_end
    scene.epb_armature = members(scene)[first]
    owner._connected_arm = scene.epb_armature
    owner._connected_arm_uid = scene.epb_armature.session_uid
    owner._schema = schemas[first]
    # All actors now exist; apply connection samples after the last frame_set.
    for member in incoming:
        rig.apply_sample(members(scene)[member['slot']], member['initial'])
    bpy.context.view_layer.update()
    active = True
    baseline = packet(owner, scene, 0)
    owner._connection_packet = baseline
    owner._preview_started = False
    owner._status = f'已连接 {len(identities)} 位队员 · 选择骨架分别编辑'
    select(scene, first)


def select(scene, slot):
    arm = members(scene).get(slot)
    if not arm: raise ValueError('此队员没有编辑骨架')
    animation.leave_tweak(scene)
    if bpy.context.object and bpy.context.object.mode != 'OBJECT':
        bpy.ops.object.mode_set(mode='OBJECT')
    for item in bpy.context.selected_objects: item.select_set(False)
    scene.epb_armature = arm
    arm.select_set(True); bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode='POSE')


def packet(owner, scene, sequence, depsgraph=None):
    if not valid(scene): raise ValueError('小队编辑骨架已移除或替换，请重新连接')
    values = []
    camera = None
    for slot, arm in members(scene).items():
        data = rig.packet(arm, scene.epb_camera if not values and scene.epb_camera_sync else None,
                          scene, owner._session, sequence, schemas[slot], depsgraph)
        if not values: camera = data['camera']
        data['slot'] = slot
        data.pop('camera', None)
        values.append(data)
    return {'session': owner._session, 'sequence': sequence,
            'time': max(0, (scene.frame_current_final-1)/owner.scene_fps(scene)),
            'members': values, 'camera': camera,
            'anchor_rotation': schemas[min(schemas)].get('anchor_rotation', [0, 0, 0, 1]),
            'playing': bool(bpy.context.screen and bpy.context.screen.is_animation_playing)}


def preview(owner, scene, depsgraph=None):
    owner._sequence += 1
    data = packet(owner, scene, owner._sequence, depsgraph)
    for member in data['members']:
        slot = member['slot']
        previous = next((v for v in baseline['members'] if v['slot'] == slot), None) if baseline else None
        face_untouched[slot] = face_untouched[slot] and bool(previous and previous['faces'] == member['faces'])
    compare = ('time', 'members', 'camera', 'anchor_rotation')
    # Ignore the transport sequence inside each actor when comparing poses.
    def content(value):
        return [{k: v for k, v in m.items() if k not in ('session', 'sequence', 'preserve_face')} for m in value]
    unchanged = baseline and all((content(data[k]) == content(baseline[k]) if k == 'members' else data[k] == baseline[k]) for k in compare)
    if unchanged and all(face_untouched.values()) and not owner._preview_started:
        owner.keep_alive(); return
    for member in data['members']:
        slot = member['slot']
        member['preserve_face'] = face_untouched[slot]
        if not schemas[slot].get('bone_scale', False):
            rest = {b.get('game_index', b['i']): b for b in schemas[slot]['bones']}
            if any(any(abs(v-r) > 1e-4 * max(1, abs(r)) for v, r in zip(b['s'], rest[b['i']]['scale'])) for b in member['bones']):
                raise ValueError(f'第 {slot+1} 位骨骼缩放接口不可用，请更新游戏端')
            for bone in member['bones']: bone.pop('s', None)
    owner.client().preview(data, owner.reply_for(owner._session, scene.frame_current_final))
    owner._preview_started = True


class Baker:
    def __init__(self, scene, fps):
        self.bakers = {slot: animation.Baker(arm, scene.epb_camera if slot == min(schemas) else None,
                                           schemas[slot], fps) for slot, arm in members(scene).items()}

    def sample(self, sample):
        for member in sample['members']:
            value = dict(member); value['camera'] = sample.get('camera')
            self.bakers[member['slot']].sample(value)

    def finish(self):
        for slot, baker in self.bakers.items():
            baker.finish(); face_untouched[slot] = False


def undo(scene):
    if not valid(scene): return False
    schemas.update({slot: rig.schema(arm) for slot, arm in members(scene).items()})
    return True
