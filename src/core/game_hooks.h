#pragma once

// Task 2.1：捕获主角色 Animator/Entity。
// 通过 MinHook 挂 PlayerController.SetMainCharacter，在角色切换时提取 Entity →
// ComplexAnimationComponent → Animator，存入 g_charAnimator 并置 g_charChanged。
// 同时提供 Humanoid 骨骼句柄封装（GetHumanoidBone / GetBoneLocalRot 等），
// 供后续 FK/IK/快照/从骨（skeleton.h / accessory.h / freeze.h）复用。
//
// 参照 {EIEM}/src/init.h L985-L1121 的 SetMainCharacter hook 逻辑精简而来。

#include "base.h"
#include "il2cpp_api.h"
#include "math/quat_math.h"

#include <cstdint>
#include <cstring>

// ---- Unity HumanBodyBones 枚举（55 根，标准顺序；新版 Unity 含 UpperChest=54）----
enum HumanBodyBones {
  Hips = 0, LeftUpperLeg = 1, RightUpperLeg = 2,
  LeftLowerLeg = 3, RightLowerLeg = 4, LeftFoot = 5, RightFoot = 6,
  Spine = 7, Chest = 8, UpperChest = 54,
  Neck = 9, Head = 10,
  LeftShoulder = 11, RightShoulder = 12,
  LeftUpperArm = 13, RightUpperArm = 14,
  LeftLowerArm = 15, RightLowerArm = 16,
  LeftHand = 17, RightHand = 18,
  LeftToes = 19, RightToes = 20,
  LeftEye = 21, RightEye = 22, Jaw = 23,
  LeftThumbProximal = 24, LeftThumbIntermediate = 25, LeftThumbDistal = 26,
  LeftIndexProximal = 27, LeftIndexIntermediate = 28, LeftIndexDistal = 29,
  LeftMiddleProximal = 30, LeftMiddleIntermediate = 31, LeftMiddleDistal = 32,
  LeftRingProximal = 33, LeftRingIntermediate = 34, LeftRingDistal = 35,
  LeftLittleProximal = 36, LeftLittleIntermediate = 37, LeftLittleDistal = 38,
  RightThumbProximal = 39, RightThumbIntermediate = 40, RightThumbDistal = 41,
  RightIndexProximal = 42, RightIndexIntermediate = 43, RightIndexDistal = 44,
  RightMiddleProximal = 45, RightMiddleIntermediate = 46, RightMiddleDistal = 47,
  RightRingProximal = 48, RightRingIntermediate = 49, RightRingDistal = 50,
  RightLittleProximal = 51, RightLittleIntermediate = 52, RightLittleDistal = 53,
  LastBone = 55,
};
static const int kHumanBoneCount = 55;

static const char *HumanBoneName(int b) {
  static const char *const names[kHumanBoneCount] = {
      "Hips", "LeftUpperLeg", "RightUpperLeg", "LeftLowerLeg", "RightLowerLeg",
      "LeftFoot", "RightFoot", "Spine", "Chest",
      "Neck", "Head", "LeftShoulder", "RightShoulder",
      "LeftUpperArm", "RightUpperArm", "LeftLowerArm", "RightLowerArm",
      "LeftHand", "RightHand", "LeftToes", "RightToes",
      "LeftEye", "RightEye", "Jaw",
      "LeftThumbProximal", "LeftThumbIntermediate", "LeftThumbDistal",
      "LeftIndexProximal", "LeftIndexIntermediate", "LeftIndexDistal",
      "LeftMiddleProximal", "LeftMiddleIntermediate", "LeftMiddleDistal",
      "LeftRingProximal", "LeftRingIntermediate", "LeftRingDistal",
      "LeftLittleProximal", "LeftLittleIntermediate", "LeftLittleDistal",
      "RightThumbProximal", "RightThumbIntermediate", "RightThumbDistal",
      "RightIndexProximal", "RightIndexIntermediate", "RightIndexDistal",
      "RightMiddleProximal", "RightMiddleIntermediate", "RightMiddleDistal",
      "RightRingProximal", "RightRingIntermediate", "RightRingDistal",
      "RightLittleProximal", "RightLittleIntermediate", "RightLittleDistal",
      "UpperChest"};
  if (b < 0 || b >= kHumanBoneCount)
    return "?";
  return names[b];
}

