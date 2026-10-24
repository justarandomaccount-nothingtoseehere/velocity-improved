#include "pch/pch.h"

#include "utilities/random/random.h"

#include <bit>

#include "protection/game_addresses.h"
#include "utilities/addresses/addresses.h"
#include "utilities/memory/memory.h"

namespace random {

namespace {

void Sha1Process(const std::uint8_t* message, std::size_t length,
                 std::array<std::uint32_t, 5>& out) {
  std::array<std::uint32_t, 80> w{};
  std::array<std::uint8_t, 64> block{};
  std::array<std::uint32_t, 5> state{0x67452301, 0xefcdab89, 0x98badcfe,
                                     0x10325476, 0xc3d2e1f0};

  const auto total = length + 9;
  const auto chunks = (total + 63) / 64;
  const auto bits = static_cast<std::uint64_t>(length) * 8u;

  for (std::size_t index = 0; index < chunks; ++index) {
    block.fill(0);
    const auto offset = index * 64;
    const auto copy =
        std::min<std::size_t>(64, length - std::min(offset, length));
    if (offset < length) {
      std::memcpy(block.data(), message + offset, copy);
    }
    if (index == chunks - 1) {
      if (copy == 64) {
        block[0] = 0x80;
      } else {
        block[copy] = 0x80;
      }
      for (auto byte = 0; byte < 8; ++byte) {
        block[63 - byte] =
            static_cast<std::uint8_t>((bits >> (byte * 8)) & 0xffu);
      }
    }

    for (auto lane = 0; lane < 16; ++lane) {
      w[lane] = static_cast<std::uint32_t>(block[lane * 4]) << 24 |
                static_cast<std::uint32_t>(block[lane * 4 + 1]) << 16 |
                static_cast<std::uint32_t>(block[lane * 4 + 2]) << 8 |
                static_cast<std::uint32_t>(block[lane * 4 + 3]);
    }
    for (auto lane = 16; lane < 80; ++lane) {
      w[lane] =
          std::rotl(w[lane - 3] ^ w[lane - 8] ^ w[lane - 14] ^ w[lane - 16], 1);
    }

    auto a = state[0];
    auto b = state[1];
    auto c = state[2];
    auto d = state[3];
    auto e = state[4];
    for (auto lane = 0; lane < 80; ++lane) {
      std::uint32_t f{};
      std::uint32_t k{};
      if (lane < 20) {
        f = (b & c) | (~b & d);
        k = 0x5a827999;
      } else if (lane < 40) {
        f = b ^ c ^ d;
        k = 0x6ed9eba1;
      } else if (lane < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8f1bbcdc;
      } else {
        f = b ^ c ^ d;
        k = 0xca62c1d6;
      }
      const auto t = std::rotl(a, 5) + f + e + k + w[lane];
      e = d;
      d = c;
      c = std::rotl(b, 30);
      b = a;
      a = t;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
  }

  for (auto lane = 0; lane < 5; ++lane) {
    out[lane] = state[lane];
  }
}

}  // namespace

void Stream::Seed(int seed) {
  m_idum_ = seed > 0 ? -seed : seed;
  m_iy_ = 0;
}

int Stream::Number() {
  constexpr int kIa{16807};
  constexpr int kIm{0x7FFFFFFF};
  constexpr int kIq{127773};
  constexpr int kIr{2836};
  constexpr int kNtab{32};
  constexpr int kNdiv{0x4000000};

  auto idum = m_idum_;
  auto iy = m_iy_;
  if (idum <= 0 || iy == 0) {
    idum = -idum;
    if (idum < 1) {
      idum = 1;
    }
    for (auto j = kNtab + 7; j >= 0; --j) {
      idum = kIa * (idum % kIq) - kIr * (idum / kIq);
      if (idum < 0) {
        idum += kIm;
      }
      if (j < kNtab) {
        m_iv_[j] = idum;
      }
    }
    iy = m_iv_[0];
  }

  idum = kIa * (idum % kIq) - kIr * (idum / kIq);
  if (idum < 0) {
    idum += kIm;
  }
  m_idum_ = idum;

  const auto slot = iy / kNdiv;
  iy = m_iv_[slot];
  m_iv_[slot] = idum;
  m_iy_ = iy;
  return iy;
}

float Stream::Float(float min, float max) {
  auto value = static_cast<float>(this->Number()) * 4.6566129e-10f;
  if (value > 0.99999988f) {
    value = 0.99999988f;
  }
  return min + value * (max - min);
}

float QuantizeAngle(float angle) {
  if (angle < -180.0f || angle > 180.0f) {
    angle = std::min(
        std::max(angle - std::floorf(angle / 360.0f) * 360.0f, -180.0f),
        180.0f);
  }
  return std::floorf(angle) * 0.5f;
}

std::uint32_t SpreadSeed(const math::Vector3& angles, int tick) {
  const auto pitch = QuantizeAngle(angles.x);
  const auto yaw = QuantizeAngle(angles.y);

  std::array<std::uint8_t, 12> message{};
  std::memcpy(message.data(), &pitch, sizeof(pitch));
  std::memcpy(message.data() + 4, &yaw, sizeof(yaw));
  std::memcpy(message.data() + 8, &tick, sizeof(tick));

  std::array<std::uint32_t, 5> digest{};
  Sha1Process(message.data(), message.size(), digest);
  return digest[0];
}

static std::mt19937& Rng() {
  static std::mt19937 gen{std::random_device{}()};
  return gen;
}

void Seed(std::uint32_t s) { Rng().seed(s); }

int Integer(int min, int max) {
  return std::uniform_int_distribution<int>{min, max}(Rng());
}

float Floating(float min, float max) {
  return std::uniform_real_distribution<float>{min, max}(Rng());
}

float Normal(float mean, float stddev) {
  return std::normal_distribution<float>{mean, stddev}(Rng());
}

float NormalClamped(float mean, float stddev, float min, float max) {
  auto val = Normal(mean, stddev);
  if (val < min) {
    val = min;
  }

  if (val > max) {
    val = max;
  }

  return val;
}

float HoldDuration(float mean, float stddev) {
  return NormalClamped(mean, stddev, 0.045f, 0.140f);
}

float SubtickFraction() { return Floating(0.0f, std::nextafter(1.0f, 0.0f)); }

namespace engine {

void Seed(std::uint32_t s) {
  memory::Call<void>(MODULE_EXPORT("tier0.dll:RandomSeed"), s);
}

int RandomInt(int min, int max) {
  return memory::Call<int>(MODULE_EXPORT("tier0.dll:RandomInt"), min, max);
}

float RandomFloat(float min, float max) {
  return memory::Call<float>(MODULE_EXPORT("tier0.dll:RandomFloat"), min, max);
}

std::int32_t Md5PseudoRandom(std::int32_t seed) {
  return memory::Call<std::int32_t>(MODULE_EXPORT("tier0.dll:MD5_PseudoRandom"),
                                    seed);
}

}  // namespace engine

}  // namespace random