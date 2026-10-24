#ifndef VELOCITY_UTILITIES_RANDOM_RANDOM_H_
#define VELOCITY_UTILITIES_RANDOM_RANDOM_H_

#include <array>
#include <cstdint>

#include "utilities/math/math.h"

namespace random {

class Stream {
 public:
  void Seed(int seed);
  int Number();
  float Float(float min, float max);

 private:
  std::int32_t m_idum_{};
  std::int32_t m_iy_{};
  std::array<std::int32_t, 32> m_iv_{};
};

void Seed(std::uint32_t s);
int Integer(int min, int max);
float Floating(float min, float max);

float QuantizeAngle(float angle);
std::uint32_t SpreadSeed(const math::Vector3& angles, int tick);

float Normal(float mean, float stddev);
float NormalClamped(float mean, float stddev, float min, float max);

float HoldDuration(float mean = 0.085f, float stddev = 0.012f);
float SubtickFraction();

namespace engine {

void Seed(std::uint32_t s);
int RandomInt(int min, int max);
float RandomFloat(float min, float max);
std::int32_t Md5PseudoRandom(std::int32_t seed);

}  // namespace engine

}  // namespace random

#endif  // VELOCITY_UTILITIES_RANDOM_RANDOM_H_
