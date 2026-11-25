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

// Installed November 2026 build: same checked job layouts; relocated producers and Burst consumers.
inline constexpr bool LayerAdapter1125Reviewed=true, DisplayAdapter1125Reviewed=true, FinishAdapter1125Reviewed=true;
inline constexpr CodeRange LayerKernelRanges1125[]{
  {0x0,1766,0xd8faee34fd4849d9ULL},
};
inline constexpr NativeMethodProfile LayerKernel1125{".SolverPointTriangleCrossFrameJobKernels","SolverPointTriangleCrossFrameKernel$BurstManaged",0x770d08c,
    LayerKernelRanges1125,std::size(LayerKernelRanges1125)};
inline constexpr CodeRange LayerIndexRanges1125[]{
  {0x0,1804,0x611d44f023b6cbabULL},
};
inline constexpr NativeMethodProfile LayerIndex1125{".SolverPointTriangleCrossFrameJob","Execute",0x7716294,
    LayerIndexRanges1125,std::size(LayerIndexRanges1125)};
inline constexpr CodeRange LayerWholeRanges1125[]{
  {0x0,62,0xfef2535231c73452ULL},
};
inline constexpr NativeMethodProfile LayerWhole1125{".SolverPointTriangleCrossFrameJob","Execute",0x77169a0,
    LayerWholeRanges1125,std::size(LayerWholeRanges1125)};
inline constexpr CodeRange DisplayProducerRanges1125[]{
  {0x0,420,0x71878d8965e13317ULL},
  {0x1a4,263,0x2dc62c0ce225c923ULL},
  {0x2ab,241,0x899266c03d060512ULL},
  {0x39c,58,0xb799b0112a4a93cdULL},
  {0x3d6,48,0xfa55558ed14ffa3cULL},
  {0x406,6,0xaaefadf2f10acb28ULL},
  {0x40c,13,0xb675d1fb5c6da367ULL},
  {0x419,10,0x667a7c8bbba0448dULL},
  {0x423,34,0x65f70592b04070eULL},
  {0x1e98ef4,334,0x86f131d35c26d01cULL},
};
inline constexpr NativeMethodProfile DisplayProducer1125{"BeyondDynamicBone.SimulationManager","CalcDisplayPosition",0x452a080,
    DisplayProducerRanges1125,std::size(DisplayProducerRanges1125)};
inline constexpr CodeRange DisplayKernelRanges1125[]{
  {0x0,1617,0xa12b440d0b34699fULL},
};
inline constexpr NativeMethodProfile DisplayKernel1125{".CalcDisplayPositionJobKernels","CalcDisplayPositionKernel$BurstManaged",0x7771f08,
    DisplayKernelRanges1125,std::size(DisplayKernelRanges1125)};
inline constexpr CodeRange FinishEdgeKernelRanges1125[]{
  {0x0,3387,0xa7394694603a164eULL},
};
inline constexpr NativeMethodProfile FinishEdgeKernel1125{".EdgeColliderCollisionConstraintJobKernels","EdgeColliderCollisionConstraintKernel$BurstManaged",0x76f6260,
    FinishEdgeKernelRanges1125,std::size(FinishEdgeKernelRanges1125)};
inline constexpr CodeRange FinishReduceKernelRanges1125[]{
  {0x0,451,0xd437acbc27e19ad9ULL},
};
inline constexpr NativeMethodProfile FinishReduceKernel1125{".SolveEdgeBufferAndClearJobKernels","SolveEdgeBufferAndClearKernel$BurstManaged",0x76f2788,
    FinishReduceKernelRanges1125,std::size(FinishReduceKernelRanges1125)};
inline constexpr CodeRange FinishStepRanges1125[]{
  {0x0,59,0x10938b492ddfcd8ULL},
  {0x3b,7094,0xa5c30d57e92a082cULL},
  {0x1bf1,10,0x142db5e1662cf0e7ULL},
  {0x1bfb,302,0xd6803d23e59de59eULL},
  {0x210ad80,2404,0x6eb6637996920fcULL},
};
inline constexpr NativeMethodProfile FinishStep1125{"BeyondDynamicBone.SimulationManager","SimulationStepUpdate",0x422cd00,
    FinishStepRanges1125,std::size(FinishStepRanges1125)};
