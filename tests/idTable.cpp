/**
* @license MIT
*/
// Fork: the table the scene finds objects in by id (n64/engine/include/scene/idTable.h).
#include "doctest/doctest.h"
#include <vector>

#include "scene/idTable.h"

namespace
{
  struct Thing { int tag{}; };
}

TEST_CASE("id table: finds what was inserted, and nothing else")
{
  P64::IdTable<Thing> table{};
  CHECK(table.find(300) == nullptr); // empty table
  CHECK(table.size() == 0);

  Thing a{1}, b{2};
  table.insert(300, &a);
  table.insert(301, &b);
  CHECK(table.find(300) == &a);
  CHECK(table.find(301) == &b);
  CHECK(table.find(302) == nullptr);
  CHECK(table.find(0) == nullptr);
  CHECK(table.size() == 2);
}

TEST_CASE("id table: a spawned prefab's consecutive ids, far more than the first size")
{
  // every object of a spawned prefab gets the next id: 2000 in a row, then every second one removed
  std::vector<Thing> things(2000);
  P64::IdTable<Thing> table{};
  for(uint32_t i = 0; i < things.size(); ++i) table.insert(static_cast<uint16_t>(256 + i), &things[i]);
  CHECK(table.size() == 2000);
  CHECK(table.capacity() >= 4000); // never more than half full
  for(uint32_t i = 0; i < things.size(); ++i) {
    REQUIRE(table.find(static_cast<uint16_t>(256 + i)) == &things[i]);
  }

  for(uint32_t i = 0; i < things.size(); i += 2) {
    REQUIRE(table.erase(static_cast<uint16_t>(256 + i), &things[i]));
  }
  CHECK(table.size() == 1000);
  for(uint32_t i = 0; i < things.size(); ++i) {
    Thing* found = table.find(static_cast<uint16_t>(256 + i));
    if(i % 2 == 0) REQUIRE(found == nullptr);
    else REQUIRE(found == &things[i]);
  }
}

TEST_CASE("id table: an id is free again after its object is gone")
{
  P64::IdTable<Thing> table{};
  Thing a{1}, b{2};
  table.insert(500, &a);
  CHECK_FALSE(table.erase(500, &b)); // another object: not its entry
  CHECK_FALSE(table.erase(501, &a)); // another id
  CHECK(table.find(500) == &a);

  CHECK(table.erase(500, &a));
  CHECK(table.find(500) == nullptr);
  CHECK_FALSE(table.erase(500, &a)); // already gone

  table.insert(500, &b);
  CHECK(table.find(500) == &b);
  CHECK(table.size() == 1);
}

TEST_CASE("id table: spawning and removing for a long time does not grow it")
{
  // projectiles: each lives for a moment, and ids keep counting up (and wrap around)
  P64::IdTable<Thing> table{};
  std::vector<Thing> pool(8);
  std::vector<uint16_t> idOf(pool.size(), 0); // the id each one has right now
  uint16_t id = 0x100;
  for(uint32_t round = 0; round < 100000; ++round) {
    const size_t slot = round % pool.size();
    Thing* thing = &pool[slot];
    if(round >= pool.size()) REQUIRE(table.erase(idOf[slot], thing));
    table.insert(id, thing);
    idOf[slot] = id;
    REQUIRE(table.find(id) == thing);
    ++id;
    if(id < 128) id = 128; // the scene's direct table covers the ids below
  }
  CHECK(table.size() == pool.size());
  CHECK(table.capacity() == 64); // removed entries are dropped when it is rebuilt
}

TEST_CASE("id table: two objects with one id are both kept, each removed by itself")
{
  // cannot happen until the 16-bit ids wrap around; nothing may be lost or left dangling then
  P64::IdTable<Thing> table{};
  Thing a{1}, b{2};
  table.insert(700, &a);
  table.insert(700, &b);
  CHECK(table.size() == 2);
  Thing* first = table.find(700);
  CHECK((first == &a || first == &b));

  CHECK(table.erase(700, &a));
  CHECK(table.find(700) == &b);
  CHECK(table.erase(700, &b));
  CHECK(table.find(700) == nullptr);
}

TEST_CASE("id table: clear empties it")
{
  P64::IdTable<Thing> table{};
  Thing a{1};
  table.insert(900, &a);
  table.clear();
  CHECK(table.size() == 0);
  CHECK(table.find(900) == nullptr);
  table.insert(900, &a);
  CHECK(table.find(900) == &a);
}