// ---- 全局状态 ----
static void *g_playerController = nullptr;
static void *g_mainCharEntity = nullptr;
static void *g_charAnimator = nullptr;
static volatile bool g_charChanged = false; // hook 捕获新角色后置真，GUI 消费后复位

// 已解析的运行时方法指针（对应 {EIEM} globals.h 的 g_animator_*/g_transform_*）
static void *g_animator_GetBoneTransform = nullptr;
static void *g_animator_get_isHuman = nullptr;
static void *g_animator_get_enabled = nullptr;  // 来自 Behaviour.get_enabled
static void *g_animator_set_enabled = nullptr; // 来自 Behaviour.set_enabled
static void *g_transform_get_localRotation = nullptr;
static void *g_transform_set_localRotation = nullptr;
static void *g_transform_get_localPosition = nullptr;
static void *g_transform_set_localPosition = nullptr;
static void *g_transform_get_position = nullptr;
static void *g_transform_set_position = nullptr; // 世界平移（自由相机写）
static void *g_transform_get_rotation = nullptr; // 世界旋转（gizmo 相机朝向用）
static void *g_transform_set_rotation = nullptr; // 世界旋转（自由相机写）
static void *g_transform_get_childCount = nullptr;
static void *g_transform_GetChild = nullptr;
static void *g_transform_get_parent = nullptr;
static void *g_object_get_name = nullptr;
static void *g_component_get_transform = nullptr;
static void *g_component_get_gameObject = nullptr;
static void *g_componentClass = nullptr; // UnityEngine.Component（GetComponents(Type) 用）
static void *g_gameObjectClass = nullptr; // UnityEngine.GameObject
static void *g_gameObject_GetComponent = nullptr; // GameObject.GetComponent(Type)
static void *g_gameObject_GetComponents = nullptr; // GameObject.GetComponents(Type)
static void *g_cameraClass = nullptr;     // UnityEngine.Camera（get_main 用）
static void *g_camera_get_main = nullptr; // Camera.get_main（gizmo 取视锥）
static void *g_camera_get_fieldOfView = nullptr;
static void *g_camera_set_fieldOfView = nullptr; // Camera.set_fieldOfView（FOV 滑条）
static void *g_skinnedMeshRendererClass = nullptr; // UnityEngine.SkinnedMeshRenderer
static void *g_smr_get_sharedMesh = nullptr;        // get_sharedMesh
static void *g_smr_GetBlendShapeWeight = nullptr;   // GetBlendShapeWeight(int)
static void *g_smr_SetBlendShapeWeight = nullptr;   // SetBlendShapeWeight(int,float)
static void *g_mesh_get_blendShapeCount = nullptr;  // Mesh.get_blendShapeCount
static void *g_mesh_GetBlendShapeName = nullptr;    // Mesh.GetBlendShapeName(int)

// 动态解析的字段偏移（-1 = 未解析，读时走 SafeOff 回退）
static int OFF_pcEntity = -1;            // PlayerController -> Entity
static int OFF_entityComplexAnim = -1;   // Entity -> ComplexAnimationComponent
static int OFF_complexAnimAnimator = -1; // ComplexAnimationComponent -> Animator

