#include "aiLibrary.h"
#include <flecs.h>
#include "ecsTypes.h"
#include <bx/rng.h>

static bx::RngShr3 rng;

class AttackEnemyState : public State
{
public:
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &/*ecs*/, flecs::entity /*entity*/) const override {}
};

template<typename T>
T sqr(T a){ return a*a; }

template<typename T, typename U>
static float dist_sq(const T &lhs, const U &rhs) { return float(sqr(lhs.x - rhs.x) + sqr(lhs.y - rhs.y)); }

template<typename T, typename U>
static float dist(const T &lhs, const U &rhs) { return sqrtf(dist_sq(lhs, rhs)); }

template<typename T, typename U>
static int move_towards(const T &from, const U &to)
{
  int deltaX = to.x - from.x;
  int deltaY = to.y - from.y;
  if (abs(deltaX) > abs(deltaY))
    return deltaX > 0 ? EA_MOVE_RIGHT : EA_MOVE_LEFT;
  return deltaY > 0 ? EA_MOVE_UP : EA_MOVE_DOWN;
}

static int inverse_move(int move)
{
  return move == EA_MOVE_LEFT ? EA_MOVE_RIGHT :
         move == EA_MOVE_RIGHT ? EA_MOVE_LEFT :
         move == EA_MOVE_UP ? EA_MOVE_DOWN :
         move == EA_MOVE_DOWN ? EA_MOVE_UP : move;
}


template<typename Callable>
static void on_closest_enemy_pos(flecs::world &ecs, flecs::entity entity, Callable c)
{
  static auto enemiesQuery = ecs.query<const Position, const Team>();
  entity.set([&](const Position &pos, const Team &t, Action &a)
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
    if (ecs.is_valid(closestEnemy))
      c(a, pos, closestPos);
  });
}


template<typename Callable>
static void on_closest_ally_pos(flecs::world &ecs, flecs::entity entity, Callable c)
{
  static auto charactersQuery = ecs.query<const Position, const Team>();
  entity.set([&](const Position &pos, const Team &t, Action &a)
  {
    flecs::entity closestAlly;
    float closestDist = FLT_MAX;
    Position closestPos;
    charactersQuery.each([&](flecs::entity character, const Position &epos, const Team &et)
    {
      if (t.team != et.team)
        return;
      float curDist = dist(epos, pos);
      if (curDist < closestDist)
      {
        closestDist = curDist;
        closestPos = epos;
        closestAlly = character;
      }
    });
    if (ecs.is_valid(closestAlly))
      c(a, pos, closestPos);
  });
}


template<typename Callable>
static void on_player_pos(flecs::world &ecs, flecs::entity entity, Callable c)
{
  static auto playerQuery = ecs.query<const Position, const IsPlayer>();
  entity.set([&](const Position &pos, const Team &t, Action &a)
  {
    flecs::entity player;
    Position playerPos;
    playerQuery.each([&](flecs::entity pentity, const Position &ppos, const IsPlayer &pIsPlayer)
    {
      player = pentity;
      playerPos = ppos;
    });
    if (ecs.is_valid(player))
      c(a, pos, playerPos);
  });
}

class MoveToEnemyState : public State
{
public:
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &ecs, flecs::entity entity) const override
  {
    on_closest_enemy_pos(ecs, entity, [&](Action &a, const Position &pos, const Position &enemy_pos)
    {
      a.action = move_towards(pos, enemy_pos);
    });
  }
};

class MoveToAllyState : public State
{
public:
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &ecs, flecs::entity entity) const override
  {
    on_closest_ally_pos(ecs, entity, [&](Action &a, const Position &pos, const Position &ally_pos)
    {
      a.action = move_towards(pos, ally_pos);
    });
  }
};

class MoveToPlayerState : public State
{
public:
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &ecs, flecs::entity entity) const override
  {
    on_player_pos(ecs, entity, [&](Action &a, const Position &pos, const Position &player_pos)
    {
      a.action = move_towards(pos, player_pos);
    });
  }
};

class FleeFromEnemyState : public State
{
public:
  FleeFromEnemyState() {}
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &ecs, flecs::entity entity) const override
  {
    on_closest_enemy_pos(ecs, entity, [&](Action &a, const Position &pos, const Position &enemy_pos)
    {
      a.action = inverse_move(move_towards(pos, enemy_pos));
    });
  }
};

class HealSelfState : public State
{
private:
  float _hpRegen = 10.f;
public:
  HealSelfState(float hpRegen): _hpRegen(hpRegen) {}
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &ecs, flecs::entity entity) const override
  {
    entity.set([&](Hitpoints& hp)
    {
      hp.hitpoints += _hpRegen;
    });
  }
};

class HealPlayerState : public State
{
public:
  HealPlayerState() {}
  uint8_t _cooldownCounter = 0;
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &ecs, flecs::entity entity) const override
  {
    entity.set([&](HealAbility& healAbility)
    {
      if (healAbility.cooldownCounter > 0)
          return;

      healAbility.cooldownCounter = healAbility.cooldown;
      
      static auto playerQuery = ecs.query<const IsPlayer>();
      playerQuery.each([&](flecs::entity pentity, const IsPlayer &pIsPlayer)
      {
        pentity.set([&](Hitpoints& hp)
        {
          hp.hitpoints += healAbility.restoration;
        });
      });
    });
  }
};

class PatrolState : public State
{
  float patrolDist;
public:
  PatrolState(float dist) : patrolDist(dist) {}
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &ecs, flecs::entity entity) const override
  {
    entity.set([&](const Position &pos, const PatrolPos &ppos, Action &a)
    {
      if (dist(pos, ppos) > patrolDist)
        a.action = move_towards(pos, ppos); // do a recovery walk
      else
      {
        // do a random walk
        a.action = EA_MOVE_START + (rng.gen() % (EA_MOVE_END - EA_MOVE_START));
      }
    });
  }
};

