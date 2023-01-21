#include "aiLibrary.h"
#include "ecsTypes.h"
#include "aiUtils.h"
#include "math.h"
#include "raylib.h"
#include "blackboard.h"

struct CompoundNode : public BehNode
{
  std::vector<BehNode*> nodes;

  virtual ~CompoundNode()
  {
    for (BehNode *node : nodes)
      delete node;
    nodes.clear();
  }

  CompoundNode &pushNode(BehNode *node)
  {
    nodes.push_back(node);
    return *this;
  }
};

struct Sequence : public CompoundNode
{
  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    for (BehNode *node : nodes)
    {
      BehResult res = node->update(ecs, entity, bb);
      if (res != BEH_SUCCESS)
        return res;
    }
    return BEH_SUCCESS;
  }
};

struct Selector : public CompoundNode
{
  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    for (BehNode *node : nodes)
    {
      BehResult res = node->update(ecs, entity, bb);
      if (res != BEH_FAIL)
        return res;
    }
    return BEH_FAIL;
  }
};

struct Parallel : public CompoundNode
{
  BehResult update(flecs::world& ecs, flecs::entity entity, Blackboard& bb) override
  {
    for (BehNode* node : nodes)
    {
      BehResult res = node->update(ecs, entity, bb);
      if (res != BEH_RUNNING)
        return res;
    }
    return BEH_RUNNING;
  }
};

struct Or : public CompoundNode
{
  BehResult update(flecs::world& ecs, flecs::entity entity, Blackboard& bb) override
  {
    for (BehNode* node : nodes)
    {
      BehResult res = node->update(ecs, entity, bb);
      assert(res != BEH_RUNNING && "Children of OR-node are prohibited to be able to be RUNNING");
      if (res == BEH_SUCCESS)
        return res;
    }
    return BEH_FAIL;
  }
};

struct And : public CompoundNode
{
  BehResult update(flecs::world& ecs, flecs::entity entity, Blackboard& bb) override
  {
    for (BehNode* node : nodes)
    {
      BehResult res = node->update(ecs, entity, bb);
      assert(res != BEH_RUNNING && "Children of OR-node are prohibited to be able to be RUNNING");
      if (res == BEH_FAIL)
        return res;
    }
    return BEH_SUCCESS;
  }
};

struct Negate : public CompoundNode
{
  CompoundNode& pushNode(BehNode* node)
  {
    assert(nodes.size() == 0 && "NOT-node can only have one child");
    return CompoundNode::pushNode(node);
  }

  BehResult update(flecs::world& ecs, flecs::entity entity, Blackboard& bb) override
  {
    BehNode* node = nodes[0];
    BehResult res = node->update(ecs, entity, bb);
    assert(res != BEH_RUNNING && "Child of NEGATE-node is prohibited to be able to be RUNNING");

    return res == BEH_FAIL ? BEH_SUCCESS : BEH_FAIL;
  }
};

struct MoveToEntity : public BehNode
{
  size_t entityBb = size_t(-1); // wraps to 0xff...
  MoveToEntity(flecs::entity entity, const char *bb_name)
  {
    entityBb = reg_entity_blackboard_var<flecs::entity>(entity, bb_name);
  }

  BehResult update(flecs::world &, flecs::entity entity, Blackboard &bb) override
  {
    BehResult res = BEH_RUNNING;
    entity.set([&](Action &a, const Position &pos)
    {
      flecs::entity targetEntity = bb.get<flecs::entity>(entityBb);
      if (!targetEntity.is_alive())
      {
        res = BEH_FAIL;
        return;
      }
      targetEntity.get([&](const Position &target_pos)
      {
        if (pos != target_pos)
        {
          a.action = move_towards(pos, target_pos);
          res = BEH_RUNNING;
        }
        else
          res = BEH_SUCCESS;
      });
    });
    return res;
  }
};

struct IsLowHp : public BehNode
{
  float threshold = 0.f;
  IsLowHp(float thres) : threshold(thres) {}

