#pragma once
namespace eiem_cloth_asset {
inline bool JsspsiGroupSource(Scene &s){return s.file.sha==JsspsiClothPrefab;}
inline bool JsspsiGroupComponent(const std::string &name){return name=="MC_jsspsi_Skirt"||JsspsiSleeveName(name.c_str());}
struct SharedSkinPart {
  std::shared_ptr<eiem_cloth_cache::Profile> profile;
  std::shared_ptr<DenseRecipe> recipe;
  std::vector<int64_t> ids;
  std::vector<MeshView> meshes;
};
inline void JsspsiSleeveContacts(Package &package,Scene &s,SharedSkinPart &part,bool left){
  auto &d=*part.recipe;int64_t renderer=0;
  for(const auto &o:s.file.objects)if(s.file.Class(o.first)==137&&s.Name(o.first)=="S_actor_jsspsi_body_01_lod0"){Need(!renderer,"si-body-renderer-ambiguous");renderer=o.first;}
  Need(renderer!=0,"si-body-renderer-missing");const auto m=ReadMesh(package,s,renderer);
  d.bodyAsset={d.String(m.name),d.String(m.mesh),d.String(m.root),m.vertices,m.submeshes,nullptr,0,d.String(m.parent)};
  for(size_t n=0;n<m.bones.size();++n){ClothBoneBinding b{d.String(s.Name(m.bones[n])),d.String(s.Name(s.Parent(m.bones[n]))),-1,{}};for(int k=0;k<16;++k)b.bind[k]=float(m.binds[n][k]);d.bodyBindings.push_back(b);}
  // Either sleeve can cross the other arm. Both arms must participate in each
  // private receiver; sharing only the same-side arm leaves cross-body poses open.
  for(bool armLeft:{left,!left}) {
  const std::string prefix=armLeft?"Bip001_L_":"Bip001_R_";
  for(int segment=0;segment<2;++segment){const auto name=prefix+(segment?"Forearm":"UpperArm"),endName=prefix+(segment?"Hand":"Forearm");int bone=-1;int64_t end=0;
    for(size_t n=0;n<m.bones.size();++n)if(s.Name(m.bones[n])==name){Need(bone<0,"si-arm-palette-alias");bone=int(n);}
    for(const auto &t:s.goTransform)if(s.Name(t.second)==endName){Need(!end,"si-arm-end-alias");end=t.second;}
    Need(bone>=0&&end&&Above(s,end,m.bones[bone]),"si-arm-body-reference");const auto inverse=Inverse(s.World(m.bones[bone]));const auto endpoint=Transform(inverse,Transform(s.World(end),{}));
    const double length=std::sqrt(Dot(endpoint,endpoint));Need(length>.1&&length<.5&&std::hypot(endpoint[1],endpoint[2])<.001,"si-arm-axis-changed");
    std::vector<double> radii;for(size_t v=0;v<m.world.size();++v){double sum=0,own=0;for(size_t k=0;k<m.weights[v].size();++k){sum+=m.weights[v][k];if(int(m.indices[v][k])==bone)own+=m.weights[v][k];}if(sum<=0||own/sum<.65)continue;
      auto point=m.world[v];for(auto &x:point)x/=sum;point=Transform(inverse,point);const double along=point[0]/endpoint[0];if(along>.15&&along<.85)radii.push_back(std::hypot(point[1],point[2]));}
    Need(radii.size()>=8,"si-arm-fit-insufficient-skin");std::sort(radii.begin(),radii.end());const double radius=(std::max)(.018,(std::min)(.07,radii[size_t(.85*(radii.size()-1))]));
    ClothBoneBodySphere shape{};shape.bone=bone;shape.radius=float(radius);shape.endRadius=float(radius*.82);shape.length=float(length*.85+radius+shape.endRadius);
    shape.center={float(endpoint[0]*.08),0,0};if(endpoint[0]>0)shape.rotation={0,1,0,0};d.bodySpheres.push_back(shape);
  }
  }
  // Borrow only the inner authored body envelope, never sleeve/skirt shells.
  // Private copies follow the same body bones and retain exact capsule volume.
  // These long sleeves reach below the hips. Calves alone leave both thighs
  // unprotected during crossed-arm and crouching poses.
  for(const char *name:{"Bip001_Spine","Bip001_Spine1","Bip001_Spine2","Bip001_L_Thigh","Bip001_R_Thigh"}) {
    int bone=-1;for(size_t n=0;n<m.bones.size();++n)if(s.Name(m.bones[n])==name){Need(bone<0,"si-torso-bone-alias");bone=int(n);}
    bool parentFrame=false;
    if(bone<0&&(std::string(name)=="Bip001_L_Thigh"||std::string(name)=="Bip001_R_Thigh")) {
      const auto child=std::string(name)=="Bip001_L_Thigh"?"Bip001_LThighTwist":"Bip001_RThighTwist";
      for(size_t n=0;n<m.bones.size();++n)if(s.Name(m.bones[n])==child&&s.Name(s.Parent(m.bones[n]))==name){Need(bone<0,"si-thigh-parent-alias");bone=int(n);parentFrame=true;}
    }
    Need(bone>=0,"si-torso-bone-missing");const auto frame=parentFrame?s.Parent(m.bones[bone]):m.bones[bone];int64_t collider=0;
    for(const auto &o:s.file.objects)if(s.file.Class(o.first)==114&&s.file.Get(o.first).Has("direction")&&
        s.Parent(s.TransformId(o.first))==frame){Need(!collider,"si-torso-collider-alias");collider=o.first;}
    Need(collider!=0,"si-torso-collider-missing");const auto &v=s.file.Get(collider);
    const auto size=Vec(v.At("size")),center=Vec(v.At("center"));Point axis{};const int direction=int(v.At("direction").Int());Need(direction>=0&&direction<=2,"si-torso-direction");axis[direction]=1;
    auto local=eiem_collision::LocalCapsule({center[0],center[1],center[2]},{axis[0],axis[1],axis[2]},
        {size[0],size[1],size[2]},v.At("reverseDirection").Int()!=0,v.At("radiusSeparation").Int()!=0,v.At("alignedOnCenter").Int()!=0);
    Need(local.valid,"si-torso-capsule-invalid");const auto relative=Mul(Inverse(s.World(frame)),s.World(s.TransformId(collider)));
    QuaternionOf(relative);auto a=Transform(relative,{local.a.x,local.a.y,local.a.z}),b=Transform(relative,{local.b.x,local.b.y,local.b.z});
    if(local.ra<local.rb){std::swap(a,b);std::swap(local.ra,local.rb);}
    auto x=Sub(a,b);const double span=std::sqrt(Dot(x,x));Need(span>.01,"si-torso-span");for(auto &k:x)k/=span;
    auto z=Cross(x,std::abs(x[1])<.9?Point{0,1,0}:Point{0,0,1});const double zl=std::sqrt(Dot(z,z));for(auto &k:z)k/=zl;const auto y=Cross(z,x);
    Matrix basis{x[0],x[1],x[2],0,y[0],y[1],y[2],0,z[0],z[1],z[2],0,0,0,0,1};
    ClothBoneBodySphere shape{};shape.bone=bone;shape.parentFrame=parentFrame;shape.center={float(a[0]),float(a[1]),float(a[2])};shape.rotation=QuaternionOf(basis);
    shape.radius=float(local.ra);shape.endRadius=float(local.rb);shape.length=float(span+local.ra+local.rb);d.bodySpheres.push_back(shape);
  }
  d.view.bodyCoverage=true;d.Link();
}
inline SharedSkinPart GenerateJsspsiSleeve(Package &package,Scene &s,int64_t id,const Query &query){
  Need(JsspsiGroupSource(s)&&JsspsiSleeveName(s.Name(id).c_str()),"si-sleeve-source");const bool left=s.Name(id)=="MC_jsspsi_sleeve_L";
  const auto &obj=s.file.Get(id),&sd=obj.At("serializeData"),&sd2=obj.At("serializeData2");
  Need(sd.At("clothType").Int()==1&&sd.At("connectionMode").Int()==0&&sd.At("colliderCollisionConstraint").At("mode").Int()==1&&ColliderRefs(sd.At("colliderCollisionConstraint").At("colliderList")).empty(),"si-sleeve-source-policy");
  auto branch=EffectiveBranch(s,sd);const auto roots=Refs(sd.At("rootBones"));Need(roots.size()==1&&branch.ids.size()==7&&branch.ignored.empty()&&branch.excluded.empty(),"si-sleeve-source-closure");
  auto attrs=SourceSelection(s,id,sd2,branch.ids);Need(!SourcePrebuildEnabled(sd2),"si-sleeve-prebuild-changed");SharedSkinPart part;part.ids=branch.ids;
  part.profile=std::make_shared<eiem_cloth_cache::Profile>();auto &out=*part.profile;auto text=[&](const std::string &v){out.strings.push_back(v);return out.strings.back().c_str();};auto &p=out.view;
  p.component=text(s.Name(id));p.prefabSha=text(s.file.sha);p.depth=7;p.loop=false;p.runtimeGenerated=p.runtimeSleeve=true;out.roots=out.originalRoots={0};
  std::map<int64_t,int> index;std::vector<Matrix> world;std::vector<Point> points;
  for(int n=0;n<7;++n){const auto b=part.ids[n];index[b]=n;const auto parent=s.Parent(b);const std::string expected=(left?"L":"R")+std::string("_sleeveD_0")+std::to_string(n+1)+"_jnt";
    Need(s.Name(b)==expected&&attrs.at(b)==(n?2:1)&&(n?parent==part.ids[n-1]:s.Name(parent)==(left?"Bip001_L_UpperArm":"Bip001_R_UpperArm")),"si-sleeve-original-chain-changed");
    const auto &v=s.file.Get(b);const auto pos=Vec(v.At("m_LocalPosition")),scale=Vec(v.At("m_LocalScale"));const auto &q=v.At("m_LocalRotation");
    out.bones.push_back({text(s.Name(b)),text(s.Name(parent)),n-1,n?2:1,0,n,{float(pos[0]),float(pos[1]),float(pos[2])},{float(q.At("x").Number()),float(q.At("y").Number()),float(q.At("z").Number()),float(q.At("w").Number())},{float(scale[0]),float(scale[1]),float(scale[2])}});
    world.push_back(s.World(b));points.push_back(Transform(world.back(),{}));}
  out.faces.emplace_back();out.lines.emplace_back();out.graphs.emplace_back();for(int n=1;n<7;++n)out.lines[0].push_back({n-1,n});
  for(const auto &o:s.file.objects)if(s.file.Class(o.first)==137){const auto palette=Refs(s.file.Get(o.first).At("m_Bones"));bool uses=false;for(auto b:palette)uses|=index.count(b)!=0;if(!uses)continue;
    auto m=ReadMesh(package,s,o.first);size_t matches=0;for(const auto &r:query.renderers)matches+=RendererMatch(s,m,r);Need(matches==1&&!query.reservedRenderers.count({m.name,m.parent}),"si-sleeve-live-renderer-contract");
    std::vector<ClothBoneBinding> bindings;for(size_t n=0;n<m.bones.size();++n){const auto b=m.bones[n];ClothBoneBinding v{text(s.Name(b)),text(s.Name(s.Parent(b))),index.count(b)?index.at(b):-1,{}};for(int k=0;k<16;++k)v.bind[k]=float(m.binds[n][k]);bindings.push_back(v);}
    out.bindings.push_back(std::move(bindings));out.renderers.push_back({text(m.name),text(m.mesh),text(m.root),m.vertices,m.submeshes,nullptr,0,text(m.parent)});part.meshes.push_back(std::move(m));}
  Need(!part.meshes.empty()&&part.meshes.size()<=16,"si-sleeve-renderer-budget");const std::string identity="si-sleeve-source-v1\n"+s.file.sha+"\n"+s.Name(id);p.signature=text(Digest(Bytes(identity.begin(),identity.end())));out.Link();eiem_cloth_cache::Validate(out);
  part.recipe=std::make_shared<DenseRecipe>();auto &d=*part.recipe;auto &r=d.view;r.runtimeGenerated=r.sourceSleeve=r.multipleLod=true;r.loop=false;r.originalCount=7;r.originalRoots=1;r.depth=7;
  r.baseSignature=d.String(p.signature);r.prefabSha=d.String(p.prefabSha);const std::string recipeId=identity+"\nsource-sleeve-thirty-fitted-straight-axis-v4";r.signature=d.String(Digest(Bytes(recipeId.begin(),recipeId.end())));
  d.radiusCurve=CurveSamples(sd.At("radius"),true);d.distanceCurve=CurveSamples(sd.At("distanceConstraint").At("stiffness"),false);
  for(float value:d.distanceCurve)Need(std::isfinite(value)&&std::abs(value-1.f)<1e-6f,"si-sleeve-source-distance-changed");
  // Retain the source distance curve; zero recipe overrides mean that the
  // live native configuration owns material parameters, including tether/bend.
  r.distanceStiffness=1;r.tetherStretch=0;r.bendingStiffness=0;
  d.columns.assign(7,-1);d.roots={7};d.faces.emplace_back();d.lines.emplace_back();
  // PMX uses a straight chain through the sleeve, not a world-vertical axis.
  // Use the offline garment's root-to-tip direction; a vertical chain leaves
  // the lower fabric far off-axis and turns its tip into a long rigid lever.
  // Retain the attachment, two fixed points and 28 moving points. Private binds
  // preserve neutral skin; original source transforms remain passive followers.
  const auto origin=points.front();double length=0;std::array<double,7> sourceArc{};
  for(int n=1;n<7;++n){length+=Distance(points[n],points[n-1]);sourceArc[n]=length;}
  Need(length>.3&&length<2,"si-sleeve-length");
  const auto span=Sub(points.back(),origin);const double chord=std::sqrt(Dot(span,span));
  // Reject a folded/incompatible source rather than constructing a short chain.
  Need(std::isfinite(chord)&&chord>length*.9&&chord<=length+1e-6,"si-sleeve-straight-axis-coverage");
  for(int row=0;row<ClothSleeveParticles;++row){
    auto wanted=world[0];for(int k=0;k<3;++k)wanted[12+k]=origin[k]+span[k]*(double(row)/ClothSleeveLinks);
    const int parent=row?7+row-1:-1;const auto parentWorld=row?world[parent]:s.World(s.Parent(part.ids[0]));
    const auto local=Mul(Inverse(parentWorld),wanted);
    ClothBoneAsset b{d.String(std::string("EP_SiSleeveChain_")+(left?"L_":"R_")+std::to_string(row)),
      row?d.added.back().name:p.bones[0].parentName,parent,row<ClothSleeveFixed?1:2,0,row,
      {float(local[12]),float(local[13]),float(local[14])},QuaternionOf(local),{1,1,1}};
    d.added.push_back(b);d.parents.push_back(0);
    world.push_back(Mul(parentWorld,BoneMatrix(b)));points.push_back(Transform(world.back(),{}));
    if(row)d.lines[0].push_back({parent,7+row});
  }
  // BBC measures depth from the nearest fixed ancestor, so both fixed points
  // have depth zero. Passive source followers must not affect normalization.
  auto candidateBones=out.bones;for(auto &b:candidateBones)b.attribute=0;
  candidateBones.insert(candidateBones.end(),d.added.begin(),d.added.end());
  const auto lengths=NativeFixedPathLengths(candidateBones,points);
  const auto maximum=*std::max_element(lengths.begin(),lengths.end());
  Need(maximum>0,"si-sleeve-native-depth-empty");
  for(size_t n=ClothSleeveSourceCount;n<candidateBones.size();++n)
    d.radii.push_back(CurveAt(d.radiusCurve,lengths[n]/maximum));
  // Existing ribbon colliders reference seven source bones. Interpolate their
  // arc-length locations on the new chain; never attach all of them at its top.
  for(int row=0;row<7;++row){
    const double coordinate=sourceArc[row]/length*ClothSleeveLinks;
    const int lo=(std::min)(ClothSleeveLinks,int(coordinate));const double mix=coordinate-lo;
    ClothBoneResponseFrame response;auto &f=response.frame;f.target=row;
    auto append=[&](int control,double weight){if(weight<1e-9)return;const int k=f.count++;
      f.controls[k]=7+control;f.weights[k]=weight;const auto offset=Mul(Inverse(world[7+control]),world[row]);
      std::copy(offset.begin(),offset.end(),f.offsets[k].v);};
    append(lo,1-mix);if(lo<ClothSleeveLinks)append(lo+1,mix);
    Need(eiem_cloth_response::Valid(f,7,7+ClothSleeveParticles),"si-sleeve-source-follow-frame");d.responses.push_back(response);
  }
  JsspsiSleeveContacts(package,s,part,left);d.Link();p.generatedLocal=&r;return part;
}
}
