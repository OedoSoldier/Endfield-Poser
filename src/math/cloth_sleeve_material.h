#pragma once
#include <cmath>

namespace cloth_sleeve_material {
// Si PMX: 30 bodies, two locked joints, remaining X/Z +/-5 degrees,
// no joint springs. BBC has a circular limit rather than a per-axis box.
// Match the box's diagonal envelope, not an identical twist constraint.
inline float LimitDegrees() {
  const double a=5.0*3.14159265358979323846/180.;
  return float(std::acos(std::cos(a)*std::cos(a))*180./3.14159265358979323846);
}
// Restore the first thirty-point material (0.5.88-0.5.90), retaining later
// safety/geometry fixes. This is a BBC preset, not a Bullet mass conversion.
// Project excessive bends to the legal boundary; legal bends are unchanged.
// Keep the corrected diagonal envelope above instead of the old 5-degree
// circle, which incorrectly rejected legal PMX X/Z combinations.
inline constexpr float LimitStiffness() {return 1.f;}
// Authored .5 becomes native .1 (the converter multiplies by .2).
inline constexpr float DampingScalar() {return .5f;}
inline constexpr float Gravity() {return 4.f;}
// Orientation affects strength, never the world-space downward direction.
// Native gravityRatio ranges from 1-falloff to 1: magnitude 2.6..4 here.
inline constexpr float GravityFalloff() {return .35f;}
// Long free chains need some coherent transport with their moving mount.
// Native inertia=1 leaves particles in world space; 0 transports the current
// bent shape with the mount. Cap at .6 (at least 40% carry), retaining native
// speed/teleport safeguards and weaker source inertia. This moves the current
// simulated shape; it never pulls particles back to the authored straight pose.
inline constexpr float MaximumInertia() {return .6f;}
}