  BehResult update(flecs::world &, flecs::entity entity, Blackboard &) override
  {
    BehResult res = BEH_SUCCESS;
    entity.get([&](const Hitpoints &hp)
    {
      res = hp.hitpoints < threshold ? BEH_SUCCESS : BEH_FAIL;
    });
    return res;
  }
};

struct FindEnemy : public BehNode
{
  size_t entityBb = size_t(-1);
  float distance = 0;
  FindEnemy(flecs::entity entity, float in_dist, const char *bb_name) : distance(in_dist)
  {
    entityBb = reg_entity_blackboard_var<flecs::entity>(entity, bb_name);
  }
  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    BehResult res = BEH_FAIL;
    static auto enemiesQuery = ecs.query<const Position, const Team>();
    entity.set([&](const Position &pos, const Team &t)
    {
      flecs::entity closestEnemy;
      float closestDist = FLT_MAX;
      Position closestPos;
      enemiesQuery.each([&](flecs::entity enemy, const Position &epos, const Team &et)
      {
        if (t.team == et.team)
          return;
        float curDist = dist(epos, pos);
        if (curDist < closestDist)
        {
          closestDist = curDist;
          closestPos = epos;
          closestEnemy = enemy;
        }
      });
      if (ecs.is_valid(closestEnemy) && closestDist <= distance)
      {
        bb.set<flecs::entity>(entityBb, closestEnemy);
        res = BEH_SUCCESS;
      }
    });
    return res;
  }
};

struct FindTreasure : public BehNode
{
  size_t entityBb = size_t(-1);

  FindTreasure(flecs::entity entity, const char *bb_name)
  {
    entityBb = reg_entity_blackboard_var<flecs::entity>(entity, bb_name);
  }

  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    BehResult res = BEH_FAIL;
    static auto treasureQuery = ecs.query<const IsTreasure, const Position>();

    entity.set([&](const Position &pos, const Team &t)
    {
      flecs::entity closest;
      float closestDist = FLT_MAX;
      Position closestPos;
      treasureQuery.each([&](flecs::entity treasure, const IsTreasure&, const Position &tpos)
      {
        float curDist = dist(tpos, pos);
        if (curDist < closestDist)
        {
          closestDist = curDist;
          closestPos = tpos;
          closest = treasure;
        }
      });
      if (ecs.is_valid(closest))
      {
        bb.set<flecs::entity>(entityBb, closest);
        res = BEH_SUCCESS;
      }
    });
    return res;
  }
};

struct Flee : public BehNode
{
  size_t entityBb = size_t(-1);
  Flee(flecs::entity entity, const char *bb_name)
  {
    entityBb = reg_entity_blackboard_var<flecs::entity>(entity, bb_name);
  }

  BehResult update(flecs::world &, flecs::entity entity, Blackboard &bb) override
  {
    BehResult res = BEH_RUNNING;
    entity.set([&](Action &a, const Position &pos)
    {
      flecs::entity targetEntity = bb.get<flecs::entity>(entityBb);
      if (!targetEntity.is_alive())
      {
        res = BEH_FAIL;
        return;
      }
      targetEntity.get([&](const Position &target_pos)
      {
        a.action = inverse_move(move_towards(pos, target_pos));
      });
    });
    return res;
  }
};

struct Patrol : public BehNode
{
  size_t pposBb = size_t(-1);
  float patrolDist = 1.f;
  Patrol(flecs::entity entity, float patrol_dist, const char *bb_name)
    : patrolDist(patrol_dist)
  {
    pposBb = reg_entity_blackboard_var<Position>(entity, bb_name);
    entity.set([&](Blackboard &bb, const Position &pos)
    {
      bb.set<Position>(pposBb, pos);
    });
  }

  BehResult update(flecs::world &, flecs::entity entity, Blackboard &bb) override
  {
    BehResult res = BEH_RUNNING;
    entity.set([&](Action &a, const Position &pos)
    {
      Position patrolPos = bb.get<Position>(pposBb);
      if (dist(pos, patrolPos) > patrolDist)
        a.action = move_towards(pos, patrolPos);
      else
        a.action = GetRandomValue(EA_MOVE_START, EA_MOVE_END - 1); // do a random walk
    });
    return res;
  }
};

