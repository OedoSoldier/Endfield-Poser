#pragma once
#include "cloth_native_profile.h"

namespace poser_cloth_optional {
// Game 1.6.3: full managed hot/cold bodies and native Burst consumers.
// Reflection and live container identity checks remain required by each adapter.
// Reviewed against installed GameAssembly and Burst PE; no wildcard hashes.
using poser_cloth_boundary::CodeRange;
using poser_cloth_boundary::NativeMethodProfile;
inline constexpr bool LayerAdapter163Reviewed=true, DisplayAdapter163Reviewed=true, FinishAdapter163Reviewed=true;
struct Body { size_t bytes; uint64_t hash; };
inline constexpr CodeRange LayerKernelRanges163[]{
  {0x0,1766,0xb1acbe789dfe157bULL},
};
inline constexpr NativeMethodProfile LayerKernel163{".SolverPointTriangleCrossFrameJobKernels","SolverPointTriangleCrossFrameKernel$BurstManaged",0x89f635c,
    LayerKernelRanges163,std::size(LayerKernelRanges163)};
inline constexpr CodeRange LayerIndexRanges163[]{
  {0x0,1804,0x1c632da699bc2db1ULL},
};
inline constexpr NativeMethodProfile LayerIndex163{".SolverPointTriangleCrossFrameJob","Execute",0x89ff404,
    LayerIndexRanges163,std::size(LayerIndexRanges163)};
inline constexpr CodeRange LayerWholeRanges163[]{
  {0x0,62,0xfef2535231c73452ULL},
};
inline constexpr NativeMethodProfile LayerWhole163{".SolverPointTriangleCrossFrameJob","Execute",0x89ffb10,
    LayerWholeRanges163,std::size(LayerWholeRanges163)};
inline constexpr CodeRange DisplayProducerRanges163[]{
  {0x0,420,0x9cfcdf6fb4432c0aULL},
  {0x1a4,263,0x47943093d271a777ULL},
  {0x2ab,241,0x4615ecf3d80d23dULL},
  {0x39c,58,0xb799b0112a4a93cdULL},
  {0x3d6,48,0x7ce359ee45da6d31ULL},
  {0x406,6,0x8a00ba4b137050e6ULL},
  {0x40c,13,0xb043a210a7ccf0f0ULL},
  {0x419,10,0x6f5077d21ce143a4ULL},
  {0x423,34,0x52b2d2a05e67224aULL},
  {0x20118c6,334,0x908ea9cba40f1c40ULL},
};
inline constexpr NativeMethodProfile DisplayProducer163{"BeyondDynamicBone.SimulationManager","CalcDisplayPosition",0x54cd790,
    DisplayProducerRanges163,std::size(DisplayProducerRanges163)};
inline constexpr CodeRange DisplayKernelRanges163[]{
  {0x0,1617,0xc3d070e8a098c69eULL},
};
inline constexpr NativeMethodProfile DisplayKernel163{".CalcDisplayPositionJobKernels","CalcDisplayPositionKernel$BurstManaged",0x8a4e578,
    DisplayKernelRanges163,std::size(DisplayKernelRanges163)};
inline constexpr CodeRange FinishEdgeKernelRanges163[]{
  {0x0,3387,0x88e8cc7b85a76502ULL},
};
inline constexpr NativeMethodProfile FinishEdgeKernel163{".EdgeColliderCollisionConstraintJobKernels","EdgeColliderCollisionConstraintKernel$BurstManaged",0x89df874,
    FinishEdgeKernelRanges163,std::size(FinishEdgeKernelRanges163)};
inline constexpr CodeRange FinishReduceKernelRanges163[]{
  {0x0,451,0x7ab50d97172b7a20ULL},
};
inline constexpr NativeMethodProfile FinishReduceKernel163{".SolveEdgeBufferAndClearJobKernels","SolveEdgeBufferAndClearKernel$BurstManaged",0x89dbd9c,
    FinishReduceKernelRanges163,std::size(FinishReduceKernelRanges163)};
inline constexpr CodeRange FinishStepRanges163[]{
  {0x0,59,0xd7f2560f0dbcb951ULL},
  {0x3b,7094,0x3468577152853be8ULL},
  {0x1bf1,10,0x2cc67a23dd6f5101ULL},
  {0x1bfb,302,0x4b1e6bae2f904b4ULL},
  {0x200c3ec,2404,0x3689ee0a705cc694ULL},
};
inline constexpr NativeMethodProfile FinishStep163{"BeyondDynamicBone.SimulationManager","SimulationStepUpdate",0x54d4d70,
    FinishStepRanges163,std::size(FinishStepRanges163)};
inline constexpr CodeRange FinishDistanceRanges163[]{
  {0x0,71,0xe246058078aedaacULL},
  {0x47,559,0xc284b8cd599a5271ULL},
  {0x276,543,0x3847614e72559635ULL},
  {0x495,48,0x7432f610bb8e8f89ULL},
  {0x4c5,6,0x6219fabfd7d4b8f8ULL},
  {0x4cb,62,0x5bfcc935e2301669ULL},
  {0x509,10,0xf1a9f9ede4a20f1bULL},
  {0x513,47,0xe8ddddba6d6108f4ULL},
  {0x200aba0,351,0x8b97db8c81cf7728ULL},
};
inline constexpr NativeMethodProfile FinishDistance163{"BeyondDynamicBone.DistanceConstraint","SolverConstraint",0x54d6f20,
    FinishDistanceRanges163,std::size(FinishDistanceRanges163)};
inline constexpr CodeRange FinishCollisionRanges163[]{
  {0x0,64,0x9a0421bd9f40b993ULL},
  {0x40,1003,0x5d86cc1d7f099a3cULL},
  {0x42b,67,0xb64785a5a57b8a37ULL},
  {0x46e,10,0x14d88fd17bf81120ULL},
  {0x478,34,0xd668c26294a2438cULL},
  {0x1a3cd3e,1314,0x4e2b981b6c33de9ULL},
};
inline constexpr NativeMethodProfile FinishCollision163{"BeyondDynamicBone.ColliderCollisionConstraint","SolverConstraint",0x5ba76b0,
    FinishCollisionRanges163,std::size(FinishCollisionRanges163)};
inline constexpr CodeRange FinishEdgeScheduleRanges163[]{
  {0x0,624,0x4708a99b1a68419dULL},
};
inline constexpr NativeMethodProfile FinishEdgeSchedule163{"Generic","FinishEdgeSchedule",0x89d5e64,
    FinishEdgeScheduleRanges163,std::size(FinishEdgeScheduleRanges163)};
inline constexpr CodeRange FinishReduceScheduleRanges163[]{
  {0x0,584,0xe75250f006625440ULL},
};
inline constexpr NativeMethodProfile FinishReduceSchedule163{"Generic","FinishReduceSchedule",0x89d62bc,
    FinishReduceScheduleRanges163,std::size(FinishReduceScheduleRanges163)};
inline constexpr Body LayerBurst163[]{
  {2644,0x9832a5d126fed87fULL}, // 0x377e0
  {1708,0x7b53da92093e91f8ULL}, // 0x215780
  {2643,0xdf9df13fcfb9ab41ULL}, // 0x814d0
  {1733,0x2a7ee1e21691ad62ULL}, // 0x24f9b0
  {93,0xfdda4ec58ab9a8a9ULL}, // 0x38240
  {93,0x789ba6fdb88d7cb5ULL}, // 0x215e30
  {57,0xac552a2a30760959ULL}, // 0x81f30
  {57,0x63e0f3de8146176dULL}, // 0x250080
};
inline constexpr Body DisplayBurst163[]{
  {1521,0x6b17bb75ff89f753ULL}, // 0x1b2be0
  {1252,0x8466bfb7690bfa2dULL}, // 0x33de30
  {1573,0x6ae6f63a5febab0bULL}, // 0x17440
  {1358,0xab11bf70425c806cULL}, // 0x1f7e80
  {1673,0x8ede6f2389a6dd5aULL}, // 0x2a710
  {1483,0xa81bd11948ac4ca1ULL}, // 0x209b10
};
inline constexpr Body FinishBurst163[]{
  {5753,0xb4ea90487e5ce55aULL}, // 0x4b280
  {4296,0x9e205998cc781aa3ULL}, // 0x225db0
  {6292,0x3cccd9873e24a0e1ULL}, // 0x6a460
  {4794,0x260eb31c47925160ULL}, // 0x23dd10
  {6388,0x5bb73279da79ec31ULL}, // 0x35d10
  {4922,0xbc3adb9124df7022ULL}, // 0x214270
  {391,0x2d3ea1a51727bb50ULL}, // 0x6bf00
  {399,0x1b70d024b445e958ULL}, // 0x23f1c0
  {493,0xdc9d339b8e52e56dULL}, // 0x4efb0
  {490,0x695d40d5513f4f80ULL}, // 0x2288d0
  {614,0xc37752512b8ae693ULL}, // 0x49020
  {610,0x6424394a1e06dbaeULL}, // 0x223d40
};
inline constexpr size_t DisplayCounterCall163=0x25b;
inline constexpr size_t FinishFirstDistanceCall163=0x1331, FinishCollisionCall163=0x13b5,
    FinishSecondDistanceCall163=0x13e1, FinishEdgeCall163=0x1a3d0c8, FinishReduceCall163=0x1a3d24e;
}
