#pragma once
namespace eiem_cloth_asset {
inline std::shared_ptr<DenseRecipe> GenerateForkCoatGraph(Scene &scene,int64_t component,const eiem_cloth_cache::Profile &base) {
  const auto &p=base.view;Need(SourceForkCoatFront(p)&&!p.generatedLocal,"fork-coat-graph-source-changed");
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
  auto out=std::make_shared<DenseRecipe>();auto &r=out->view;r.runtimeGenerated=r.multipleLod=r.sourceForkCoat=true;r.loop=false;
  r.originalCount=p.boneCount;r.originalRoots=r.rootCount=p.rootCount;r.depth=p.depth;
  r.baseSignature=out->String(p.signature);r.prefabSha=out->String(p.prefabSha);
  const auto key=std::string(p.signature)+"\nfork-coat-shoulder-graph-free-upper-inputs-native-colliders-v4";
  r.signature=out->String(Digest(Bytes(key.begin(),key.end())));
  auto candidate=base;std::vector<Point> points;
  for(int n=0;n<p.boneCount;++n){SourceForkCoatSelection(p,n,candidate.bones[n]);points.push_back(Transform(scene.World(branch.ids[n]),{}));}
  for(auto &root:candidate.roots)if(root==17||root==60)--root;
  candidate.view.depth=p.depth+1;candidate.Link();GraphVariants(candidate,points);
  out->roots=candidate.roots;for(const auto &bone:candidate.bones)out->columns.push_back(bone.column);
  out->faces=candidate.faces;out->lines=candidate.lines;out->graphOrder=candidate.graphOrder;
  const auto &sd=scene.file.Get(component).At("serializeData");out->radiusCurve=CurveSamples(sd.At("radius"),true);
  out->distanceCurve=CurveSamples(sd.At("distanceConstraint").At("stiffness"),false);
  out->Link();
  Need(r.ForkCoatGraphOnly(),"fork-coat-graph-original-graph-contract");return out;
}
}
