#include "roguelike.h"
#include "ecsTypes.h"
#include "raylib.h"
#include "stateMachine.h"
#include "aiLibrary.h"
#include "blackboard.h"
#include "math.h"

static void create_fuzzy_monster_beh(flecs::entity e)
{
  e.set(Blackboard{});
  BehNode *root =
    utility_selector({
      std::make_pair(
        sequence({
          find_enemy(e, 4.f, "flee_enemy"),
          flee(e, "flee_enemy")
        }),
        [](Blackboard &bb)
        {
          const float hp = bb.get<float>("hp");
          const float enemyDist = bb.get<float>("enemyDist");
          return (100.f - hp) * 5.f - 50.f * enemyDist;
        }
      ),
      std::make_pair(
        sequence({
          find_enemy(e, 3.f, "attack_enemy"),
          move_to_entity(e, "attack_enemy")
        }),
        [](Blackboard &bb)
        {
          const float enemyDist = bb.get<float>("enemyDist");
          return 100.f - 10.f * enemyDist;
        }
      ),
      std::make_pair(
        patrol(e, 2.f, "patrol_pos"),
        [](Blackboard &)
        {
          return 50.f;
        }
      ),
      std::make_pair(
        patch_up(100.f),
        [](Blackboard &bb)
        {
          const float hp = bb.get<float>("hp");
          return 140.f - hp;
        }
      )
    });
  e.add<WorldInfoGatherer>();
  e.set(BehaviourTree{root});
}

static void create_hive_beh(flecs::entity e, float atk_rad, float ally_rad,
                           float base_rad1, float base_rad2, flecs::entity base_wp)
{
  float b_atk_enemy = 100.0f;
  float k_atk_enemy = -b_atk_enemy / atk_rad;

  float k_atk_base = 100 / (base_rad1 - base_rad2);
  float b_atk_base = -k_atk_base * base_rad2;

  float k_ret_ally = 100 / ally_rad;

  float k_ret_base = 100 / (base_rad2 - base_rad1);
  float b_ret_base = -k_ret_base * base_rad1;

  e.set(Blackboard{});
  BehNode* root =
    utility_selector({
      std::make_pair(
        sequence({
          patrol(e, FLT_MAX, "patrol_pos")
        }),
        [](Blackboard& bb)
        {
          return 50.f;
        }
      ),
      std::make_pair(
        sequence({
          find_enemy(e, atk_rad, "attack_enemy"),
          move_to_entity(e, "attack_enemy")
        }),
        [b_atk_base, k_atk_base, b_atk_enemy, k_atk_enemy](Blackboard& bb)
        {
          const float dist_enemy = bb.get<float>("enemyDist");
          const float dist_base = bb.get<float>("baseDist");

          float ub = max(0.0f, b_atk_base + k_atk_base * dist_base);
          float ue = max(0.0f, b_atk_enemy + k_atk_enemy * dist_enemy);

          return min(ue, ub);
        }
      ),
      std::make_pair(
        sequence({
          choose_waypoint(e, base_wp, "base_wp"),
          move_to_entity(e, "base_wp")
        }),
        [k_ret_ally, b_ret_base, k_ret_base](Blackboard& bb)
        {
          const float dist_ally = bb.get<float>("allyDist");
          const float dist_base = bb.get<float>("baseDist");

          float ua = max(0.0f, k_ret_ally * dist_ally);
          float ub = max(0.0f, b_ret_base + k_ret_base * dist_base);

          float step = min(ua, ub);
          float slope = (ua + ub) / 5;
          return min(step, slope);
        }
      )
      });
  e.add<WorldInfoGatherer>();
  e.set(BehaviourTree{ root });
}

static void create_minotaur_beh(flecs::entity e)
{
  e.set(Blackboard{});
  BehNode *root =
    selector({
      sequence({
        is_low_hp(50.f),
        find_enemy(e, 4.f, "flee_enemy"),
        flee(e, "flee_enemy")
      }),
      sequence({
        find_enemy(e, 3.f, "attack_enemy"),
        move_to_entity(e, "attack_enemy")
      }),
      patrol(e, 2.f, "patrol_pos")
    });
  e.set(BehaviourTree{root});
}