// 运行时方法解析（在 IL2CPP Resolve() 之后调用）
static void ResolveGameApi() {
  __try {
    void *asms[8] = {};
    size_t ac = 0;
    void *domain = il2cpp_domain_get();
    if (domain) {
      void **buf = il2cpp_domain_get_assemblies(domain, &ac);
      for (size_t i = 0; i < ac && i < 8; i++)
        asms[i] = buf[i];
      if (ac > 8)
        ac = 8;
    }

    void *animClass = FindClass("UnityEngine", "Animator", asms, ac);
    if (animClass) {
      g_animator_GetBoneTransform = FindMethod(animClass, "GetBoneTransform", 1);
      g_animator_get_isHuman = FindMethod(animClass, "get_isHuman", 0);
      void *behClass = FindClass("UnityEngine", "Behaviour", asms, ac);
      if (behClass) {
        g_animator_get_enabled = FindMethod(behClass, "get_enabled", 0);
        g_animator_set_enabled = FindMethod(behClass, "set_enabled", 1);
      }
    }

    void *trClass = FindClass("UnityEngine", "Transform", asms, ac);
    if (trClass) {
      g_transform_get_localRotation =
          FindMethod(trClass, "get_localRotation", 0);
      g_transform_set_localRotation =
          FindMethod(trClass, "set_localRotation", 1);
      g_transform_get_localPosition =
          FindMethod(trClass, "get_localPosition", 0);
      g_transform_set_localPosition =
          FindMethod(trClass, "set_localPosition", 1);
      g_transform_get_position = FindMethod(trClass, "get_position", 0);
      g_transform_set_position = FindMethod(trClass, "set_position", 1);
      g_transform_get_rotation = FindMethod(trClass, "get_rotation", 0);
      g_transform_set_rotation = FindMethod(trClass, "set_rotation", 1);
      g_transform_get_childCount = FindMethod(trClass, "get_childCount", 0);
      g_transform_GetChild = FindMethod(trClass, "GetChild", 1);
      g_transform_get_parent = FindMethod(trClass, "get_parent", 0);
    }

    void *objClass = FindClass("UnityEngine", "Object", asms, ac);
    if (objClass)
      g_object_get_name = FindMethod(objClass, "get_name", 0);

    void *compClass = FindClass("UnityEngine", "Component", asms, ac);
    if (compClass) {
      g_componentClass = compClass;
      g_component_get_transform = FindMethod(compClass, "get_transform", 0);
      g_component_get_gameObject = FindMethod(compClass, "get_gameObject", 0);
    }

    void *goClass = FindClass("UnityEngine", "GameObject", asms, ac);
    if (goClass) {
      g_gameObjectClass = goClass;
      g_gameObject_GetComponent = FindMethod(goClass, "GetComponent", 1);
      g_gameObject_GetComponents = FindMethod(goClass, "GetComponents", 1);
    }

    void *camClass = FindClass("UnityEngine", "Camera", asms, ac);
    if (camClass) {
      g_cameraClass = camClass;
      g_camera_get_main = FindMethod(camClass, "get_main", 0);
      g_camera_get_fieldOfView = FindMethod(camClass, "get_fieldOfView", 0);
      g_camera_set_fieldOfView = FindMethod(camClass, "set_fieldOfView", 1);
    }

    // Task 4.1：面部/身体 BlendShape 读写
    void *smrClass = FindClass("UnityEngine", "SkinnedMeshRenderer", asms, ac);
    if (smrClass) {
      g_skinnedMeshRendererClass = smrClass;
      g_smr_get_sharedMesh = FindMethod(smrClass, "get_sharedMesh", 0);
      g_smr_GetBlendShapeWeight =
          FindMethod(smrClass, "GetBlendShapeWeight", 1);
      g_smr_SetBlendShapeWeight =
          FindMethod(smrClass, "SetBlendShapeWeight", 2);
    }
    void *meshClass = FindClass("UnityEngine", "Mesh", asms, ac);
    if (meshClass) {
      g_mesh_get_blendShapeCount = FindMethod(meshClass, "get_blendShapeCount", 0);
      g_mesh_GetBlendShapeName = FindMethod(meshClass, "GetBlendShapeName", 1);
    }

    Log("[POSER] Game API resolved: GetBoneTransform=%p set_enabled=%p "
        "SetLocalRot=%p SetLocalPos=%p GetChild=%p GetComponents=%p "
        "cam_main=%p cam_fov=%p smr_setBS=%p",
        g_animator_GetBoneTransform, g_animator_set_enabled,
        g_transform_set_localRotation, g_transform_set_localPosition,
        g_transform_GetChild, g_gameObject_GetComponents,
        g_camera_get_main, g_camera_get_fieldOfView, g_smr_SetBlendShapeWeight);
  } __except (1) {
    Log("[POSER] ResolveGameApi exception");
  }
}