struct ChooseWaypoint : public BehNode
{
  size_t waypointEntityBb = size_t(-1);
  size_t waypointPosBb = size_t(-1);

  ChooseWaypoint(flecs::entity entity, flecs::entity firstWaypoint, const char* bb_waypointName)
  {
    waypointEntityBb = reg_entity_blackboard_var<flecs::entity>(entity, bb_waypointName);
    waypointPosBb = reg_entity_blackboard_var<Position>(entity, bb_waypointName);

    firstWaypoint.get([&](Position &pos)
    {
      entity.set([&](Blackboard &bb)
      {
        bb.set<flecs::entity>(waypointEntityBb, firstWaypoint);
        bb.set<Position>(waypointPosBb, pos);
      });
    });
  }

  BehResult update(flecs::world &, flecs::entity entity, Blackboard &bb) override
  {
    BehResult ret = BEH_SUCCESS;

    entity.get([&](const Position &pos)
    {
      Position waypointPos = bb.get<Position>(waypointPosBb);

      // if entity has reached its waypoint
      if (dist(pos, waypointPos) < 0.5f)
      {
        flecs::entity waypointEntity = bb.get<flecs::entity>(waypointEntityBb);
        bool hasNext = waypointEntity.get([&](const NextEntity &nextWaypoint)
        {
          nextWaypoint.entity.get([&](const Position &nextPosition)
          {
            bb.set<flecs::entity>(waypointEntityBb, nextWaypoint.entity);
            bb.set<Position>(waypointPosBb, nextPosition);
          });
        });

        if (!hasNext)
          ret = BEH_FAIL;
      }
    });

    return ret;
  }
};

BehNode *sequence(const std::vector<BehNode*> &nodes)
{
  Sequence *seq = new Sequence;
  for (BehNode *node : nodes)
    seq->pushNode(node);
  return seq;
}

BehNode *selector(const std::vector<BehNode*> &nodes)
{
  Selector *sel = new Selector;
  for (BehNode *node : nodes)
    sel->pushNode(node);
  return sel;
}

BehNode *parallel(const std::vector<BehNode*>& nodes)
{
  Parallel *parallel = new Parallel;
  for (BehNode *node : nodes)
    parallel->pushNode(node);
  return parallel;
}

BehNode *negate(BehNode* node)
{
  Negate *negate = new Negate;
  negate->pushNode(node);
  return negate;
}

BehNode *orNode(const std::vector<BehNode*>& nodes)
{
  Or *orNode = new Or;
  for (BehNode *node : nodes)
    orNode->pushNode(node);
  return orNode;
}

BehNode *andNode(const std::vector<BehNode*>& nodes)
{
  And* andNode = new And;
  for (BehNode* node : nodes)
    andNode->pushNode(node);
  return andNode;
}

BehNode *move_to_entity(flecs::entity entity, const char *bb_name)
{
  return new MoveToEntity(entity, bb_name);
}

BehNode *is_low_hp(float thres)
{
  return new IsLowHp(thres);
}

BehNode *find_enemy(flecs::entity entity, float dist, const char *bb_name)
{
  return new FindEnemy(entity, dist, bb_name);
}

BehNode *find_treasure(flecs::entity entity, const char* bb_name)
{
  return new FindTreasure(entity, bb_name);
}

BehNode *flee(flecs::entity entity, const char *bb_name)
{
  return new Flee(entity, bb_name);
}

BehNode *patrol(flecs::entity entity, float patrol_dist, const char *bb_name)
{
  return new Patrol(entity, patrol_dist, bb_name);
}

BehNode *choose_waypoint(flecs::entity entity, flecs::entity firstWaypoint, const char* bb_waypointEntityName)
{
  return new ChooseWaypoint(entity, firstWaypoint, bb_waypointEntityName);
}