static flecs::entity create_monster(flecs::world &ecs, int x, int y, Color col, const char *texture_src)
{
  flecs::entity textureSrc = ecs.entity(texture_src);
  return ecs.entity()
    .set(Position{x, y})
    .set(MovePos{x, y})
    .set(Hitpoints{100.f})
    .set(Action{EA_NOP})
    .set(Color{col})
    .add<TextureSource>(textureSrc)
    .set(StateMachine{})
    .set(Team{1})
    .set(NumActions{1, 0})
    .set(MeleeDamage{20.f})
    .set(Blackboard{});
}

static void create_player(flecs::world &ecs, int x, int y, const char *texture_src)
{
  flecs::entity textureSrc = ecs.entity(texture_src);
  ecs.entity("player")
    .set(Position{x, y})
    .set(MovePos{x, y})
    .set(Hitpoints{100.f})
    //.set(Color{0xee, 0xee, 0xee, 0xff})
    .set(Action{EA_NOP})
    .add<IsPlayer>()
    .set(Team{0})
    .set(PlayerInput{})
    .set(NumActions{2, 0})
    .set(Color{255, 255, 255, 255})
    .add<TextureSource>(textureSrc)
    .set(MeleeDamage{50.f});
}

static void create_heal(flecs::world &ecs, int x, int y, float amount)
{
  ecs.entity()
    .set(Position{x, y})
    .set(HealAmount{amount})
    .set(Color{0xff, 0x44, 0x44, 0xff});
}

static void create_powerup(flecs::world &ecs, int x, int y, float amount)
{
  ecs.entity()
    .set(Position{x, y})
    .set(PowerupAmount{amount})
    .set(Color{0xff, 0xff, 0x00, 0xff});
}

static flecs::entity create_waypoint(flecs::world& ecs, int x, int y)
{
  return ecs.entity().set(Position{ x, y });
}

static void register_roguelike_systems(flecs::world &ecs)
{
  ecs.system<PlayerInput, Action, const IsPlayer>()
    .each([&](PlayerInput &inp, Action &a, const IsPlayer)
    {
      bool left = IsKeyDown(KEY_LEFT);
      bool right = IsKeyDown(KEY_RIGHT);
      bool up = IsKeyDown(KEY_UP);
      bool down = IsKeyDown(KEY_DOWN);
      if (left && !inp.left)
        a.action = EA_MOVE_LEFT;
      if (right && !inp.right)
        a.action = EA_MOVE_RIGHT;
      if (up && !inp.up)
        a.action = EA_MOVE_UP;
      if (down && !inp.down)
        a.action = EA_MOVE_DOWN;
      inp.left = left;
      inp.right = right;
      inp.up = up;
      inp.down = down;
    });
  ecs.system<const Position, const Color>()
    .term<TextureSource>(flecs::Wildcard).not_()
    .each([&](const Position &pos, const Color color)
    {
      const Rectangle rect = {float(pos.x), float(pos.y), 1, 1};
      DrawRectangleRec(rect, color);
    });
  ecs.system<const Position, const Color>()
    .term<TextureSource>(flecs::Wildcard)
    .each([&](flecs::entity e, const Position &pos, const Color color)
    {
      const auto textureSrc = e.target<TextureSource>();
      DrawTextureQuad(*textureSrc.get<Texture2D>(),
          Vector2{1, 1}, Vector2{0, 0},
          Rectangle{float(pos.x), float(pos.y), 1, 1}, color);
    });
  ecs.system<const Position, const Hitpoints>()
    .each([&](const Position &pos, const Hitpoints &hp)
    {
      constexpr float hpPadding = 0.05f;
      const float hpWidth = 1.f - 2.f * hpPadding;
      const Rectangle underRect = {float(pos.x + hpPadding), float(pos.y-0.25f), hpWidth, 0.1f};
      DrawRectangleRec(underRect, BLACK);
      const Rectangle hpRect = {float(pos.x + hpPadding), float(pos.y-0.25f), hp.hitpoints / 100.f * hpWidth, 0.1f};
      DrawRectangleRec(hpRect, RED);
    });
}


