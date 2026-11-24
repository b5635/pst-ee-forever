// SPDX-License-Identifier: GPL-2.0-or-later
// L0 tests for the real-time d20 (src/modules/rolls/die3d.h). Expected faces, numbers and the rest pose
// are tools/make_dice_bams.py's own (FACES, NUMS, tilt(0.12, -0.08) @ rot_to_front(NUMS.index(17))), so
// the 3D die and the BAM fallback show the same die.
#include <cmath>

#include "mini_test.h"
#include "modules/rolls/die3d.h"

using namespace pstf::rolls;
using namespace pstf::rolls::die3d;

TEST_CASE("die3d mesh: the BAM script's 20 faces in its order, numbers 2 1 8 6 5 7 4 3 20 19 ...") {
  const Mesh& m = D20();
  const int faces[20][3] = {{0, 1, 8},  {0, 9, 1},  {0, 4, 5},  {0, 8, 4},  {0, 5, 9},  {1, 7, 6},  {1, 6, 8},
                            {1, 9, 7},  {2, 10, 3}, {2, 3, 11}, {2, 5, 4},  {2, 4, 10}, {2, 11, 5}, {3, 6, 7},
                            {3, 10, 6}, {3, 7, 11}, {4, 8, 10}, {5, 11, 9}, {6, 10, 8}, {7, 9, 11}};
  const int nums[20] = {2, 1, 8, 6, 5, 7, 4, 3, 20, 19, 14, 18, 17, 13, 16, 15, 12, 11, 10, 9};
  for (int i = 0; i < 20; ++i) {
    CHECK(m.f[i][0] == faces[i][0] && m.f[i][1] == faces[i][1] && m.f[i][2] == faces[i][2]);
    CHECK(m.num[i] == nums[i]);
  }
}

TEST_CASE("die3d mesh: opposite faces sum to 21, every number once, faces wound outward") {
  const Mesh& m = D20();
  int seen[21] = {};
  for (int i = 0; i < 20; ++i) {
    ++seen[m.num[i]];
    const V3 c = FaceCentre(i);
    for (int j = 0; j < 20; ++j)
      if (Len(FaceCentre(j) + c) < 1e-6) CHECK(m.num[i] + m.num[j] == 21);
    const V3 n = Cross(m.v[m.f[i][1]] - m.v[m.f[i][0]], m.v[m.f[i][2]] - m.v[m.f[i][0]]);
    CHECK(Dot(n, c) > 0);
  }
  for (int n = 1; n <= 20; ++n) CHECK(seen[n] == 1);
}

TEST_CASE("die3d rest pose: 17 matches the BAM script's pose; each number rests on its own face, front") {
  const M3 r = RestPose(17);
  const double want[3][3] = {{0.346605, 0.759568, 0.550383}, {0.819442, 0.040355, -0.571739},
                             {-0.456486, 0.649175, -0.608434}};
  for (int i = 0; i < 3; ++i) {
    CHECK(std::fabs(r.r[i].x - want[i][0]) < 1e-5);
    CHECK(std::fabs(r.r[i].y - want[i][1]) < 1e-5);
    CHECK(std::fabs(r.r[i].z - want[i][2]) < 1e-5);
  }
  for (int n = 1; n <= 20; ++n) CHECK(D20().num[FrontFace(RestPose(n))] == n);
  CHECK(FrontFace(RestPose(27)) == FaceOf(20));  // totals past 20 rest on the 20 face
}

TEST_CASE("die3d throw: tumbles, ends exactly on the natural face at kLand, no frame jumps > 0.9 rad / 16 ms") {
  for (uint32_t seed : {1u, 7u, 12345u, 0xDEADBEEFu}) {
    const Throw th = MakeThrow(seed);
    for (int nat : {1, 11, 20}) {
      const Frame a = At(0, 0, nat, nat + 3, true, th);
      CHECK(PoseDistance(a.pose, RestPose(nat)) > 0.2);  // not resting at the click
      const Frame l = At(RollTiming::kLand, 0, nat, nat + 3, true, th);
      CHECK(PoseDistance(l.pose, RestPose(nat)) < 1e-9);
      CHECK(l.front_text == nat && l.front_face == FaceOf(nat));
      double worst = 0;
      M3 prev = At(0, 0, nat, nat + 3, true, th).pose;
      for (int ms = 16; ms < 8000; ms += 16) {
        const M3 p = At(ms, 0, nat, nat + 3, true, th).pose;
        const double d = PoseDistance(prev, p);
        if (d > worst) worst = d;
        prev = p;
      }
      CHECK(worst < 0.9);
      CHECK(worst > 0.1);  // it does move fast at the start
    }
  }
}

TEST_CASE("die3d total: front shows the natural until the half turn at kTotal, then the total; rests again") {
  const Throw th = MakeThrow(3);
  CHECK(At(RollTiming::kTotal - 200, 0, 12, 17, true, th).front_text == 12);
  CHECK(At(RollTiming::kTotal + 200, 0, 12, 17, true, th).front_text == 17);
  CHECK(At(RollTiming::kTotal + 200, 0, 12, 17, true, th).front_face == FaceOf(12));
  CHECK(PoseDistance(At(RollTiming::kTotal + 400, 0, 12, 17, true, th).pose, RestPose(12)) < 1e-6);
  // no modifier: no turn
  CHECK(PoseDistance(At(RollTiming::kTotal, 0, 12, 12, true, th).pose, RestPose(12)) < 1e-9);
}

TEST_CASE("die3d scale: 1 at rest, pulses at the landing, the total and the pop (success 5250, failure 5100)") {
  const Throw th = MakeThrow(3);
  CHECK(At(1000, 0, 12, 17, true, th).scale == 1.0);
  CHECK(At(RollTiming::kLand + 70, 0, 12, 17, true, th).scale > 1.1);
  CHECK(At(RollTiming::kTotal + 130, 0, 12, 17, true, th).scale > 1.11);
  CHECK(At(RollTiming::kPopOk + 144, 0, 12, 17, true, th).scale > 1.24);
  CHECK(At(RollTiming::kPopFail + 144, 0, 12, 4, false, th).scale > 1.24);
  CHECK(At(RollTiming::kPopOk + 144, 0, 12, 4, false, th).scale < 1.2);
  CHECK(At(7000, 0, 12, 17, true, th).scale == 1.0);
}

TEST_CASE("die3d idle: before the click the 20 is in front and the die sways a little") {
  const Throw th = MakeThrow(3);
  const Frame a = At(-1, 0, 0, 0, true, th), b = At(-1, 1000, 0, 0, true, th);
  CHECK(FrontFace(a.pose) == FaceOf(20) && a.front_text == 20);
  CHECK(PoseDistance(a.pose, b.pose) > 0.02 && PoseDistance(a.pose, RestPose(20)) < 0.2);
}

TEST_CASE("die3d atlas cells: big -9..40 at 0..49, small 1..20 at 50..69, inside the 10 x 7 grid") {
  CHECK(AtlasCell(-9, true) == 0);
  CHECK(AtlasCell(40, true) == 49);
  CHECK(AtlasCell(55, true) == 49);
  CHECK(AtlasCell(1, false) == 50);
  CHECK(AtlasCell(20, false) == 69);
  CHECK(AtlasCell(20, false) < kCols * kRows);
}
