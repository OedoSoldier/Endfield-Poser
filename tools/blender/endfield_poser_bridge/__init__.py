"""Endfield skeleton, expression, camera and correction-layer editor."""
bl_info = {
    'name': 'Endfield Poser Bridge', 'author': 'Endfield Poser',
    'version': (0, 3, 2), 'blender': (5, 2, 0),
    'location': '3D View > N > Endfield', 'category': 'Animation',
    'description': '编辑游戏骨架、中文表情、镜头与动作修正层',
}
import math
import json
import os
import tempfile
import time
import sys
from contextlib import contextmanager
from bisect import bisect_right
from concurrent.futures import ThreadPoolExecutor
import bpy
from bpy.app.handlers import persistent
from bpy.props import BoolProperty, IntProperty, StringProperty, PointerProperty, EnumProperty
from bpy_extras.io_utils import ImportHelper, ExportHelper
from . import rig, animation, transport, console, squad, clip_io

_client = None
_session = 0
_sequence = 0
_status = '未连接'
_connection_status = '未连接游戏'
_schema = None
_file_import = None
_last = 0.
_busy = False
_baker = None
_connected_arm = None
_display_pending = False
_connected_scene = None
_sampling = False
_ack_frame = None
_ack_time = 0.
_connection_packet = None
_face_untouched = False
_last_ping = 0.
_preview_started = False
_undoing = False
_undo_live = False
_connected_scene_uid = None
_connected_arm_uid = None
_connecting = False
_connect_generation = 0
_connect_scene_uid = None
_connect_retry = 0.
_connect_deadline = 0.
_pending_session = 0
_recovering = False
_failure_since = None


def scene_fps(scene):
    fps = scene.render.fps / scene.render.fps_base
    if not math.isfinite(fps) or not 1 <= fps <= 120:
        raise ValueError('联动支持 1–120 FPS，请调整场景帧率')
    return fps


def keep_alive():
    global _last_ping
    now = time.monotonic()
    if _session and now-_last_ping >= 1:
        client().request('ping', {'session': _session}, reply_for(_session))
        _last_ping = now


def connection_error(exc):
    global _status
    reason = str(exc) or type(exc).__name__
    console.exception()
    disconnect()
    _status = '同步中断：' + reason
    globals()['_connection_status'] = _status


def client():
    global _client
    if _client is None:
        _client = transport.Client()
    return _client


def report(operator, levels, message):
    global _status
    # Blender's native report also prints to its console. Keep normal feedback
    # in our panel, including errors; verbose reports are explicitly opt-in.
    _status = message
    if console.enabled():
        console.configure()
        operator.report(levels, message)


def check(result):
    global _status
    if not result.get('ok'):
        _status = result.get('error', '联动失败')
        raise RuntimeError(_status)
    return result


def preview_result(result):
    global _status, _connection_status, _failure_since
    if not result.get('ok'):
        _connection_status = result.get('error', '联动中断')
        if result.get('retryable'):
            if _failure_since is None:
                _failure_since = time.monotonic()
            if time.monotonic()-_failure_since >= 8:
                suspend_connection(_connection_status)
            return
        suspend_connection(_connection_status, retry='Session expired or character changed' in _connection_status)
    else:
        _failure_since = None
        _connection_status = '已连接游戏'


def suspend_connection(reason, retry=True):
    """Release only the live lease; Blender owns all authored data."""
    global _recovering, _connecting, _connect_scene_uid, _connect_retry
    global _status, _connection_status, _busy
    scene = _connected_scene
    # An offline file export does not depend on the lost game connection.
    exporting = _busy and _baker is None
    disconnect()
    _busy = exporting
    if scene and scene.epb_armature and retry:
        _recovering = _connecting = True
        _connect_scene_uid = scene.session_uid
        _connect_retry = time.monotonic()+2
    _connection_status = '游戏连接中断 · 编辑内容保留'
    _status = ('等待游戏恢复后重连；可继续离线编辑' if _recovering else '请检查后重新连接：'+reason)


def reply_for(session, frame=None):
    generation = _connect_generation
    def reply(result):
        global _ack_frame, _ack_time
        if _session == session and generation == _connect_generation:
            preview_result(result)
            if result.get('ok') and frame is not None:
                _ack_frame, _ack_time = frame, time.monotonic()
                globals()['_connection_status'] = '已连接游戏'
    return reply


def connect_result(result):
    with edit_transaction():
        check(result)
        if _recovering:
            validate_resume(bpy.context.scene, result)
        if result.get('mode') == 'squad': squad.connect(sys.modules[__name__], result)
        else:
            squad.clear()
            connect_current(result)


def reusable_schema(arm, incoming):
    previous = rig.schema(arm) if arm and 'epb_scene' in arm else None
    mapped = rig.reconnect_schema(previous, incoming)
    if mapped and all(b['blender_name'] in arm.pose.bones for b in mapped['bones']):
        return mapped
    return None


def validate_resume(scene, result):
    # Preflight every member before modifying any pointer/schema. Never move
    # edits to another character or quietly build an empty rig after relogin.
    if result.get('mode') == 'squad':
        members = squad.members(scene)
        incoming = result['members']
        if not scene.get('epb_squad') or set(members) != {m['slot'] for m in incoming}:
            raise ValueError('小队成员或顺序已改变；请恢复原小队后重新连接，原编辑保留')
        pairs = [(members[m['slot']], m) for m in incoming]
    else:
        if scene.epb_mode == 'squad':
            raise ValueError('连接对象已改变，请恢复原小队后重新连接')
        pairs = [(scene.epb_armature, result)]
    for arm, incoming in pairs:
        mapped = reusable_schema(arm, incoming)
        if not mapped or any(rig.principal(old) and not new['editable']
                             for old, new in zip(rig.schema(arm)['bones'], mapped['bones'])):
            raise ValueError('角色或身体骨架不匹配；请切回原角色后重新连接，原编辑保留')


