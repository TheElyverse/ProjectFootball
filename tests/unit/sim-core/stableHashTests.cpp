#include <catch2/catch_test_macros.hpp>
#include <cstdint>

#include "stableHash.hpp"

using ElyverseFootball::SimCore::StableHasher;

TEST_CASE("StableHasher matches the published FNV-1a 64 test vectors", "[stableHash]") {
  REQUIRE(StableHasher{}.value() == 0xcbf29ce484222325ULL);

  StableHasher single;
  single.addByte('a');
  REQUIRE(single.value() == 0xaf63dc4c8601ec8cULL);

  StableHasher text;
  for (const char character : {'f', 'o', 'o', 'b', 'a', 'r'}) {
    text.addByte(static_cast<std::uint8_t>(character));
  }
  REQUIRE(text.value() == 0x85944171f73967e8ULL);
}

TEST_CASE("StableHasher feeds integers as little-endian bytes", "[stableHash]") {
  StableHasher asInteger;
  asInteger.addU64(0x0807060504030201ULL);

  StableHasher asBytes;
  for (std::uint8_t byte = 1; byte <= 8; ++byte) {
    asBytes.addByte(byte);
  }

  REQUIRE(asInteger.value() == asBytes.value());
}

TEST_CASE("StableHasher distinguishes bit patterns, not numeric equality", "[stableHash]") {
  StableHasher positiveZero;
  positiveZero.addDouble(0.0);
  StableHasher negativeZero;
  negativeZero.addDouble(-0.0);
  StableHasher again;
  again.addDouble(0.0);

  REQUIRE(positiveZero.value() != negativeZero.value());
  REQUIRE(positiveZero.value() == again.value());
}

TEST_CASE("StableHasher prefixes strings with their length", "[stableHash]") {
  StableHasher first;
  first.addString("ab");
  first.addString("c");
  StableHasher second;
  second.addString("a");
  second.addString("bc");

  REQUIRE(first.value() != second.value());
}

TEST_CASE("StableHasher depends on the order of its input", "[stableHash]") {
  StableHasher forward;
  forward.addI64(1);
  forward.addBool(true);
  StableHasher backward;
  backward.addBool(true);
  backward.addI64(1);

  REQUIRE(forward.value() != backward.value());
}
