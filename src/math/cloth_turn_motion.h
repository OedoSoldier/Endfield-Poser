#pragma once
#include "mmd_secondary_motion.h"
#include <string>

namespace cloth_turn {
using mmd_secondary::Finite;
using mmd_secondary::Limited;
enum class Part { None, Cloth, Hair, Tail, Ear, Accessory };
inline bool HeadPart(Part p){return p==Part::Hair||p==Part::Ear;}
inline Part Classify(std::string name) {
  for(char &c:name)if(c>='A'&&c<='Z')c+=char('a'-'A');
  for(const char *key:{"breast","bust","weapon","sword","ultmachine"})
    if(name.find(key)!=name.npos)return Part::None;
  // Ponytails are hair; test this before the animal-tail rule.
  if(name.find("hair")!=name.npos||name.find("bang")!=name.npos)return Part::Hair;
  if(name.find("tail")!=name.npos)return Part::Tail;
  if(name.find("ear")!=name.npos)return Part::Ear;
  for(const char *key:{"coat","cloak","cape","skirt","dress","ribbon","robbin","sleeve","cloth","scarf","apron","tie","rope","strap"})
    if(name.find(key)!=name.npos)return Part::Cloth;
  for(const char *key:{"bag","lantern","pendant","props","acc_","hat","wing","armband"})
    if(name.find(key)!=name.npos)return Part::Accessory;
  return Part::None; // Only already simulated, recognized flexible parts.
}
struct Pose {Vec3 position;Quat rotation;};
// Fixed-size, body-local sample of existing BBC roots/children. Captured once,
// never fed back from the simulated chain on subsequent frames. No mesh data.
struct Shape {
  Vec3 points[8]{},up{0,1,0};unsigned count=0;
  bool add(Vec3 world,const Pose &reference) {
    if(count==8||!Finite(world)||!Finite(reference.position)||!Finite(reference.rotation))return false;
    Vec3 local=Conj(NormQ(reference.rotation))*(world-reference.position);
    if(Len(local)>2.5f)return false;
    if(!count)up=Conj(NormQ(reference.rotation))*Vec3{0,1,0};
    points[count++]=local;return true;
  }
  Vec3 lever(Quat rotation,Vec3 fallback) const {
    if(!count)return Limited(fallback,.8f);
    Vec3 mean,strongest;float peak=0;
    // Use the actual chain extent instead of its fixed-root inertia center.
    // A symmetric set uses one stable representative branch. Select in the
    // captured body frame, never from live angular velocity: changing the turn
    // axis must not flip between opposite branches and excite ribbon jitter.
    for(unsigned i=0;i<count;++i) {
      Vec3 r=points[i];mean=mean+r;
      Vec3 radial=r-up*Dot(r,up);float radius2=Dot(radial,radial);
      if(radius2>peak){peak=radius2;strongest=r;}
    }
    mean=mean*(1.f/count);
    Vec3 radial=mean-up*Dot(mean,up);
    if(Dot(radial,radial)<peak*.16f)mean=strongest;
    return rotation*Limited(mean,1.2f);
  }
};
struct Motion {
  Pose pose;Vec3 acceleration,omega,alpha,velocity;
  float dt=0;
  Vec3 airResponse(Vec3 center,float lightness,Part part) const {
    if(!std::isfinite(lightness)||lightness<=0||!Finite(velocity)||!Finite(omega))return {};
    const float l=std::clamp(lightness,0.f,1.f);
    const float extra=1.f/(1.f-.75f*l)-1.f;
    Vec3 flow=velocity+Cross(omega,Limited(center-pose.position,1.2f));
    // This is an artistic moving-air response, not a BBC mass parameter.
    // Horizontal flow/turning creates the existing upward flutter. Vertical
    // flow instead opposes ascent/descent: it must not turn both directions
    // into upward lift or add a force to a stationary pose.
    const float vertical=std::clamp(flow.y,-6.f,6.f);
    const float response=part==Part::Ear?.25f:part==Part::Accessory?.35f:1.f;
    const float verticalDrag=std::clamp(-.20f*vertical*std::fabs(vertical)*extra,-6.f,6.f)*response;
    flow=Limited({flow.x,0,flow.z},6.f);
    const float radius=HeadPart(part)?.18f:part==Part::Tail?.35f:.30f;
    // A finite patch still sees moving air during a turn when its component
    // center lies on the rotation axis. No game mesh is needed for this proxy.
    const float pressure=(std::min)(12.f,Dot(flow,flow)+.5f*radius*radius*Dot(omega,omega));
    const Vec3 drag=Limited(flow*(-.06f*Len(flow)*extra),2.f);
    const float lift=(std::min)(6.f,2.f*pressure*extra)*(part==Part::Ear?.25f:part==Part::Accessory?.35f:1.f);
    return drag+Vec3{0,lift+verticalDrag,0};
  }
  Vec3 impulse(Vec3 center,float strength,float lightness=0,Part part=Part::Cloth,const Shape *shape=nullptr) const {
    if(dt<=0||!Finite(center)||!std::isfinite(strength))return {};
    const Vec3 radius=shape?shape->lever(pose.rotation,center-pose.position):Limited(center-pose.position,.8f);
    const Vec3 tangent=Cross(omega,radius);
    // Supplemental inertial lag and outward force. BBC retains its own
    // gravity, collision, constraints and depth-weighted particle response.
    const float response=part==Part::Ear?.3f:part==Part::Accessory?.4f:1.f;
    // Vertical body motion needs its own response: the former uniform 0.35
    // gain made takeoff/apex/landing weak even with lightness disabled. Keep
    // horizontal/turn tuning, native gravity and the final force bound intact.
    const Vec3 linear=acceleration*(-.35f)+Vec3{0,-.40f*acceleration.y,0};
    const Vec3 a=linear+Cross(alpha,radius)*(-.65f)
      +Cross(omega,tangent)*(-.65f)+tangent*(-.25f*Len(omega));
    // Hair has an independent gain covering both inertia and moving-air
    // response. Zero disables our extra force, leaving native physics alone.
    const float gain=std::clamp(strength,0.f,part==Part::Hair?3.f:2.f);
    return Limited(a*(response*gain)+airResponse(pose.position+radius,lightness,part)*(part==Part::Hair?gain:1.f),12.f)*dt;
  }
};
struct Tracker {
  Pose previous;Motion value;
  Vec3 velocity,omega;
  double clock=0,cursor=0;uint64_t epoch=0;
  bool ready=false,velocityReady=false,playing=false;
  void reset(const Pose &p,double now,double time,uint64_t revision,bool running) {
    previous=p;clock=now;cursor=time;epoch=revision;ready=true;playing=running;
    velocityReady=false;velocity=omega={};value={};value.pose=p;
  }
  Motion step(Pose p,double now,double time,uint64_t revision,bool running) {
    if(!Finite(p.position)||!Finite(p.rotation)||!std::isfinite(now)||!std::isfinite(time)) {
      *this={};return {};
    }
    p.rotation=NormQ(p.rotation);const double dt=now-clock;
    if(!ready||!running||!playing||revision!=epoch||time<cursor-1e-5||dt<0||dt>.12||
       Len(p.position-previous.position)>1.0f||Quat::Angle(p.rotation,previous.rotation)>1.25f) {
      reset(p,now,time,revision,running);return value;
    }
    // An already sampled pose never adds another impulse. Native physics may
    // run at a different frequency: the mailbox consumes each result once.
    if(dt<1e-5||time==cursor)return {};
    const Vec3 v=(p.position-previous.position)*float(1/dt);
    Quat dq=NormQ(p.rotation*Conj(previous.rotation));
    if(dq.w<0)dq={-dq.x,-dq.y,-dq.z,-dq.w};
    Vec3 xyz{dq.x,dq.y,dq.z};const float length=Len(xyz);
    const Vec3 w=Limited(length>1e-7f?xyz*float(2*std::atan2(length,dq.w)/(length*dt)):Vec3{},12.f);
    const float filter=1-std::exp(-float(dt)/.05f);
    const Vec3 a=velocityReady?Limited((v-velocity)*float(1/dt),50.f):Vec3{};
    const Vec3 aa=velocityReady?Limited((w-omega)*float(1/dt),80.f):Vec3{};
    value.acceleration=value.acceleration+(a-value.acceleration)*filter;
    value.alpha=value.alpha+(aa-value.alpha)*filter;
    value.omega=value.omega+(w-value.omega)*filter;
    value.velocity=value.velocity+(Limited(v,12.f)-value.velocity)*filter;
    value.pose=p;value.dt=float(dt);
    previous=p;velocity=v;omega=w;clock=now;cursor=time;velocityReady=true;
    return value;
  }
};
// Bounded queue: average acceleration across body samples. The game's BBC
// solver integrates impactForce with its own simulationDeltaTime; passing a
// pre-integrated velocity increment would multiply by delta time twice.
struct Mailbox {
  struct Frame {Motion body,head,hips;double time=0;};
  Frame frames[8]{};unsigned count=0;
  void clear(){count=0;}
  void push(Motion body,Motion head,double now,Motion hips={}) {
    if(body.dt<=0&&head.dt<=0&&hips.dt<=0){clear();return;}
    if(count==8)clear();
    frames[count++]={body,head,hips,now};
  }
  static const Motion &select(const Frame &f,Part part) {
    return HeadPart(part)?f.head:part==Part::Tail?f.hips:f.body;
  }
  Vec3 impulse(Part part,Vec3 center,float strength,double now,float lightness=0,const Shape *shape=nullptr) const {
    Vec3 total;
    for(unsigned i=0;i<count;++i)if(now>=frames[i].time&&now-frames[i].time<=.12)
      total=total+select(frames[i],part).impulse(center,strength,lightness,part,shape);
    return Limited(total,.6f);
  }
  Vec3 force(Part part,Vec3 center,float strength,double now,float lightness=0,const Shape *shape=nullptr) const {
    Vec3 integrated;double duration=0;
    for(unsigned i=0;i<count;++i) {
      const auto &m=select(frames[i],part);
      if(now<frames[i].time||now-frames[i].time>.12||!std::isfinite(m.dt)||m.dt<=0||m.dt>.12f)continue;
      integrated=integrated+m.impulse(center,strength,lightness,part,shape);
      duration+=m.dt;
    }
    // Do not use the capped impulse() result: its burst guard would make the
    // acceleration depend on render/simulation frequency when batching.
    return duration>1e-6?Limited(integrated*float(1/duration),12.f):Vec3{};
  }
};
}