def connect_current(result, group_scene=None, shared_camera=None):
    global _session, _sequence, _schema, _status, _connected_arm, _connected_scene, _ack_frame
    global _connection_packet, _face_untouched, _preview_started
    global _connected_scene_uid, _connected_arm_uid
    check(result)
    scene = bpy.context.scene
    _session, _sequence = result['session'], 0
    arm = scene.epb_armature
    compatible = reusable_schema(arm, result)
    if compatible:
        _schema = compatible
        # Keep existing property indices so saved face curves remain attached
        # to the same expression. New VMD aliases append instead of reordering.
        available = {f['name']: f for f in result['faces']}
        for face in _schema['faces']:
            face['available'] = face['name'] in available
        known = {f['name'] for f in _schema['faces']}
        for face in result['faces']:
            if face['name'] not in known:
                i = len(_schema['faces'])
                _schema['faces'].append(face)
                key = rig.bone_key(i)
                arm[key] = 0.0
                arm.id_properties_ui(key).update(min=0, max=1, description=face['label'])
        _schema['session'] = _session
        arm['epb_scene'] = json.dumps(_schema, ensure_ascii=False)
    else:
        compatible = None
        # Keep the user's scene untouched; a dedicated scene contains no mesh.
        scene = group_scene or bpy.data.scenes.new('Endfield 动作编辑')
        bpy.context.window.scene = scene
        if not group_scene: scene.render.fps, scene.render.fps_base = 30, 1.
        scene.render.resolution_x, scene.render.resolution_y = 1920, 1080
        arm = rig.create_rig(result, scene)
        scene.epb_armature = arm
        _schema = rig.schema(arm)
        scene.epb_camera = shared_camera or rig.create_camera(result, scene)
    if shared_camera: scene.epb_camera = shared_camera
    # Keep the saved rig/action basis, but record the new game heading for
    # incoming world-space camera/root samples. Updating only the output anchor
    # would instead rotate already saved animation and camera curves.
    _schema['sample_anchor_rotation'] = result.get('anchor_rotation', [0, 0, 0, 1])
    _schema['bone_scale'] = result.get('bone_scale', False)
    # Source metadata is only used for an explicit later import. Existing
    # camera Actions own their cuts and are never rewritten by reconnecting.
    _schema['camera_cuts'] = result.get('camera_cuts', [])
    arm['epb_scene'] = json.dumps(_schema, ensure_ascii=False)
    if not compatible:
        animation.repair_camera_quaternions(scene.epb_camera)
    rig.apply_display(arm, scene.epb_show_fingers)
    fps = scene_fps(scene)
    if not compatible and not group_scene:
        scene.frame_start = 1
        scene.frame_end = max(2, math.ceil(result['duration'] * fps) + 1)
        scene.epb_import_end = scene.frame_end
    # Only a new rig adopts the game pose. On reconnect this would jump the
    # timeline and write a full game pose into an additive correction layer.
    initial = result.get('initial') if not compatible else None
    if initial:
        frame = 1 + initial['time']*fps
        scene.frame_set(int(frame), subframe=frame-int(frame))
        rig.apply_sample(arm, initial, scene.epb_camera)
        animation.capture_base(arm, scene)
        bpy.context.view_layer.update()
        if not initial.get('camera'):
            scene.epb_camera_sync = False
    if not _recovering:
        scene.epb_live = True
    _connected_arm = arm
    _connected_scene = scene
    _connected_scene_uid, _connected_arm_uid = scene.session_uid, arm.session_uid
    _ack_frame = None
    _preview_started = False
    _connection_packet = rig.packet(arm, scene.epb_camera if scene.epb_camera_sync else None,
                                   scene, _session, 0, _schema) if initial else None
    _face_untouched = initial is not None
    _status = '已重连 · 保留当前帧、动作、表情和镜头修改' if compatible else '已连接 · 可编辑骨骼、中文表情和镜头'
    globals()['_connection_status'] = '已连接游戏'


def begin_result(result):
    check(result)
    # One callback later the camera observer has an actual scene camera pose.
    client().request('scene', {'session': result['session']}, connect_result)


def attempt_connect():
    global _connect_retry
    generation = _connect_generation
    _connect_retry = float('inf')
    def reply(result):
        global _connecting, _connect_retry, _connection_status, _status, _pending_session
        global _recovering
        if generation != _connect_generation or not _connecting:
            if result.get('ok') and result.get('session'):
                client().request('end', {'session': result['session']})
                if _connecting:
                    _connect_retry = time.monotonic() + .5
            return
        unavailable = result.get('error', '') in (
            'Character skeleton is not ready', 'Character changed; wait for the skeleton to settle',
            'Game is closing')
        if not result.get('ok') and ((result.get('retryable') and (_recovering or time.monotonic() < _connect_deadline)) or
                                    (_recovering and unavailable)):
            _connect_retry = time.monotonic() + (3 if _recovering else .5)
            _connection_status = '等待游戏就绪或上次连接释放…'
            return
        check(result)
        _pending_session = result['session']
        def ready(data):
            global _connecting, _pending_session, _recovering, _failure_since, _connect_retry
            if generation != _connect_generation or not _connecting:
                return
            if _recovering and (_busy or bpy.context.scene.session_uid != _connect_scene_uid or
                    (not data.get('ok') and (data.get('retryable') or
                     'Session expired or character changed' in data.get('error', '')))):
                client().request('end', {'session': _pending_session})
                _pending_session = 0
                _connect_retry = time.monotonic()+3
                return
            scene = next((s for s in bpy.data.scenes if s.session_uid == _connect_scene_uid), None)
            if not scene:
                disconnect()
                return
            bpy.context.window.scene = scene
            connect_result(data)
            _pending_session = 0
            _connecting = False
            _recovering = False
            _failure_since = None
        client().request('scene', {'session': result['session']}, ready)
    target = next((s for s in bpy.data.scenes if s.session_uid == _connect_scene_uid), None)
    client().request('begin', {'mode': target.epb_mode if target else 'single'}, reply)


def send_preview(scene=None, depsgraph=None):
    global _sequence, _sampling, _last, _face_untouched, _preview_started
    scene = scene if scene is not None else bpy.context.scene
    if _sampling or _undoing or not _session or not scene.epb_armature:
        return
    _sampling = True
    try:
        if squad.active:
            squad.preview(sys.modules[__name__], scene, depsgraph)
            _last = time.monotonic()
            return
        _sequence += 1
        data = rig.packet(scene.epb_armature, scene.epb_camera if scene.epb_camera_sync else None,
                          scene, _session, _sequence, _schema, depsgraph)
        if not _schema.get('bone_scale', False):
            rest = {b.get('game_index', b['i']): b for b in _schema['bones']}
            if any(any(abs(v-r) > 1e-4 * max(1, abs(r)) for v, r in zip(b['s'], rest[b['i']]['scale'])) for b in data['bones']):
                raise ValueError('游戏端尚不支持骨骼缩放，请更新到 Poser 0.5.42 或以上版本')
        unchanged_face = _connection_packet and data['faces'] == _connection_packet['faces']
        _face_untouched = _face_untouched and bool(unchanged_face)
        if (_face_untouched and not _preview_started and _connection_packet and all(data[k] == _connection_packet[k]
                for k in ('time', 'root', 'bones', 'faces', 'camera', 'visible'))):
            # A timer/UI redraw is not an edit. Keep the exact captured native
            # expression and camera, including channels absent from this rig.
            keep_alive()
        else:
            data['preserve_face'] = _face_untouched
            if not _schema.get('bone_scale', False):
                for bone in data['bones']: bone.pop('s', None)
            client().preview(data, reply_for(_session, scene.frame_current_final))
            _preview_started = True
        _last = time.monotonic()
    finally:
        _sampling = False


@persistent
def evaluated_preview(scene, depsgraph):
    # Pose drags, Graph Editor edits and NLA mixing can change the evaluated
    # pose without changing frames. Capture the same graph shown in Blender.
    if (_sampling or _busy or _undoing or not _session or scene != _connected_scene or
            not scene.epb_live or not connected_target(scene) or
            time.monotonic()-_last < 1/min(120, max(1, scene.render.fps / scene.render.fps_base))):
        return
    try:
        send_preview(scene, depsgraph)
    except Exception as exc:
        connection_error(exc)


@persistent
def frame_preview(scene, depsgraph=None):
    if (_busy or _sampling or _undoing or not _session or scene != _connected_scene or
            not scene.epb_live or not connected_target(scene)):
        return
    try:
        send_preview(scene, depsgraph)
    except Exception as exc:
        connection_error(exc)


def refresh_preview(context):
    if _session and context.scene == _connected_scene and context.scene.epb_live and not _busy:
        # Do not frame_set here: it discards unkeyed R/G edits. The dependency
        # graph evaluates curves/constraints while retaining the visible pose.
        send_preview(context.scene)


@contextmanager
def edit_transaction():
    global _busy
    previous = _busy
    _busy = True
    try:
        yield
    finally:
        _busy = previous


def connected_target(scene):
    return scene == _connected_scene and (squad.valid(scene) if squad.active else scene.epb_armature == _connected_arm)