// 从 Entity 解析两级字段偏移（Entity -> ComplexAnimComp -> Animator），懒加载
static void ResolveEntityOffsets(void *entity) {
  if (OFF_entityComplexAnim >= 0 && OFF_complexAnimAnimator >= 0)
    return;
  __try {
    void *entClass = il2cpp_object_get_class(entity);
    if (entClass && OFF_entityComplexAnim < 0) {
      const char *caNames[] = {"<animatorCom>k__BackingField", "animatorCom",
                               "complexAnimationComponent",
                               "m_complexAnimationComponent",
                               "_complexAnimationComponent"};
      const char *matched = nullptr;
      OFF_entityComplexAnim =
          FindFieldInHierarchy(entClass, caNames, 5, &matched);
      if (OFF_entityComplexAnim >= 0)
        Log("[POSER] Entity.%s=0x%X", matched, OFF_entityComplexAnim);
      else
        Log("[WARN] Entity complexAnim field unresolved, fallback 0x110");
    }
    int ecOff = SafeOff(OFF_entityComplexAnim, 0x110, "entityComplexAnim");
    void *cac = *(void **)((char *)entity + ecOff);
    if (cac && OFF_complexAnimAnimator < 0) {
      void *cacClass = il2cpp_object_get_class(cac);
      if (cacClass) {
        const char *animNames[] = {"animator", "m_animator", "_animator",
                                   "<animator>k__BackingField"};
        const char *matched2 = nullptr;
        OFF_complexAnimAnimator =
            FindFieldInHierarchy(cacClass, animNames, 4, &matched2);
        if (OFF_complexAnimAnimator >= 0)
          Log("[POSER] ComplexAnimComp.%s=0x%X", matched2,
              OFF_complexAnimAnimator);
        else
          Log("[WARN] ComplexAnimComp.animator unresolved, fallback 0x148");
      }
    }
  } __except (1) {
  }
}

// 设置当前角色 Entity → 提取 Animator 存入 g_charAnimator
static void SetCharacterEntity(void *entity) {
  if (!entity)
    return;
  ResolveEntityOffsets(entity);
  __try {
    int ecOff = SafeOff(OFF_entityComplexAnim, 0x110, "entityComplexAnim");
    int caOff =
        SafeOff(OFF_complexAnimAnimator, 0x148, "complexAnimAnimator");
    void *cac = *(void **)((char *)entity + ecOff);
    void *animator = cac ? *(void **)((char *)cac + caOff) : nullptr;
    if (animator && animator != g_charAnimator) {
      g_mainCharEntity = entity;
      g_charAnimator = animator;
      g_charChanged = true;
      Log("[POSER] CharAnimator=%p", g_charAnimator);
    }
  } __except (1) {
  }
}

// 插件加载时游戏可能已就绪：直接从 PlayerController 捞一次当前角色
static void TryCaptureFromPlayerController() {
  if (!g_playerController || OFF_pcEntity < 0)
    return;
  __try {
    void *entity = *(void **)((char *)g_playerController + OFF_pcEntity);
    if (entity)
      SetCharacterEntity(entity);
  } __except (1) {
  }
}

// 安装 PlayerController.SetMainCharacter hook（IL2CPP Resolve() 之后调用）
static void InstallSetMainCharacterHook() {
  __try {
    void *asms[8] = {};
    size_t ac = 0;
    void *domain = il2cpp_domain_get();
    if (domain) {
      void **buf = il2cpp_domain_get_assemblies(domain, &ac);
      for (size_t i = 0; i < ac && i < 8; i++)
        asms[i] = buf[i];
      if (ac > 8)
        ac = 8;
    }

    void *pcClass =
        FindClass("Beyond.Gameplay.Core", "PlayerController", asms, ac);
    if (!pcClass) {
      Log("[POSER] WARN: PlayerController class not found");
      return;
    }

    const char *entNames[] = {"mainCharacter", "m_entity", "_entity",
                              "m_mainCharacter", "entity",
                              "m_controlledEntity", "controlledEntity"};
    const char *matched = nullptr;
    OFF_pcEntity = FindFieldInHierarchy(pcClass, entNames, 7, &matched);
    if (OFF_pcEntity >= 0)
      Log("[POSER] PlayerController.%s=0x%X", matched, OFF_pcEntity);
    else
      Log("[WARN] PlayerController entity field unresolved, fallback 0x70");

    void *setMainChar = FindMethod(pcClass, "SetMainCharacter", 2);
    if (!setMainChar) {
      Log("[POSER] WARN: SetMainCharacter not found");
      return;
    }

    typedef void (*SetMainCharacter_t)(void *, void *, bool);
    static SetMainCharacter_t orig_SetMainCharacter = nullptr;

    struct SMHook {
      static void Hooked(void *self, void *entity, bool flag) {
        if (self && !g_playerController) {
          g_playerController = self;
          Log("[POSER] Captured PlayerController: %p", self);
        }
        if (entity)
          SetCharacterEntity(entity);
        if (orig_SetMainCharacter)
          orig_SetMainCharacter(self, entity, flag);
      }
    };

    if (Hook(setMainChar, "PlayerController.SetMainCharacter",
             (void *)SMHook::Hooked, (void **)&orig_SetMainCharacter)) {
      Log("[POSER] SetMainCharacter hooked");
      TryCaptureFromPlayerController(); // 尝试补捞当前已就绪角色
    }
  } __except (1) {
  }
}

