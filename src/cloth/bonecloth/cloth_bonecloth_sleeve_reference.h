#pragma once
// PMX-style topology. The sleeve policy applies a BBC approximation of PMX
// free joints and damping; rigid-body mass and axial twist are not converted.
constexpr int ClothSleeveSourceCount=7;
constexpr int ClothSleeveParticles=30,ClothSleeveFixed=2;
constexpr int ClothSleeveLinks=ClothSleeveParticles-1;
struct ClothSleeveNativeAngle {
  bool restore;unsigned char pad[3];float stiffness[16],velocity,gravity;
  bool limit;unsigned char pad2[3];float limitCurve[16],limitStiffness;
};
struct ClothSleeveNativeTether {float compression,stretch;};
struct ClothSleeveNativeInertia {
  float anchorInertia,worldInertia,movementInertiaSmoothing,movementSpeedLimit,rotationSpeedLimit;
  float localInertia,localMovementSpeedLimit,localRotationSpeedLimit,depthInertia,centrifualAcceleration,particleSpeedLimit;
  int teleportMode;float teleportDistance,teleportRotation;
};
struct ClothSleeveMaterial {
  float gravity=0,falloff=0,damping[16]{},distance[16]{},attenuation=0;
  struct {float x=0,y=0,z=0;} gravityDirection;
  ClothSleeveNativeAngle angle{};ClothSleeveNativeTether tether{};
  ClothSleeveNativeInertia inertia{};
  struct {int method=0;float stiffness=0;} bending;
};
