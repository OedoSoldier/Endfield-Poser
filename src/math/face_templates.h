#pragma once
#include "math/mmd_retarget.h"
#include <array>
#include <cctype>

// Authored facial poses, independent of the game's morph delta table. All
// distances are proportions of the neutral face, never world-space constants.
namespace face_template {
constexpr int MaxBones = 256;
enum Id {
  A, I, U, E, O, Blink, WinkL, WinkR, SmileEyes, Squint, WideEyes, SadEyes,
  LowerLids, BrowUp, BrowDown, Angry, Worried, HappyBrow, BrowForward,
  BrowUpL, BrowUpR, BrowDownL, BrowDownR, AngryL, AngryR, WorriedL, WorriedR,
  SmileMouth, FrownMouth, Smirk, OpenMouth, WideMouth, NarrowMouth,
  Pout, CloseMouth, Omega, Triangle, CheekPuff, Count
};
struct Definition { const char *key, *label, *aliases; };
inline const std::array<Definition, Count> &Definitions() {
  static const std::array<Definition, Count> d{{
    {"a",u8"口型 A",u8"あ|ああ|あーれー"}, {"i",u8"口型 I",u8"い|いい|いー"},
    {"u",u8"口型 U",u8"う|うう"}, {"e",u8"口型 E",u8"え|ええ"},
    {"o",u8"口型 O",u8"お|おお|ほ"},
    {"blink",u8"眨眼",u8"まばたき|瞬き|blink|はぅ"},
    {"wink_l",u8"左眼闭合",u8"ウィンク|ウィンク２|ウィンク2|ｳｨﾝｸ|ｳｨﾝｸ2|まば左|まばたき左"},
    {"wink_r",u8"右眼闭合",u8"ウィンク右|ウィンク２右|ウィンク2右|ｳｨﾝｸ右|ｳｨﾝｸ2右|ｳｨﾝｸ２右|まば右|まばたき右"},
    {"smile_eyes",u8"笑眼",u8"笑い|笑い目|にっこり|なごみ"},
    {"squint",u8"半睁眼 / 无语",u8"ジト目|じと目|じとめ|半目|どんより|じーっ"},
    {"wide_eyes",u8"睁大眼",u8"びっくり|驚き|見開き"},
    {"sad_eyes",u8"悲伤眼",u8"悲しい|悲しみ"},
    {"lower_lids",u8"下眼睑上提",u8"下瞼上げ|下まぶた上げ"},
    {"brow_up",u8"眉毛上抬",u8"上|まゆ上|眉上|眉上げ"},
    {"brow_down",u8"眉毛下压",u8"下|まゆ下|眉下|眉下げ"},
    {"angry",u8"生气眉",u8"怒り|真面目|まじめ"},
    {"worried",u8"困扰眉",u8"困る|困り|困り眉"},
    {"happy_brow",u8"微笑眉",u8"にこり|にこ"},
    {"brow_forward",u8"眉毛前移",u8"まゆ前|眉前|前"},
    {"brow_up_l",u8"左眉上抬",u8"上左|まゆ上左"},
    {"brow_up_r",u8"右眉上抬",u8"上右|まゆ上右"},
    {"brow_down_l",u8"左眉下压",u8"下左|まゆ下左"},
    {"brow_down_r",u8"右眉下压",u8"下右|まゆ下右"},
    {"angry_l",u8"左生气眉",u8"怒り左|真面目左"},
    {"angry_r",u8"右生气眉",u8"怒り右|真面目右"},
    {"worried_l",u8"左困扰眉",u8"困り左|困る左"},
    {"worried_r",u8"右困扰眉",u8"困り右|困る右"},
    {"smile_mouth",u8"嘴角上扬",u8"にこり口|口角上げ|笑い口|口わらい|にやり|にたり|にやり2|ニカッ"},
    {"frown_mouth",u8"嘴角下压",u8"口角下げ|への字|∧|Λ|へ"},
    {"smirk",u8"单侧嘴角上扬",u8"にやり左|口角上げ左"},
    {"open_mouth",u8"张嘴",u8"口開け|口開き|ワ|叫び|Д"},
    {"wide_mouth",u8"嘴部放大",u8"口拡大|口横広げ"},
    {"narrow_mouth",u8"嘴部收窄",u8"口縮小|口横狭め"},
    {"pout",u8"噘嘴",u8"む|口すぼめ|ちゅ|キス|唇せくしー"},
    {"close_mouth",u8"抿嘴",u8"ん|一文字|口閉じ|まじめ口"},
    {"omega",u8"ω 嘴（骨骼近似）",u8"ω|ω口"},
    {"triangle",u8"三角嘴（骨骼近似）",u8"△|▲"},
    {"cheek_puff",u8"鼓腮",u8"頬膨らみ|ぷくー|頬ぷく"}
  }};
  return d;
}
inline int Find(const std::string &name) {
  for (int i=0;i<Count;++i) {
    if (name==Definitions()[i].key) return i;
    std::string list=Definitions()[i].aliases;
    size_t start=0;
    while(start<=list.size()) {
      size_t end=list.find('|',start);
      if(name==list.substr(start,end-start))return i;
      if(end==std::string::npos)break;
      start=end+1;
    }
  }
  return -1; // Never guess material, pupil, teeth or effect channels.
}
inline float Clamp(float x,float lo,float hi) {
  return std::isfinite(x)?(std::max)(lo,(std::min)(hi,x)):0.f;
}
inline Vec3 Vector(const mmd::Matrix &m,Vec3 v) {
  return {m.m[0]*v.x+m.m[4]*v.y+m.m[8]*v.z,
          m.m[1]*v.x+m.m[5]*v.y+m.m[9]*v.z,
          m.m[2]*v.x+m.m[6]*v.y+m.m[10]*v.z};
}
struct Bone {
  std::string name;
  int parent=-1; // Index in this list; -1 means an external parent.
  mmd::Matrix neutral, parentNeutral;
};
enum class Part { None, Brow, Lid, Lash, Lip, Jaw, Cheek };
// Shared order for regional gains: brows, eyes, mouth, cheeks.
inline int RegionIndex(Part p) {
  if(p==Part::Brow)return 0;
  if(p==Part::Lid||p==Part::Lash)return 1;
  if(p==Part::Lip||p==Part::Jaw)return 2;
  return p==Part::Cheek?3:-1;
}
struct BoundBone {
  Part part=Part::None;
  int side=0, segment=0, parent=-1, source=-1;
  Vec3 position, localX,localY,localZ;
  float horizontal=0,vertical=0;
  bool upper=false;
};
struct Binding {
  std::array<BoundBone,MaxBones> bones{};
  int count=0, controlled=0, brows=0,lids=0,lips=0,cheeks=0;
  Vec3 x,y,z,eye[2],mouth;
  float eyeSpan=0,mouthHalf=0,mouthGap=0;
  bool ready=false;
  std::array<float,2> eyeHalf{},eyeHeight{};
};
inline std::string Canonical(std::string s) {
  auto sep=s.find_last_of("/:|");if(sep!=std::string::npos)s=s.substr(sep+1);
  for(char &c:s)if(static_cast<unsigned char>(c)<128)c=char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
inline BoundBone Classify(const std::string &raw) {
  BoundBone b;auto n=Canonical(raw);
  if(n.find("browlf")==0||n.find("browrt")==0||n.find("browlinelf")==0||n.find("browlinert")==0) {
    b.part=Part::Brow;b.side=n.find("lf")!=std::string::npos?0:1;
  } else if(n.find("eyelf")==0||n.find("eyert")==0) {
    b.side=n.find("lf")!=std::string::npos?0:1;
    if(n.size()>7 && std::isdigit(static_cast<unsigned char>(n[5])) && std::isdigit(static_cast<unsigned char>(n[6]))) {
      b.segment=(n[5]-'0')*10+n[6]-'0';
      if(n.find("eyelash")!=std::string::npos)b.part=Part::Lash;
      else if(n.substr(7)=="joint"&&b.segment>=1&&b.segment<=8)b.part=Part::Lid;
    }
  } else if(n.find("liplup")==0||n.find("liprup")==0||n=="lipmupjoint"||
            n.find("lipldn")==0||n.find("liprdn")==0||n=="lipmdnjoint") {
    b.part=Part::Lip;b.upper=n.find("up")!=std::string::npos;
  } else if(n=="jawjoint"||n=="facemdjawdnjoint"||n=="facemdtoothdnjoint") b.part=Part::Jaw;
  else if(n.find("facelfcheek")==0||n.find("facertcheek")==0) {
    b.part=Part::Cheek;b.side=n.find("lf")!=std::string::npos?0:1;
  }
  return b;
}
inline Binding Bind(const std::vector<Bone> &input) {
  Binding b;if(input.empty()||input.size()>MaxBones)return b;
  b.count=int(input.size());
  int ring[2][9];for(auto &r:ring)std::fill(std::begin(r),std::end(r),-1);
  int up=-1,dn=-1;
  for(int i=0;i<b.count;++i) {
    for(float f:input[i].neutral.m)if(!std::isfinite(f))return Binding{};
    auto &n=b.bones[i];n=Classify(input[i].name);n.position=input[i].neutral.position();n.parent=input[i].parent;
    // Validate the hierarchy even for bones not used by the template.
    int p=i,steps=0;while(p>=0&&p<b.count&&steps++<=b.count)p=input[p].parent;
    if(p>=b.count||steps>b.count||n.parent < -1)return Binding{};
    if(n.part==Part::Lid)ring[n.side][n.segment]=i;
    auto name=Canonical(input[i].name);
    if(name=="lipmupjoint")up=i;
    if(name=="lipmdnjoint")dn=i;
  }
  // Eyeball pivots sit behind the skin: using them with lip positions tilts
  // the facial plane. Use the actual upper/lower eyelid surface instead.
  for(int s=0;s<2;++s)for(int j:{1,3,5,7})if(ring[s][j]<0)return b;
  for(int s=0;s<2;++s) {
    for(int j=1;j<=8;++j)if(ring[s][j]>=0)b.eye[s]=b.eye[s]+b.bones[ring[s][j]].position;
    int n=0;for(int j=1;j<=8;++j)n+=ring[s][j]>=0;b.eye[s]=b.eye[s]*(1.f/n);
  }
  b.x=Norm(b.eye[1]-b.eye[0]);b.eyeSpan=Len(b.eye[1]-b.eye[0]);
  Vec3 vertical;
  for(int s=0;s<2;++s)vertical=vertical+b.bones[ring[s][3]].position-b.bones[ring[s][7]].position;
  b.y=Norm(vertical-b.x*Dot(vertical,b.x));b.z=Norm(Cross(b.x,b.y));
  if(b.eyeSpan<1e-5f||Len(b.y)<.9f||Len(b.z)<.9f)return b;
  for(int s=0;s<2;++s) {
    b.eyeHalf[s]=std::fabs(Dot(b.bones[ring[s][5]].position-b.bones[ring[s][1]].position,b.x))*.5f;
    b.eyeHeight[s]=Dot(b.bones[ring[s][3]].position-b.bones[ring[s][7]].position,b.y);
    if(b.eyeHeight[s]<b.eyeSpan*.01f||b.eyeHalf[s]<b.eyeSpan*.02f)return b;
  }
  if(up>=0&&dn>=0) {
    b.mouth=(b.bones[up].position+b.bones[dn].position)*.5f;
    b.mouthGap=(std::max)(0.f,Dot(b.bones[up].position-b.bones[dn].position,b.y));
    for(int i=0;i<b.count;++i)if(b.bones[i].part==Part::Lip)
      b.mouthHalf=(std::max)(b.mouthHalf,std::fabs(Dot(b.bones[i].position-b.mouth,b.x)));
  }
  for(int i=0;i<b.count;++i) {
    auto &n=b.bones[i];mmd::Matrix inverse;
    if(!mmd::Inverse(input[i].parentNeutral,inverse))return Binding{};
    n.localX=Vector(inverse,b.x);n.localY=Vector(inverse,b.y);n.localZ=Vector(inverse,b.z);
    if(n.part==Part::Brow) {
      ++b.brows;
      n.horizontal=Clamp(Dot(n.position-b.eye[n.side],b.x)*(n.side?1.f:-1.f)/b.eyeHalf[n.side],-1,1);
    }
    if(n.part==Part::Lid) {
      ++b.lids;
      // Close both arcs to the neutral corner line, preserving its slope.
      auto inner=b.bones[ring[n.side][1]].position,outer=b.bones[ring[n.side][5]].position;
      float t=Clamp(Dot(n.position-inner,outer-inner)/(std::max)(1e-12f,Dot(outer-inner,outer-inner)),0,1);
      n.vertical=Dot(n.position-(inner+(outer-inner)*t),b.y);
    }
    if(n.part==Part::Lash) {
      float best=1e30f;
      for(int j=1;j<=8;++j)if(ring[n.side][j]>=0) {
        int k=ring[n.side][j];float d=Len(n.position-b.bones[k].position);
        if(d<best){best=d;n.source=k;}
      }
    }
    if(n.part==Part::Lip||n.part==Part::Jaw) {
      if(b.mouthHalf<b.eyeSpan*.02f){n.part=Part::None;continue;}
      n.horizontal=Clamp(Dot(n.position-b.mouth,b.x)/b.mouthHalf,-1,1);
      n.vertical=Dot(n.position-b.mouth,b.y);
      if(n.part==Part::Lip)++b.lips;
    }
    if(n.part==Part::Cheek)++b.cheeks;
    if(n.part!=Part::None)++b.controlled;
  }
  b.ready=b.controlled>0;return b;
}
using Weights=std::array<float,Count>;
using Deltas=std::array<Vec3,MaxBones>;
inline bool Supported(const Binding &b,int id) {
  if(!b.ready||id<0||id>=Count)return false;
  if(id>=BrowUp&&id<=WorriedR)return b.brows>0;
  if(id>=Blink&&id<=LowerLids)return b.lids>0;
  if(id==CheekPuff)return b.cheeks>0;
  return b.lips>0;
}
inline Deltas Evaluate(const Binding &b,const Weights &weights,float strength,
                       const std::array<float,4> &regionGain={{1,1,1,1}}) {
  Deltas local{},desired{};if(!b.ready)return local;
  strength=Clamp(strength,0,2);if(strength==0)return local;
  auto w=[&](int i){return Clamp(weights[i],0,1);};
  float regionStrength=strength*Clamp(regionGain[2],0,2);
  auto strong=[&](float v,float lo,float hi){return Clamp(v*regionStrength,lo,hi);};
  float mouthTotal=w(A)+w(I)+w(U)+w(E)+w(O),vscale=1.f/(std::max)(1.f,mouthTotal);
  float a=w(A)*vscale,ii=w(I)*vscale,u=w(U)*vscale,e=w(E)*vscale,o=w(O)*vscale;
  float opening=strong(.42f*a+.08f*ii+.16f*u+.23f*e+.36f*o+.5f*w(OpenMouth)+.3f*w(Triangle),0,.75f);
  float width=strong(.1f*a+.3f*ii-.35f*u+.2f*e-.3f*o+.4f*w(WideMouth)-.35f*w(NarrowMouth)-.25f*w(Pout),-.65f,.65f);
  float corner=strong(.18f*w(SmileMouth)-.16f*w(FrownMouth),-.3f,.3f);
  float purse=strong(.16f*u+.1f*o+.28f*w(Pout),0,.42f);
  float close=strong(w(CloseMouth),0,1);
  opening*=1-close;
  for(int i=0;i<b.count;++i) {
    const auto &n=b.bones[i];Vec3 d;
    int region=RegionIndex(n.part);
    regionStrength=strength*(region>=0?Clamp(regionGain[region],0,2):0.f);
    if(n.part==Part::Brow) {
      int s=n.side;float inner=(1-n.horizontal)*.5f;
      float up=w(BrowUp)+w(s?BrowUpR:BrowUpL),down=w(BrowDown)+w(s?BrowDownR:BrowDownL);
      float anger=(std::max)(w(Angry),w(s?AngryR:AngryL)),worry=(std::max)(w(Worried),w(s?WorriedR:WorriedL));
      d.y=b.eyeSpan*strong(.085f*(up-down)+.10f*anger*(.35f-inner)+.10f*worry*(inner-.25f)+.035f*w(HappyBrow),-.16f,.16f);
      d.z=b.eyeSpan*strong(.025f*w(BrowForward),0,.05f);
    } else if(n.part==Part::Lid) {
      float h=Clamp(Dot(n.position-b.eye[n.side],b.x)/b.eyeHalf[n.side],-1,1);
      float blink=(std::max)(w(Blink),w(n.side?WinkR:WinkL));
      float smile=w(SmileEyes);
      float closure=strong((std::max)(blink,(std::max)(smile,n.vertical>0?.72f*w(Squint):.18f*w(Squint))),0,1);
      closure=(std::max)(closure,n.vertical<0?strong(.65f*w(LowerLids),0,1):0.f);
      float outer=(1+h*(n.side?1.f:-1.f))*.5f;
      closure=(std::max)(closure,strong(w(SadEyes)*(n.vertical>0?.3f+.4f*outer:.15f),0,1));
      float open=strong(w(WideEyes),0,1.5f)*(1-closure);
      d.y=-n.vertical*closure+n.vertical*.6f*open;
      // Both arcs meet on the same upward-curved smile line; never cross.
      d.y+=b.eyeHeight[n.side]*.2f*strong(smile,0,1)*(1-h*h);
      d.z=b.eyeHeight[n.side]*.12f*closure*(1-h*h);
    } else if(n.part==Part::Lip) {
      float h=n.horizontal,edge=std::fabs(h),middle=1-edge*edge;
      d.x=h*b.mouthHalf*width;
      d.y=b.mouthHalf*(opening*(n.upper?.3f:-.7f)*middle+corner*edge*edge);
      d.y+=b.mouthHalf*strong(.15f*w(Smirk),0,.25f)*(std::max)(0.f,-h);
      d.y-=n.vertical*close;
      d.y+=b.mouthHalf*strong(w(Omega)*.14f,0,.22f)*(n.upper?(edge<.5f?-1.f:1.f)*middle:0.f);
      d.y+=b.mouthHalf*strong(w(Triangle)*.08f,0,.12f)*(n.upper?-edge:0.f);
      d.z=b.mouthHalf*purse*middle;
    } else if(n.part==Part::Jaw) {
      d.y=-b.mouthHalf*opening*.6f;
    } else if(n.part==Part::Cheek) {
      float puff=strong(w(CheekPuff),0,1.5f);
      d.x=(n.side?1.f:-1.f)*b.eyeSpan*.035f*puff;
      d.z=b.eyeSpan*.025f*puff;
      d.y=b.eyeSpan*.015f*strong(w(SmileMouth),0,1.5f);
    }
    desired[i]=d;
  }
  // Sculpted mouth variants must not turn the upper lip inside the lower lip.
  for(int i=0;i<b.count;++i)if(Clamp(regionGain[2],0,2)>0&&b.bones[i].part==Part::Lip&&b.bones[i].upper) {
    int pair=-1;float nearest=1e30f;
    for(int j=0;j<b.count;++j)if(b.bones[j].part==Part::Lip&&!b.bones[j].upper) {
      float distance=std::fabs(b.bones[j].horizontal-b.bones[i].horizontal);
      if(distance<nearest){nearest=distance;pair=j;}
    }
    if(pair>=0) {
      float gap=b.bones[i].vertical+desired[i].y-b.bones[pair].vertical-desired[pair].y;
      if(gap<0){desired[i].y-=gap*.5f;desired[pair].y+=gap*.5f;}
    }
  }
  for(int i=0;i<b.count;++i)if(b.bones[i].part==Part::Lash&&b.bones[i].source>=0)
    desired[i]=desired[b.bones[i].source];
  // Targets are absolute displacements from neutral. Compensate controlled
  // ancestors, including intervening unclassified bones, to avoid double motion.
  for(int i=0;i<b.count;++i) {
    auto &n=b.bones[i];if(n.part==Part::None)continue;
    Vec3 inherited;int parent=n.parent;
    while(parent>=0) {
      if(b.bones[parent].part!=Part::None){inherited=desired[parent];break;}
      parent=b.bones[parent].parent;
    }
    Vec3 d=desired[i]-inherited;
    local[i]=n.localX*d.x+n.localY*d.y+n.localZ*d.z;
  }
  return local;
}
} // namespace face_template
