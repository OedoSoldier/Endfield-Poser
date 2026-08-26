#include "math/ik_two_bone.h"
#include <cmath>
#include <cstdio>
static int fails=0;
#define CHECK(c,m) do{ if(!(c)){fails++;std::printf("FAIL: %s\n",m);} }while(0)

int main(){
    // 上臂/前臂各 0.5，初始平伸 (1,0,0)；目标 (0.7,0.6,0) 距原点 0.92 < 1.0，可解
    Vec3 a{0,0,0}, b{0.5f,0,0}, c{1.0f,0,0};
    Vec3 target{0.7f, 0.6f, 0.0f};
    SolveTwoBone(a,b,c,target,{0,0,1}, true);
    float err = Len(c - target);
    CHECK(err < 1e-3f, "end effector reaches target");
    CHECK(b.z > 0.0f, "elbow bends toward pole side (+Z)");

    // 不可达目标（3.0 远超臂长 1.0）→ 应完全伸直朝向目标
    Vec3 far{3.0f,0,0};
    SolveTwoBone(a,b,c,far,{0,0,1}, true);
    CHECK(fabsf(Len(c) - 1.0f) < 1e-2f, "unreachable: fully extended (len==lab+lbc)");
    CHECK(c.x > 0.99f, "unreachable: pointing toward target");

    // pole 与 target 平行（退化）→ 不应崩溃，仍能命中目标
    Vec3 b2{0.5f,0,0}, c2{1.0f,0,0};
    SolveTwoBone(a,b2,c2,{1.0f,0,0},{0,0,1}, true);
    CHECK(Len(c2 - Vec3{1.0f,0,0}) < 1e-3f, "degenerate pole: still reaches target");

    std::printf(fails?"%d FAILURES\n":"ik_two_bone OK\n", fails);
    return fails?1:0;
}