// 总入口：在 IL2CPP Resolve() 成功后调用一次
static void InitGameHooks() {
  ResolveGameApi();
  InstallSetMainCharacterHook();
}

// ---- 骨骼句柄封装（供 FK/IK/快照/从骨层使用）----
static void *GetHumanoidBone(HumanBodyBones bone) {
  if (!g_charAnimator || !g_animator_GetBoneTransform)
    return nullptr;
  __try {
    int b = (int)bone;
    void *params[] = {&b};
    return Invoke(g_animator_GetBoneTransform, g_charAnimator, params);
  } __except (1) {
    return nullptr;
  }
}

// Component.get_transform（获取组件所在 GameObject 的 Transform）
static void *SafeGetComponentTransform(void *component) {
  if (!component || !g_component_get_transform)
    return nullptr;
  __try {
    return Invoke(g_component_get_transform, component);
  } __except (1) {
    return nullptr;
  }
}

// 角色根 Transform（Animator 组件所在 GameObject 的 Transform）
static void *GetCharRootTransform() {
  return g_charAnimator ? SafeGetComponentTransform(g_charAnimator) : nullptr;
}

static Quat GetBoneLocalRot(void *t) {
  Quat q{0, 0, 0, 1};
  if (!t || !g_transform_get_localRotation)
    return q;
  __try {
    void *boxed = Invoke(g_transform_get_localRotation, t);
    if (boxed)
      q = *(Quat *)((char *)boxed + 16); // IL2CPP 盒对象数据区偏移 16
  } __except (1) {
  }
  return q;
}

static void SetBoneLocalRot(void *t, Quat q) {
  if (!t || !g_transform_set_localRotation)
    return;
  __try {
    void *params[] = {&q};
    Invoke(g_transform_set_localRotation, t, params);
  } __except (1) {
  }
}

static Vec3 GetBoneLocalPos(void *t) {
  Vec3 p{0, 0, 0};
  if (!t || !g_transform_get_localPosition)
    return p;
  __try {
    void *boxed = Invoke(g_transform_get_localPosition, t);
    if (boxed)
      p = *(Vec3 *)((char *)boxed + 16);
  } __except (1) {
  }
  return p;
}

static void SetBoneLocalPos(void *t, Vec3 p) {
  if (!t || !g_transform_set_localPosition)
    return;
  __try {
    void *params[] = {&p};
    Invoke(g_transform_set_localPosition, t, params);
  } __except (1) {
  }
}

// 取骨骼名称（用于日志/面板显示）
static void GetBoneName(void *transform, char *buf, int sz) {
  buf[0] = 0;
  if (!transform || !g_object_get_name || sz <= 0)
    return;
  __try {
    void *nameStr = Invoke(g_object_get_name, transform);
    if (nameStr)
      ReadStrUtf8(nameStr, buf, sz);
  } __except (1) {
  }
}

// ---- 世界空间位姿（IK 求解 / gizmo 定位用）----
static Vec3 GetBoneWorldPos(void *t) {
  Vec3 p{0, 0, 0};
  if (!t || !g_transform_get_position)
    return p;
  __try {
    void *boxed = Invoke(g_transform_get_position, t);
    if (boxed)
      p = *(Vec3 *)((char *)boxed + 16);
  } __except (1) {
  }
  return p;
}

static Quat GetBoneWorldRot(void *t) {
  Quat q{0, 0, 0, 1};
  if (!t || !g_transform_get_rotation)
    return q;
  __try {
    void *boxed = Invoke(g_transform_get_rotation, t);
    if (boxed)
      q = *(Quat *)((char *)boxed + 16);
  } __except (1) {
  }
  return q;
}

// ---- 世界空间写（自由相机/IK 目标移动用）----
static void SetBoneWorldPos(void *t, Vec3 p) {
  if (!t || !g_transform_set_position)
    return;
  __try {
    void *params[] = {&p};
    Invoke(g_transform_set_position, t, params);
  } __except (1) {
  }
}

static void SetBoneWorldRot(void *t, Quat q) {
  if (!t || !g_transform_set_rotation)
    return;
  __try {
    void *params[] = {&q};
    Invoke(g_transform_set_rotation, t, params);
  } __except (1) {
  }
}