def tick():
    global _last, _status, _busy, _display_pending
    try:
        if _undoing:
            return 1/60
        if _display_pending:
            load_post(None)
            _display_pending = False
        if _client:
            _client.poll()
        if (_connecting and not _busy and time.monotonic() >= _connect_retry and
                (not _recovering or bpy.context.scene.session_uid == _connect_scene_uid)):
            attempt_connect()
        scene = bpy.context.scene
        if _session and not connected_target(scene):
            disconnect()
        now = time.monotonic()
        if _session and now-_last > (1/scene_fps(scene) if scene.epb_live and not _busy else 1):
            _last = now
            if scene.epb_live and not _busy:
                send_preview()
            else:
                keep_alive()
        for screen in bpy.data.screens:
            for area in screen.areas:
                if area.type == 'VIEW_3D':
                    area.tag_redraw()
    except Exception as exc:
        connection_error(exc)
    return 1/120


def disconnect():
    global _session, _busy, _baker, _status, _connecting, _connect_generation, _pending_session
    global _recovering, _failure_since
    _connecting = False
    _recovering = False
    _failure_since = None
    _connect_generation += 1
    session = _session or _pending_session
    _pending_session = 0
    _session, _busy, _baker = 0, False, None
    squad.clear()
    _status = '已断开；编辑内容保留，可离线编辑或导出'
    globals()['_connection_status'] = '未连接游戏'
    if session and _client:
        try:
            with _client.lock:
                _client.latest = None
            _client.request('end', {'session': session})
        except Exception:
            # Cleanup must not throw out of the Blender timer and permanently
            # stop all synchronization. The game also has its lease timeout.
            console.exception()


@persistent
def load_pre(_):
    if _file_import: _file_import.cancel(bpy.context)
    disconnect()


@persistent
def undo_pre(_):
    global _undoing, _undo_live
    _undoing = True
    _undo_live = bool(_session and bpy.context.scene.epb_live)


@persistent
def undo_post(_):
    global _undoing, _connected_scene, _connected_arm, _schema, _last, _status
    try:
        if not _session:
            return
        # Undo replaces RNA wrappers even when the same scene/rig survives.
        # session_uid survives undo and renames; names and cached pointers don't.
        scene = next((s for s in bpy.data.scenes if s.session_uid == _connected_scene_uid), None)
        arm = scene.epb_armature if scene else None
        if not arm or (not squad.undo(scene) if squad.active else arm.session_uid != _connected_arm_uid):
            disconnect()
            _status = '撤销已移除联动骨架；恢复骨架后可重新连接'
            return
        if _busy:
            disconnect()
            _status = '导入／导出已因撤销取消；可重新连接，已有动作仍保留'
            return
        _connected_scene, _connected_arm = scene, arm
        _schema = rig.schema(arm)
        scene.epb_live = _undo_live
        _last = 0.
    except Exception as exc:
        connection_error(exc)
    finally:
        _undoing = False


def update_display(scene, context):
    for arm in (squad.members(scene).values() if scene.get('epb_squad') else [scene.epb_armature]):
        rig.apply_display(arm, scene.epb_show_fingers)


@persistent
def load_post(_):
    # Keep existing actions and fix equivalent quaternion signs in old layers.
    for scene in bpy.data.scenes:
        managed = [o for o in scene.objects if o.type == 'ARMATURE' and o.get('epb_managed')]
        if not scene.epb_armature and len(managed) == 1:
            scene.epb_armature = managed[0]
        if not scene.epb_camera and scene.camera and (scene.camera.get('epb_managed_camera') or
                scene.camera.name.startswith('Endfield 镜头')):
            scene.epb_camera = scene.camera
        animation.repair_camera_quaternions(scene.epb_camera)
        if not _session:
            scene.epb_live = False
        for arm in scene.objects:
            if arm.type == 'ARMATURE' and arm.get('epb_managed'):
                rig.apply_display(arm, scene.epb_show_fingers)
    if not bpy.app.timers.is_registered(tick):
        bpy.app.timers.register(tick, persistent=True)


class EPB_OT_connect(bpy.types.Operator):
    bl_idname = 'endfield.connect'
    bl_label = '连接当前游戏角色'
    bl_description = '已有工程保留当前帧和全部修改；新工程读取游戏当前姿态'
    def execute(self, context):
        global _connecting, _connect_generation, _connect_scene_uid, _connect_deadline, _connection_status
        if _session or _connecting:
            return {'CANCELLED'}
        try:
            if context.scene.epb_armature:
                scene_fps(context.scene)
        except ValueError as exc:
            report(self, {'ERROR'}, str(exc))
            return {'CANCELLED'}
        _connect_generation += 1
        _connecting = True
        _connect_scene_uid = context.scene.session_uid
        _connect_deadline = time.monotonic()+12
        _connection_status = '正在连接游戏…'
        attempt_connect()
        return {'FINISHED'}


class EPB_OT_disconnect(bpy.types.Operator):
    bl_idname = 'endfield.disconnect'
    bl_label = '断开并恢复游戏'
    def execute(self, context):
        disconnect()
        return {'FINISHED'}


class EPB_OT_open_vmd(bpy.types.Operator, ImportHelper):
    bl_idname = 'endfield.open_vmd'
    bl_label = '选择 VMD 动作'
    filename_ext = '.vmd'
    filter_glob: StringProperty(default='*.vmd', options={'HIDDEN'})
    camera_only: BoolProperty(name='作为镜头文件', default=False)
    def execute(self, context):
        if _session:
            report(self, {'ERROR'}, '请先断开，再选择新的动作或镜头')
            return {'CANCELLED'}
        def loaded(result):
            global _status
            check(result)
            _status = '游戏正在读取 VMD，稍后连接并导入'
        client().request('load_vmd', {'path': self.filepath, 'camera': self.camera_only}, loaded)
        return {'FINISHED'}


def export_header(data, camera=False):
    if camera:
        return {'format': 'endfield-blender-camera', 'version': 3}
    bones = data.get('game_bones', data['bones'])
    return {'format': 'endfield-blender-motion', 'version': 4 if 'root_rotation' in data else 3, 'model': data['model'],
            'bone_names': [rig.stable_bone_name(data['model'], b['name'], b['parent']) for b in bones],
            'bone_parents': [b['parent'] for b in bones], 'editor_reference': clip_io.editor_reference(data)}


def export_sample(packet, camera=False):
    keys = ('time', 'anchor_rotation', 'camera') if camera else ('time', 'anchor_rotation', 'root', 'root_rotation', 'bones', 'faces')
    if camera and not packet.get('camera'):
        raise ValueError('需要可用的镜头')
    return {key: packet[key] for key in keys if key in packet}


