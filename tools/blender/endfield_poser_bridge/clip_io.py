"""Portable playback-file IO. Reading/validation never accesses Blender objects."""
import copy
import json
import math
from pathlib import Path
from . import rig


def number(value, low, high):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or not low <= value <= high:
        raise ValueError('文件中的数值超出支持范围')
    return value


def vector(value, size=3, low=-10000, high=10000):
    if not isinstance(value, list) or len(value) != size:
        raise ValueError('文件中的坐标格式无效')
    for v in value: number(v, low, high)
    return value


def rotation(value):
    vector(value, 4, -1.01, 1.01)
    if not .5 <= sum(v*v for v in value) <= 1.5:
        raise ValueError('文件中的旋转无效')


def paths(model, names, parents):
    if not isinstance(names, list) or not 1 <= len(names) <= 4096 or len(names) != len(parents):
        raise ValueError('骨架数量或父级列表无效')
    result = []
    for i, (name, parent) in enumerate(zip(names, parents)):
        if not isinstance(name, str) or not name or len(name) > 256 or type(parent) is not int or not -1 <= parent < i:
            raise ValueError('骨架名称或父级无效')
        name = rig.stable_bone_name(model, name, parent)
        result.append((result[parent] if parent >= 0 else ())+(name,))
    return result


def read(filename, camera=False):
    with Path(filename).open('rb') as stream:
        content = stream.read(256*1024*1024+1)
    if len(content) > 256*1024*1024: raise ValueError('文件超过 256 MB')
    doc = json.loads(content.decode('utf-8-sig'))
    expected = 'endfield-blender-camera' if camera else 'endfield-blender-motion'
    if doc.get('format') != expected or doc.get('version') not in ((2, 3) if camera else (1, 2, 3, 4)):
        raise ValueError('请选择对应的 .epcamera 镜头或 .epmotion 动作文件')
    if not camera:
        if not isinstance(doc.get('model'), str) or not doc['model'] or len(doc['model']) > 256:
            raise ValueError('缺少角色标识')
        paths(doc['model'], doc['bone_names'], doc['bone_parents'])
    if 'fps' in doc: number(doc['fps'], 1, 120)
    frames = doc['frames']
    if not isinstance(frames, list) or not 1 <= len(frames) <= 108001:
        raise ValueError('文件帧数无效')
    last, tracks, face_names, root_rotation, total = -1., None, None, None, 0
    for frame in frames:
        t = number(frame['time'], 0, 86400)
        if t <= last or (last < 0 and t != 0): raise ValueError('时间轴须从零开始且严格递增')
        last = t
        if 'anchor_rotation' in frame: rotation(frame['anchor_rotation'])
        if camera:
            c = frame['camera']
            vector(c['p']); vector(c['target']); rotation(c['q'])
            number(c['fov'], 1, 179); number(c.get('size', 5), .001, 10000)
        else:
            vector(frame['root'])
            has_rotation = 'root_rotation' in frame
            if has_rotation: rotation(frame['root_rotation'])
            if root_rotation is not None and root_rotation != has_rotation: raise ValueError('根节点旋转轨道不一致')
            root_rotation = has_rotation
            bones = frame['bones']
            if not isinstance(bones, list) or len(bones) > len(doc['bone_names']): raise ValueError('动画骨骼数量无效')
            current, seen = [], set()
            for b in bones:
                i = b['i']
                if type(i) is not int or not 0 <= i < len(doc['bone_names']) or i in seen:
                    raise ValueError('动画骨骼索引无效或重复')
                seen.add(i); current.append((i, 's' in b))
                vector(b['p'], low=-100, high=100); rotation(b['q'])
                if 's' in b: vector(b['s'], low=.0001, high=1000)
            faces = frame.get('faces', {})
            if not isinstance(faces, dict) or len(faces) > 1024: raise ValueError('表情数量无效')
            for name, weight in faces.items():
                if not isinstance(name, str) or len(name) > 192: raise ValueError('表情名称无效')
                number(weight, 0, 1)
            if tracks is not None and (current != tracks or set(faces) != face_names):
                raise ValueError('文件中的骨骼或表情轨道不一致')
            tracks, face_names = current, set(faces)
            total += len(bones)+len(faces)
            if total > 12000000: raise ValueError('动作数据量超过限制')
    return doc


def editor_reference(data):
    """Keep just the native reference rig, never Blender names or local paths."""
    bones = []
    for b in data['bones']:
        item = {key: copy.deepcopy(b[key]) for key in ('i', 'parent', 'role', 'editable', 'p', 'q', 'scale', 'world')}
        item['name'] = rig.stable_bone_name(data['model'], b['name'], b['parent'])
        if 'game_index' in b: item['game_index'] = b['game_index']
        bones.append(item)
    result = {'model': data['model'], 'bones': bones, 'faces': copy.deepcopy(data['faces']),
              'anchor_rotation': data.get('anchor_rotation', [0, 0, 0, 1]), 'bone_scale': True}
    if 'root_rotation' in data: result['root_rotation'] = data['root_rotation']
    return result


def reference(doc, fallback=None):
    data = copy.deepcopy(doc.get('editor_reference') or fallback)
    if not data or data.get('model') != doc['model']:
        raise ValueError('旧动作未保存参考骨架：请先打开该角色的 .blend 工程，或连接对应游戏角色后断开，再导入')
    bones = data['bones']
    source = paths(doc['model'], doc['bone_names'], doc['bone_parents'])
    target = paths(data['model'], [b['name'] for b in bones], [b['parent'] for b in bones])
    if len(set(source)) != len(source) or len(set(target)) != len(target):
        raise ValueError('骨架中存在同路径的重复骨骼')
    lookup = {p: i for i, p in enumerate(source)}
    required = {b['i'] for b in doc['frames'][0]['bones']}
    mapped = set()
    for i, (b, path) in enumerate(zip(bones, target)):
        if b['i'] != i: raise ValueError('参考骨架索引不连续')
        vector(b['p']); rotation(b['q']); vector(b['scale'], low=.0001, high=1000); vector(b['world'], 16)
        b['game_index'] = lookup.get(path, -1)
        b['editable'] = b['game_index'] in required
        if b['editable']: mapped.add(b['game_index'])
        b.pop('blender_name', None); b.pop('compensation', None)
    if mapped != required: raise ValueError('此角色的骨架与动作不一致，不能导入')
    catalog = {f['name']: f for f in data.get('faces', [])}
    data['faces'] = [dict(catalog.get(name, {'name': name, 'label': name, 'panel': 4}), available=True)
                     for name in doc['frames'][0].get('faces', {})]
    data['game_bones'] = [{'name': n, 'parent': p} for n, p in zip(doc['bone_names'], doc['bone_parents'])]
    data['duration'] = doc['frames'][-1]['time']
    data['session'] = 0; data['camera_cuts'] = []
    data['bone_scale'] = True
    if 'root_rotation' in doc['frames'][0] and 'root_rotation' not in data:
        data['root_rotation'] = doc['frames'][0]['root_rotation']
    return data


def sample(frame, camera=False):
    if camera:
        return dict(frame, root=[0, 0, 0], bones=[], faces={})
    # Split motion imports never replace the scene camera or visibility.
    return {k: v for k, v in dict(frame, faces=frame.get('faces', {})).items() if k not in ('camera', 'visible')}
