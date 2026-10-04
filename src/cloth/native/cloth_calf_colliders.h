#pragma once
#include "../collision/cloth_calf_math.h"
// Separate owned capsules keep native Team counts of authored shapes unchanged.
struct ClothCalfShape {
  ClothRef go{},collider{},transform{},parent{};
  Vector3 position{},size{};Quaternion rotation{0,0,0,1};bool destroyed=false;
};
using ClothCalfShapes=std::vector<ClothCalfShape>;
static bool ClothCalfUnder(void *child,void *root) {
  if(!child||!root)return false;void *box=nullptr,*args[]{root};
  return child==root||(ClothInvoke(ClothMethod(g_transformClass,"IsChildOf","System.Boolean","UnityEngine.Transform"),child,args,box)&&box&&ClothUnboxBool(box));
}
static bool ClothCalfRelease(ClothCalfShapes &shapes,int team) {
  for(auto &s:shapes){void *c=nullptr;const auto life=ClothInspect(s.collider,c);
    if(s.collider.handle&&life==ClothLife::Unreadable)return false;
    if(life==ClothLife::Alive){bool member=false;int count=-1;if(!CollisionTeams(c,team,member,count)||member||count)return false;}}
  for(auto &s:shapes){auto go=ClothTarget(s.go);void *unused=nullptr,*args[]{go};
    if(go&&!s.destroyed&&!ClothInvoke(ClothMethod(SurfaceClass("UnityEngine","Object"),"Destroy","System.Void","UnityEngine.Object",true),nullptr,args,unused))return false;
    s.destroyed=true;}
  for(auto &s:shapes){ClothFree(s.go);ClothFree(s.collider);ClothFree(s.transform);ClothFree(s.parent);}shapes.clear();return true;
}
static bool ClothCalfRegistration(const ClothCalfShapes &shapes,void *process,int team,bool required) {
  for(const auto &s:shapes){void *c=nullptr;const auto life=ClothInspect(s.collider,c);if(life==ClothLife::Destroyed&&!required)continue;
    bool member=false,listed=false;int count=-1;
    if(life!=ClothLife::Alive||!CollisionTeams(c,team,member,count)||member!=required||
        !CollisionProcessContains(process,c,listed)||listed!=required)return false;
    if(required){Vector3 p{},scale{};Quaternion q{};const auto t=ClothTarget(s.transform),parent=ClothTarget(s.parent);
      if(count!=1||!t||!parent||CollisionParent(t)!=parent||!ClothAnchorUnderOwner(parent)||
          !SurfaceVisiblePose(t,p,q,scale)||!ClothSameLocal(p,q,s.position,s.rotation)||
          !SurfaceVisibleSame(scale,Vector3{1,1,1}))return false;}}
  return true;
}
static bool ClothCalfHas(const std::vector<void*> &list,void *calf,void *foot,Vector3 knee,Vector3 ankle,void **match=nullptr) {
  for(auto c:list){auto t=CollisionTransform(c);if(!ClothCalfUnder(t,calf)||ClothCalfUnder(t,foot))continue;
    const auto g=CollisionReadGeometry(c);
    if(g.valid&&g.active&&g.enabled&&g.uniform&&eiem_collision::CalfSpan(g.world,CollisionV(knee),CollisionV(ankle))){if(match)*match=c;return true;}}
  return false;
}
static bool ClothCalfCreate(ClothCalfShapes &shapes,void *calf,void *foot,void *reference) {
  Vector3 footLocal{},position{},size{},scale{};Quaternion rotation{0,0,0,1};
  void *parent=calf;
  if(reference){const auto g=CollisionReadGeometry(reference);auto t=CollisionTransform(reference);
    // Only clone verified capsules, preserving their axis and tapered sizes.
    if(strcmp(g.type,"BeyondBoneCapsuleCollider")||!g.valid||!g.flagsKnown)return false;
    parent=CollisionParent(t);if(!parent||!SurfaceVisiblePose(t,position,rotation,scale)||!SurfaceVisibleSame(scale,Vector3{1,1,1}))return false;
    size=g.size;
  }else{
    if(CollisionParent(foot)!=calf||!ClothValue(g_transform_get_localPosition,foot,footLocal))return false;
    const auto fit=eiem_collision::CalfEnvelope(CollisionV(footLocal));if(!fit.valid)return false;
    position=CollisionV(fit.a);size={float(fit.ra),float(fit.rb),float(eiem_collision::Length(fit.b-fit.a)+fit.ra+fit.rb)};
    const auto q=Quat::FromTo(Vec3{-1,0,0},Vec3{footLocal.x,footLocal.y,footLocal.z});rotation={q.x,q.y,q.z,q.w};
  }
  if(!CollisionScale(parent,scale)||!eiem_collision::UniformPositive(CollisionV(scale))||!CollisionUniformFrame(parent,scale.x))return false;
  auto cls=SurfaceClass("BeyondDynamicBone","BeyondBoneCapsuleCollider");
  auto ctor=ClothMethod(g_gameObjectClass,".ctor","System.Void","System.String"),add=ClothMethod(g_gameObjectClass,"AddComponent","UnityEngine.Component","System.Type");
  auto update=ClothMethod(cls,"UpdateParameters","System.Void");if(!cls||!ctor||!add||!update)return false;
  void *go=il2cpp_object_new(g_gameObjectClass),*label=il2cpp_string_new("Poser_CalfContact"),*unused=nullptr,*args[]{label};
  if(!go||!label)return false;const auto hold=il2cpp_gchandle_new(go,false),text=il2cpp_gchandle_new(label,false);
  if(!hold||!text){if(hold)il2cpp_gchandle_free(hold);if(text)il2cpp_gchandle_free(text);return false;}
  const bool made=ClothInvoke(ctor,go,args,unused);shapes.emplace_back();auto &s=shapes.back();s.go=ClothProtect(go);
  if(made&&!s.go.handle){void *destroy[]{go};ClothInvoke(ClothMethod(SurfaceClass("UnityEngine","Object"),"Destroy","System.Void","UnityEngine.Object",true),nullptr,destroy,unused);}
  il2cpp_gchandle_free(hold);il2cpp_gchandle_free(text);
  auto t=CollisionTransform(go);s.transform=ClothProtect(t);s.parent=ClothProtect(parent);s.position=position;s.rotation=rotation;s.size=size;
  if(!made||!s.go.handle||!s.transform.handle||!s.parent.handle||!SurfaceTRS(t,parent,position,rotation,Vector3{1,1,1}))return false;
  void *type=il2cpp_type_get_object(il2cpp_class_get_type(cls)),*component=nullptr,*addArgs[]{type};
  if(!type||!ClothInvoke(add,go,addArgs,component)||!component)return false;s.collider=ClothProtect(component);
  if(!s.collider.handle||!SurfaceScalar(component,"size","UnityEngine.Vector3",size))return false;
  if(reference){const auto g=CollisionReadGeometry(reference);int direction=0;
    if(!ClothField(reference,"direction","BeyondDynamicBone.BeyondBoneCapsuleCollider.Direction",direction)||
        !SurfaceScalar(component,"direction","BeyondDynamicBone.BeyondBoneCapsuleCollider.Direction",direction)||
        !SurfaceScalar(component,"center","UnityEngine.Vector3",g.center)||
        !SurfaceScalar(component,"reverseDirection","System.Boolean",g.reverse)||!SurfaceScalar(component,"alignedOnCenter","System.Boolean",g.centered)||
        !SurfaceScalar(component,"radiusSeparation","System.Boolean",g.separated))return false;
  }else if(!SurfaceEnum(component,"direction","BeyondDynamicBone.BeyondBoneCapsuleCollider.Direction","X")||
      !SurfaceScalar(component,"center","UnityEngine.Vector3",Vector3{})||!SurfaceScalar(component,"reverseDirection","System.Boolean",false)||
      !SurfaceScalar(component,"alignedOnCenter","System.Boolean",false)||!SurfaceScalar(component,"radiusSeparation","System.Boolean",true))return false;
  if(!ClothInvoke(update,component,nullptr,unused))return false;const auto g=CollisionReadGeometry(component);
  return g.valid&&g.active&&g.enabled&&g.uniform&&SurfaceVisibleSame(g.size,size);
}
static bool ClothCalfPrepare(ClothCalfShapes &shapes,std::vector<void*> &colliders,const char *name) {
  if(!shapes.empty())return false;auto animator=ClothTarget(s_cloth.animator);if(!animator)return false;
  int retained=0,reused=0,unavailable=0;
  for(int side=0;side<2;++side){auto calf=CollisionBody(animator,side?10:7),foot=CollisionBody(animator,side?11:8);Vector3 knee{},ankle{};
    if(!calf||!foot||CollisionParent(foot)!=calf||!ClothValue(g_transform_get_position,calf,knee)||!ClothValue(g_transform_get_position,foot,ankle)) {++unavailable;continue;}
    if(ClothCalfHas(colliders,calf,foot,knee,ankle)){++retained;continue;}
    void *reference=nullptr;
    for(int n=0;n<s_cloth.count&&!reference;++n){auto &i=s_cloth.instances[n];void *data=nullptr,*constraint=nullptr,*list=nullptr;auto bbc=ClothTarget(i.ref);
      if(!bbc||!ClothInvoke(i.api.serialize,bbc,nullptr,data)||!CollisionList(data,constraint,list))continue;
      const int count=CollisionCount(list);if(count<0||count>64)continue;std::vector<void*> pool;
      for(int k=0;k<count;++k)if(auto c=CollisionItem(list,k,"BeyondDynamicBone.ColliderComponent"))pool.push_back(c);
      ClothCalfHas(pool,calf,foot,knee,ankle,&reference);
    }
    if(!ClothCalfCreate(shapes,calf,foot,reference))return false;
    colliders.push_back(ClothTarget(shapes.back().collider));reused+=reference!=nullptr;
  }
  Log("[CLOTH-CALF] component=%s retained=%d copiedAuthored=%d generated=%zu unavailable=%d liveMeshReads=0 registration=pending",name,retained,reused,shapes.size()-reused,unavailable);
  return true;
}