class ExportClip:
    camera_only = False
    _timer = None
    _file = None
    def execute(self, context):
        global _busy
        s = context.scene
        if _busy or (not s.epb_camera if self.camera_only else not s.epb_armature):
            report(self, {'ERROR'}, '需要编辑骨架，且不能与导入同时进行')
            return {'CANCELLED'}
        try:
            self._fps = scene_fps(s)
        except ValueError as exc:
            report(self, {"ERROR"}, str(exc))
            return {"CANCELLED"}
        self._original, self._frame = s.frame_current, s.frame_start
        self._start, self._end = s.frame_start, s.frame_end
        self._data = rig.schema(s.epb_armature) if s.epb_armature else {
            'bones': [], 'faces': [], 'anchor_rotation': list(s.get('epb_camera_anchor', [0, 0, 0, 1]))}
        self._arm = s.epb_armature
        self._camera_cuts = animation.camera_cut_frames(s.epb_camera) if self.camera_only else []
        self._file = tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', delete=False,
            dir=os.path.dirname(os.path.abspath(self.filepath)), prefix='.epmotion-', suffix='.tmp')
        header = export_header(self._data, self.camera_only)
        header['fps'] = self._fps
        self._file.write(json.dumps(header, ensure_ascii=False)[:-1] + ',"frames":[')
        self._first = True
        _busy = True
        self._timer = context.window_manager.event_timer_add(.01, window=context.window)
        context.window_manager.modal_handler_add(self)
        return {'RUNNING_MODAL'}

    def cleanup(self, context, success):
        global _busy, _status
        if self._timer:
            context.window_manager.event_timer_remove(self._timer)
        if self._file:
            name = self._file.name
            if success:
                self._file.write(']}')
            self._file.close()
            if success:
                os.replace(name, self.filepath)
            else:
                os.unlink(name)
        context.scene.frame_set(self._original)
        _busy = False
        _status = '已导出，可在游戏「MMD 播放器 → Blender 联动」中分别加载' if success else '已取消导出'

    def modal(self, context, event):
        global _status
        if event.type == 'ESC':
            self.cleanup(context, False)
            return {'CANCELLED'}
        if event.type != 'TIMER':
            return {'PASS_THROUGH'}
        try:
            s = context.scene
            fps = self._fps
            for _ in range(3):
                s.frame_set(self._frame)
                sample = rig.packet(self._arm, s.epb_camera if self.camera_only else None, s, 0, self._frame, self._data)
                sample = export_sample(sample, self.camera_only)
                if self.camera_only:
                    sample['camera']['cut'] = self._frame > self._start and (
                        bisect_right(self._camera_cuts,self._frame-1) < bisect_right(self._camera_cuts,self._frame+1e-4))
                sample['time'] = (self._frame-self._start)/fps
                if not self._first:
                    self._file.write(',')
                json.dump(sample, self._file, ensure_ascii=False, allow_nan=False, separators=(',', ':'))
                self._first = False
                self._frame += 1
                if self._file.tell() > 256*1024*1024:
                    raise ValueError('编辑动作超过 256 MB，请缩短导出帧段')
                if self._frame > self._end:
                    self.cleanup(context, True)
                    return {'FINISHED'}
            _status = f'导出编辑动作 {self._frame}/{self._end} · Esc 取消'
        except Exception as exc:
            self.cleanup(context, False)
            report(self, {'ERROR'}, str(exc))
            return {'CANCELLED'}
        return {'RUNNING_MODAL'}


class EPB_OT_export(ExportClip, bpy.types.Operator, ExportHelper):
    bl_idname = 'endfield.export_motion'
    bl_label = '导出动作与表情'
    filename_ext = '.epmotion'
    filter_glob: StringProperty(default='*.epmotion', options={'HIDDEN'})


class EPB_OT_export_camera(ExportClip, bpy.types.Operator, ExportHelper):
    bl_idname = 'endfield.export_camera'
    bl_label = '导出镜头'
    camera_only = True
    filename_ext = '.epcamera'
    filter_glob: StringProperty(default='*.epcamera', options={'HIDDEN'})


class ImportClip:
    camera_only = False

    @classmethod
    def poll(cls, context):
        return not (_session or _connecting or _busy)

    def execute(self, context):
        global _busy, _file_import, _status
        if not self.poll(context):
            report(self, {'ERROR'}, '请先断开游戏连接，并等待当前导入／导出结束')
            return {'CANCELLED'}
        self._window = context.window
        self._original_scene = context.scene
        self._original_camera = context.scene.camera
        self._original_bridge_camera = context.scene.epb_camera
        self._original_range = (context.scene.frame_start, context.scene.frame_end, context.scene.epb_import_end)
        self._original_frame = (context.scene.frame_current, context.scene.frame_subframe)
        self._target = None; self._new_scene = False
        self._objects = []; self._baker = None; self._doc = None
        self._fallback = rig.schema(context.scene.epb_armature) if context.scene.epb_armature else None
        self._executor = ThreadPoolExecutor(max_workers=1)
        self._future = self._executor.submit(clip_io.read, self.filepath, self.camera_only)
        self._timer = context.window_manager.event_timer_add(.01, window=context.window)
        _busy = True; _file_import = self
        _status = '正在读取文件；Esc 可取消'
        context.window_manager.modal_handler_add(self)
        return {'RUNNING_MODAL'}

    def prepare(self, context):
        doc = self._doc
        if self.camera_only:
            self._target = self._original_scene
            if not self._target.epb_armature and not self._target.epb_camera:
                self._target = bpy.data.scenes.new('Endfield 镜头编辑'); self._new_scene = True
            data = rig.schema(self._target.epb_armature) if self._target.epb_armature else {
                'bones': [], 'faces': [], 'anchor_rotation': doc['frames'][0].get('anchor_rotation', [0, 0, 0, 1])}
            data = dict(data, camera_cuts=[f['time'] for f in doc['frames'] if f['camera'].get('cut', False)])
            self._arm = None
        else:
            data = clip_io.reference(doc, self._fallback)
            # Fully composed exports become a fresh base; original correction
            # layers remain intact in their original scene, never applied twice.
            self._target = bpy.data.scenes.new('Endfield 导入 · '+doc['model']); self._new_scene = True
        self._window.scene = self._target
        if self._new_scene:
            fps = doc.get('fps', 30.)
            self._target.render.fps = max(1, round(fps))
            self._target.render.fps_base = self._target.render.fps/fps
            self._target.render.resolution_x, self._target.render.resolution_y = 1920, 1080
        fps = scene_fps(self._target)
        self._end = max(2, math.ceil(doc['frames'][-1]['time']*fps)+1)
        if self._end > 1000000: raise ValueError('导入后的时间轴超过支持的长度')
        if not self.camera_only:
            self._arm = rig.create_rig(data, self._target); self._objects.append(self._arm)
            self._target.epb_armature = self._arm
            data = rig.schema(self._arm)
        self._camera = rig.create_camera({}, self._target); self._objects.append(self._camera)
        if self.camera_only and not self._new_scene:
            self._target.camera = self._original_camera
        self._data = data
        self._baker = animation.Baker(self._arm, self._camera if self.camera_only else None, data, fps)
        self._cursor = 0

    def cleanup(self, context, success):
        global _busy, _file_import
        if self._timer:
            context.window_manager.event_timer_remove(self._timer); self._timer = None
        self._future.cancel(); self._executor.shutdown(wait=False, cancel_futures=True)
        if not success:
            if self._baker: self._baker.channels.clear()
            self._window.scene = self._original_scene
            self._original_scene.camera = self._original_camera
            self._original_scene.epb_camera = self._original_bridge_camera
            self._original_scene.frame_start, self._original_scene.frame_end, self._original_scene.epb_import_end = self._original_range
            for obj in reversed(self._objects):
                data = obj.data; bpy.data.objects.remove(obj, do_unlink=True)
                if data.users == 0:
                    if isinstance(data, bpy.types.Armature): bpy.data.armatures.remove(data)
                    elif isinstance(data, bpy.types.Camera): bpy.data.cameras.remove(data)
            if self._new_scene and self._target: bpy.data.scenes.remove(self._target)
            self._original_scene.frame_set(self._original_frame[0], subframe=self._original_frame[1])
        self._doc = None; self._baker = None
        _busy = False; _file_import = None

    def cancel(self, context):
        global _status
        self.cleanup(context, False)
        _status = '已取消文件导入，原工程保留'

    def modal(self, context, event):
        global _status
        if event.type == 'ESC':
            self.cancel(context); return {'CANCELLED'}
        if event.type == 'Z' and event.ctrl: return {'RUNNING_MODAL'}
        if event.type != 'TIMER': return {'PASS_THROUGH'}
        try:
            if self._doc is None:
                if not self._future.done(): return {'RUNNING_MODAL'}
                self._doc = self._future.result(); self.prepare(context)
            if self._window.scene != self._target:
                raise ValueError('场景已切换，已取消本次导入')
            deadline = time.monotonic()+.03
            while self._cursor < len(self._doc['frames']) and time.monotonic() < deadline:
                self._baker.sample(clip_io.sample(self._doc['frames'][self._cursor], self.camera_only))
                self._cursor += 1
            _status = f"正在导入 {'镜头' if self.camera_only else '动作与表情'} {self._cursor}/{len(self._doc['frames'])}"
            if self._cursor < len(self._doc['frames']): return {'RUNNING_MODAL'}
            self._baker.finish()
            scene = self._target
            scene.epb_camera = self._camera; scene.camera = self._camera
            scene['epb_camera_anchor'] = self._data.get('anchor_rotation', [0, 0, 0, 1])
            scene.frame_start = 1
            scene.frame_end = self._end if self._new_scene else max(scene.frame_end, self._end)
            scene.epb_import_end = scene.frame_end
            scene.frame_set(1)
            self.cleanup(context, True)
            report(self, {'INFO'}, '已导入最终结果为新基础层；可继续添加修正层，原工程与旧镜头保留')
            return {'FINISHED'}
        except Exception as exc:
            self.cleanup(context, False)
            report(self, {'ERROR'}, '文件导入失败：'+str(exc))
            return {'CANCELLED'}


