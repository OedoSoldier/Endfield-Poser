#pragma once
namespace eiem_cloth_asset {
// Authored Li Zhiyan coat layout: chest on Spine1 and transverse waist on
// Spine. Scale the two regions independently; all coordinates are bone-local.
inline std::array<ClothBoneBodySphere,2> CoatTorsoCapsules(double chestLength,double hipWidth,int chest,int waist,bool male=false) {
  const double upper=chestLength/.080229469,lower=hipWidth/.151599968;
  Need(chest>=0&&waist>=0&&chest!=waist&&std::isfinite(upper)&&std::isfinite(lower)&&
      upper>=.75&&upper<=1.4&&lower>=.75&&lower<=1.4,"coat-torso-reference-proportion");
  // Character-specific envelopes from the shipped torso surface, excluding
  // arms and attachments by skin weights. Keep clearance for cloth thickness;
  // the hidden body is not reconstructed from an inflated coat silhouette.
  const float chestRadius=float(.100*upper),waistRadius=float((male?.083:.070)*lower);
  const float waistHalf=float(.036*lower);
  return {{{chest,{0,float(.020*upper),0},chestRadius,chestRadius,float(.330*upper),{0,0,0,1}},
      {waist,{0,0,waistHalf},waistRadius,waistRadius,2*(waistRadius+waistHalf),{0,-.7071067812f,0,.7071067812f}}}};
}
inline void ConfigureCoatTorso(Scene &scene,const Query &query,const ClothBoneProfile &p,DenseRecipe &out) {
  auto &r=out.view;
  Need((SourceOriginalCoverageCoat(p.prefabSha,p.component)&&r.CoatCalfCoverage())||
      (SourceForkCoatFront(p)&&!r.bodyCoverage&&!r.bodySphereCount),"coat-torso-source-policy");
  const auto spine=FindBodyKey(scene,query.spine),chest=FindBodyKey(scene,query.chest),upper=FindBodyKey(scene,query.upperChest);
  Need(scene.Name(spine)=="Bip001_Spine"&&scene.Name(chest)=="Bip001_Spine1"&&scene.Name(upper)=="Bip001_Spine2"&&
      scene.Parent(chest)==spine&&scene.Parent(upper)==chest,"coat-torso-source-hierarchy");
  const auto a=scene.World(chest),b=scene.World(upper);QuaternionOf(a);QuaternionOf(scene.World(spine));
  const auto end=Transform(Inverse(a),Transform(b,{}));
  Need(end[0]<0&&std::hypot(end[1],end[2])<.002,"coat-torso-source-axis");
  int chestIndex=-1,waistIndex=-1;
  for(size_t n=0;n<out.bodyBindings.size();++n){const auto &binding=out.bodyBindings[n];
    if(!strcmp(binding.name,"Bip001_Spine1")){Need(chestIndex<0,"coat-torso-duplicate-chest");chestIndex=int(n);}
    if(!strcmp(binding.name,"Bip001_Spine")){Need(waistIndex<0,"coat-torso-duplicate-waist");waistIndex=int(n);}}
  const auto hipWidth=Distance(Transform(scene.World(FindBodyKey(scene,query.leftThigh)),{}),Transform(scene.World(FindBodyKey(scene,query.rightThigh)),{}));
  const auto shapes=CoatTorsoCapsules(-end[0],hipWidth,chestIndex,waistIndex,SourceUpperCoatFront(p));
  out.bodySpheres.insert(out.bodySpheres.end(),shapes.begin(),shapes.end());r.bodyCoverage=r.sourceCoatTorso=true;
  const auto key=std::string(r.signature)+"\nsource-coat-chest-character-waist-clearance-no-pelvis-bridge-v4";
  r.signature=out.String(Digest(Bytes(key.begin(),key.end())));out.Link();
  Need(r.CoatTorsoCoverage(),"coat-torso-generated-contract");
  out.densityReport+=" addedChestCapsules=1 addedWaistCapsules=1 torsoLayout=authored-Spine1-Spine independentlyScaled=1 originalCollidersRetained=1 originalSkinRetained=1 contactAndVisual=pending";
}
inline std::shared_ptr<DenseRecipe> GenerateForkCoatBody(Package &package,Scene &scene,int64_t component,const Query &query,const eiem_cloth_cache::Profile &base) {
  const auto &p=base.view;Need(SourceForkCoatFront(p)&&!p.generatedLocal,"fork-coat-body-source-changed");
  const auto &sourceData=scene.file.Get(component).At("serializeData");
  const auto branch=EffectiveBranch(scene,sourceData);
  auto proposed=SourcePrebuildEnabled(scene.file.Get(component).At("serializeData2"))?
      SourcePrebuildAttributes(scene.file.Get(component).At("serializeData2")):
      SourceSelection(scene,component,scene.file.Get(component).At("serializeData2"),branch.ids);
  const auto originalPeers=SharedSelection(scene,component,branch.ids,proposed);
  Need(branch.ids.size()==size_t(p.boneCount),"fork-coat-release-source-order");
  for(int n=0;n<p.boneCount;++n){Need(scene.Name(branch.ids[n])==p.bones[n].name,"fork-coat-release-source-order");
    auto bone=p.bones[n];SourceForkCoatSelection(p,n,bone);proposed[branch.ids[n]]=bone.attribute;}
  const auto peers=SharedSelection(scene,component,branch.ids,proposed);
  // The original coat contains excluded ribbon branches. Keep their ownership
  // unchanged; only release coat inputs that no other BBC writes or anchors.
  Need(peers.foreign==originalPeers.foreign,"fork-coat-release-foreign-ownership-changed");
  for(int n=0;n<p.boneCount;++n)if(SourceForkCoatRelease(p,n)||SourceForkCoatRoot(p,n)) {
    const auto bone=branch.ids[n];Need(!peers.foreign.count(bone),"fork-coat-release-foreign-output");
    for(const auto &peer:peers.proofs)if(peer.bone==bone)
      Need(peer.attribute<=0,"fork-coat-release-shared-input");
  }
  int64_t renderer=0;for(const auto &o:scene.file.objects)if(scene.file.Class(o.first)==137&&scene.Name(o.first)=="S_actor_endminf_cloth_02_lod0"){
    Need(!renderer,"fork-coat-body-renderer-ambiguous");renderer=o.first;}
  Need(renderer!=0,"fork-coat-body-renderer-missing");const auto m=ReadMesh(package,scene,renderer);
  Need(m.vertices==470,"fork-coat-body-renderer-changed");
  auto out=std::make_shared<DenseRecipe>();auto &r=out->view;r.runtimeGenerated=r.multipleLod=true;r.loop=false;
  r.originalCount=p.boneCount;r.originalRoots=r.rootCount=p.rootCount;r.depth=p.depth;
  r.baseSignature=out->String(p.signature);r.prefabSha=out->String(p.prefabSha);
  const auto key=std::string(p.signature)+"\nfork-coat-shoulder-graph-body-contact-free-upper-inputs-v3\n"+m.sourceHash;
  r.signature=out->String(Digest(Bytes(key.begin(),key.end())));
  auto candidate=base;std::vector<Point> points;
  for(int n=0;n<p.boneCount;++n){SourceForkCoatSelection(p,n,candidate.bones[n]);points.push_back(Transform(scene.World(branch.ids[n]),{}));}
  for(auto &root:candidate.roots)if(root==17||root==60)--root;
  candidate.view.depth=p.depth+1;candidate.Link();GraphVariants(candidate,points);
  out->roots=candidate.roots;for(const auto &bone:candidate.bones)out->columns.push_back(bone.column);
  out->faces=candidate.faces;out->lines=candidate.lines;out->graphOrder=candidate.graphOrder;
  const auto &sd=scene.file.Get(component).At("serializeData");out->radiusCurve=CurveSamples(sd.At("radius"),true);
  out->distanceCurve=CurveSamples(sd.At("distanceConstraint").At("stiffness"),false);
  out->bodyAsset={out->String(m.name),out->String(m.mesh),out->String(m.root),m.vertices,m.submeshes,nullptr,0,out->String(m.parent)};
  for(size_t n=0;n<m.bones.size();++n){ClothBoneBinding binding{out->String(scene.Name(m.bones[n])),out->String(scene.Name(scene.Parent(m.bones[n]))),-1,{}};
    for(int k=0;k<16;++k)binding.bind[k]=float(m.binds[n][k]);out->bodyBindings.push_back(binding);}
  ConfigureCoatTorso(scene,query,p,*out);
  Need(r.ForkCoatBodyOnly(),"fork-coat-body-original-graph-contract");return out;
}
}
