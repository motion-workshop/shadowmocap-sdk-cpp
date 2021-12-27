#include "catch.hpp"

#include <shadowmocap/channel.hpp>

#include <string>

TEST_CASE(
  "bitwise operators work for channel enums and integer types",
  "[shadowmocap][channel]")
{
  using namespace shadowmocap;

  auto mask = channel::Gq | channel::Gdq;
  REQUIRE(mask == 3);

  mask |= channel::a;
  REQUIRE(mask == 259);

  mask |= (channel::m | channel::g);
  REQUIRE(mask == 1795);

  REQUIRE((mask & channel::Gq) != 0);
  REQUIRE((mask & channel::Lq) == 0);

  REQUIRE((mask & (channel::Gq | channel::Lq)) != 0);
  REQUIRE((mask & (channel::Lq | channel::Bq)) == 0);

  constexpr auto cmask = (channel::la | channel::a) & channel::Gq;
  REQUIRE(cmask == 0);
}