class EPB_OT_import_motion(ImportClip, bpy.types.Operator, ImportHelper):
    bl_idname = 'endfield.import_motion_file'
    bl_label = '导入已导出动作与表情'
    filename_ext = '.epmotion'
    filter_glob: StringProperty(default='*.epmotion', options={'HIDDEN'})


class EPB_OT_import_camera(ImportClip, bpy.types.Operator, ImportHelper):
    bl_idname = 'endfield.import_camera_file'
    bl_label = '导入已导出镜头'
    camera_only = True
    filename_ext = '.epcamera'
    filter_glob: StringProperty(default='*.epcamera', options={'HIDDEN'})


class EPB_OT_pull(bpy.types.Operator):
    bl_idname = 'endfield.pull_motion'
    bl_label = '导入动作／表情／镜头'
    bl_description = '按当前角色实际适配结果烘焙；不改动原始 VMD'
    def execute(self, context):
        global _busy, _baker, _status
        scene = context.scene
        if not _session or _busy:
            report(self, {'ERROR'}, '请先连接，并等待当前导入结束')
            return {'CANCELLED'}
        start, end = scene.epb_import_start, scene.epb_import_end
        try:
            fps = scene_fps(scene)
        except ValueError as exc:
            report(self, {'ERROR'}, str(exc))
            return {'CANCELLED'}
        if end < start or end-start > fps * 3600:
            report(self, {'ERROR'}, '帧范围无效（单次最多一小时）')
            return {'CANCELLED'}
        _busy = True
        _baker = squad.Baker(scene, fps) if squad.active else animation.Baker(scene.epb_armature, scene.epb_camera, _schema, fps)
        active_session = _session
        def pull(frame):
            if not _busy or _session != active_session:
                return
            count = min(8, end-frame+1)
            def receive(result):
                global _busy, _status, _baker, _face_untouched
                if not _busy or _session != active_session:
                    return
                if not result.get('ok'):
                    suspend_connection(result.get('error', '动作导入中断'), retry=bool(result.get('retryable')) or
                                       'Session expired or character changed' in result.get('error', ''))
                    _status = '动作导入中断；原动作与修正层保留，重连后可重新导入'
                    return
                for sample in result['frames']:
                    _baker.sample(sample)
                _status = f'导入动作 {min(frame+count-1, end)}/{end}'
                if frame+count <= end:
                    pull(frame+count)
                else:
                    _baker.finish()
                    _face_untouched = False
                    _baker, _busy = None, False
                    scene.frame_start, scene.frame_end = start, end
                    scene.frame_set(start)
                    _status = '原动作已更新；已有动作和镜头修正层保留'
            client().request('sample', {'session': _session, 'start': (frame-1)/fps, 'count': count, 'fps': fps}, receive)
        pull(start)
        return {'FINISHED'}


class EPB_OT_layer(bpy.types.Operator):
    bl_idname = 'endfield.new_layer'
    bl_label = '新建动作修正层'
    bl_options = {'REGISTER', 'UNDO'}
    @classmethod
    def poll(cls, context):
        return not _busy and context.scene.epb_armature is not None

    def execute(self, context):
        if not context.scene.epb_armature:
            return {'CANCELLED'}
        with edit_transaction():
            animation.correction_layer(context.scene.epb_armature, context.scene)
            context.scene.frame_set(context.scene.frame_current, subframe=context.scene.frame_subframe)
        bpy.ops.endfield.show_keys()
        refresh_preview(context)
        return {'FINISHED'}


class EPB_OT_key(bpy.types.Operator):
    bl_idname = 'endfield.key_pose'
    bl_label = '给选中关节打关键帧'
    bl_options = {'REGISTER', 'UNDO'}
    @classmethod
    def poll(cls, context):
        arm = context.scene.epb_armature
        return not _busy and arm is not None and context.object == arm and arm.mode == 'POSE'

    def execute(self, context):
        with edit_transaction():
            count = animation.key_pose(context.scene.epb_armature, context.scene.frame_current, scene=context.scene)
        if not count:
            report(self, {'WARNING'}, '请先选中可见的身体关节')
            return {'CANCELLED'}
        report(self, {'INFO'}, f'已为 {count} 个关节打关键帧')
        refresh_preview(context)
        return {'FINISHED'}


class EPB_OT_restore_pose(bpy.types.Operator):
    bl_idname = 'endfield.restore_pose'
    bl_label = '恢复选中关节状态'
    bl_description = '恢复本帧下层动作的姿态；再打关键帧即可平滑回到原动作'
    bl_options = {'REGISTER', 'UNDO'}
    @classmethod
    def poll(cls, context):
        return EPB_OT_key.poll(context)

    def execute(self, context):
        with edit_transaction():
            count = animation.restore_selected(context.scene.epb_armature, context.scene)
        if not count:
            report(self, {'WARNING'}, '请先选中可见的身体关节')
            return {'CANCELLED'}
        report(self, {'INFO'}, '已恢复本帧原动作；点击打关键帧保存恢复点')
        refresh_preview(context)
        return {'FINISHED'}


class EPB_OT_face_key(bpy.types.Operator):
    bl_idname = 'endfield.face_key'
    bl_label = '表情打关键帧'
    index: IntProperty(default=-1)
    @classmethod
    def poll(cls, context):
        return not _busy and context.scene.epb_armature is not None

    def execute(self, context):
        arm = context.scene.epb_armature
        if arm:
            arm.keyframe_insert(f'["{rig.bone_key(self.index)}"]', frame=context.scene.frame_current, group='表情')
            squad.touch_face(context.scene)
            refresh_preview(context)
        return {'FINISHED'}


class EPB_OT_face_zero(bpy.types.Operator):
    bl_idname = 'endfield.face_zero'
    bl_label = '表情归零'
    bl_options = {'REGISTER', 'UNDO'}
    def execute(self, context):
        global _face_untouched
        _face_untouched = False
        squad.touch_face(context.scene)
        arm = context.scene.epb_armature
        if arm:
            for i, _ in enumerate(rig.schema(arm)['faces']):
                arm[rig.bone_key(i)] = 0.
            refresh_preview(context)
        return {'FINISHED'}


