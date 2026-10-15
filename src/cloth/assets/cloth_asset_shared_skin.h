#pragma once
namespace eiem_cloth_asset {
inline void PublishSharedSkin(Package &package,Scene &scene,std::array<SharedSkinPart,3> &parts){
  auto &publisher=parts[0];auto &output=*publisher.recipe;Need(!output.view.NativeSkinRetained(),"shared-skin-publisher-must-own-surface");
  std::array<std::vector<int>,3> remap;int originalCount=0,total=0;
  for(int p=0;p<3;++p){Need(parts[p].profile&&parts[p].recipe,"shared-skin-missing-member");const auto &r=parts[p].recipe->view;Need(r.originalCount==int(parts[p].ids.size()),"shared-skin-source-count");remap[p].resize(r.Total());for(int n=0;n<r.originalCount;++n)remap[p][n]=originalCount++;}
  total=originalCount;for(int p=0;p<3;++p)for(int n=parts[p].recipe->view.originalCount;n<parts[p].recipe->view.Total();++n)remap[p][n]=total++;
  Need(total<=256,"shared-skin-union-particle-budget");std::vector<ClothBoneAsset> bones(total);std::vector<Matrix> world(total);std::vector<Point> points(total);std::vector<int> ownPart(total),ownLocal(total);
  eiem_cloth_cache::Profile unionBase;unionBase.view.runtimeGenerated=true;unionBase.bones.resize(originalCount);Graph graph;
  std::map<int64_t,int> originalOwner;
  for(int p=0;p<3;++p){const auto &base=*parts[p].profile;const auto &r=*parts[p].recipe;for(int n=0;n<r.view.Total();++n){const int mapped=remap[p][n];auto b=n<r.view.originalCount?base.bones[n]:r.added[n-r.view.originalCount];
      if(n<r.view.originalCount){Need(originalOwner.emplace(parts[p].ids[n],p).second,"shared-skin-original-bone-two-owners");b.column=r.columns[n];if(base.view.Passive(n))b.attribute=0;world[mapped]=scene.World(parts[p].ids[n]);}
      else {const auto parent=b.parent<0?scene.World(scene.Parent(parts[p].ids[base.originalRoots[0]])):world[remap[p][b.parent]];world[mapped]=Mul(parent,BoneMatrix(b));}
      if(b.column>=0)b.column+=p*32;if(b.parent>=0)b.parent=remap[p][b.parent];
      // Original sleeve identities remain available for restoration/proofs.
      // Only the private straight chain is a skin support and solver output.
      if(n<r.view.originalCount){unionBase.bones[mapped]=b;if(r.view.sourceSleeve){unionBase.bones[mapped].column=p*32;b.attribute=0;b.column=b.depth=-1;}}
      bones[mapped]=b;points[mapped]=Transform(world[mapped],{});ownPart[mapped]=p;ownLocal[mapped]=n;}
    Need(!r.faces.empty(),"shared-skin-native-graph-missing");for(auto f:r.faces[0]){for(auto &b:f)b=remap[p][b];graph.faces.push_back(f);}for(auto line:r.lines[0]){for(auto &b:line)b=remap[p][b];graph.lines.push_back(line);}}
  unionBase.Link();
  for(const auto &f:graph.faces)Need(ownPart[f[0]]==ownPart[f[1]]&&ownPart[f[0]]==ownPart[f[2]],"shared-skin-cross-part-native-face");
  for(const auto &e:graph.lines)Need(ownPart[e[0]]==ownPart[e[1]],"shared-skin-cross-part-native-line");
  output.bindingParts=ownPart;
  std::map<std::pair<std::string,std::string>,MeshView> meshes;for(auto &part:parts)for(const auto &m:part.meshes){const auto key=std::make_pair(m.name,m.parent);auto inserted=meshes.emplace(key,m);if(!inserted.second)Need(inserted.first->second.bones==m.bones&&inserted.first->second.binds==m.binds&&inserted.first->second.sourceHash==m.sourceHash,"shared-skin-renderer-source-disagrees");}
  Need(!meshes.empty()&&meshes.size()<=16,"shared-skin-renderer-budget");std::string identity="shared-three-part-private-skin-v1";for(const auto &part:parts)identity+='\n'+std::string(part.recipe->view.signature);const auto group=Digest(Bytes(identity.begin(),identity.end()));
  output.meshes.clear();output.sharedBindings.clear();publisher.profile->renderers.clear();publisher.profile->bindings.clear();
  auto profileText=[&](const std::string &v){publisher.profile->strings.push_back(v);return publisher.profile->strings.back().c_str();};
  auto external=[&](int part,int bone){Need(part>0&&part<3,"shared-skin-borrow-owner");for(size_t n=0;n<output.sharedBindings.size();++n)if(output.sharedBindings[n].bone==bone&&!strcmp(output.sharedBindings[n].component,parts[part].profile->view.component))return -ClothSharedSkinBase-int(n);
    Need(output.sharedBindings.size()<ClothSharedSkinMaxRefs,"shared-skin-borrow-budget");output.sharedBindings.push_back({output.String(parts[part].profile->view.component),bone});return -ClothSharedSkinBase-int(output.sharedBindings.size()-1);};
  size_t totalBytes=0;
  for(auto &entry:meshes){const auto &m=entry.second;
    // A vertex is edited by exactly one cloth part. Preserve original foreign
    // terms rather than averaging two unrelated collision surfaces together.
    for(size_t v=0;v<m.weights.size();++v){int owner=-1;for(size_t k=0;k<m.weights[v].size();++k)if(m.weights[v][k]>0){const auto found=originalOwner.find(m.bones[int(m.indices[v][k])]);if(found!=originalOwner.end()){Need(owner<0||owner==found->second,"shared-skin-overlapping-weight-regions");owner=found->second;}}}
    const bool loop=output.view.loop;output.view.loop=false;DenseMesh built;
    try{built=DenseBinding(package,scene,m,unionBase,output,world,points,bones,graph,nullptr,nullptr,1);}catch(...){output.view.loop=loop;throw;}output.view.loop=loop;
    Need(built.view.skinVertices>0,"shared-skin-required-renderer-unchanged");
    std::map<int,int> palette;for(size_t n=0;n<m.bones.size();++n)for(int b=0;b<originalCount;++b)if(scene.Name(m.bones[n])==bones[b].name&&scene.Name(scene.Parent(m.bones[n]))==bones[b].parentName)palette[b]=int(n);
    for(size_t n=0;n<built.bindingIds.size();++n){const int b=built.bindingIds[n];palette[b]=int(m.bones.size()+n);built.bindingIds[n]=ownPart[b]?external(ownPart[b],ownLocal[b]):ownLocal[b];}
    std::map<int,int> borrowed;
    auto proofBone=[&](int b){if(b>=total)return output.view.Total()+(b-total);if(!ownPart[b])return ownLocal[b];
      const int binding=palette.at(b);auto found=borrowed.find(binding);if(found!=borrowed.end())return found->second;
      const int result=output.view.Total()+int(built.foreign.size());built.foreign.push_back(binding);borrowed[binding]=result;return result;};
    for(auto &sample:built.samples)for(auto *skin:{sample.original,sample.candidate})for(int k=0;k<4;++k)if(skin[k].weight>0)skin[k].bone=proofBone(skin[k].bone);
    Need(output.view.Total()+built.foreign.size()<=eiem_cloth_skin::MaxMatrices,"shared-skin-proof-matrix-budget");built.Link();totalBytes+=built.payload.size()+built.samples.size()*sizeof(eiem_cloth_skin::Sample);Need(totalBytes<=32*1024*1024,"shared-skin-payload-budget");output.meshes.push_back(std::move(built));
    std::vector<ClothBoneBinding> binding;for(size_t n=0;n<m.bones.size();++n){int native=-1;for(int b=0;b<publisher.profile->view.boneCount;++b)if(scene.Name(m.bones[n])==publisher.profile->bones[b].name&&scene.Name(scene.Parent(m.bones[n]))==publisher.profile->bones[b].parentName)native=b;
      ClothBoneBinding value{profileText(scene.Name(m.bones[n])),profileText(scene.Name(scene.Parent(m.bones[n]))),native,{}};for(int k=0;k<16;++k)value.bind[k]=float(m.binds[n][k]);binding.push_back(value);}
    publisher.profile->bindings.push_back(std::move(binding));publisher.profile->renderers.push_back({profileText(m.name),profileText(m.mesh),profileText(m.root),m.vertices,m.submeshes,nullptr,0,profileText(m.parent)});
  }
  for(int p=0;p<3;++p){auto &d=*parts[p].recipe;d.view.sharedSkinGroup=d.String(group);d.view.sharedPublisher=d.String(publisher.profile->view.component);d.view.sharedSkinSupplier=p!=0;if(p)d.meshes.clear();d.Link();parts[p].profile->view.generatedLocal=&d.view;}
  publisher.profile->dependencies.clear();for(int p=1;p<3;++p)publisher.profile->dependencies.push_back(profileText(parts[p].profile->view.component));publisher.profile->Link();output.Link();
  Need(output.view.meshCount>0&&output.view.sharedBindingCount>0&&parts[1].recipe->view.SharedSupplier()&&parts[2].recipe->view.SharedSupplier(),"shared-skin-final-contract");
}
inline std::array<SharedSkinPart,3> GenerateJsspsiGroup(Package &package,Scene &s,const Query &query){
  Need(JsspsiGroupSource(s),"si-group-source");const char *names[]{"MC_jsspsi_Skirt","MC_jsspsi_sleeve_L","MC_jsspsi_sleeve_R"};std::array<int64_t,3> ids{};
  for(int n=0;n<3;++n){int captured=0;for(const auto &live:query.cloths)captured+=live.name==names[n];Need(captured==1,"si-group-all-members-required");
    for(const auto &o:s.file.objects)if(s.file.Class(o.first)==114&&s.Name(o.first)==names[n]){Need(!ids[n],"si-group-source-ambiguous");ids[n]=o.first;}Need(ids[n]!=0,"si-group-source-missing");}
  std::array<SharedSkinPart,3> parts;SurfaceInput surface;
  try{parts[0].profile=GenerateConnections(package,s,ids[0],query,&surface);}catch(const std::exception &e){throw std::runtime_error(std::string("si-skirt-source:")+e.what());}
  auto &skirt=*parts[0].profile;
  Need(skirt.view.OriginalCount()==36&&skirt.view.EffectiveCount()==30&&skirt.ignored.size()==6,"si-skirt-source-and-candidate-closure");
  for(int n:skirt.ignored)Need(skirt.bones[n].attribute==2&&skirt.bones[n].parent>=0&&skirt.bones[n].column==-1,"si-skirt-helper-contract");
  const auto sourceIdentity=std::string(skirt.view.signature)+"\nsi-source-nearest-helper-selection-v2";
  skirt.strings.push_back(Digest(Bytes(sourceIdentity.begin(),sourceIdentity.end())));skirt.view.signature=skirt.strings.back().c_str();
  Need(surface.groups.empty()&&surface.densityIssue.empty(),surface.densityIssue.empty()?"si-skirt-isolated-panels":surface.densityIssue.c_str());parts[0].ids=surface.ids;parts[0].meshes=surface.meshes;
  parts[0].recipe=GenerateDenseCandidate(package,s,ids[0],*parts[0].profile,parts[0].ids,parts[0].meshes,{}, {},2,false,true);parts[0].profile->view.generatedLocal=&parts[0].recipe->view;
  for(int side=1;side<3;++side)try{parts[side]=GenerateJsspsiSleeve(package,s,ids[side],query);}catch(const std::exception &e){throw std::runtime_error(std::string(names[side])+":"+e.what());}
  PublishSharedSkin(package,s,parts);return parts;
}
}