inline constexpr CodeRange FinishDistanceRanges1125[]{
  {0x0,71,0x800d114794d04493ULL},
  {0x47,559,0x41811842eb6193abULL},
  {0x276,521,0x8ce56b564043a91aULL},
  {0x47f,48,0xca09c8fe0b419427ULL},
  {0x4af,6,0xc98cf74684c17e55ULL},
  {0x4b5,26,0x7b44c7d379245a75ULL},
  {0x4cf,10,0x4100e6939c28186bULL},
  {0x4d9,47,0x1cbeb721e20f51bULL},
  {0x1e994e4,351,0x7ad70cbcde33417dULL},
};
inline constexpr NativeMethodProfile FinishDistance1125{"BeyondDynamicBone.DistanceConstraint","SolverConstraint",0x4529930,
    FinishDistanceRanges1125,std::size(FinishDistanceRanges1125)};
inline constexpr CodeRange FinishCollisionRanges1125[]{
  {0x0,64,0x3f2bda491476539dULL},
  {0x40,1003,0xe72a4ea8e5c3901eULL},
  {0x42b,67,0xc1d2fd4478b406c6ULL},
  {0x46e,10,0xe382164082f00bULL},
  {0x478,34,0xec1b4f4d53df7b92ULL},
  {0x2109774,1314,0x28c5d58df15fa445ULL},
};
inline constexpr NativeMethodProfile FinishCollision1125{"BeyondDynamicBone.ColliderCollisionConstraint","SolverConstraint",0x422ec70,
    FinishCollisionRanges1125,std::size(FinishCollisionRanges1125)};
inline constexpr CodeRange FinishEdgeScheduleRanges1125[]{
  {0x0,624,0x8760a3c9c8471955ULL},
};
inline constexpr NativeMethodProfile FinishEdgeSchedule1125{"Generic","FinishEdgeSchedule",0x76ec850,
    FinishEdgeScheduleRanges1125,std::size(FinishEdgeScheduleRanges1125)};
inline constexpr CodeRange FinishReduceScheduleRanges1125[]{
  {0x0,584,0x60571942a39fc065ULL},
};
inline constexpr NativeMethodProfile FinishReduceSchedule1125{"Generic","FinishReduceSchedule",0x76ecca8,
    FinishReduceScheduleRanges1125,std::size(FinishReduceScheduleRanges1125)};
inline constexpr Body LayerBurst1125[]{
  {2644,0xf4b294bda2f2dc8cULL}, // 0x677a0
  {1708,0x8f0b0cf73f530a65ULL}, // 0x25f610
  {2643,0xcdadd8ceb6776d17ULL}, // 0xb29a0
  {1733,0x2d1f6069241beeb7ULL}, // 0x299db0
  {93,0xfdda4ec58ab9a8a9ULL}, // 0x68200
  {93,0x789ba6fdb88d7cb5ULL}, // 0x25fcc0
  {57,0xac552a2a30760959ULL}, // 0xb3400
  {57,0x63e0f3de8146176dULL}, // 0x29a480
};
inline constexpr Body DisplayBurst1125[]{
  {1521,0x800d6d896fdf2ac6ULL}, // 0xb6ec0
  {1252,0x6999385d2806901bULL}, // 0x29db50
  {1573,0xe1fbbdc454a893b2ULL}, // 0x46d80
  {1358,0xee3abbe06f8e70b7ULL}, // 0x241910
  {1673,0x158f87b6ba2524c7ULL}, // 0x5aee0
  {1483,0xb41dee027a41edc1ULL}, // 0x254130
};
inline constexpr Body FinishBurst1125[]{
  {5753,0x11142937b017e458ULL}, // 0x7c420
  {4296,0x875b0d15a489f4afULL}, // 0x2707a0
  {6292,0xcf5f3c75cab7e11bULL}, // 0x9b600
  {4794,0xded68bca415aa0fcULL}, // 0x287e40
  {6388,0x17e4212fe0b6d64cULL}, // 0x65cd0
  {4922,0x417b9cd8c7ea4f5aULL}, // 0x25e100
  {391,0x5da54e57cfebecULL}, // 0x9d0a0
  {399,0x2d09175b0a336cd3ULL}, // 0x2892f0
  {493,0xace02a84e3755b75ULL}, // 0x80150
  {490,0x49e85fa3a174c5d7ULL}, // 0x2732c0
  {614,0xc13aafa203d0bbb2ULL}, // 0x7a1c0
  {610,0xba9868223f55bc45ULL}, // 0x26e730
};
inline constexpr size_t DisplayCounterCall1125=0x25b;
inline constexpr size_t FinishFirstDistanceCall1125=0x1331, FinishCollisionCall1125=0x13b5,
    FinishSecondDistanceCall1125=0x13e1, FinishEdgeCall1125=0x2109afe, FinishReduceCall1125=0x2109c84;
}