class NopState : public State
{
public:
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &ecs, flecs::entity entity) const override {}
};

class EnemyAvailableTransition : public StateTransition
{
  float triggerDist;
public:
  EnemyAvailableTransition(float in_dist) : triggerDist(in_dist) {}
  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {
    static auto enemiesQuery = ecs.query<const Position, const Team>();
    bool enemiesFound = false;
    entity.get([&](const Position &pos, const Team &t)
    {
      enemiesQuery.each([&](flecs::entity enemy, const Position &epos, const Team &et)
      {
        if (t.team == et.team)
          return;
        float curDist = dist(epos, pos);
        enemiesFound |= curDist <= triggerDist;
      });
    });
    return enemiesFound;
  }
};

class PlayerNearbyTransition : public StateTransition
{
  float triggerDist;
public:
  PlayerNearbyTransition(float in_dist) : triggerDist(in_dist) {}
  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {
    static auto playerQuery = ecs.query<const Position, const IsPlayer>();
    bool playerFound = false;
    entity.get([&](const Position &pos)
    {
      playerQuery.each([&](flecs::entity player, const Position &ppos, const IsPlayer &)
      {
        float curDist = dist(ppos, pos);
        playerFound = curDist <= triggerDist;
      });
    });
    return playerFound;
  }
};

class HitpointsLessThanTransition : public StateTransition
{
  float threshold;
public:
  HitpointsLessThanTransition(float in_thres) : threshold(in_thres) {}
  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {
    bool hitpointsThresholdReached = false;
    entity.get([&](const Hitpoints &hp)
    {
      hitpointsThresholdReached |= hp.hitpoints < threshold;
    });
    return hitpointsThresholdReached;
  }
};

class PlayerHitpointsLessThanTransition : public StateTransition
{
  float threshold;
public:
  PlayerHitpointsLessThanTransition(float in_thres) : threshold(in_thres) {}
  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {
    bool hitpointsThresholdReached = false;

    static auto playerQuery = ecs.query<const Hitpoints, const IsPlayer>();
    playerQuery.each([&](flecs::entity player, const Hitpoints &hp, const IsPlayer &)
    {
      hitpointsThresholdReached |= hp.hitpoints < threshold;
    });
    return hitpointsThresholdReached;
  }
};

class EnemyReachableTransition : public StateTransition
{
public:
  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {
    return false;
  }
};

class NegateTransition : public StateTransition
{
  const StateTransition *transition; // we own it
public:
  NegateTransition(const StateTransition *in_trans) : transition(in_trans) {}
  ~NegateTransition() override { delete transition; }

  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {
    return !transition->isAvailable(ecs, entity);
  }
};

class AndTransition : public StateTransition
{
  const StateTransition *lhs; // we own it
  const StateTransition *rhs; // we own it
public:
  AndTransition(const StateTransition *in_lhs, const StateTransition *in_rhs) : lhs(in_lhs), rhs(in_rhs) {}
  ~AndTransition() override
  {
    delete lhs;
    delete rhs;
  }

  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {
    return lhs->isAvailable(ecs, entity) && rhs->isAvailable(ecs, entity);
  }
};

class OrTransition : public StateTransition
{
    const StateTransition* lhs; // we own it
    const StateTransition* rhs; // we own it
public:
    OrTransition(const StateTransition* in_lhs, const StateTransition* in_rhs) : lhs(in_lhs), rhs(in_rhs) {}
    ~OrTransition() override
    {
        delete lhs;
        delete rhs;
    }

    bool isAvailable(flecs::world& ecs, flecs::entity entity) const override
    {
        return lhs->isAvailable(ecs, entity) || rhs->isAvailable(ecs, entity);
    }
};

// states
State *create_attack_enemy_state()
{
  return new AttackEnemyState();
}
State *create_move_to_enemy_state()
{
  return new MoveToEnemyState();
}

State *create_move_to_ally_state()
{
  return new MoveToAllyState();
}

State *create_move_to_player_state()
{
    return new MoveToPlayerState();
}

State *create_flee_from_enemy_state()
{
  return new FleeFromEnemyState();
}

State *create_heal_self_state(float hpRegen)
{
  return new HealSelfState(hpRegen);
}

State *create_heal_player_state()
{
    return new HealPlayerState();
}

State *create_patrol_state(float patrol_dist)
{
  return new PatrolState(patrol_dist);
}

State *create_nop_state()
{
  return new NopState();
}

// transitions
StateTransition *create_enemy_available_transition(float dist)
{
  return new EnemyAvailableTransition(dist);
}

StateTransition *create_player_nearby_transition(float dist)
{
    return new PlayerNearbyTransition(dist);
}

StateTransition *create_enemy_reachable_transition()
{
  return new EnemyReachableTransition();
}

StateTransition *create_hitpoints_less_than_transition(float thres)
{
  return new HitpointsLessThanTransition(thres);
}

StateTransition *create_player_hitpoints_less_than_transition(float thres)
{
  return new PlayerHitpointsLessThanTransition(thres);
}

StateTransition *create_negate_transition(StateTransition *in)
{
  return new NegateTransition(in);
}
StateTransition* create_and_transition(StateTransition* lhs, StateTransition* rhs)
{
    return new AndTransition(lhs, rhs);
}
StateTransition* create_or_transition(StateTransition* lhs, StateTransition* rhs)
{
    return new OrTransition(lhs, rhs);
}