class EPB_OT_camera_key(bpy.types.Operator):
    bl_idname = 'endfield.camera_key'
    bl_label = '镜头打关键帧'
    bl_description = '把当前构图保存到镜头修正层；保留下层原镜头'
    bl_options = {'REGISTER', 'UNDO'}
    @classmethod
    def poll(cls, context):
        return not _busy and context.scene.epb_camera is not None

    def execute(self, context):
        obj = context.scene.epb_camera
        if obj:
            with edit_transaction():
                animation.key_camera(obj, context.scene)
            refresh_preview(context)
        return {'FINISHED'}


class EPB_OT_camera_layer(bpy.types.Operator):
    bl_idname = 'endfield.new_camera_layer'
    bl_label = '新建镜头修正层'
    bl_options = {'REGISTER', 'UNDO'}
    @classmethod
    def poll(cls, context):
        return EPB_OT_camera_key.poll(context)

    def execute(self, context):
        with edit_transaction():
            animation.camera_layer(context.scene.epb_camera, context.scene)
        refresh_preview(context)
        return {'FINISHED'}


class EPB_OT_camera_restore(bpy.types.Operator):
    bl_idname = 'endfield.restore_camera'
    bl_label = '恢复本帧下层镜头'
    bl_description = '恢复位置、朝向、焦距与对焦距离；再打关键帧即可平滑返回原镜头'
    bl_options = {'REGISTER', 'UNDO'}
    @classmethod
    def poll(cls, context):
        return EPB_OT_camera_key.poll(context)

    def execute(self, context):
        with edit_transaction():
            animation.restore_camera(context.scene.epb_camera, context.scene)
        refresh_preview(context)
        return {'FINISHED'}


class EPB_OT_layer_visibility(bpy.types.Operator):
    bl_idname = 'endfield.layer_visibility'
    bl_label = '开关此层'
    bl_options = {'REGISTER', 'UNDO'}
    camera: BoolProperty(default=False)
    index: IntProperty(default=-1)
    def execute(self, context):
        owner = context.scene.epb_camera if self.camera else context.scene.epb_armature
        if not owner or not owner.animation_data:
            return {'CANCELLED'}
        ad = owner.animation_data
        other = owner.data.animation_data if self.camera else None
        with edit_transaction():
            if self.index < 0:
                value = 0. if ad.action_influence else 1.
                ad.action_influence = value
                if other and other.action == ad.action:
                    other.action_influence = value
            elif self.index < len(ad.nla_tracks):
                track = ad.nla_tracks[self.index]
                track.mute = not track.mute
                if other:
                    actions = {strip.action for strip in track.strips}
                    for second in other.nla_tracks:
                        if any(strip.action in actions for strip in second.strips):
                            second.mute = track.mute
            context.view_layer.update()
        refresh_preview(context)
        return {'FINISHED'}


class EPB_OT_show_keys(bpy.types.Operator):
    bl_idname = 'endfield.show_keys'
    bl_label = '查看此层关键帧'
    bl_description = '在动作编辑器显示此层关键帧，保持原动作与修正层一起播放'
    camera: BoolProperty(default=False)
    index: IntProperty(default=-1)
    def execute(self, context):
        area = next((a for a in context.screen.areas if a.type in {'DOPESHEET_EDITOR', 'NLA_EDITOR'}), None)
        obj = context.scene.epb_camera if self.camera else context.scene.epb_armature
        if not area or not obj or not obj.animation_data:
            return {'CANCELLED'}
        bpy.ops.endfield.edit_target(camera=self.camera)
        with edit_transaction():
            animation.leave_tweak(context.scene)
            ad = obj.animation_data
            if self.camera and obj.data.animation_data:
                obj.data.animation_data.use_tweak_mode = False
            if self.index >= 0:
                if self.index >= len(ad.nla_tracks):
                    return {'CANCELLED'}
                for track in ad.nla_tracks:
                    track.select = False
                    for strip in track.strips:
                        strip.select = False
                track = ad.nla_tracks[self.index]
                if not track.strips:
                    return {'CANCELLED'}
                ad.nla_tracks.active = track
                track.select = True
                track.strips[0].select = True
                area.type = 'NLA_EDITOR'
                region = next(r for r in area.regions if r.type == 'WINDOW')
                with context.temp_override(area=area, region=region):
                    bpy.ops.nla.tweakmode_enter(isolate_action=False, use_upper_stack_evaluation=True)
            area.type = 'DOPESHEET_EDITOR'
            space = area.spaces.active
            space.ui_mode = 'ACTION'
            space.dopesheet.show_only_selected = False
            space.dopesheet.show_hidden = True
            region = next(r for r in area.regions if r.type == 'WINDOW')
            with context.temp_override(area=area, region=region):
                bpy.ops.action.view_all()
            context.scene.frame_set(context.scene.frame_current, subframe=context.scene.frame_subframe)
        refresh_preview(context)
        return {'FINISHED'}


class EPB_OT_show_layers(bpy.types.Operator):
    bl_idname = 'endfield.show_layers'
    bl_label = '查看全部层（NLA）'
    bl_description = '在下方非线性动画编辑器查看原动作和修正层，不替换当前动作'
    camera: BoolProperty(default=False)
    def execute(self, context):
        area = next((a for a in context.screen.areas if a.type in {'DOPESHEET_EDITOR', 'NLA_EDITOR'}), None)
        if not area:
            report(self, {'INFO'}, '请将一个区域切换为「非线性动画」编辑器查看全部层')
            return {'CANCELLED'}
        bpy.ops.endfield.edit_target(camera=self.camera)
        area.type = 'NLA_EDITOR'
        region = next((r for r in area.regions if r.type == 'WINDOW'), None)
        if region:
            with context.temp_override(area=area, region=region):
                bpy.ops.nla.view_all()
        return {'FINISHED'}


class EPB_OT_clear_solo(bpy.types.Operator):
    bl_idname = 'endfield.clear_solo'
    bl_label = '恢复全部层叠加'
    bl_options = {'REGISTER', 'UNDO'}
    camera: BoolProperty(default=False)
    def execute(self, context):
        obj = context.scene.epb_camera if self.camera else context.scene.epb_armature
        with edit_transaction():
            for owner in ([obj, obj.data] if self.camera and obj else [obj]):
                if owner and owner.animation_data:
                    owner.animation_data.use_nla = True
                    for track in owner.animation_data.nla_tracks:
                        track.is_solo = False
            context.view_layer.update()
        refresh_preview(context)
        return {'FINISHED'}


class EPB_OT_enable_corrections(bpy.types.Operator):
    bl_idname = 'endfield.enable_corrections'
    bl_label = '重新启用已有修正层'
    bl_description = '恢复旧版导入后关闭的修正层，保留原关键帧'
    bl_options = {'REGISTER', 'UNDO'}
    camera: BoolProperty(default=False)

    def execute(self, context):
        obj = context.scene.epb_camera if self.camera else context.scene.epb_armature
        if _busy or not obj:
            return {'CANCELLED'}
        with edit_transaction():
            animation.leave_tweak(context.scene)
            for owner in ([obj, obj.data] if self.camera else [obj]):
                ad = owner.animation_data
                if not ad:
                    continue
                if animation.is_base(ad.action) and ad.nla_tracks:
                    animation.replace_base(owner, ad.action)
                ad.use_nla = True
                for track in ad.nla_tracks:
                    track.is_solo = False
                    if any(animation.is_any_correction(strip.action) for strip in track.strips):
                        track.mute = False
                if animation.is_any_correction(ad.action):
                    ad.action_influence = 1.
                owner.update_tag()
            context.scene.frame_set(context.scene.frame_current, subframe=context.scene.frame_subframe)
        refresh_preview(context)
        return {'FINISHED'}


