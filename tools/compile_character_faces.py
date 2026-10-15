"""Compile local PMX facial shapes into per-character bone calibration files.

Requires Python 3, numpy and scipy. No game access; no model meshes/textures are
copied into profiles. Input models and diagnostic reports stay local; reusable
calibration profiles can be maintained under resources/character-faces.
"""
import argparse
import copy
import hashlib
import json
import math
import os
import re
import struct
from pathlib import Path

# Small solves are faster and more predictable without a large BLAS thread pool.
os.environ.setdefault('OPENBLAS_NUM_THREADS', '1')
os.environ.setdefault('OMP_NUM_THREADS', '1')
import numpy as np
from scipy.linalg import lstsq
from scipy.optimize import least_squares
from scipy.spatial import cKDTree
from scipy.spatial.transform import Rotation


class Reader:
    def __init__(self, data):
        self.data, self.offset = data, 0

    def raw(self, n):
        if n < 0 or n > len(self.data) - self.offset:
            raise ValueError('Truncated PMX')
        p = self.offset
        self.offset += n
        return self.data[p:self.offset]

    def get(self, fmt):
        values = struct.unpack('<' + fmt, self.raw(struct.calcsize('<' + fmt)))
        if any(isinstance(v, float) and not math.isfinite(v) for v in values):
            raise ValueError('Non-finite PMX number')
        return values[0] if len(values) == 1 else values

    def count(self, limit=2000000, stride=1):
        n = self.get('i')
        if n < 0 or n > limit or n * stride > len(self.data) - self.offset:
            raise ValueError('Invalid PMX count')
        return n

    def text(self, encoding):
        return self.raw(self.count(1048576)).decode(encoding, errors='strict')

    def index(self, width, unsigned=False):
        return self.get({1: 'B' if unsigned else 'b', 2: 'H' if unsigned else 'h', 4: 'I' if unsigned else 'i'}[width])


