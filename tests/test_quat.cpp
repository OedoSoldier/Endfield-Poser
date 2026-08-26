#include "math/quat_math.h"
#include <cmath>
#include <cstdio>

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { fails++; std::printf("FAIL: %s\n", msg); } } while (0)

int main() {
    Quat q(0.0f, 0.0f, 0.0f, 1.0f);            // identity
    Quat q90 = Quat::AxisAngle({0,1,0}, 1.5707963f); // 90° around Y
    Vec3 fwd = q90 * Vec3(0,0,1);               // 前向(0,0,1) 绕Y+90° → (1,0,0)
    CHECK(fabsf(fwd.x - 1.0f) < 1e-4f && fabsf(fwd.z) < 1e-4f, "axis-angle rotate forward");

    Quat a = Quat::AxisAngle({1,0,0}, 0.5f);
    Quat b = Quat::AxisAngle({1,0,0}, 1.5f);
    Quat mid = Quat::Slerp(a, b, 0.5f);
    Vec3 va = a * Vec3(0,1,0), vb = b * Vec3(0,1,0), vm = mid * Vec3(0,1,0);
    CHECK((vm.y - va.y) * (vm.y - vb.y) < 0.0f, "slerp stays between endpoints");

    Quat d = Quat::Delta(a, b);                 // b = d * a
    Vec3 vd = d * va;
    CHECK(fabsf(vd.x - vb.x) < 1e-3f && fabsf(vd.y - vb.y) < 1e-3f, "delta composition");

    Vec3 e = q90.ToEulerDeg();
    CHECK(fabsf(e.y - 90.0f) < 0.5f, "euler round trip");
    Quat back = Quat::FromEulerDeg(e);
    CHECK(fabsf(Quat::Angle(q90, back)) < 1e-2f, "euler->quat round trip");

    std::printf(fails ? "%d FAILURES\n" : "quat_math OK\n", fails);
    return fails ? 1 : 0;
}