def draw_layers(layout, obj, camera=False):
    if not obj or not obj.animation_data:
        return
    ad = obj.animation_data
    if any(t.mute and any(animation.is_any_correction(s.action) for s in t.strips) for t in ad.nla_tracks):
        layout.label(text='有修正层已关闭，可按需重新启用', icon='INFO')
        layout.operator('endfield.enable_corrections').camera = camera
    if not ad.use_nla or any(t.is_solo for t in ad.nla_tracks):
        layout.label(text='下层独显／停用会屏蔽修正效果', icon='ERROR')
        layout.operator('endfield.clear_solo').camera = camera
    if ad.action:
        layout.label(text='当前编辑层（关键帧显示在动作编辑器）')
        row = layout.row(align=True)
        op = row.operator('endfield.layer_visibility', text='', icon='HIDE_OFF' if ad.action_influence else 'HIDE_ON')
        op.camera, op.index = camera, -1
        row.prop(ad.action, 'name', text='')
        if not ad.use_tweak_mode:
            row.operator('endfield.show_keys', text='', icon='KEY_HLT').camera = camera
    if ad.use_tweak_mode:
        layout.label(text='正在查看下层关键帧；原动作仍参与叠加')
        layout.operator('endfield.show_keys', text='返回顶层关键帧').camera = camera
    if ad.nla_tracks:
        layout.label(text='下层动作（保留并参与播放）')
        for index in reversed(range(len(ad.nla_tracks))):
            track = ad.nla_tracks[index]
            row = layout.row(align=True)
            # A camera correction owns both transform and lens slots. Toggle
            # both together; base transform/lens tracks remain separate in NLA.
            if not camera or any(s.action and s.action.get('epb_camera_correction') for s in track.strips):
                op = row.operator('endfield.layer_visibility', text='', icon='HIDE_ON' if track.mute else 'HIDE_OFF')
                op.camera, op.index = camera, index
            else:
                row.label(text='', icon='ACTION')
            row.prop(track, 'name', text='')
            op = row.operator('endfield.show_keys', text='', icon='KEY_HLT')
            op.camera, op.index = camera, index
    layout.operator('endfield.show_layers', icon='NLA').camera = camera


class EPB_OT_edit_target(bpy.types.Operator):
    bl_idname = 'endfield.edit_target'
    bl_label = '切换编辑对象'
    camera: BoolProperty(default=False)
    @classmethod
    def poll(cls, context):
        return not _busy

    def execute(self, context):
        obj = context.scene.epb_camera if self.camera else context.scene.epb_armature
        if not obj:
            return {'CANCELLED'}
        with edit_transaction():
            if context.object and context.object.mode != 'OBJECT':
                bpy.ops.object.mode_set(mode='OBJECT')
            for item in context.selected_objects:
                item.select_set(False)
            obj.select_set(True)
            context.view_layer.objects.active = obj
            if not self.camera:
                bpy.ops.object.mode_set(mode='POSE')
            else:
                # Connecting without a sampled game camera disables this flag.
                # Explicitly choosing camera editing must enable it again.
                context.scene.epb_camera_sync = True
            context.view_layer.update()
        refresh_preview(context)
        return {'FINISHED'}


class EPB_OT_camera_view(bpy.types.Operator):
    bl_idname = 'endfield.camera_view'
    bl_label = '用当前视角设置镜头'
    bl_options = {'REGISTER', 'UNDO'}
    def execute(self, context):
        if not context.scene.epb_camera:
            return {'CANCELLED'}
        context.scene.camera = context.scene.epb_camera
        region = next((r for r in context.area.regions if r.type == 'WINDOW'), None)
        if not region:
            return {'CANCELLED'}
        with context.temp_override(region=region):
            bpy.ops.view3d.camera_to_view()
        return {'FINISHED'}


class EPB_OT_select_member(bpy.types.Operator):
    bl_idname = 'endfield.select_member'
    bl_label = '选择编辑队员'
    slot: IntProperty(default=0, min=0, max=3)
    def execute(self, context):
        if _busy: return {'CANCELLED'}
        arm = squad.members(context.scene).get(self.slot)
        if not arm: return {'CANCELLED'}
        with edit_transaction():
            squad.select(context.scene, self.slot)
        return {'FINISHED'}


class EPB_PT_main(bpy.types.Panel):
    bl_label = 'Endfield · 动作编辑'
    bl_idname = 'EPB_PT_main'
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = 'Endfield'
    def draw(self, context):
        s, l = context.scene, self.layout
        l.label(text=_connection_status[:72], icon='LINKED' if _session else 'UNLINKED')
        l.label(text=_status[:72])
        row = l.row(); row.enabled = not (_session or _connecting or _busy)
        row.prop(s, 'epb_mode', text='连接对象')
        l.operator('endfield.connect' if not (_session or _connecting) else 'endfield.disconnect',
                   text='停止自动重连' if _recovering else '取消连接' if _connecting else '断开并恢复游戏' if _session else '连接当前小队' if s.epb_mode == 'squad' else '连接当前游戏角色')
        if _recovering:
            l.label(text='可继续编辑和导出；返回原角色场景后自动同步')
        if s.epb_mode == 'squad':
            l.label(text='在游戏多人面板按第 1–4 位选择参与成员和动作')
        else: l.operator('endfield.open_vmd', icon='FILE_FOLDER')
        if s.epb_mode == 'squad' and s.get('epb_squad'):
            row = l.column(align=True); row.enabled = not _busy
            for slot, arm in squad.members(s).items():
                op = row.operator('endfield.select_member', text=f'第 {slot+1} 位 · {arm.name}', depress=s.epb_armature == arm)
                op.slot = slot
        else: l.prop(s, 'epb_armature')
        l.prop(s, 'epb_live')
        if _session:
            if _busy:
                l.label(text='导入／导出期间暂停实时预览', icon='TIME')
            elif not s.epb_live:
                l.label(text='实时同步已关闭', icon='PAUSE')
            elif not _preview_started and _connection_packet:
                l.label(text='已保留游戏当前姿态 · 调整后开始同步', icon='CHECKMARK')
            elif _ack_frame is None or time.monotonic()-_ack_time > 3:
                l.label(text='等待游戏接收当前姿态…', icon='TIME')
            else:
                l.label(text=f'实时同步中 · 第 {_ack_frame:g} 帧', icon='CHECKMARK')
        l.operator('endfield.edit_target', text='编辑骨骼', icon='ARMATURE_DATA').camera = False
        l.prop(s, 'epb_show_fingers')
        row = l.row(align=True)
        row.prop(s, 'frame_current', text='当前帧')
        row.operator('screen.animation_play', text='', icon='PLAY')
        if s.epb_armature or s.epb_camera:
            row = l.row(align=True);row.enabled = not _busy
            row.prop(s.render, 'fps', text='场景帧率');row.prop(s.render, 'fps_base', text='基数')
            l.label(text='1–120 FPS；修改后现有关键帧帧号不变')
        else:
            l.label(text='新建联动工程默认 30 FPS，连接后可调整')
        box = l.box();box.label(text='从游戏导入（先在游戏选择 VMD）')
        row = box.row(align=True);row.prop(s, 'epb_import_start');row.prop(s, 'epb_import_end')
        row = box.row();row.enabled = bool(_session) and not _busy
        row.operator('endfield.pull_motion')
        box = l.box();box.label(text='从已导出文件继续编辑')
        row = box.row(align=True);row.enabled = not (_session or _connecting or _busy)
        row.operator('endfield.import_motion_file', text='导入 .epmotion', icon='IMPORT')
        row.operator('endfield.import_camera_file', text='导入 .epcamera', icon='CAMERA_DATA')
        box.label(text='先断开连接；文件是最终结果，不含原修正层')
        row = l.row(align=True)
        row.operator('endfield.export_motion', text='导出当前队员动作与表情' if s.get('epb_squad') else '导出动作与表情', icon='EXPORT')
        row.operator('endfield.export_camera', icon='CAMERA_DATA')
        l.label(text='Ctrl+S 保存工程；时间轴 / 曲线编辑器调整节奏')


