#pragma once

#include <flecs.h>
#include <memory>
#include "blackboard.h"

enum BehResult
{
  BEH_SUCCESS,
  BEH_FAIL,
  BEH_RUNNING
};

enum ReactionEvent
{
  ENEMY_IS_NEAR
};

struct BehNode
{
  virtual ~BehNode() {}
  virtual BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) = 0;

  /// <summary>
  /// Describes a reaction to a provided event
  /// </summary>
  /// <param name="event">Event to react to</param>
  /// <returns>
  /// BEH_SUCCESS, if the node succeeded to react
  /// BEH_FAIL, if the node failed to react or had no reaction implemented
  /// </returns>
  virtual BehResult react(ReactionEvent e, flecs::world& ecs, flecs::entity entity, Blackboard& bb) { return BEH_FAIL; }
};

struct BehaviourTree
{
  std::unique_ptr<BehNode> root = nullptr;

  BehaviourTree() = default;
  BehaviourTree(BehNode *r) : root(r) {}

  BehaviourTree(const BehaviourTree &bt) = delete;
  BehaviourTree(BehaviourTree &&bt) = default;

  BehaviourTree &operator=(const BehaviourTree &bt) = delete;
  BehaviourTree &operator=(BehaviourTree &&bt) = default;

  ~BehaviourTree() = default;

  void update(flecs::world &ecs, flecs::entity entity, Blackboard &bb)
  {
    root->update(ecs, entity, bb);
  }

  void react(ReactionEvent e, flecs::world &ecs, flecs::entity entity, Blackboard &bb)
  {
    root->react(e, ecs, entity, bb);
  }
};