void init_roguelike(flecs::world &ecs)
{
  register_roguelike_systems(ecs);

  ecs.entity("swordsman_tex")
    .set(Texture2D{LoadTexture("assets/swordsman.png")});
  ecs.entity("minotaur_tex")
    .set(Texture2D{LoadTexture("assets/minotaur.png")});

  ecs.observer<Texture2D>()
    .event(flecs::OnRemove)
    .each([](Texture2D texture)
      {
        UnloadTexture(texture);
      });

  // create_fuzzy_monster_beh(create_monster(ecs, 5, 5, Color{0xee, 0x00, 0xee, 0xff}, "minotaur_tex"));
  // create_fuzzy_monster_beh(create_monster(ecs, 10, -5, Color{0xee, 0x00, 0xee, 0xff}, "minotaur_tex"));
  // create_fuzzy_monster_beh(create_monster(ecs, -5, -5, Color{0x11, 0x11, 0x11, 0xff}, "minotaur_tex"));
  // create_fuzzy_monster_beh(create_monster(ecs, -5, 5, Color{0, 255, 0, 255}, "minotaur_tex"));

  Color antColor = Color{ 0, 255, 0, 255 };
  auto baseWp = create_waypoint(ecs, 0, 0);
  
  // Create hive
  {
    create_ant_beh(create_monster(ecs, +0, 0, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, -2, 0, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, -4, 0, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, -6, 0, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, -8, 0, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, +2, 0, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, +4, 0, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, +6, 0, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, +8, 0, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);

    create_ant_beh(create_monster(ecs, +0, +2, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, -2, +2, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, -4, +2, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, -6, +2, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, -8, +2, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, +2, +2, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, +4, +2, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, +6, +2, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, +8, +2, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);

    create_ant_beh(create_monster(ecs, +0, +4, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, -2, +4, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, -4, +4, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, -6, +4, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, -8, +4, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, +2, +4, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, +4, +4, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, +6, +4, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, +8, +4, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);

    create_ant_beh(create_monster(ecs, +0, +6, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, -2, +6, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, -4, +6, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, -6, +6, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, -8, +6, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, +2, +6, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, +4, +6, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, +6, +6, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, +8, +6, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);

    create_ant_beh(create_monster(ecs, +0, +8, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, -2, +8, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, -4, +8, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, -6, +8, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, -8, +8, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, +2, +8, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, +4, +8, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, +6, +8, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, +8, +8, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);

    create_ant_beh(create_monster(ecs, +0, -2, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, -2, -2, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, -4, -2, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, -6, -2, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, -8, -2, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, +2, -2, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, +4, -2, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, +6, -2, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, +8, -2, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);

    create_ant_beh(create_monster(ecs, +0, -4, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, -2, -4, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, -4, -4, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, -6, -4, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, -8, -4, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, +2, -4, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, +4, -4, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, +6, -4, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, +8, -4, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);

    create_ant_beh(create_monster(ecs, +0, -6, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, -2, -6, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, -4, -6, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, -6, -6, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, -8, -6, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, +2, -6, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, +4, -6, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, +6, -6, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, +8, -6, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);

    create_ant_beh(create_monster(ecs, +0, -8, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, -2, -8, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, -4, -8, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, -6, -8, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, -8, -8, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, +2, -8, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, +4, -8, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, +6, -8, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
    create_ant_beh(create_monster(ecs, +8, -8, antColor, "minotaur_tex"),
      /*atk_rad*/ 10, /*ally_rad*/ 12, /*base_rad1*/ 10, /*base_rad2*/ 15, baseWp);
  }

  create_player(ecs, 0, 10, "swordsman_tex");

  create_powerup(ecs, 7, 7, 10.f);
  create_powerup(ecs, 10, -6, 10.f);
  create_powerup(ecs, 10, -4, 10.f);

  create_heal(ecs, -5, -5, 50.f);
  create_heal(ecs, -5, 5, 50.f);

  ecs.entity("world")
    .set(TurnCounter{})
    .set(ActionLog{});
}

static bool is_player_acted(flecs::world &ecs)
{
  static auto processPlayer = ecs.query<const IsPlayer, const Action>();
  bool playerActed = false;
  processPlayer.each([&](const IsPlayer, const Action &a)
  {
    playerActed = a.action != EA_NOP;
  });
  return playerActed;
}

static bool upd_player_actions_count(flecs::world &ecs)
{
  static auto updPlayerActions = ecs.query<const IsPlayer, NumActions>();
  bool actionsReached = false;
  updPlayerActions.each([&](const IsPlayer, NumActions &na)
  {
    na.curActions = (na.curActions + 1) % na.numActions;
    actionsReached |= na.curActions == 0;
  });
  return actionsReached;
}

static Position move_pos(Position pos, int action)
{
  if (action == EA_MOVE_LEFT)
    pos.x--;
  else if (action == EA_MOVE_RIGHT)
    pos.x++;
  else if (action == EA_MOVE_UP)
    pos.y--;
  else if (action == EA_MOVE_DOWN)
    pos.y++;
  return pos;
}

static void push_to_log(flecs::world &ecs, const char *msg)
{
  static auto queryLog = ecs.query<ActionLog, const TurnCounter>();
  printf("pushing to log %s\n", msg);
  queryLog.each([&](ActionLog &l, const TurnCounter &c)
  {
    l.log.push_back(std::to_string(c.count) + ": " + msg);
    printf("pushed to log %s\n", msg);
    if (l.log.size() > l.capacity)
      l.log.erase(l.log.begin());
  });
}

static void process_actions(flecs::world &ecs)
{
  static auto processActions = ecs.query<Action, Position, MovePos, const MeleeDamage, const Team>();
  static auto processHeals = ecs.query<Action, Hitpoints>();
  static auto checkAttacks = ecs.query<const MovePos, Hitpoints, const Team>();
  // Process all actions
  ecs.defer([&]
  {
    processHeals.each([&](Action &a, Hitpoints &hp)
    {
      if (a.action != EA_HEAL_SELF)
        return;
      a.action = EA_NOP;
      push_to_log(ecs, "Monster healed itself");
      hp.hitpoints += 10.f;

    });
    processActions.each([&](flecs::entity entity, Action &a, Position &pos, MovePos &mpos, const MeleeDamage &dmg, const Team &team)
    {
      Position nextPos = move_pos(pos, a.action);
      bool blocked = false;
      checkAttacks.each([&](flecs::entity enemy, const MovePos &epos, Hitpoints &hp, const Team &enemy_team)
      {
        if (entity != enemy && epos == nextPos)
        {
          blocked = true;
          if (team.team != enemy_team.team)
          {
            push_to_log(ecs, "damaged entity");
            hp.hitpoints -= dmg.damage;
          }
        }
      });
      if (blocked)
        a.action = EA_NOP;
      else
        mpos = nextPos;
    });
    // now move
    processActions.each([&](Action &a, Position &pos, MovePos &mpos, const MeleeDamage &, const Team&)
    {
      pos = mpos;
      a.action = EA_NOP;
    });
  });

  static auto deleteAllDead = ecs.query<const Hitpoints>();
  ecs.defer([&]
  {
    deleteAllDead.each([&](flecs::entity entity, const Hitpoints &hp)
    {
      if (hp.hitpoints <= 0.f)
        entity.destruct();
    });
  });

  static auto playerPickup = ecs.query<const IsPlayer, const Position, Hitpoints, MeleeDamage>();
  static auto healPickup = ecs.query<const Position, const HealAmount>();
  static auto powerupPickup = ecs.query<const Position, const PowerupAmount>();
  ecs.defer([&]
  {
    playerPickup.each([&](const IsPlayer&, const Position &pos, Hitpoints &hp, MeleeDamage &dmg)
    {
      healPickup.each([&](flecs::entity entity, const Position &ppos, const HealAmount &amt)
      {
        if (pos == ppos)
        {
          hp.hitpoints += amt.amount;
          entity.destruct();
        }
      });
      powerupPickup.each([&](flecs::entity entity, const Position &ppos, const PowerupAmount &amt)
      {
        if (pos == ppos)
        {
          dmg.damage += amt.amount;
          entity.destruct();
        }
      });
    });
  });
}

template<typename T>
static void push_info_to_bb(Blackboard &bb, const char *name, const T &val)
{
  size_t idx = bb.regName<T>(name);
  bb.set(idx, val);
}

// sensors
static void gather_world_info(flecs::world &ecs)
{
  static auto gatherWorldInfo = ecs.query<Blackboard,
                                          const Position, const Hitpoints,
                                          const WorldInfoGatherer,
                                          const Team>();
  static auto alliesQuery = ecs.query<const Position, const Team>();
  gatherWorldInfo.each([&](Blackboard &bb, const Position &pos, const Hitpoints &hp,
                           WorldInfoGatherer, const Team &team)
  {
    // first gather all needed names (without cache)
    push_info_to_bb(bb, "hp", hp.hitpoints);
    float numAllies = 0; // note float
    float closestEnemyDist = FLT_MAX;
    float closestAllyDist = FLT_MAX;
    alliesQuery.each([&](const Position &apos, const Team &ateam)
    {
      constexpr float limitDist = 5.f;
      if (team.team == ateam.team && dist_sq(pos, apos) < sqr(limitDist))
        numAllies += 1.f;
      if (team.team != ateam.team)
      {
        const float enemyDist = dist(pos, apos);
        if (enemyDist < closestEnemyDist)
          closestEnemyDist = enemyDist;
      }
      if (team.team == ateam.team)
      {
        const float allyDist = dist(pos, apos);
        if (allyDist < closestAllyDist)
          closestAllyDist = allyDist;
      }
    });

    size_t waypointPosBb = bb.regName<Position>("base_wp");
    Position waypointPos = bb.get<Position>(waypointPosBb);
    float baseDist = dist(pos, waypointPos);

    push_info_to_bb(bb, "alliesNum", numAllies);
    push_info_to_bb(bb, "enemyDist", closestEnemyDist);
    push_info_to_bb(bb, "allyDist", closestAllyDist);
    push_info_to_bb(bb, "baseDist", baseDist);
  });
}

void process_turn(flecs::world &ecs)
{
  static auto stateMachineAct = ecs.query<StateMachine>();
  static auto behTreeUpdate = ecs.query<BehaviourTree, Blackboard>();
  static auto turnIncrementer = ecs.query<TurnCounter>();
  if (is_player_acted(ecs))
  {
    if (upd_player_actions_count(ecs))
    {
      // Plan action for NPCs
      gather_world_info(ecs);
      ecs.defer([&]
      {
        stateMachineAct.each([&](flecs::entity e, StateMachine &sm)
        {
          sm.act(0.f, ecs, e);
        });
        behTreeUpdate.each([&](flecs::entity e, BehaviourTree &bt, Blackboard &bb)
        {
          bt.update(ecs, e, bb);
        });
      });
      turnIncrementer.each([](TurnCounter &tc) { tc.count++; });
    }
    process_actions(ecs);
  }
}

void print_stats(flecs::world &ecs)
{
  static auto playerStatsQuery = ecs.query<const IsPlayer, const Hitpoints, const MeleeDamage>();
  playerStatsQuery.each([&](const IsPlayer &, const Hitpoints &hp, const MeleeDamage &dmg)
  {
    DrawText(TextFormat("hp: %d", int(hp.hitpoints)), 20, 20, 20, WHITE);
    DrawText(TextFormat("power: %d", int(dmg.damage)), 20, 40, 20, WHITE);
  });

  static auto actionLogQuery = ecs.query<const ActionLog>();
  actionLogQuery.each([&](const ActionLog &l)
  {
    int yPos = GetRenderHeight() - 20;
    for (const std::string &msg : l.log)
    {
      DrawText(msg.c_str(), 20, yPos, 20, WHITE);
      yPos -= 20;
    }
  });
}

