"""Native Blender actions and NLA correction layers, stored in the .blend."""
from array import array
from bisect import bisect_right
import math
import bpy
from mathutils import Vector
from . import rig


class Baker:
    def __init__(self, arm, camera, data, fps):
        self.arm, self.camera, self.data, self.fps = arm, camera, data, fps
        self.channels, self.previous = {}, {}
        self.cut_frames = sorted(1+float(t)*fps for t in data.get('camera_cuts', []))

    def add(self, owner, path, index, frame, value):
        self.channels.setdefault((owner, path, index), array('f')).extend((frame, value))

    def quaternion(self, key, q):
        if key in self.previous and q.dot(self.previous[key]) < 0:
            q.negate()
        self.previous[key] = q.copy()
        return q

    def sample(self, sample):
        sample = rig.rebase_sample(self.data, sample)
        frame = 1 + sample['time'] * self.fps
        if self.arm:
            for b, value in zip(self.data['bones'], rig.basis_matrices(self.data, sample)):
                if not b['editable']:
                    continue
                pb = self.arm.pose.bones[b['blender_name']]
                p, q, scale = value.decompose()
                q = self.quaternion(b['i'], q)
                for path, values in [('location', p), ('rotation_quaternion', q), ('scale', scale)]:
                    for i, v in enumerate(values):
                        self.add(self.arm, pb.path_from_id(path), i, frame, v)
            for i, v in enumerate(rig.C.to_3x3() @ Vector(sample['root'])):
                self.add(self.arm, 'location', i, frame, v)
            if 'root_rotation' in sample:
                q = self.quaternion('root', rig.root_rotation(self.data, sample))
                for i, v in enumerate(q): self.add(self.arm, 'rotation_quaternion', i, frame, v)
            for i, face in enumerate(self.data['faces']):
                self.add(self.arm, f'["{rig.bone_key(i)}"]', 0, frame, sample['faces'].get(face['name'], 0))
            self.add(self.arm, '["epb_visible"]', 0, frame, float(sample.get('visible', True)))
        c = sample.get('camera')
        if self.camera and c:
            world = rig.C @ rig.trs(c['p'], c['q']) @ rig.CAM
            p, q, _ = world.decompose()
            q = self.quaternion('camera', q)
            for path, values in [('location', p), ('rotation_quaternion', q)]:
                for i, v in enumerate(values):
                    self.add(self.camera, path, i, frame, v)
            self.add(self.camera.data, 'lens', 0, frame, 12 / math.tan(math.radians(c['fov']) / 2))
            self.add(self.camera.data, 'ortho_scale', 0, frame, 2*c.get('size', 5))
            self.add(self.camera.data, 'type', 0, frame, 0 if c.get('perspective', True) else 1)
            target = rig.C.to_3x3() @ Vector(c['target'])
            focus = max(.1, (target - p).dot(q @ Vector((0, 0, -1))))
            self.add(self.camera.data, 'dof.focus_distance', 0, frame, focus)

    def finish(self):
        leave_tweak(bpy.context.scene)
        actions, bags = {}, {}
        for (obj, path, index), values in self.channels.items():
            if obj not in actions:
                action = bpy.data.actions.new('原始动作 · ' + obj.name)
                action.use_fake_user = True
                action['epb_base'] = True
                actions[obj] = action
                slot = action.slots.new(id_type=obj.id_type, name=obj.name)
                layer = action.layers.new('导入关键帧')
                strip = layer.strips.new(type='KEYFRAME')
                bags[obj] = strip.channelbag(slot, ensure=True)
            fc = bags[obj].fcurves.new(path, index=index)
            if len(values) > 4 and all(v == values[1] for v in values[1::2]):
                values = array('f', (values[0], values[1], values[-2], values[-1]))
            fc.keyframe_points.add(len(values)//2)
            fc.keyframe_points.foreach_set('co', values)
            for key in fc.keyframe_points:
                key.interpolation = 'CONSTANT' if path in ('["epb_visible"]', 'type') else 'LINEAR'
            if self.camera and obj in (self.camera, self.camera.data):
                actions[obj]['epb_camera_cuts'] = self.cut_frames
                for a, b in zip(fc.keyframe_points, list(fc.keyframe_points)[1:]):
                    if bisect_right(self.cut_frames, a.co.x) < bisect_right(self.cut_frames, b.co.x+1e-4):
                        a.interpolation = 'CONSTANT'
            fc.update_autoflags(obj)
            fc.update()
        # Replace only the imported source, below the user's correction stack.
        # The active correction and its two camera slots must survive reimport.
        for obj, action in actions.items():
            replace_base(obj, action)
        self.channels.clear()
        return actions


def is_base(action):
    return action is not None and (action.get('epb_base', False) or
        action.name.startswith(('原始动作 · ', '原始镜头 · ', '连接时姿态')))


def bottom_track(obj, track):
    # RNA only inserts *after* another track. The NLA channel operator can
    # place a new source below an independently created correction stack.
    if obj.animation_data.nla_tracks[0] == track:
        return
    context = bpy.context
    area = next(a for a in context.screen.areas if a.type in {'NLA_EDITOR', 'DOPESHEET_EDITOR', 'VIEW_3D'})
    area_type = area.type
    selected = []
    owners = dict.fromkeys(list(context.scene.objects) + [o.data for o in context.scene.objects if o.data])
    for owner in owners:
        ad = owner.animation_data
        if ad:
            for t in ad.nla_tracks:
                selected.append((t, t.select))
                t.select = t == track
    try:
        area.type = 'NLA_EDITOR'
        space = area.spaces.active
        only_selected = space.dopesheet.show_only_selected
        space.dopesheet.show_only_selected = False
        try:
            with context.temp_override(area=area, region=next(r for r in area.regions if r.type == 'WINDOW')):
                bpy.ops.anim.channels_move(direction='BOTTOM')
        finally:
            space.dopesheet.show_only_selected = only_selected
        if obj.animation_data.nla_tracks[0] != track:
            raise RuntimeError('无法放置底层原动作，请在 NLA 编辑器检查轨道顺序')
    finally:
        for t, value in selected:
            t.select = value
        area.type = area_type


def replace_base(obj, action):
    ad = obj.animation_data_create()
    ad.use_nla = True
    slot = next(s for s in action.slots if s.target_id_type == obj.id_type)
    if ad.action and is_base(ad.action) and not ad.nla_tracks:
        ad.action.use_fake_user = True
        ad.action, ad.action_slot = action, slot
        ad.action_blend_type, ad.action_influence = 'REPLACE', 1.
        return
    if not ad.action and not ad.nla_tracks:
        ad.action, ad.action_slot = action, slot
        ad.action_blend_type, ad.action_influence = 'REPLACE', 1.
        return
    # A legacy reimport left the new base at the top, masking the lower layers.
    if is_base(ad.action):
        ad.action.use_fake_user = True
        ad.action = None
    bases = [(track, strip) for track in ad.nla_tracks for strip in track.strips if is_base(strip.action)]
    if bases:
        track, strip = bases[0]
        strip.action.use_fake_user = True
        strip.action, strip.action_slot = action, slot
        track.mute = False
    else:
        # No previous source, e.g. an independently created correction.
        track = ad.nla_tracks.new()
        track.name = '原始动作'
        strip = track.strips.new(action.name, int(action.frame_range[0]), action)
        strip.action_slot = slot
        bottom_track(obj, track)
    start, end = action.frame_range
    strip.action_frame_start, strip.action_frame_end = start, max(start+1, end)
    strip.frame_start, strip.frame_end = start, max(start+1, end)
    strip.blend_type, strip.extrapolation, strip.influence = 'REPLACE', 'HOLD', 1.
    obj.update_tag()


def leave_tweak(scene):
    if not scene.is_nla_tweakmode:
        return
    context = bpy.context
    area = next((a for a in context.screen.areas if a.type in {'DOPESHEET_EDITOR', 'NLA_EDITOR'}), None)
    if area:
        previous = area.type
        area.type = 'NLA_EDITOR'
        region = next(r for r in area.regions if r.type == 'WINDOW')
        with context.temp_override(area=area, region=region):
            bpy.ops.nla.tweakmode_exit(isolate_action=False)
        area.type = previous


def push_action(obj, scene):
    ad = obj.animation_data_create()
    if not ad.action:
        return None
    if not ad.action.slots:
        return None
    action, slot, blend = ad.action, ad.action_slot, ad.action_blend_type
    track = ad.nla_tracks.new()
    track.name = action.name
    start, end = action.frame_range
    strip = track.strips.new(action.name, int(start), action)
    strip.action_slot = slot
    strip.action_frame_start = start
    strip.action_frame_end = max(start+1, end)
    strip.blend_type = blend
    track.mute = ad.action_influence == 0
    strip.influence = ad.action_influence or 1.
    strip.extrapolation = 'HOLD'
    ad.action = None
    return track


def capture_base(arm, scene):
    ad = arm.animation_data_create()
    if ad.action is None and not ad.nla_tracks:
        # A captured frozen pose is also a valid base, without importing a VMD.
        ad.action = bpy.data.actions.new('连接时姿态')
        ad.action_blend_type = 'REPLACE'
        for pb in arm.pose.bones:
            pb.keyframe_insert('location', frame=scene.frame_start, group=pb.name)
            pb.keyframe_insert('rotation_quaternion', frame=scene.frame_start, group=pb.name)
            pb.keyframe_insert('scale', frame=scene.frame_start, group=pb.name)
        arm.keyframe_insert('location', frame=scene.frame_start)
        if 'root_rotation' in rig.schema(arm):
            arm.keyframe_insert('rotation_quaternion', frame=scene.frame_start)
        for i, _ in enumerate(rig.schema(arm)['faces']):
            arm.keyframe_insert(f'["{rig.bone_key(i)}"]', frame=scene.frame_start)
        arm.keyframe_insert('["epb_visible"]', frame=scene.frame_start)


def correction_layer(arm, scene):
    leave_tweak(scene)
    ad = arm.animation_data_create()
    ad.use_tweak_mode = False
    if ad.action and ad.action.name.startswith('动作修正层') and not ad.action.slots:
        return ad.action
    capture_base(arm, scene)
    push_action(arm, scene)
    action = bpy.data.actions.new('动作修正层')
    action['epb_correction'] = True
    action.use_fake_user = True
    ad = arm.animation_data_create()
    ad.action = action
    ad.action_blend_type = 'COMBINE'
    ad.action_extrapolation = 'HOLD'
    ad.action_influence = 1
    for pb in arm.pose.bones:
        pb.location = (0, 0, 0)
        pb.rotation_quaternion = (1, 0, 0, 0)
        pb.scale = (1, 1, 1)
    arm.location = (0, 0, 0)
    for i, _ in enumerate(rig.schema(arm)['faces']):
        arm[rig.bone_key(i)] = 0.0
    arm['epb_visible'] = 0.0
    return action


def is_correction(action):
    return action is not None and (action.get('epb_correction', False) or action.name.startswith('动作修正层'))


def is_any_correction(action):
    return is_correction(action) or (action is not None and action.get('epb_camera_correction', False))


def selected_joints(arm):
    return [arm.pose.bones[b['blender_name']] for b in rig.schema(arm)['bones']
            if b['editable'] and arm.pose.bones[b['blender_name']].select
            and not arm.pose.bones[b['blender_name']].hide
            and not arm.pose.bones[b['blender_name']].bone.hide_select]


def pose_values(pb):
    return (pb.location.copy(), pb.rotation_quaternion.copy(), pb.scale.copy())


def set_pose_values(pb, values):
    pb.location, pb.rotation_quaternion, pb.scale = values


def ensure_correction(arm, scene):
    ad = arm.animation_data_create()
    if is_correction(ad.action):
        return
    # The user may adjust first, then press Key. Preserve that visible pose
    # while moving the original action below the new additive layer.
    visible = [(pb, pose_values(pb)) for pb in arm.pose.bones]
    location = arm.location.copy()
    properties = {rig.bone_key(i): arm.get(rig.bone_key(i), 0.) for i, _ in enumerate(rig.schema(arm)['faces'])}
    properties['epb_visible'] = arm.get('epb_visible', 1.)
    correction_layer(arm, scene)
    scene.frame_set(scene.frame_current, subframe=scene.frame_subframe)
    for pb, values in visible:
        set_pose_values(pb, values)
    arm.location = location
    # Facial/root values may be a freshly captured game pose, not yet keyed.
    # Preserve them when the first joint correction creates an additive layer.
    for key, value in properties.items():
        arm[key] = value
    bpy.context.view_layer.update()


def key_pose(arm, frame, selected=True, scene=None):
    scene = scene or bpy.context.scene
    joints = selected_joints(arm) if selected else [arm.pose.bones[b['blender_name']]
                                                   for b in rig.schema(arm)['bones'] if b['editable']]
    if not joints:
        return 0
    ensure_correction(arm, scene)
    count = 0
    action = arm.animation_data.action
    for pb in joints:
        pb.keyframe_insert('location', frame=frame, group=pb.name)
        pb.keyframe_insert('rotation_quaternion', frame=frame, group=pb.name)
        pb.keyframe_insert('scale', frame=frame, group=pb.name)
        for path, channels in [('location', 3), ('rotation_quaternion', 4), ('scale', 3)]:
            for i in range(channels):
                fc = action.fcurve_ensure_for_datablock(arm, pb.path_from_id(path), index=i)
                # A new joint correction starts from zero at the motion start.
                # Blender keyframe_insert performs the NLA inverse mapping, so
                # stored keys are offsets rather than a second copy of the pose.
                if len(fc.keyframe_points) == 1 and frame > scene.frame_start:
                    neutral = 1.0 if path == 'scale' or (path == 'rotation_quaternion' and i == 0) else 0.0
                    fc.keyframe_points.insert(scene.frame_start, neutral)
                for key in fc.keyframe_points:
                    if key.co.x in (frame, scene.frame_start):
                        key.interpolation = 'BEZIER'
                        key.handle_left_type = key.handle_right_type = 'AUTO_CLAMPED'
                fc.update()
        count += 1
    # Direct FCurve inserts do not invalidate the evaluated Action cache.
    action.update_tag()
    arm.update_tag()
    bpy.context.view_layer.update()
    return count


def enter_tweak(obj, index, scene):
    leave_tweak(scene)
    context = bpy.context
    area = next(a for a in context.screen.areas if a.type in {'DOPESHEET_EDITOR', 'NLA_EDITOR'})
    previous = area.type
    area.type = 'NLA_EDITOR'
    ad = obj.animation_data
    for track in ad.nla_tracks:
        track.select = False
        for strip in track.strips:
            strip.select = False
    track = ad.nla_tracks[index]
    ad.nla_tracks.active = track
    track.select = True
    track.strips[0].select = True
    region = next(r for r in area.regions if r.type == 'WINDOW')
    with context.temp_override(area=area, region=region):
        bpy.ops.nla.tweakmode_enter(isolate_action=False, use_upper_stack_evaluation=True)
    area.type = previous


def restore_selected(arm, scene):
    joints = selected_joints(arm)
    if not joints:
        return 0
    ensure_correction(arm, scene)
    visible = [(pb, pose_values(pb)) for pb in arm.pose.bones]
    ad = arm.animation_data
    index = next((i for i, track in enumerate(ad.nla_tracks)
                  if any(strip.action == ad.action for strip in track.strips)), -1) if ad.use_tweak_mode else -1
    if index >= 0:
        leave_tweak(scene)
        control, attribute = ad.nla_tracks[index], 'mute'
        previous = control.mute
        control.mute = True
    else:
        control, attribute = ad, 'action_influence'
        previous = ad.action_influence
        ad.action_influence = 0
    underlying = []
    try:
        for pb in joints:
            pb.location = (0, 0, 0)
            pb.rotation_quaternion = (1, 0, 0, 0)
            pb.scale = (1, 1, 1)
        scene.frame_set(scene.frame_current, subframe=scene.frame_subframe)
        underlying = [(pb, pose_values(pb)) for pb in joints]
    finally:
        setattr(control, attribute, previous)
        if index >= 0:
            enter_tweak(arm, index, scene)
        scene.frame_set(scene.frame_current, subframe=scene.frame_subframe)
        for pb, values in visible:
            set_pose_values(pb, values)
    for pb, values in underlying:
        set_pose_values(pb, values)
    bpy.context.view_layer.update()
    return len(joints)


def camera_channels(camera):
    return ((camera, ('location', 'rotation_quaternion')),
            (camera.data, ('lens', 'dof.focus_distance', 'ortho_scale')))


def path_parent(owner, path):
    if '.' in path:
        prefix, key = path.rsplit('.', 1)
        return owner.path_resolve(prefix), key
    return owner, path


def camera_values(camera):
    result = []
    for owner, paths in camera_channels(camera):
        for path in paths:
            parent, key = path_parent(owner, path)
            value = getattr(parent, key)
            result.append((parent, key, value.copy() if hasattr(value, 'copy') else value))
    return result


def set_values(values):
    for parent, key, value in values:
        setattr(parent, key, value)


def has_camera_correction(camera):
    ad = camera.animation_data
    return bool(ad and ad.action and ad.action.get('epb_camera_correction') and
                camera.data.animation_data and camera.data.animation_data.action == ad.action)


def action_has_keys(action):
    return any(fc.keyframe_points for layer in action.layers for strip in layer.strips
               for bag in strip.channelbags for fc in bag.fcurves)


def camera_layer(camera, scene):
    leave_tweak(scene)
    if has_camera_correction(camera) and not action_has_keys(camera.animation_data.action):
        return camera.animation_data.action
    # Object transform and lens/focus have separate ID owners. One Action with
    # two slots keeps each camera correction together in the .blend file.
    for owner, paths in camera_channels(camera):
        ad = owner.animation_data_create()
        ad.use_tweak_mode = False
        if not ad.action and not ad.nla_tracks:
            ad.action = bpy.data.actions.new('原始镜头 · ' + owner.name)
            ad.action.use_fake_user = True
            ad.action_blend_type = 'REPLACE'
            for path in paths:
                owner.keyframe_insert(path, frame=scene.frame_start)
        push_action(owner, scene)
    action = bpy.data.actions.new('镜头修正层')
    action['epb_camera_correction'] = True
    action.use_fake_user = True
    for owner, _ in camera_channels(camera):
        ad = owner.animation_data_create()
        ad.action = action
        ad.action_slot = action.slots.new(id_type=owner.id_type, name=owner.name)
        ad.action_blend_type = 'COMBINE'
        ad.action_extrapolation = 'HOLD'
        ad.action_influence = 1
        owner.update_tag()
    bpy.context.view_layer.update()
    return action


def ensure_camera_correction(camera, scene):
    if not has_camera_correction(camera):
        visible = camera_values(camera)
        camera_layer(camera, scene)
        set_values(visible)
        bpy.context.view_layer.update()


def key_camera(camera, scene):
    ensure_camera_correction(camera, scene)
    frame = scene.frame_current_final
    action = camera.animation_data.action
    for owner, paths in camera_channels(camera):
        for path in paths:
            owner.keyframe_insert(path, frame=frame, group='镜头修正')
            parent, key = path_parent(owner, path)
            prop = parent.bl_rna.properties[key]
            neutral = list(prop.default_array) if prop.is_array else [prop.default]
            for index, default in enumerate(neutral):
                curve = action.fcurve_ensure_for_datablock(owner, path, index=index)
                if len(curve.keyframe_points) == 1 and frame > scene.frame_start:
                    curve.keyframe_points.insert(scene.frame_start, default)
                for point in curve.keyframe_points:
                    if point.co.x in (frame, scene.frame_start):
                        point.interpolation = 'BEZIER'
                        point.handle_left_type = point.handle_right_type = 'AUTO_CLAMPED'
                curve.update()
        owner.update_tag()
    align_camera_quaternions(action)
    action.update_tag()
    bpy.context.view_layer.update()


def align_camera_quaternions(action):
    # q and -q describe the same view. Component FCurves must never interpolate
    # through zero between equivalent signs (a one-frame 180-degree flip).
    changed = False
    for layer in action.layers:
        for strip in layer.strips:
            for bag in strip.channelbags:
                curves = sorted((fc for fc in bag.fcurves if fc.data_path == 'rotation_quaternion'), key=lambda fc: fc.array_index)
                if len(curves) != 4 or [fc.array_index for fc in curves] != list(range(4)):
                    continue
                points = [{p.co.x: p for p in fc.keyframe_points} for fc in curves]
                previous = None
                for frame in sorted(set.intersection(*(set(p) for p in points))):
                    current = [p[frame].co.y for p in points]
                    if previous is not None and sum(a*b for a,b in zip(previous,current)) < 0:
                        for p in points:
                            key=p[frame]
                            key.co.y *= -1; key.handle_left.y *= -1; key.handle_right.y *= -1
                        current = [-v for v in current]
                        changed = True
                    previous = current
                for fc in curves: fc.update()
    if changed: action.update_tag()
    return changed


def camera_cut_frames(camera):
    """Authored cuts, including constant user keys, mapped through NLA time."""
    cuts = set()
    def action_cuts(action, slot):
        values = set(float(f) for f in action.get('epb_camera_cuts', []))
        for layer in action.layers:
            for strip in layer.strips:
                bag = strip.channelbag(slot) if slot else None
                if not bag: continue
                for fc in bag.fcurves:
                    for a,b in zip(fc.keyframe_points, list(fc.keyframe_points)[1:]):
                        if a.interpolation == 'CONSTANT' and abs(a.co.y-b.co.y) > 1e-6:
                            values.add(float(b.co.x))
        return values
    for owner in (camera, camera.data):
        ad = owner.animation_data
        if not ad: continue
        if ad.action and ad.action_influence > 0:
            cuts.update(action_cuts(ad.action, ad.action_slot))
        solo = any(t.is_solo for t in ad.nla_tracks)
        for track in ad.nla_tracks:
            if track.mute or (solo and not track.is_solo): continue
            for strip in track.strips:
                if strip.mute or not strip.action or strip.use_animated_time: continue
                span=strip.action_frame_end-strip.action_frame_start
                for f in action_cuts(strip.action, strip.action_slot):
                    if not strip.action_frame_start < f <= strip.action_frame_end: continue
                    for repeat in range(min(1000, math.ceil(strip.repeat))):
                        at=strip.frame_start+(f-strip.action_frame_start+repeat*span)*strip.scale
                        if strip.frame_start < at <= strip.frame_end+1e-4: cuts.add(at)
    return sorted(cuts)


def repair_camera_quaternions(camera):
    if not camera or not camera.animation_data: return
    ad = camera.animation_data
    actions = {s.action for t in ad.nla_tracks for s in t.strips if s.action}
    if ad.action: actions.add(ad.action)
    for action in actions:
        if action.get('epb_camera_correction'): align_camera_quaternions(action)


def restore_camera(camera, scene):
    ensure_camera_correction(camera, scene)
    owners = [owner for owner, _ in camera_channels(camera)]
    influences = [owner.animation_data.action_influence for owner in owners]
    try:
        for owner in owners:
            owner.animation_data.action_influence = 0
            owner.update_tag()
        bpy.context.view_layer.update()
        underlying = camera_values(camera)
    finally:
        for owner, influence in zip(owners, influences):
            owner.animation_data.action_influence = influence
            owner.update_tag()
        bpy.context.view_layer.update()
    set_values(underlying)
    bpy.context.view_layer.update()


def fade_layer(arm, scene, start, end, fade):
    if end <= start:
        raise ValueError('生效终点必须晚于起点')
    track = push_action(arm, scene)
    if track is None:
        raise ValueError('请先创建修正层并打关键帧')
    strip = track.strips[0]
    strip.action_frame_start = min(strip.action_frame_start, start-1)
    strip.action_frame_end = max(strip.action_frame_end, end+1)
    strip.frame_start = strip.action_frame_start
    strip.frame_end = strip.action_frame_end
    strip.use_animated_influence = True
    fade = min(fade, (end-start)/2)
    for f, weight in [(start-1, 0), (start+fade, 1), (end-fade, 1), (end+1, 0)]:
        strip.influence = weight
        strip.keyframe_insert('influence', frame=f)
    strip.extrapolation = 'NOTHING'
    return track