class EPB_PT_layers(bpy.types.Panel):
    bl_label = '动作修正层'
    bl_idname = 'EPB_PT_layers'
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = 'Endfield'
    def draw(self, context):
        s, l = context.scene, self.layout
        l.enabled = not _busy
        l.operator('endfield.new_layer', icon='ADD')
        l.label(text='选关节 → R 旋转 / G 移动 → 打关键帧')
        l.operator('endfield.key_pose', icon='KEY_HLT')
        l.operator('endfield.restore_pose', icon='LOOP_BACK')
        l.label(text='另一帧恢复关节 → 再打关键帧，自动平滑过渡')
        arm = s.epb_armature
        draw_layers(l, arm)


class EPB_PT_faces(bpy.types.Panel):
    bl_label = 'MMD 表情'
    bl_idname = 'EPB_PT_faces'
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = 'Endfield'
    bl_options = {'DEFAULT_CLOSED'}
    def draw(self, context):
        s, l = context.scene, self.layout
        arm = s.epb_armature
        if not arm or 'epb_scene' not in arm:
            l.label(text='连接后显示当前角色的中文表情')
            return
        l.prop(s, 'epb_face_search', text='', icon='VIEWZOOM')
        l.operator('endfield.face_zero')
        faces = rig.schema(arm)['faces']
        for panel, label in {1: '眉部', 2: '眼部', 3: '嘴部', 4: '其他'}.items():
            matching = [(i, f) for i, f in enumerate(faces) if f.get('available', True) and f['panel'] == panel and s.epb_face_search in f['label']]
            if not matching:
                continue
            l.label(text=label)
            for i, f in matching:
                row = l.row(align=True)
                row.prop(arm, f'["{rig.bone_key(i)}"]', text=f['label'], slider=True)
                row.operator('endfield.face_key', text='', icon='KEY_HLT').index = i


class EPB_PT_camera(bpy.types.Panel):
    bl_label = '镜头'
    bl_idname = 'EPB_PT_camera'
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = 'Endfield'
    def draw(self, context):
        s, l = context.scene, self.layout
        l.enabled = not _busy
        l.prop(s, 'epb_camera');l.prop(s, 'epb_camera_sync')
        obj = s.epb_camera
        if obj:
            l.operator('endfield.edit_target', text='编辑镜头', icon='CAMERA_DATA').camera = True
            l.operator('endfield.camera_view')
            l.prop(obj, 'location', text='位置')
            l.prop(obj.data, 'lens', text='焦距（mm）')
            l.prop(obj.data.dof, 'focus_distance', text='对焦距离')
            l.operator('endfield.new_camera_layer', icon='ADD')
            l.operator('endfield.camera_key')
            l.operator('endfield.restore_camera')
            l.label(text='另一帧恢复 → 再打关键帧，平滑返回下层镜头')
            draw_layers(l, obj, camera=True)
            l.label(text='选中镜头：G / R 移动旋转；小键盘 0 取景')


classes = (EPB_OT_connect, EPB_OT_disconnect, EPB_OT_open_vmd, EPB_OT_export, EPB_OT_export_camera, EPB_OT_import_motion, EPB_OT_import_camera, EPB_OT_pull, EPB_OT_layer, EPB_OT_key, EPB_OT_select_member,
           EPB_OT_restore_pose, EPB_OT_face_key, EPB_OT_face_zero, EPB_OT_camera_key, EPB_OT_edit_target, EPB_OT_camera_view,
           EPB_OT_camera_layer, EPB_OT_camera_restore, EPB_OT_layer_visibility, EPB_OT_show_layers, EPB_OT_show_keys, EPB_OT_clear_solo, EPB_OT_enable_corrections,
           EPB_PT_main, EPB_PT_layers, EPB_PT_faces, EPB_PT_camera)


def register():
    global _display_pending
    console.configure()
    for cls in classes:
        bpy.utils.register_class(cls)
    props = {
        'epb_mode': EnumProperty(name='连接对象', items=[('single', '当前角色', ''), ('squad', '小队（最多 4 人）', '')], default='single'),
        'epb_armature': PointerProperty(type=bpy.types.Object, name='编辑骨架', poll=lambda _, obj: obj.type == 'ARMATURE'),
        'epb_camera': PointerProperty(type=bpy.types.Object, name='预览镜头', poll=lambda _, obj: obj.type == 'CAMERA'),
        'epb_live': BoolProperty(name='实时同步到游戏', default=False),
        'epb_show_fingers': BoolProperty(name='显示手指', default=True, update=update_display),
        'epb_camera_sync': BoolProperty(name='同步镜头', default=True),
        'epb_import_start': IntProperty(name='开始帧', default=1, min=1),
        'epb_import_end': IntProperty(name='结束帧', default=300, min=1),
        'epb_face_search': StringProperty(name='搜索表情'),
    }
    for slot in range(4): props[f'epb_member_{slot}'] = PointerProperty(type=bpy.types.Object, name=f'第 {slot+1} 位骨架', poll=lambda _, obj: obj.type == 'ARMATURE')
    for name, prop in props.items():
        setattr(bpy.types.Scene, name, prop)
    bpy.app.timers.register(tick, persistent=True)
    bpy.app.handlers.load_pre.append(load_pre)
    bpy.app.handlers.load_post.append(load_post)
    bpy.app.handlers.depsgraph_update_post.append(evaluated_preview)
    bpy.app.handlers.frame_change_post.append(frame_preview)
    bpy.app.handlers.undo_pre.append(undo_pre)
    bpy.app.handlers.undo_post.append(undo_post)
    bpy.app.handlers.redo_pre.append(undo_pre)
    bpy.app.handlers.redo_post.append(undo_post)
    # Blender restricts bpy.data during add-on registration. Migrate on the
    # first UI tick (or load_post), after access to existing scenes is allowed.
    _display_pending = True


def unregister():
    global _client
    if _file_import: _file_import.cancel(bpy.context)
    disconnect()
    if bpy.app.timers.is_registered(tick):
        bpy.app.timers.unregister(tick)
    if load_pre in bpy.app.handlers.load_pre:
        bpy.app.handlers.load_pre.remove(load_pre)
    if load_post in bpy.app.handlers.load_post:
        bpy.app.handlers.load_post.remove(load_post)
    if frame_preview in bpy.app.handlers.frame_change_post:
        bpy.app.handlers.frame_change_post.remove(frame_preview)
    if evaluated_preview in bpy.app.handlers.depsgraph_update_post:
        bpy.app.handlers.depsgraph_update_post.remove(evaluated_preview)
    for handlers, handler in ((bpy.app.handlers.undo_pre, undo_pre), (bpy.app.handlers.undo_post, undo_post),
                              (bpy.app.handlers.redo_pre, undo_pre), (bpy.app.handlers.redo_post, undo_post)):
        if handler in handlers:
            handlers.remove(handler)
    if _client:
        _client.close();_client = None
    for name in list(bpy.types.Scene.__dict__):
        if name.startswith('epb_'):
            delattr(bpy.types.Scene, name)
    for cls in reversed(classes):
        bpy.utils.unregister_class(cls)
    console.restore()