def read_pmx(path):
    if path.stat().st_size > 256 * 1024 * 1024:
        raise ValueError('PMX exceeds 256 MiB')
    data = path.read_bytes()
    r = Reader(data)
    magic=r.raw(4);version=r.get('f')
    if magic != b'PMX ' or min(abs(version-2.0),abs(version-2.1))>.001:
        raise ValueError('Expected PMX 2.0/2.1')
    header = r.raw(r.get('B'))
    if len(header) < 8 or header[0] > 1 or header[1] > 4 or any(n not in (1, 2, 4) for n in header[2:8]):
        raise ValueError('Invalid PMX header')
    enc = 'utf-8' if header[0] else 'utf-16-le'
    uv, vi, ti, mi, bi, morphi, rigid = header[1:8]
    name, english, _, _ = (r.text(enc) for _ in range(4))
    n = r.count(stride=33)
    vertices = np.empty((n, 3)); indices = np.full((n, 4), -1, dtype=np.int32); weights = np.zeros((n, 4))
    for i in range(n):
        vertices[i] = r.get('3f'); r.raw(20 + 16 * uv)
        skin = r.get('B')
        if skin == 0:
            indices[i, 0] = r.index(bi); weights[i, 0] = 1
        elif skin in (1, 3):
            indices[i, :2] = [r.index(bi), r.index(bi)]
            w = r.get('f'); weights[i, :2] = [w, 1-w]
            if skin == 3: r.raw(36)
        elif skin in (2, 4):
            indices[i] = [r.index(bi) for _ in range(4)]; weights[i] = r.get('4f')
        else:
            raise ValueError('Unknown PMX skinning mode')
        r.get('f')
    face_count = r.count(20000000, vi)
    if face_count % 3: raise ValueError('Invalid PMX triangle count')
    faces = np.frombuffer(r.raw(face_count * vi), dtype={1:'u1',2:'<u2',4:'<u4'}[vi]).reshape(-1,3)
    if np.any(faces >= len(vertices)): raise ValueError('Invalid PMX triangle vertex')
    textures = [r.text(enc) for _ in range(r.count(100000, 4))]
    materials=[];face_start=0
    for _ in range(r.count(100000, 8)):
        material_name=r.text(enc); r.text(enc); r.raw(65 + 2*ti + 1)
        shared = r.get('B')
        if shared not in (0, 1): raise ValueError('Invalid toon flag')
        r.raw(1 if shared else ti); r.text(enc); material_count=r.get('i')
        if material_count<0 or material_count%3 or face_start+material_count//3>len(faces):
            raise ValueError('Invalid PMX material face count')
        materials.append({'name':material_name,'start':face_start,'count':material_count//3})
        face_start+=material_count//3
    if face_start!=len(faces):raise ValueError('Incomplete PMX material faces')
    bones = []
    for _ in range(r.count(8192, 8)):
        b = {'name': r.text(enc), 'english': r.text(enc), 'rest': r.get('3f'), 'parent': r.index(bi)}
        b['layer']=r.get('i'); flags = b['flags'] = r.get('H')
        b['tail'] = r.index(bi) if flags & 1 else r.get('3f')
        if flags & 0x300: b['append']=(r.index(bi),r.get('f'))
        if flags & 0x400: b['fixed_axis']=r.get('3f')
        # Some references contain unused NaNs in optional axes. Preserve this
        # metadata for diagnostics without rejecting otherwise valid face data.
        if flags & 0x800: b['local_axes']=struct.unpack('<6f',r.raw(24))
        if flags & 0x2000: r.raw(4)
        if flags & 0x20:
            b['ik']={'target':r.index(bi),'iterations':r.get('i'),'angle':r.get('f'),'links':[]}
            for _ in range(r.count(256, bi+1)):
                link={'bone':r.index(bi)}
                if r.get('B'): link['limits']=(r.get('3f'),r.get('3f'))
                b['ik']['links'].append(link)
        bones.append(b)
    if np.any(indices >= len(bones)) or np.any(weights < -1e-5) or np.any(weights > 1.00001):
        raise ValueError('Invalid PMX skinning weights')
    totals=weights.sum(1)
    if np.any(totals<.5) or np.any(totals>1.5): raise ValueError('Invalid PMX weight total')
    weight_fixes=int(np.sum(np.abs(totals-1)>.02))
    weights/=totals[:,None]
    for b in bones:
        if b['parent'] < -1 or b['parent'] >= len(bones): raise ValueError('Invalid PMX parent')
    morphs = []
    for _ in range(r.count(100000, 14)):
        m = {'name': r.text(enc), 'english': r.text(enc), 'category': r.get('B'), 'type': r.get('B')}
        kind = m['type']; strides = {0:morphi+4,1:vi+12,2:bi+28,3:vi+16,4:vi+16,5:vi+16,6:vi+16,7:vi+16,8:mi+113,9:morphi+4,10:rigid+25}
        if kind not in strides: raise ValueError('Invalid morph kind')
        n = r.count(20000000, strides[kind])
        if kind == 1:
            ids = np.empty(n, dtype=np.int32); offsets = np.empty((n,3))
            for i in range(n): ids[i]=r.index(vi,True); offsets[i]=r.get('3f')
            if np.any(ids >= len(vertices)): raise ValueError('Invalid morph vertex')
            m['ids'], m['offsets'] = ids, offsets
        elif kind == 0:
            m['groups'] = [(r.index(morphi),r.get('f')) for _ in range(n)]
        else: r.raw(n*strides[kind])
        morphs.append(m)
    return {'name':name,'english':english,'vertices':vertices,'indices':indices,'weights':weights,
            'bones':bones,'morphs':morphs,'textures':textures,'faces':faces,'materials':materials,
            'weight_fixes':weight_fixes,'hash':hashlib.sha256(data).hexdigest()}


def normalize(name):
    return name.translate(str.maketrans('０１２３４５６７８９ＩＫ','0123456789IK'))


def game_bone(name):
    m = re.fullmatch('([左右])眉毛([1-5])',name)
    if m: return 'brow'+('Lf' if m[1]=='左' else 'Rt')+f'{int(m[2]):02}Joint'
    m = re.fullmatch('([左右])二重([1-3])',name)
    if m: return 'browLine'+('Lf' if m[1]=='左' else 'Rt')+f'{4-int(m[2]):02}Joint'
    m = re.fullmatch('([左右])嘴([上下])([1-4])',name)
    if m: return 'lip'+('L' if m[1]=='左' else 'R')+('up' if m[2]=='上' else 'dn')+str(5-int(m[3]))+'Joint'
    m = re.fullmatch(r'([左右])眼(\d+)',name)
    if m:
        lut={13:'01Joint',2:'02Joint',1:'01EyelashJoint',3:'02EyelashJoint',4:'03Joint',5:'03EyelashJoint',6:'04Joint',7:'04EyelashJoint',8:'05Joint',9:'05EyelashJoint',10:'06Joint',11:'07Joint',12:'08Joint'}
        return 'eye'+('Lf' if m[1]=='左' else 'Rt')+lut.get(int(m[2]),'?')
    return {'嘴上中':'lipMupJoint','嘴下中':'lipMdnJoint','牙下':'faceMdToothDnJoint','牙上':'faceMdToothUpJoint',
            '舌头后':'TongueMd01Joint','舌头前':'TongueMd02Joint','左目':'eyeLfJoint','右目':'eyeRtJoint',
            '瞳左':'faceLfIrisJoint','瞳右':'faceRtIrisJoint'}.get(name,name)


def region(name):
    n=name.lower()
    if n.startswith(('browline','eye')): return 'eyes'
    if n.startswith(('facelfiris','facertiris','facelfpupil','facertpupil','facelfhighlight','facerthighlight')): return 'eyes'
    if n.startswith('brow'): return 'brows'
    if n.startswith(('lip','tongue','facemdtooth')) or n in ('jawjoint','facemdjawdnjoint','line_toothjoint','linejoint'): return 'mouth'
    if n.startswith(('facelfcheek','facertcheek')): return 'cheeks'
    return None


def prepare_mouth_line_retraction(model):
    """Separate authored mouth-line hiding from the visible lip shape.

    Some vertex morphs hide a lightly weighted line overlay far inside the
    head. Fitting that displacement as lip motion distorts the entire mouth.
    Only recognize a detached line control with consistently inward hiding;
    use neighboring lip motion plus bounded line retraction instead. This is
    an offline visibility approximation, not an exact vertex reconstruction.
    """
    if model.get('mouth_line_retraction'):
        return model, model['mouth_line_retraction']
    report={'method':'bounded_mouth_line_v1','shapes':[]}
    names=[game_bone(b['name']).lower() for b in model['bones']]
    if names.count('linejoint')!=1:return model,report
    line=names.index('linejoint');indices=model['indices'];weights=model['weights']
    lw=np.sum(np.where(indices==line,weights,0),axis=1);mask=lw>1e-7
    lips=[i for i,n in enumerate(names) if n.startswith('lip')]
    if np.count_nonzero(mask)<3 or not lips:return model,report
    # This case is a lightly weighted mouth overlay, never skin or teeth.
    if np.max(lw)>.05 or np.min(lw[mask])<.005:return model,report
    W=np.column_stack([np.sum(np.where(indices==i,weights,0),axis=1) for i in lips])
    if np.any(W[mask].sum(1)<.9):return model,report
    vertices=model['vertices'];span=float(np.ptp(vertices[mask,0]))
    if span<1e-5:return model,report
    candidates=np.flatnonzero((W.sum(1)>.5)&~mask)
    if len(candidates)<3:return model,report
    points=np.c_[W*span,vertices]
    _,nearest=cKDTree(points[candidates]).query(points[mask])
    nearest=candidates[nearest]
    if np.max(np.linalg.norm(vertices[nearest]-vertices[mask],axis=1))>span*.5:
        return model,report
    # PMX faces look toward -Z. A few millimetres of inset is enough to hide
    # the overlay; never emit the source's metre-scale control translation.
    control_depth=min(4.,span*.2/float(np.median(lw[mask])))
    result=copy.deepcopy(model)
    for morph in result['morphs']:
        if morph['type']!=1 or morph.get('category')!=3:continue
        full=np.zeros_like(vertices);np.add.at(full,morph['ids'],morph['offsets'])
        surplus=full[mask]-full[nearest]
        if np.min(surplus[:,2])<span or np.max(np.ptp(surplus[:,1:],axis=0))>span*.5:
            continue
        full[mask]=full[nearest]+lw[mask,None]*np.array([0.,0.,control_depth])
        morph['ids']=np.union1d(morph['ids'],np.flatnonzero(mask))
        morph['offsets']=full[morph['ids']]
        report['shapes'].append({'name':normalize(morph['name']),
                                'vertices':int(mask.sum()),'control_depth':control_depth})
    if not report['shapes']:return model,report
    result['mouth_line_retraction']=report
    return result,report


def recover_teeth_weights(model, selected):
    """Recover detached tooth surfaces only with unambiguous geometric evidence.

    Some references attach the entire mouth interior to the head even though
    matching game tooth controls remain in the skeleton. A zero skinning column
    cannot fit their motion. Use isolated oral surfaces near the tooth anchors;
    never guess weights for the outer face or alter already weighted teeth.
    These weights are only used by the offline fit, never applied to the model.
    """
    vertices=model['vertices'];indices=model['indices'].copy();weights=model['weights'].copy()
    teeth=[i for i in selected if game_bone(model['bones'][i]['name']).lower() in
           ('facemdtoothupjoint','facemdtoothdnjoint','line_toothjoint')]
    missing=[i for i in teeth if not np.any((indices==i)&(weights>1e-7))]
    report={'recovered':[],'unresolved':[game_bone(model['bones'][i]['name']) for i in missing]}
    if not missing or 'faces' not in model:return indices,weights,report
    oral=[]
    for mat in model.get('materials',[]):
        if mat['name'].lower() in ('口内','口腔','mouth','mouth_inside','mouthinside','oral'):
            oral.extend(model['faces'][mat['start']:mat['start']+mat['count']].tolist())
    if not oral:return indices,weights,report
    ids=sorted({int(i) for face in oral for i in face});parent={i:i for i in ids}
    def find(i):
        while parent[i]!=i:parent[i]=parent[parent[i]];i=parent[i]
        return i
    def union(a,b):parent[find(a)]=find(b)
    for a,b,d in oral:union(a,b);union(a,d)
    # Split normals / UV seams may duplicate a single rigid tooth surface.
    anchors=np.array([model['bones'][i]['rest'] for i in teeth])
    span=float(np.max(np.linalg.norm(anchors[:,None]-anchors,axis=2)))
    if span<1e-5:return indices,weights,report
    epsilon=span*1e-5;coincident={}
    for i in ids:
        key=tuple(np.rint(vertices[i]/epsilon).astype(np.int64))
        if key in coincident:union(i,coincident[key])
        else:coincident[key]=i
    groups={}
    for i in ids:groups.setdefault(find(i),[]).append(i)
    components=[]
    for group in groups.values():
        group=np.array(group,dtype=int)
        if len(group)<6:continue
        # Only head-attached components qualify. Do not steal another facial
        # control's surface or combine existing teeth with an inferred surface.
        active=set(indices[group][weights[group]>1e-7].tolist())
        if not active or any(i<0 or model['bones'][i]['name'].lower() not in
            ('頭','head','face_head','bip001_head') for i in active):continue
        points=vertices[group];radius=float(np.linalg.norm(np.ptp(points,axis=0)))
        if radius<=1e-6:continue
        components.append((group,points.mean(0),radius))
    proposed={}
    for i in missing:
        anchor=np.array(model['bones'][i]['rest'])
        ranked=sorted((float(np.linalg.norm(center-anchor)),j) for j,(_,center,_) in enumerate(components))
        if not ranked:continue
        distance,j=ranked[0];group,center,radius=components[j]
        # Both a size-relative bound and a nearest-candidate margin are needed.
        if distance>min(radius*.15,span*.3) or (len(ranked)>1 and ranked[1][0]<max(distance*1.5,span*.025)):continue
        proposed[i]=j
    for i,j in proposed.items():
        if list(proposed.values()).count(j)!=1:continue
        group=components[j][0];indices[group]=-1;weights[group]=0
        indices[group,0]=i;weights[group,0]=1
        name=game_bone(model['bones'][i]['name'])
        report['recovered'].append({'bone':name,'vertices':len(group)})
        report['unresolved'].remove(name)
    return indices,weights,report


def refit_mouth(model, profile, *, vertex_importance=None, fit_jaw=False):
    """Refit lip translations with neutral rotations, preserving other controls.

    The old positional fit can rotate tiny lip controls to reduce vertex error
    while changing their skinned surface normals. Keep the lip orientation and
    solve the positions again; simply dropping rotations changes the mouth.
    Teeth, tongue, other regions and unsupported shapes remain untouched.
    Offline references may prioritize the lip contour and include the lower
    jaw in the solve. The default keeps the jaw and uses uniform importance.
    This also updates existing profiles without losing gaze or manual fixes.
    """
    model,_=prepare_mouth_line_retraction(model)
    if profile.get('source_hash') != model['hash']:
        raise ValueError('Mouth refit requires the exact source PMX')
    skin_reference=model.get('mouth_skin_reference')
    if profile.get('mouth_skin_reference') and profile['mouth_skin_reference']!=skin_reference:
        raise ValueError('Mouth refit requires the matching offline skin reference')
    importance=np.ones(len(model['vertices'])) if vertex_importance is None else np.asarray(vertex_importance,dtype=float)
    if importance.shape!=(len(model['vertices']),) or not np.isfinite(importance).all() or np.any(importance<=0) or np.any(importance>10000):
        raise ValueError('Invalid mouth vertex importance')
    result=copy.deepcopy(profile)
    targets={}
    for i,b in enumerate(model['bones']):
        name=game_bone(b['name']).lower()
        if name not in targets or name==b['name'].lower():targets[name]=i
    names=[b['name'].lower() for b in profile['bones']]
    if len(set(names))!=len(names) or any(n not in targets for n in names):
        raise ValueError('Mouth refit has missing or duplicate source bones')
    selected=[targets[n] for n in names]
    positions=np.array([b['rest'] for b in profile['bones']],dtype=float)
    expected=np.array([model['bones'][i]['rest'] for i in selected],dtype=float)
    if not np.allclose(positions,expected,atol=1e-6,rtol=0):
        raise ValueError('Mouth refit has mismatched reference positions')
    lips=[i for i,n in enumerate(names) if n.startswith('lip') or (fit_jaw and n=='facemdjawdnjoint')]
    if not lips:return result,{'method':'lip_translation_v1','shapes':[]}
    indices,weights,_=recover_teeth_weights(model,selected)
    # Limit the solve to vertices actually influenced by face controls. The
    # unmodeled surface still counts in the final full-expression error.
    covered=np.any(np.isin(indices,selected)&(weights>1e-7),axis=1)
    ids=np.flatnonzero(covered);vertices=model['vertices'][ids]
    W=np.column_stack([np.sum(np.where(indices[ids]==i,weights[ids],0),axis=1) for i in selected])
    lip_mask=np.any(W[:,lips]>1e-7,axis=1)
    if np.count_nonzero(lip_mask)<3:raise ValueError('Insufficient weighted lip vertices')
    mouth=[i for i,n in enumerate(names) if region(n)=='mouth']
    mouth_mask=np.any(W[:,mouth]>1e-7,axis=1)
    mouth_points=positions[mouth]
    span=float(np.max(np.linalg.norm(mouth_points[:,None]-mouth_points,axis=2)))
    # A small zero-centered regularizer keeps shared/zero-weight controls
    # bounded and makes repeated offline refits independent of previous output.
    row_scale=np.sqrt(importance[ids][lip_mask])
    reg=.02;A=np.vstack([W[lip_mask][:,lips]*row_scale[:,None],np.eye(len(lips))*reg])
    source_morphs={normalize(m['name']):i for i,m in enumerate(model['morphs'])}
    if len(source_morphs)!=len(model['morphs']):raise ValueError('Ambiguous source morph names')
    def vertex_delta(index,stack=()):
        if index<0 or index>=len(model['morphs']) or index in stack or len(stack)>32:
            raise ValueError('Invalid/cyclic group morph')
        m=model['morphs'][index];delta=np.zeros_like(model['vertices'])
        if m['type']==1:np.add.at(delta,m['ids'],m['offsets'])
        elif m['type']==0:
            for child,weight in m['groups']:delta+=vertex_delta(child,stack+(index,))*weight
        else:raise ValueError('Mouth refit requires vertex or group morphs')
        return delta
    reports=[]
    for old,out in zip(profile['morphs'],result['morphs']):
        if old.get('panel')!=3 or not old['supported']:continue
        full=vertex_delta(source_morphs[normalize(old['name'])]);target=full[ids]
        other=np.zeros_like(vertices);before=np.zeros_like(vertices)
        for d in old['deltas']:
            i=d['bone'];x=vertices-positions[i]
            contribution=W[:,i,None]*(x@Rotation.from_rotvec(d['rotation']).as_matrix().T-x+d['position'])
            before+=contribution
            if i not in lips:other+=contribution
        fitted=lstsq(A,np.vstack([(target-other)[lip_mask]*row_scale[:,None],np.zeros((len(lips),3))]),cond=1e-7,lapack_driver='gelsy')[0]
        if not np.isfinite(fitted).all() or np.any(np.linalg.norm(fitted,axis=1)>5):
            raise ValueError('Unstable lip translation fit')
        # Measure exactly what the runtime will read, including quantization.
        fitted=fitted.round(8);predicted=other+W[:,lips]@fitted
        error=np.sum((target-predicted)**2)+np.sum(full[~covered]**2)
        energy=float(np.sum(full*full));residual=float(np.sqrt(error/max(energy,1e-20)))
        mouth_error=float(np.sum((target-predicted)[mouth_mask]**2))
        mouth_energy=float(np.sum(target[mouth_mask]**2))
        mouth_rms=float(np.sqrt(mouth_error/max(1,np.count_nonzero(mouth_mask))))
        mouth_residual=float(np.sqrt(mouth_error/mouth_energy)) if mouth_energy>1e-12 else 0.
        records=[copy.deepcopy(d) for d in old['deltas'] if d['bone'] not in lips]
        records.extend({'bone':i,'position':fitted[k].tolist(),'rotation':[0.,0.,0.]}
                       for k,i in enumerate(lips) if np.linalg.norm(fitted[k])>=1e-6)
        records.sort(key=lambda d:d['bone'])
        # Same mouth quality policy as initial calibration. A shape that cannot
        # be represented reliably uses the existing optional fixed fallback.
        supported=bool(records) and residual<.98 and not (mouth_residual>.5 and mouth_rms>span*.02)
        out['supported']=supported;out['residual']=round(residual,6)
        out['reason']=('' if residual<.1 else '源顶点形变只能由现有骨骼近似') if supported else '嘴部形状还原误差过大'
        out['deltas']=records if supported else []
        rms=lambda value:float(np.sqrt(np.mean(np.sum((target-value)[lip_mask]**2,axis=1))))
        reports.append({'name':old['name'],'supported':supported,'residual':residual,
                        'mouth_residual':mouth_residual,'mouth_rms':mouth_rms,
                        'lip_rms_before':rms(before),'lip_rms_after':rms(predicted)})
    method='lip_contour_translation_v1' if vertex_importance is not None or fit_jaw else 'lip_translation_v1'
    result['mouth_calibration']=method
    if skin_reference:result['mouth_skin_reference']=skin_reference
    return result,{'method':method,'shapes':reports}


def refit_teeth(model, profile):
    """Keep rigid tooth controls behind the source morph's forward envelope.

    PMX vertex morphs can compress a tooth surface in depth. A rigid rotation /
    translation fit cannot reproduce that compression: its centroid may match
    while its front vertices protrude through the chin. Add the minimum inward
    translation required by the source surface, including intermediate weights.
    Lip motion, tooth rotations, neutral poses and non-mouth shapes are retained.
    This is an offline approximation, not runtime mesh collision detection.
    """
    model,_=prepare_mouth_line_retraction(model)
    if profile.get('source_hash') != model['hash']:
        raise ValueError('Tooth refit requires the exact source PMX')
    result=copy.deepcopy(profile);targets={}
    for i,b in enumerate(model['bones']):
        name=game_bone(b['name']).lower()
        if name not in targets or name==b['name'].lower():targets[name]=i
    names=[b['name'].lower() for b in profile['bones']]
    if len(set(names))!=len(names) or any(n not in targets for n in names):
        raise ValueError('Tooth refit has missing or duplicate source bones')
    selected=[targets[n] for n in names]
    positions=np.array([b['rest'] for b in profile['bones']],dtype=float)
    if not np.allclose(positions,[model['bones'][i]['rest'] for i in selected],atol=1e-6,rtol=0):
        raise ValueError('Tooth refit has mismatched reference positions')
    teeth=[i for i,n in enumerate(names) if n in ('facemdtoothdnjoint','facemdtoothupjoint','line_toothjoint')]
    indices,weights,_=recover_teeth_weights(model,selected)
    covered=np.any(np.isin(indices,[selected[i] for i in teeth])&(weights>1e-7),axis=1)
    ids=np.flatnonzero(covered);vertices=model['vertices'][ids]
    W=np.column_stack([np.sum(np.where(indices[ids]==i,weights[ids],0),axis=1) for i in selected])
    source_morphs={normalize(m['name']):i for i,m in enumerate(model['morphs'])}
    if len(source_morphs)!=len(model['morphs']):raise ValueError('Ambiguous source morph names')
    def vertex_delta(index,stack=()):
        if index<0 or index>=len(model['morphs']) or index in stack or len(stack)>32:
            raise ValueError('Invalid/cyclic group morph')
        m=model['morphs'][index];delta=np.zeros_like(model['vertices'])
        if m['type']==1:np.add.at(delta,m['ids'],m['offsets'])
        elif m['type']==0:
            for child,weight in m['groups']:delta+=vertex_delta(child,stack+(index,))*weight
        else:raise ValueError('Tooth refit requires vertex or group morphs')
        return delta
    samples=np.array([.125,.25,.5,.75,1.]);reports=[]
    for old,out in zip(profile['morphs'],result['morphs']):
        if old.get('panel')!=3 or not old['supported']:continue
        records={d['bone']:d for d in out['deltas']}
        affected=[i for i in teeth if i in records and np.any(W[:,i]>.5)]
        if not affected:continue
        full=vertex_delta(source_morphs[normalize(old['name'])]);target=full[ids]
        predicted=np.zeros((len(samples),len(ids),3))
        for d in old['deltas']:
            i=d['bone']
            if not np.any(W[:,i]>1e-7):continue
            x=vertices-positions[i]
            for k,t in enumerate(samples):
                predicted[k]+=W[:,i,None]*(x@Rotation.from_rotvec(np.array(d['rotation'])*t).as_matrix().T-x+np.array(d['position'])*t)
        before=predicted[-1].copy();corrections=[]
        for i in affected:
            # Tiny secondary weights must not turn a submillimetre mismatch
            # into an unbounded translation. Tooth surfaces have a dominant
            # tooth control; uncertain skinning remains outside this refit.
            mask=W[:,i]>.5
            required=(target[mask,2][None,:]*samples[:,None]-predicted[:,mask,2])/(samples[:,None]*W[mask,i])
            correction=max(0.,float(np.max(required)))
            if correction<=1e-6:continue
            # PMX uses -Z towards the front of the face. Round inward so saved
            # profiles retain the bound; rerunning the refit is idempotent.
            d=records[i];prior=d['position'][2]
            value=float(np.ceil((prior+correction)*1e8)/1e8)
            if not math.isfinite(value) or np.linalg.norm([*d['position'][:2],value])>5:
                raise ValueError('Unstable tooth clearance fit')
            d['position'][2]=value;shift=value-prior
            predicted[:,:,2]+=samples[:,None]*W[:,i][None,:]*shift
            corrections.append({'bone':names[i],'inward':shift})
        if corrections:
            # Only tooth vertices changed. Update the full-expression error
            # without re-fitting or weakening any lip/eye/brow control.
            energy=float(np.sum(full*full))
            error=old['residual']**2*energy+float(np.sum((target-predicted[-1])**2)-np.sum((target-before)**2))
            out['residual']=round(math.sqrt(max(0.,error)/max(energy,1e-20)),6)
        reports.append({'name':old['name'],'corrections':corrections,
                        'forward_excess_before':float(np.max(before[:,2]*-1+target[:,2])) if len(ids) else 0.,
                        'forward_excess_after':float(np.max(predicted[-1,:,2]*-1+target[:,2])) if len(ids) else 0.})
    result['teeth_calibration']='depth_envelope_v1'
    return result,{'method':'depth_envelope_v1','shapes':reports}


def prune_unused_face_anchors(model, profile):
    """Remove unweighted, unused terminal controls from calibration anchors.

    Keep the source hierarchy and all constrained or animated controls intact.
    This only changes the profile's anchor list and its delta indices; it never
    changes the PMX or relaxes the runtime neutral-geometry validation.
    """
    if profile.get('source_hash') != model['hash']:
        raise ValueError('Face anchor pruning requires the exact source PMX')
    targets={}
    for i,bone in enumerate(model['bones']):
        name=game_bone(bone['name']).lower()
        if name not in targets or name==bone['name'].lower():targets[name]=i
    names=[bone['name'].lower() for bone in profile['bones']]
    if len(set(names))!=len(names) or any(name not in targets for name in names):
        raise ValueError('Face anchor pruning has missing or duplicate source bones')
    selected=[targets[name] for name in names]
    if not np.allclose([bone['rest'] for bone in profile['bones']],
                       [model['bones'][i]['rest'] for i in selected],atol=1e-6,rtol=0):
        raise ValueError('Face anchor pruning has mismatched reference positions')
    # Count even tiny weights, including tooth weights recovered for the fit.
    indices,weights,_=recover_teeth_weights(model,selected)
    used=set(model['indices'][model['weights']!=0].tolist())
    used.update(indices[weights!=0].tolist())
    # SMCGazeBind uses these pivots and optical-axis references even when the
    # source skinning and ordinary expression deltas do not move them.
    gaze_anchors={'eyelfjoint','eyertjoint','facelfirisjoint','facertirisjoint'}
    used.update(selected[i] for i,name in enumerate(names) if name in gaze_anchors)
    for i,bone in enumerate(model['bones']):
        if bone['parent']>=0:used.add(bone['parent'])
        if 'append' in bone:
            used.update((i,bone['append'][0]))
        if 'ik' in bone:
            used.update((i,bone['ik']['target']))
            used.update(link['bone'] for link in bone['ik']['links'])
        # External-parent constraints have no ordinary source-bone reference.
        if bone.get('flags',0)&0x2000:used.add(i)
    for morph in profile['morphs']:
        for delta in morph['deltas']:
            index=delta['bone']
            if not isinstance(index,int) or not 0<=index<len(selected):
                raise ValueError('Face anchor pruning has invalid delta indices')
            if any(value!=0 for value in delta['position']+delta['rotation']):
                used.add(selected[index])
    keep=[i for i,source in enumerate(selected) if source in used]
    remap={old:new for new,old in enumerate(keep)}
    result=copy.deepcopy(profile)
    result['bones']=[result['bones'][i] for i in keep]
    for morph in result['morphs']:
        # Only identically zero records can reference a removed anchor.
        morph['deltas']=[dict(delta,bone=remap[delta['bone']])
                         for delta in morph['deltas'] if delta['bone'] in remap]
    return result,{'removed':[names[i] for i in range(len(names)) if i not in remap],
                   'before':len(names),'after':len(keep)}


def compile_profile(model, label):
    model,line_report=prepare_mouth_line_retraction(model)
    # Exact original names take precedence over additional MMD control bones.
    targets={}
    for i,b in enumerate(model['bones']):
        name=game_bone(b['name']); key=name.lower()
        if region(name) and (key not in targets or name==b['name']): targets[key]=i
    selected=list(targets.values()); names=list(targets)
    if len(selected)>256: raise ValueError('Too many face bones')
    source_ids={b:i for i,b in enumerate(selected)}
    original_indices,original_weights,teeth_report=recover_teeth_weights(model,selected)
    mask=np.zeros(len(model['vertices']),dtype=bool)
    for b in selected: mask |= np.any((original_indices==b)&(original_weights>1e-7),axis=1)
    vertex_ids=np.flatnonzero(mask); vertices=model['vertices'][vertex_ids]
    W=np.zeros((len(vertices),len(selected)))
    for slot in range(4):
        for b,j in source_ids.items(): W[:,j]+=np.where(original_indices[vertex_ids,slot]==b,original_weights[vertex_ids,slot],0)
    positions=np.array([model['bones'][i]['rest'] for i in selected])
    if len(selected)<12 or len(vertices)<12: return None, {'reason':'模型缺少足够的可对应面部控制点'}
    mouth_columns=[i for i,n in enumerate(names) if region(n)=='mouth']
    mouth_vertices=np.any(W[:,mouth_columns]>1e-7,axis=1) if mouth_columns else np.zeros(len(vertices),dtype=bool)
    mouth_points=positions[mouth_columns]
    mouth_span=float(np.max(np.linalg.norm(mouth_points[:,None]-mouth_points,axis=2))) if mouth_columns else 0.
    tooth_names=('facemdtoothupjoint','facemdtoothdnjoint')
    missing_teeth=[name for name in tooth_names if name not in names or np.count_nonzero(W[:,names.index(name)]>1e-7)<6]
    missing_teeth+=teeth_report['unresolved']
    # Solve global translations and infinitesimal rotations using the source
    # skinning Jacobian. All vertices influenced by these bones are included,
    # including unchanged vertices, to prevent unrelated regions drifting.
    blocks=[]
    for j in range(len(selected)):
        x=vertices-positions[j]; z=np.zeros(len(x))
        cross=np.stack([z,x[:,2],-x[:,1],-x[:,2],z,x[:,0],x[:,1],-x[:,0],z],axis=1).reshape(-1,3,3)
        block=np.concatenate([np.broadcast_to(np.eye(3),(len(x),3,3)),cross],axis=2)
        blocks.append(block*W[:,j,None,None])
    A=np.concatenate(blocks,axis=2).reshape(-1,len(selected)*6)
    # Ridge selects a stable solution for shared/degenerate weights. The head
    # bone is excluded: unrepresentable vertex motion is reported, never faked
    # by translating the entire head.
    regularizer=np.tile([.02,.02,.02,.08,.08,.08],len(selected))
    solver=np.vstack([A,np.diag(regularizer)])
    morphs=model['morphs']; chosen=[]; deltas=[]; skipped=[]; panels=[]
    def vertex_delta(index, stack=()):
        if index<0 or index>=len(morphs) or index in stack or len(stack)>32: raise ValueError('Invalid/cyclic group morph')
        m=morphs[index]; d=np.zeros_like(model['vertices'])
        if m['type']==1: np.add.at(d,m['ids'],m['offsets'])
        elif m['type']==0:
            for child,weight in m['groups']: d+=vertex_delta(child,stack+(index,))*weight
        else: raise ValueError('非顶点形状或包含材质/特殊控制')
        return d
    for i,m in enumerate(morphs):
        n=normalize(m['name'])
        if m['category'] not in (1,2,3) or any(x in n for x in ('瞳','恐ろしい','ハイライト','歯消','照れ')):
            skipped.append({'name':n,'reason':'特殊效果不校准'}); continue
        try: d=vertex_delta(i)
        except ValueError as e: skipped.append({'name':n,'reason':str(e)}); continue
        if np.linalg.norm(d)<1e-8: skipped.append({'name':n,'reason':'没有形变'});continue
        chosen.append(n);deltas.append(d);panels.append(m['category'])
    if not chosen:return None,{'reason':'模型没有可校准的普通表情','skipped':skipped}
    rhs=np.zeros((solver.shape[0],len(chosen)))
    for k,d in enumerate(deltas):rhs[:len(vertices)*3,k]=d[vertex_ids].reshape(-1)
    params=lstsq(solver,rhs,cond=1e-6,lapack_driver='gelsy')[0].reshape(len(selected),6,-1)
    # Refine exact rigid transforms by weighted local Procrustes, retaining the
    # stable Jacobian initialization. No approximation of rotations as offsets
    # is used when measuring the exported calibration error.
    expressions=[];quality=[]
    for k,name in enumerate(chosen):
        translation=params[:,:3,k].copy();rotvec=params[:,3:,k].copy()
        lengths=np.linalg.norm(rotvec,axis=1);rotvec*=np.minimum(1,.9/np.maximum(lengths,1e-15))[:,None]
        rotations=Rotation.from_rotvec(rotvec).as_matrix()
        target=deltas[k][vertex_ids]; predicted=np.zeros_like(vertices)
        contributions=[]
        for j in range(len(selected)):
            x=vertices-positions[j];c=W[:,j,None]*(x@rotations[j].T-x+translation[j]);contributions.append(c);predicted+=c
        # Six bounded sweeps improve curved eyelids without unconstrained 180°
        # rotations on tiny one-vertex controls.
        for sweep in range(6):
            for j in range(len(selected)):
                ids=np.flatnonzero(W[:,j]>1e-5)
                if len(ids)<3:continue
                x=vertices[ids]-positions[j];w=W[ids,j];w2=w*w
                goal=x+(target[ids]-predicted[ids]+contributions[j][ids])/w[:,None]
                # Anchor the rigid motion near its current neutral orientation.
                covariance=((x-(x*w2[:,None]).sum(0)/w2.sum())*w2[:,None]).T @ (goal-(goal*w2[:,None]).sum(0)/w2.sum())
                covariance+=np.eye(3)*.002
                u,_,vt=np.linalg.svd(covariance);fix=np.eye(3);fix[2,2]=np.linalg.det(vt.T@u.T)
                R=vt.T@fix@u.T;rv=Rotation.from_matrix(R).as_rotvec()
                if np.linalg.norm(rv)>1.2:continue
                t=((goal-x@R.T)*w2[:,None]).sum(0)/(w2.sum()+.0004)
                if np.linalg.norm(t)>2:continue
                candidate=w[:,None]*(x@R.T-x+t)
                old=contributions[j][ids];res=target[ids]-predicted[ids]
                if np.sum((res+old-candidate)**2)>np.sum(res**2)+1e-12:continue
                predicted[ids]+=candidate-old;contributions[j][ids]=candidate
                rotations[j]=R;rotvec[j]=rv;translation[j]=t
        full=deltas[k];covered=np.zeros(len(full),dtype=bool);covered[vertex_ids]=True
        error=np.sum((target-predicted)**2)+np.sum(full[~covered]**2)
        residual=float(np.sqrt(error/np.sum(full*full)))
        mouth_energy=float(np.sum(target[mouth_vertices]**2))
        mouth_error=float(np.sum((target-predicted)[mouth_vertices]**2))
        # Judge the mouth separately: eye vertices must not hide an inaccurate
        # mouth, and missing tooth weights must not count as a successful fit.
        mouth_residual=float(np.sqrt(mouth_error/mouth_energy)) if mouth_energy>1e-12 else 0.
        mouth_rms=float(np.sqrt(mouth_error/max(1,np.count_nonzero(mouth_vertices))))
        records=[]
        for j in range(len(selected)):
            if np.linalg.norm(translation[j])+np.linalg.norm(rotvec[j])<1e-6:continue
            if np.linalg.norm(translation[j])>5 or np.linalg.norm(rotvec[j])>1.6:raise ValueError('Unstable facial calibration')
            records.append({'bone':j,'position':translation[j].round(8).tolist(),'rotation':rotvec[j].round(8).tolist()})
        supported=bool(records) and residual<.98
        reason='' if residual<.1 else '源顶点形变只能由现有骨骼近似' if supported else '现有面部骨骼无法重建该形状'
        # Tiny closed-mouth changes can have high relative error but negligible
        # visible displacement. Require a substantial absolute error as well.
        bad_mouth=mouth_residual>.5 and mouth_rms>mouth_span*.02
        if panels[k]==3 and (missing_teeth or bad_mouth):
            supported=False
            reason='嘴部缺少可靠的牙齿校准' if missing_teeth else '嘴部形状还原误差过大'
        expressions.append({'name':name,'panel':panels[k],'supported':supported,'residual':round(residual,6),'reason':reason,'deltas':records if supported else []})
        quality.append({'name':name,'residual':residual,'mouth_residual':mouth_residual,'mouth_rms':mouth_rms,'supported':supported,'reason':reason})
    key=model['name'].lower()
    # The male PMX's author suffix is not part of the game's character key.
    if key=='endminm_f':key='endminm'
    if not re.fullmatch(r'[a-z0-9_]{1,128}',key):
        raise ValueError('Model identity must match a game character key')
    profile={'version':1,'model':key,'label':label,'source_hash':model['hash'],
             'bones':[{'name':name,'rest':positions[i].tolist()} for i,name in enumerate(names)],'morphs':expressions}
    profile,mouth_report=refit_mouth(model,profile)
    profile,tooth_clearance=refit_teeth(model,profile)
    if line_report['shapes']:profile['mouth_line_calibration']=line_report['method']
    for q in quality:
        match=next((s for s in mouth_report['shapes'] if s['name']==q['name']),None)
        if match:
            q.update(match);q['reason']=next(m['reason'] for m in profile['morphs'] if m['name']==q['name'])
        q['residual']=next(m['residual'] for m in profile['morphs'] if m['name']==q['name'])
    profile,unused_anchors=prune_unused_face_anchors(model,profile)
    return profile,{'model':key,'label':label,'bones':len(profile['bones']),'morphs':quality,'skipped':skipped,'teeth':teeth_report,'missing_teeth':missing_teeth,'normalized_weight_vertices':model.get('weight_fixes',0),
                   'mouth_refit':mouth_report,'tooth_clearance':tooth_clearance,'unused_anchors':unused_anchors,'mouth_line_retraction':line_report,
                   'note':'Source PMX reconstruction error, not game visual acceptance.'}


def scaled_face_delta(neutral, parents, points, weights, parameters):
    """Evaluate the runtime's global position/rotation + local scale contract.

    Controlled children retain their desired global position and quaternion,
    but inherit their parent's nonuniform scale. Do not polar-decompose the
    resulting skin matrix: Unity's quaternion chain is separate from shear.
    Neutral matrices and points are expressed in source model coordinates.
    """
    parameters=np.asarray(parameters).reshape(len(neutral),9)
    rotations=Rotation.from_rotvec(parameters[:,3:6]).as_matrix()@neutral[:,:3,:3]
    matrices=[];result=np.zeros_like(points)
    for i,parent in enumerate(parents):
        inherited=matrices[parent]@rotations[parent].T if parent>=0 else np.eye(3)
        matrix=inherited@rotations[i]@np.diag(np.clip(1+parameters[i,6:9],.01,4))
        matrices.append(matrix)
        skin=matrix@neutral[i,:3,:3].T
        result+=weights[:,i,None]*((points-neutral[i,:3,3])@skin.T+
                                  neutral[i,:3,3]+parameters[i,:3]-points)
    return result


def refit_scaled_shapes(model, profile, reference, shape_names):
    """Fit selected shapes against an offline, matching game skin reference.

    Opt-in only: bone axes and actual game weights are mandatory. Existing
    calibration outside these shapes is preserved, and ambiguous geometry,
    unmatched hierarchy, excessive deformation or a poor fit aborts the edit.
    The caller writes the result only after every requested shape succeeds.
    """
    reference_model=Path(reference.get('model','')).name.lower()
    prefab=re.fullmatch(r'chr_\d+_(.+)_postmodel\.prefab',reference_model)
    if prefab:reference_model=prefab[1]
    if model['hash']!=profile['source_hash'] or reference_model!=profile['model']:
        raise ValueError('Scale reference/model identity mismatch')
    if not shape_names or len(set(shape_names))!=len(shape_names):
        raise ValueError('Specify unique scale-fit shapes')
    game={}
    for bone in reference['bones']:
        key=bone['name'].lower()
        if key in game:raise ValueError('Ambiguous game face bone')
        game[key]=bone
    names=[b['name'].lower() for b in profile['bones']]
    anchors=[i for i,n in enumerate(names) if n in game]
    if len(anchors)<12:raise ValueError('Insufficient matching face anchors')
    source=np.array([profile['bones'][i]['rest'] for i in anchors])
    target=np.array([game[names[i]]['matrix'][12:15] for i in anchors])
    sm,tm=source.mean(0),target.mean(0);x,y=source-sm,target-tm
    affine=np.linalg.lstsq(x,y,rcond=None)[0];scale=float(np.linalg.norm(affine,axis=1).mean())
    qx=affine[0]/np.linalg.norm(affine[0]);qy=affine[1]-qx*np.dot(qx,affine[1]);qy/=np.linalg.norm(qy)
    qz=np.cross(qx,qy);qz*=1 if np.dot(qz,affine[2])>=0 else -1
    basis=np.column_stack([qx,qy,qz]);translation=tm-sm@basis.T*scale
    alignment=float(np.linalg.norm(x@basis.T*scale-y)/np.linalg.norm(y))
    if not np.isfinite(alignment) or alignment>.001 or not .001<scale<1:
        raise ValueError('Offline game reference does not match the source neutral face')
    # Scale axes may have either sign. Choose a proper source-space frame even
    # when the PMX/game alignment contains a reflection.
    axis_sign=np.diag([np.linalg.det(basis),1.,1.])
    source_bones={}
    for i,bone in enumerate(model['bones']):
        key=game_bone(bone['name']).lower()
        if key not in source_bones or key==bone['name'].lower():source_bones[key]=i
    source_vertices=model['vertices']
    pmx_names={normalize(m['name']):i for i,m in enumerate(model['morphs'])}
    def vertex_delta(index,stack=()):
        if index in stack:raise ValueError('Cyclic PMX expression')
        morph=model['morphs'][index];delta=np.zeros_like(source_vertices)
        if morph['type']==1:np.add.at(delta,morph['ids'],morph['offsets'])
        elif morph['type']==0:
            for child,weight in morph['groups']:delta+=vertex_delta(child,stack+(index,))*weight
        else:raise ValueError('Scale fitting requires a vertex expression')
        return delta
    result=copy.deepcopy(profile);reports=[]
    for shape_name in shape_names:
        matches=[m for m in result['morphs'] if m['name']==shape_name]
        if len(matches)!=1 or shape_name not in pmx_names:raise ValueError('Scale expression not found')
        shape=matches[0];selected={names[d['bone']] for d in shape['deltas']}
        if not selected or not selected.issubset(game) or not selected.issubset(source_bones):
            raise ValueError('Incomplete scale expression bone mapping')
        ordered=[];visiting=set()
        def visit(name):
            if name in ordered:return
            if name in visiting:raise ValueError('Cyclic scale bone hierarchy')
            visiting.add(name);parent=game[name]['parent'].lower()
            if parent in selected:visit(parent)
            ordered.append(name);visiting.remove(name)
        for name in sorted(selected):visit(name)
        parents=[ordered.index(game[n]['parent'].lower()) if game[n]['parent'].lower() in selected else -1 for n in ordered]
        # A non-selected intermediary would inherit scale while retaining its
        # own neutral local transform. Require a complete controlled chain.
        for name in ordered:
            parent=game[name]['parent'].lower();seen=set()
            while parent in game and parent not in seen:
                seen.add(parent);ancestor=game[parent]['parent'].lower()
                if parent not in selected and ancestor in selected:
                    raise ValueError('Scale hierarchy has an uncontrolled intermediary')
                parent=ancestor
        for name,bone in game.items():
            if name not in selected and bone['parent'].lower() in selected:
                raise ValueError('Scale fitting requires all descendants of its controlled bones')
        neutral=[]
        for name in ordered:
            matrix=np.array(game[name]['matrix']).reshape(4,4,order='F')
            axes=matrix[:3,:3];lengths=np.linalg.norm(axes,axis=0)
            unit=axes/lengths
            if np.max(np.abs(unit.T@unit-np.eye(3)))>1e-4 or np.max(np.abs(lengths-1))>1e-4:
                raise ValueError('Scale reference has a non-rigid neutral bone')
            n=np.eye(4);n[:3,:3]=Rotation.from_matrix(basis.T@unit@axis_sign).as_matrix()
            n[:3,3]=(matrix[:3,3]-translation)@basis/scale;neutral.append(n)
        neutral=np.array(neutral);source_ids=[source_bones[n] for n in ordered]
        eligible=np.flatnonzero(np.any(np.isin(model['indices'],source_ids)&(model['weights']>1e-7),axis=1))
        if len(eligible)<3:raise ValueError('No matching source skin weights')
        tree=cKDTree(source_vertices[eligible]);full_target=vertex_delta(pmx_names[shape_name])
        points=[];weight_sets=[];targets=[];mesh_counts={};max_distance=0.
        for mesh in reference['meshes']:
            bone_names=[n.lower() for n in mesh['bones']]
            indices=np.array(mesh['indices'],int);skin_weights=np.array(mesh['weights'])
            if indices.shape!=skin_weights.shape or len(indices)!=len(mesh['vertices']):
                raise ValueError('Invalid offline game skin weights')
            if np.any(skin_weights<0) or np.max(np.abs(skin_weights.sum(1)-1))>1e-4:
                raise ValueError('Unnormalized offline game skin weights')
            columns=[]
            for name in ordered:
                columns.append(np.where(indices==bone_names.index(name),skin_weights,0).sum(1)
                               if name in bone_names else np.zeros(len(indices)))
            weights=np.column_stack(columns);mask=np.any(weights>1e-7,axis=1)
            if not mask.any():continue
            v=(np.array(mesh['vertices'])[mask]-translation)@basis/scale
            distances,near=tree.query(v);nearest=eligible[near]
            max_distance=max(max_distance,float(distances.max()*scale))
            if max_distance>.0001:raise ValueError('Offline/source surface correspondence exceeds 0.1 mm')
            for i,candidates in enumerate(tree.query_ball_point(v,distances+1e-7)):
                if np.any(np.linalg.norm(full_target[eligible[candidates]]-full_target[nearest[i]],axis=1)>1e-5):
                    raise ValueError('Ambiguous coincident source expression vertices')
            points.append(v);weight_sets.append(weights[mask]);targets.append(full_target[nearest])
            mesh_counts[mesh['name']]=int(mask.sum())
        if not points:raise ValueError('No matching offline game skin')
        points=np.vstack(points);weights=np.vstack(weight_sets);target_delta=np.vstack(targets)
        if np.any((weights>1e-7).sum(0)<3):raise ValueError('Scale bone has insufficient skin evidence')
        original=np.zeros((len(ordered),9))
        for delta in shape['deltas']:
            original[ordered.index(names[delta['bone']])]=delta['position']+delta['rotation']+delta.get('scale',[0,0,0])
        levels=(.25,.5,.75,1.)
        def residual(parameters):
            parameters=parameters.reshape(len(ordered),9)
            return np.r_[*( (scaled_face_delta(neutral,parents,points,weights,parameters*w)-target_delta*w).ravel()
                            for w in levels),.001*parameters.ravel()]
        lo=np.tile([-2]*3+[-1.5]*3+[-.75]*3,len(ordered))
        hi=np.tile([2]*3+[1.5]*3+[1.5]*3,len(ordered))
        solved=least_squares(residual,original.ravel(),bounds=(lo,hi),max_nfev=300,
                             xtol=1e-10,ftol=1e-10,gtol=1e-9)
        fitted=np.round(solved.x.reshape(len(ordered),9),8)
        if not solved.success or np.max(np.linalg.norm(fitted[:,3:6],axis=1))>1.6:
            raise ValueError('Scale shape fit did not converge within rotation limits')
        def metrics(parameters,w):
            difference=scaled_face_delta(neutral,parents,points,weights,parameters*w)-target_delta*w
            errors=np.linalg.norm(difference,axis=1)*scale*1000
            return {'rms_mm':float(np.sqrt(np.mean(errors**2))),'max_mm':float(errors.max()),
                    'relative':float(np.linalg.norm(difference)/max(np.linalg.norm(target_delta*w),1e-12)) if w else 0.}
        samples=[{'weight':w,'before':metrics(original,w),'after':metrics(fitted,w)} for w in (0.,)+levels]
        if any(s['after']['rms_mm']>.5 or s['after']['max_mm']>1.5 for s in samples):
            raise ValueError('Scaled face shape exceeds the skin error limits')
        if samples[-1]['after']['rms_mm']>samples[-1]['before']['rms_mm']+1e-5:
            raise ValueError('Scaled face fit does not improve the original shape')
        for delta in shape['deltas']:
            i=ordered.index(names[delta['bone']]);values=fitted[i]
            delta['position']=values[:3].tolist();delta['rotation']=values[3:6].tolist();delta['scale']=values[6:9].tolist()
            result['bones'][delta['bone']]['scale_axes']=np.round(neutral[i,:3,:3].T,9).tolist()
        shape['residual']=round(samples[-1]['after']['relative'],6)
        reports.append({'name':shape_name,'bones':ordered,'meshes':mesh_counts,'vertices':len(points),
                        'neutral_match_max_mm':max_distance*1000,'iterations':solved.nfev,
                        'local_scale':(1+fitted[:,6:9]).tolist(),'samples':samples})
    return result,{'method':'offline_native_axis_scale_v1','alignment':alignment,'scale':scale,'shapes':reports}


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('source',type=Path);ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--report',type=Path);ap.add_argument('--only',default='')
    ap.add_argument('--refit-mouth',action='store_true',help='Refit only mouths in existing output profiles; preserve gaze and other calibration')
    ap.add_argument('--refit-teeth',action='store_true',help='Correct tooth depth in existing output profiles without changing lips or other calibration')
    ap.add_argument('--refit-scale-shape',action='append',default=[],help='Refit one existing expression with verified game bone axes and skin weights; repeat for multiple shapes')
    ap.add_argument('--game-reference',type=Path,help='Offline game face bones/meshes JSON; required for scale fitting')
    args=ap.parse_args()
    if args.refit_scale_shape and (not args.game_reference or args.refit_mouth or args.refit_teeth):
        ap.error('Scale fitting requires --game-reference and cannot be combined with mouth/teeth refitting')
    args.output.mkdir(parents=True,exist_ok=True)
    paths=sorted(args.source.rglob('*.pmx')) if args.source.is_dir() else [args.source]
    reports=[];seen=set()
    for path in paths:
        if args.only and args.only not in path.stem:continue
        print('Calibrating:',path.stem,flush=True)
        try:
            model=read_pmx(path)
            if args.refit_mouth or args.refit_teeth or args.refit_scale_shape:
                # Locate by hash, not local filename or translated character label.
                matches=list(args.output.glob('*-'+model['hash'][:12]+'.face.json'))
                if not matches:continue
                if len(matches)!=1:raise ValueError('Ambiguous existing profile')
                original=json.loads(matches[0].read_text(encoding='utf-8'))
                if args.refit_scale_shape:
                    reference=json.loads(args.game_reference.read_text(encoding='utf-8-sig'))
                    profile,report=refit_scaled_shapes(model,original,reference,args.refit_scale_shape)
                elif args.refit_mouth:
                    profile,report=refit_mouth(model,original)
                    profile,report['tooth_clearance']=refit_teeth(model,profile)
                else:profile,report=refit_teeth(model,original)
            else:profile,report=compile_profile(model,path.stem)
            report['source_name']=path.name;reports.append(report)
            if profile:
                identity=profile['model']+'-'+profile['source_hash'][:12]
                if identity in seen:continue
                seen.add(identity);dest=args.output/(identity+'.face.json');tmp=dest.with_suffix('.tmp')
                tmp.write_text(json.dumps(profile,ensure_ascii=False,separators=(',',':')),encoding='utf-8');tmp.replace(dest)
                usable=sum(m['supported'] for m in profile['morphs'])
                print(' ',profile['model'],usable,'/',len(profile['morphs']),'shapes',flush=True)
            else:print(' ',report['reason'],flush=True)
        except Exception as e:
            reports.append({'source_name':path.name,'error':str(e)});print(' ERROR:',e,flush=True)
    if args.report:
        args.report.parent.mkdir(parents=True,exist_ok=True)
        args.report.write_text(json.dumps(reports,ensure_ascii=False,indent=2),encoding='utf-8')
    if any('error' in r for r in reports):raise SystemExit(1)


if __name__=='__main__':main()
