#include "pch/pch.h"

#include "core/features/features.h"
#include "core/rendering/rendering.h"
#include "core/systems/systems.h"
#include "protection/game_addresses.h"
#include "utilities/addresses/addresses.h"
#include "utilities/logging/logging.h"
#include "utilities/memory/memory.h"
namespace features::combat {

void Misc::Antiaim::OnCreateMove(systems::Input::Usercmd* cmd) {
  this->m_antiaim_active_ = false;

  if (!settings::g_combat.m_antiaim.enabled.value) {
    return;
  }

  if (systems::g_local.IsInCinematic() || systems::g_local.IsInTimeFreeze()) {
    return;
  }

  if (settings::g_combat.m_antiaim.manual_left.value &&
      settings::g_combat.m_antiaim.manual_right.value) {
    settings::g_combat.m_antiaim.manual_right.value = false;
    settings::g_combat.m_antiaim.manual_right.bind.active = false;
  }

  if (settings::g_combat.m_antiaim.manual_left.value) {
    this->m_yaw_side_ = -1;
  } else if (settings::g_combat.m_antiaim.manual_right.value) {
    this->m_yaw_side_ = 1;
  } else {
    this->m_yaw_side_ = 0;
  }

  const auto local = systems::g_local.Get();
  const auto base = cmd->csgo_user_cmd.MutableBase();
  if (!base || !base->Viewangles()) {
    return;
  }

  const auto view_angles = systems::g_input.GetViewAngles();
  const auto& ctx = g_shared.Ctx();

  if (cmd->buttons.value & cstypes::command_buttons::kInUse) {
    return;
  }

  if (ctx.weapon_type == cstypes::weapon_type::kGrenade) {
    if (memory::Read<float>(ctx.weapon +
                            SCHEMA("C_BaseCSGrenade", "m_fThrowTime"_hash)) >
        0.0f)  // bail regardless of pin state
    {
      return;
    }
  }

  const auto move_type = memory::Read<int>(
      local.pawn + SCHEMA("C_BaseEntity", "m_nActualMoveType"_hash));
  if (move_type == cstypes::move_type::kLadder ||
      move_type == cstypes::move_type::kNoclip) {
    return;
  }

  if (this->IsNearLadder(local.pawn)) {
    return;
  }

  this->m_old_angles_ = view_angles;
  this->m_antiaim_active_ = true;

  this->m_modified_angles_ = this->m_old_angles_;
  this->m_modified_angles_.x = this->GetPitch(this->m_old_angles_.x);
  this->m_modified_angles_.y = this->GetYaw(this->m_old_angles_, local);

  math::helpers::NormalizeAngles(this->m_modified_angles_);

  base->MutableViewangles()->SetX(this->m_modified_angles_.x);
  base->MutableViewangles()->SetY(this->m_modified_angles_.y);
  base->MutableViewangles()->SetZ(this->m_modified_angles_.z);

  this->m_should_correct_ = true;

  const auto subtick_view_angles =
      CONVAR("sv_subtick_movement_view_angles")->Get<bool>();
  const auto on_ground = (systems::g_prediction.Pre().flags &
                          cstypes::entity_flags::kOnGround) != 0;

  if (subtick_view_angles && on_ground &&
      CONVAR("sv_quantize_movement_input")->Get<bool>()) {
    this->CorrectMovementReprojection(cmd);
  } else if (subtick_view_angles) {
    this->CorrectMovementYawDelta(cmd);
  } else {
    this->CorrectMovement(cmd);
  }
}

void Misc::Antiaim::OnRender(xdraw::draw_list& draw_list) const {
  if (!settings::g_combat.m_antiaim.enabled.value) {
    return;
  }

  if (!this->m_antiaim_active_) {
    return;
  }

  if (!systems::g_frame_data.Valid()) {
    return;
  }

  const auto origin = systems::g_frame_data.Origin();

#if defined(DEV)
  {
    const auto feet = systems::g_view.Project(origin);
    if (systems::g_view.ProjectionValid(feet)) {
      xdraw::push_font(
          rendering::g_fonts.smallest_pixel7[rendering::Fonts::Size::kNormal]);

      const auto text =
          std::format("ray yaw offset {:.1f}", this->m_head_offset_);
      const auto [text_w, text_h] = xdraw::measure_text(text);

      draw_list.text(feet.x - text_w * 0.5f, feet.y + 30.0f, text,
                     xdraw::color{255, 255, 255, 220},
                     xdraw::text_style::outlined);
      xdraw::pop_font();
    }
  }
#endif

  if (!settings::g_combat.m_antiaim.direction_indicator.value) {
    return;
  }
  const auto aa_yaw_rad =
      this->m_indicator_yaw_ * (std::numbers::pi_v<float> / 180.0f);

  constexpr auto kRadius{28.0f};
  constexpr auto kFeetOffset{-2.0f};
  constexpr auto kArcSweepDeg{60.0f};
  constexpr auto kArcSegments{48};
  constexpr auto kMaxThickness{3.0f};

  const auto& cfg = settings::g_combat.m_antiaim;
  const auto& color = cfg.direction_indicator_color;
  const auto base = math::Vector3{origin.x, origin.y, origin.z + kFeetOffset};

  const auto half_sweep =
      (kArcSweepDeg * 0.5f) * (std::numbers::pi_v<float> / 180.0f);
  const auto start_angle = aa_yaw_rad - half_sweep;
  const auto end_angle = aa_yaw_rad + half_sweep;
  const auto angle_step =
      (end_angle - start_angle) / static_cast<float>(kArcSegments);

  std::vector<math::Vector2> pts;
  pts.reserve(kArcSegments + 1);

  for (auto i = 0; i <= kArcSegments; ++i) {
    const auto angle = start_angle + angle_step * static_cast<float>(i);

    const auto world_pt =
        math::Vector3{base.x + std::cosf(angle) * kRadius,
                      base.y + std::sinf(angle) * kRadius, base.z};

    const auto sp = systems::g_view.Project(world_pt);

    if (!systems::g_view.ProjectionValid(sp)) {
      return;
    }

    pts.push_back({sp.x, sp.y});
  }

  if (pts.size() < 2) {
    return;
  }

  const auto total = static_cast<float>(pts.size() - 1);

  const auto fade_at = [](std::size_t idx, float total) -> float {
    const auto frac = static_cast<float>(idx) / total;
    const auto edge = 1.0f - std::fabsf(frac - 0.5f) * 2.0f;
    return edge * edge * edge * (edge * (edge * 6.0f - 15.0f) + 10.0f);
  };

  if (cfg.direction_indicator_glow) {
    auto& glow = xdraw::get_glow();

    for (auto i = 0ull; i + 1 < pts.size(); ++i) {
      const auto f0 = fade_at(i, total);
      const auto f1 = fade_at(i + 1, total);

      const auto ga0 = static_cast<std::uint8_t>(
          static_cast<float>(color.value.a) *
          cfg.direction_indicator_glow_strength * std::fmaxf(f0, 0.05f));
      const auto ga1 = static_cast<std::uint8_t>(
          static_cast<float>(color.value.a) *
          cfg.direction_indicator_glow_strength * std::fmaxf(f1, 0.05f));

      const auto thickness =
          (kMaxThickness + 2.0f) * ((f0 + f1) * 0.5f * 0.85f + 0.15f);

      const float seg[]{pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y};
      const xdraw::color cols[]{
          {color.value.r, color.value.g, color.value.b, ga0},
          {color.value.r, color.value.g, color.value.b, ga1}};

      glow.polyline_gradient(seg, cols, false, thickness);
    }
  }

  for (auto i = 0ull; i + 1 < pts.size(); ++i) {
    const auto f0 = fade_at(i, total);
    const auto f1 = fade_at(i + 1, total);

    const auto a0 =
        static_cast<std::uint8_t>(color.value.a * std::fmaxf(f0, 0.05f));
    const auto a1 =
        static_cast<std::uint8_t>(color.value.a * std::fmaxf(f1, 0.05f));

    const auto thickness = kMaxThickness * ((f0 + f1) * 0.5f * 0.85f + 0.15f);

    const float seg[]{pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y};
    const xdraw::color cols[]{
        {color.value.r, color.value.g, color.value.b, a0},
        {color.value.r, color.value.g, color.value.b, a1}};

    draw_list.polyline_gradient(seg, cols, false, thickness);
  }
}

float Misc::Antiaim::GetPitch(float view_pitch) {
  switch (settings::g_combat.m_antiaim.pitch) {
    case settings::Combat::Antiaim::PitchMode::kDown:
      return 89.0f;
    case settings::Combat::Antiaim::PitchMode::kUp:
      return -89.0f;
    default:
      return view_pitch;
  }
}

std::optional<float> Misc::Antiaim::MeasureEnemyRayYawOffset(
    const systems::Local::Snapshot& local) const {
  if (!local.pawn) {
    return std::nullopt;
  }

  const auto local_game_scene_node = memory::Read<std::uintptr_t>(
      local.pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
  if (!local_game_scene_node) {
    return std::nullopt;
  }

  const auto local_origin = memory::Read<math::Vector3>(
      local_game_scene_node + SCHEMA("CGameSceneNode", "m_vecAbsOrigin"_hash));

  const auto head_bone =
      systems::g_bones.Get(local.pawn, cstypes::bone_ids::kHead);
  const auto neck_bone =
      systems::g_bones.Get(local.pawn, cstypes::bone_ids::kNeck);
  if (head_bone.position.LengthSqr() < 1.0f ||
      neck_bone.position.LengthSqr() < 1.0f) {
    return std::nullopt;
  }

  const math::Vector2 head_horizontal{head_bone.position.x - local_origin.x,
                                      head_bone.position.y - local_origin.y};
  const math::Vector2 neck_horizontal{neck_bone.position.x - local_origin.x,
                                      neck_bone.position.y - local_origin.y};

  const auto body = (head_horizontal + neck_horizontal) * 0.5f;
  if (body.LengthSqr() < 1.0e-6f) {
    return std::nullopt;
  }

  const auto players =
      systems::g_entities.GetByType(systems::Entities::Type::kPlayer);

  auto best_dist_sq = std::numeric_limits<float>::max();
  auto best_pawn{std::uintptr_t{0}};
  auto enemy_found{false};

  for (const auto& p : players) {
    if (!p.ptr || p.ptr == local.controller) {
      continue;
    }

    if (!memory::Read<bool>(
            p.ptr + SCHEMA("CCSPlayerController", "m_bPawnIsAlive"_hash))) {
      continue;
    }

    const auto pawn_handle = memory::Read<std::uint32_t>(
        p.ptr + SCHEMA("CBasePlayerController", "m_hPawn"_hash));
    const auto pawn = systems::g_entities.Lookup(pawn_handle);

    if (!pawn || pawn == local.pawn) {
      continue;
    }

    const auto team =
        memory::Read<int>(pawn + SCHEMA("C_BaseEntity", "m_iTeamNum"_hash));
    if (!local.IsThisOtherTeam(team)) {
      continue;
    }

    const auto health =
        memory::Read<int>(pawn + SCHEMA("C_BaseEntity", "m_iHealth"_hash));
    if (health <= 0) {
      continue;
    }

    const auto enemy_game_scene_node = memory::Read<std::uintptr_t>(
        pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
    if (!enemy_game_scene_node) {
      continue;
    }

    const auto enemy_origin = memory::Read<math::Vector3>(
        enemy_game_scene_node +
        SCHEMA("CGameSceneNode", "m_vecAbsOrigin"_hash));
    const auto dx = enemy_origin.x - local_origin.x;
    const auto dy = enemy_origin.y - local_origin.y;
    const auto dist_sq = dx * dx + dy * dy;

    if (dist_sq < best_dist_sq) {
      best_dist_sq = dist_sq;
      best_pawn = pawn;
      enemy_found = true;
    }
  }

  if (!enemy_found) {
    return std::nullopt;
  }

  const auto enemy_scene = memory::Read<std::uintptr_t>(
      best_pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
  const auto enemy_abs = memory::Read<math::Vector3>(
      enemy_scene + SCHEMA("CGameSceneNode", "m_vecAbsOrigin"_hash));
  const auto enemy_eye =
      enemy_abs +
      memory::Read<math::Vector3>(
          best_pawn + SCHEMA("C_BaseModelEntity", "m_vecViewOffset"_hash));

  const auto toward_enemy =
      math::Vector2{enemy_eye.x - local_origin.x, enemy_eye.y - local_origin.y};
  if (toward_enemy.LengthSqr() < 1.0e-6f) {
    return std::nullopt;
  }

  static constexpr auto kRadToDeg{180.0f / std::numbers::pi_v<float>};

  const auto target_yaw =
      std::atan2f(toward_enemy.y, toward_enemy.x) * kRadToDeg;
  const auto body_yaw = std::atan2f(body.y, body.x) * kRadToDeg;

  return math::helpers::NormalizeYaw(target_yaw - body_yaw);
}

float Misc::Antiaim::GetYaw(const math::Vector3& view_angles,
                            const systems::Local::Snapshot& local) {
  auto base_yaw_offset{180.0f};

  const auto view_yaw = view_angles.y;
  auto base_yaw = view_yaw - base_yaw_offset;

  const auto local_game_scene_node = memory::Read<std::uintptr_t>(
      local.pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
  const auto local_origin = memory::Read<math::Vector3>(
      local_game_scene_node + SCHEMA("CGameSceneNode", "m_vecAbsOrigin"_hash));
  const auto players =
      systems::g_entities.GetByType(systems::Entities::Type::kPlayer);
  const auto eye_pos =
      local_origin +
      memory::Read<math::Vector3>(
          local.pawn + SCHEMA("C_BaseModelEntity", "m_vecViewOffset"_hash));

  if (settings::g_combat.m_antiaim.avoid_backstab.value) {
    constexpr auto kBackstabRangeSq = 350.0f * 350.0f;
    auto knife_dist = std::numeric_limits<float>::max();
    auto knife_yaw{0.0f};
    auto knife_found{false};

    for (const auto& p : players) {
      if (!p.ptr || p.ptr == local.controller) {
        continue;
      }

      if (!memory::Read<bool>(
              p.ptr + SCHEMA("CCSPlayerController", "m_bPawnIsAlive"_hash))) {
        continue;
      }

      const auto pawn_handle = memory::Read<std::uint32_t>(
          p.ptr + SCHEMA("CBasePlayerController", "m_hPawn"_hash));
      const auto pawn = systems::g_entities.Lookup(pawn_handle);

      if (!pawn || pawn == local.pawn) {
        continue;
      }

      const auto team =
          memory::Read<int>(pawn + SCHEMA("C_BaseEntity", "m_iTeamNum"_hash));
      if (!local.IsThisOtherTeam(team)) {
        continue;
      }

      const auto health =
          memory::Read<int>(pawn + SCHEMA("C_BaseEntity", "m_iHealth"_hash));
      if (health <= 0) {
        continue;
      }

      const auto enemy_game_scene_node = memory::Read<std::uintptr_t>(
          pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
      if (!enemy_game_scene_node) {
        continue;
      }

      const auto enemy_origin = memory::Read<math::Vector3>(
          enemy_game_scene_node +
          SCHEMA("CGameSceneNode", "m_vecAbsOrigin"_hash));
      const auto dx = enemy_origin.x - local_origin.x;
      const auto dy = enemy_origin.y - local_origin.y;
      const auto dist_sq = dx * dx + dy * dy;

      if (dist_sq > kBackstabRangeSq) {
        continue;
      }

      const auto weapon_services = memory::Read<std::uintptr_t>(
          pawn + SCHEMA("C_BasePlayerPawn", "m_pWeaponServices"_hash));
      if (!weapon_services) {
        continue;
      }

      const auto weapon_handle = memory::Read<std::uint32_t>(
          weapon_services +
          SCHEMA("CPlayer_WeaponServices", "m_hActiveWeapon"_hash));
      if (!weapon_handle) {
        continue;
      }

      const auto weapon = systems::g_entities.Lookup(weapon_handle);
      if (!weapon) {
        continue;
      }

      const auto weapon_vdata = memory::Read<std::uintptr_t>(
          weapon + SCHEMA("C_BaseEntity", "m_nSubclassID"_hash) + 0x8);
      if (!weapon_vdata) {
        continue;
      }

      const auto weapon_type = memory::Read<std::uint32_t>(
          weapon_vdata + SCHEMA("CCSWeaponBaseVData", "m_WeaponType"_hash));
      if (weapon_type != cstypes::weapon_type::kNife) {
        continue;
      }

      if (dist_sq < knife_dist) {
        knife_dist = dist_sq;
        knife_yaw = std::atan2f(dy, dx) * (180.0f / std::numbers::pi_v<float>);
        knife_found = true;
      }
    }

    if (knife_found) {
      this->m_indicator_yaw_ = knife_yaw;
      return knife_yaw;
    }
  }

  const auto pick_target_yaw = [&]() -> std::optional<float> {
    auto best_yaw = base_yaw;
    auto best_threat_score = std::numeric_limits<float>::max();

    for (const auto& p : players) {
      if (!p.ptr || p.ptr == local.controller) {
        continue;
      }

      if (!memory::Read<bool>(
              p.ptr + SCHEMA("CCSPlayerController", "m_bPawnIsAlive"_hash))) {
        continue;
      }

      const auto pawn_handle = memory::Read<std::uint32_t>(
          p.ptr + SCHEMA("CBasePlayerController", "m_hPawn"_hash));
      const auto pawn = systems::g_entities.Lookup(pawn_handle);

      if (!pawn || pawn == local.pawn) {
        continue;
      }

      const auto team =
          memory::Read<int>(pawn + SCHEMA("C_BaseEntity", "m_iTeamNum"_hash));
      if (!local.IsThisOtherTeam(team)) {
        continue;
      }

      if (memory::Read<int>(pawn + SCHEMA("C_BaseEntity", "m_iHealth"_hash)) <=
          0) {
        continue;
      }

      if (memory::Read<bool>(
              pawn + SCHEMA("C_CSPlayerPawn", "m_bGunGameImmunity"_hash))) {
        continue;
      }

      const auto enemy_game_scene_node = memory::Read<std::uintptr_t>(
          pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
      if (!enemy_game_scene_node) {
        continue;
      }

      const auto enemy_origin = memory::Read<math::Vector3>(
          enemy_game_scene_node +
          SCHEMA("CGameSceneNode", "m_vecAbsOrigin"_hash));
      const auto enemy_eye_pos =
          enemy_origin +
          memory::Read<math::Vector3>(
              pawn + SCHEMA("C_BaseModelEntity", "m_vecViewOffset"_hash));
      const auto angle_to_enemy =
          math::helpers::CalculateAngle(eye_pos, enemy_eye_pos);
      const auto fov =
          math::helpers::AngleDistance(view_angles, angle_to_enemy);
      const auto distance = eye_pos.Distance(enemy_eye_pos);

      // Match Requiem's threat order: crosshair first, then proximity, whether
      // the enemy is looking at us, and finally whether they are visible.
      auto threat_score = fov * 4.0f + distance * 0.01f;

      math::Vector3 enemy_forward{};
      const auto enemy_eye_angles = memory::Read<math::Vector3>(
          pawn + SCHEMA("C_CSPlayerPawn", "m_angEyeAngles"_hash));
      math::helpers::AngleVectorsLeft(enemy_eye_angles, &enemy_forward);
      const auto direction_to_us = (eye_pos - enemy_eye_pos).Normalized();
      threat_score -=
          std::clamp(enemy_forward.Dot(direction_to_us), -1.0f, 1.0f) * 25.0f;

      if (systems::g_tracing.IsVisible(eye_pos, enemy_eye_pos, pawn,
                                       local.pawn)) {
        threat_score -= 15.0f;
      }

      if (threat_score < best_threat_score) {
        best_threat_score = threat_score;
        best_yaw = angle_to_enemy.y - base_yaw_offset;
      }
    }

    return best_threat_score < std::numeric_limits<float>::max()
               ? std::optional<float>{best_yaw}
               : std::nullopt;
  };

  if (!settings::g_combat.m_antiaim.real_view_goal.value) {
    if (const auto target_yaw = pick_target_yaw()) {
      base_yaw = *target_yaw;
    }
  }
  auto yaw = base_yaw;
  if (this->m_yaw_side_ == -1) {
    yaw -= 90.0f;
  } else if (this->m_yaw_side_ == 1) {
    yaw += 90.0f;
  }

  this->m_head_offset_ = 0.0f;

  if (const auto ray_offset = this->MeasureEnemyRayYawOffset(local)) {
    const auto slash =
        std::max(settings::g_combat.m_antiaim.head_offset_slash.value, 1);
    const auto correction = *ray_offset / static_cast<float>(slash);

    this->m_head_offset_ = correction;

    if (settings::g_combat.m_antiaim.auto_yaw_adjust.value &&
        settings::g_combat.m_antiaim.pitch !=
            settings::Combat::Antiaim::PitchMode::kNone) {
      yaw += correction;
    }
  }

  this->m_indicator_yaw_ = yaw;

  return yaw;
}

void Misc::Antiaim::CorrectMovement(systems::Input::Usercmd* cmd) {
  if (!this->m_should_correct_) {
    return;
  }

  this->m_should_correct_ = false;

  const auto base = cmd->csgo_user_cmd.MutableBase();
  const auto forward_move = base->Forwardmove();
  const auto side_move = base->Leftmove();

  if (forward_move == 0.0f && side_move == 0.0f) {
    return;
  }

  math::Vector3 new_forward{}, new_left{};
  math::helpers::AngleVectorsLeft(this->m_modified_angles_, &new_forward,
                                  &new_left, nullptr);

  math::Vector3 old_forward{}, old_left{};
  math::helpers::AngleVectorsLeft(this->m_old_angles_, &old_forward, &old_left,
                                  nullptr);

  new_forward.z = 0.0f;
  new_left.z = 0.0f;
  old_forward.z = 0.0f;
  old_left.z = 0.0f;
  new_forward.Normalize();
  new_left.Normalize();
  old_forward.Normalize();
  old_left.Normalize();

  const auto intent = old_forward * forward_move + old_left * -side_move;
  const auto intent_len = intent.Length();

  if (intent_len == 0.0f) {
    return;
  }

  const auto intent_dir = intent / intent_len;
  const auto corrected_forward = new_forward.Dot(intent_dir) * intent_len;
  const auto corrected_side = -new_left.Dot(intent_dir) * intent_len;

  base->SetForwardmove(std::clamp(corrected_forward, -1.0f, 1.0f));
  base->SetLeftmove(std::clamp(corrected_side, -1.0f, 1.0f));

  if (systems::g_prediction.Pre().flags & cstypes::entity_flags::kOnGround) {
    auto buttons = cmd->buttons.value;
    buttons &=
        ~static_cast<std::uintptr_t>(cstypes::command_buttons::kInForward |
                                     cstypes::command_buttons::kInBack |
                                     cstypes::command_buttons::kInMoveleft |
                                     cstypes::command_buttons::kInMoveright);

    if (base->Forwardmove() > 0.0f) {
      buttons |= cstypes::command_buttons::kInForward;
    } else if (base->Forwardmove() < 0.0f) {
      buttons |= cstypes::command_buttons::kInBack;
    }

    if (base->Leftmove() > 0.0f) {
      buttons |= cstypes::command_buttons::kInMoveleft;
    } else if (base->Leftmove() < 0.0f) {
      buttons |= cstypes::command_buttons::kInMoveright;
    }

    cmd->buttons.value = buttons;
  }
}

void Misc::Antiaim::CorrectMovementReprojection(systems::Input::Usercmd* cmd) {
  if (!this->m_should_correct_) {
    return;
  }

  this->m_should_correct_ = false;

  const auto base = cmd->csgo_user_cmd.MutableBase();
  if (!base) {
    return;
  }

  const auto held = cmd->buttons.value;

  auto real_forward{0.0f};
  auto real_left{0.0f};

  if (held & cstypes::command_buttons::kInForward) {
    real_forward = 1.0f;
  } else if (held & cstypes::command_buttons::kInBack) {
    real_forward = -1.0f;
  }

  if (held & cstypes::command_buttons::kInMoveleft) {
    real_left = 1.0f;
  } else if (held & cstypes::command_buttons::kInMoveright) {
    real_left = -1.0f;
  }

  if (real_forward == 0.0f && real_left == 0.0f) {
    return;
  }

  if (!(systems::g_prediction.Pre().flags & cstypes::entity_flags::kOnGround)) {
    return;
  }

  auto yaw_delta = this->m_modified_angles_.y - this->m_old_angles_.y;
  math::helpers::NormalizeAngle(yaw_delta);

  if (std::fabsf(yaw_delta) <= 0.5f) {
    return;
  }

  const auto fake_yaw = this->m_modified_angles_.y;
  const auto fake_pitch = this->m_modified_angles_.x;
  const auto reverse = fake_pitch > 89.0f ? -1 : 1;

  auto wish_yaw = fake_yaw;
  math::helpers::NormalizeAngle(wish_yaw);

  const auto wish_pitch = std::clamp(fake_pitch, -89.0f, 89.0f);
  const math::Vector3 wish_angles{wish_pitch, wish_yaw, 0.0f};
  const math::Vector3 true_angles{this->m_old_angles_.x, this->m_old_angles_.y,
                                  0.0f};

  math::Vector3 wish_forward{}, wish_right{}, wish_up{};
  math::Vector3 true_forward{}, true_right{}, true_up{};
  math::helpers::AngleVectorsLeft(wish_angles, &wish_forward, &wish_right,
                                  &wish_up);
  math::helpers::AngleVectorsLeft(true_angles, &true_forward, &true_right,
                                  &true_up);

  const auto wish_forward_len = wish_forward.Length2d();
  const auto wish_right_len = wish_right.Length2d();
  const auto true_forward_len = true_forward.Length2d();
  const auto true_right_len = true_right.Length2d();

  if (wish_forward_len == 0.0f || wish_right_len == 0.0f ||
      true_forward_len == 0.0f || true_right_len == 0.0f) {
    return;
  }

  const math::Vector3 nff{wish_forward.x / wish_forward_len,
                          wish_forward.y / wish_forward_len, 0.0f};
  const math::Vector3 nfr{wish_right.x / wish_right_len,
                          wish_right.y / wish_right_len, 0.0f};
  const math::Vector3 ntf{true_forward.x / true_forward_len,
                          true_forward.y / true_forward_len, 0.0f};
  const math::Vector3 ntr{true_right.x / true_right_len,
                          true_right.y / true_right_len, 0.0f};

  const auto side_input = -real_left;
  const auto true_forward_x = ntf.x * real_forward;
  const auto true_forward_y = ntf.y * real_forward;
  const auto true_side_x = ntr.x * side_input;
  const auto true_side_y = ntr.y * side_input;

  auto corrected_forward = static_cast<float>(reverse) *
                           ((nff.x * true_side_x + nff.y * true_side_y) +
                            (nff.x * true_forward_x + nff.y * true_forward_y));
  auto corrected_side = (nfr.x * true_side_x + nfr.y * true_side_y) +
                        (nfr.x * true_forward_x + nfr.y * true_forward_y);

  corrected_forward = std::clamp(corrected_forward, -1.0f, 1.0f);
  corrected_side = std::clamp(corrected_side, -1.0f, 1.0f);

  const math::Vector3 dir{nff.x * corrected_forward + nfr.x * corrected_side,
                          nff.y * corrected_forward + nfr.y * corrected_side,
                          0.0f};
  const auto adjusted =
      std::atan2f(dir.y, dir.x) * (180.0f / std::numbers::pi_v<float>);

  auto roll = fake_pitch >= 0.0f ? -adjusted + fake_yaw + 90.0f
                                 : adjusted - fake_yaw - 90.0f;
  math::helpers::NormalizeAngle(roll);

  if (auto* view_angles = base->MutableViewangles()) {
    view_angles->SetX(fake_pitch);
    view_angles->SetZ(roll);
  }

  base->SetForwardmove(0.0f);
  base->SetLeftmove(1.0f);

  cmd->buttons.value &= ~static_cast<std::uintptr_t>(
      cstypes::command_buttons::kInForward | cstypes::command_buttons::kInBack |
      cstypes::command_buttons::kInMoveleft |
      cstypes::command_buttons::kInMoveright);
  cmd->buttons.value |= cstypes::command_buttons::kInMoveleft;
}

void Misc::Antiaim::CorrectMovementYawDelta(systems::Input::Usercmd* cmd) {
  if (!this->m_should_correct_) {
    return;
  }

  this->m_should_correct_ = false;

  const auto base = cmd->csgo_user_cmd.MutableBase();
  if (!base) {
    return;
  }

  const auto angles = base->Viewangles();
  if (!angles) {
    return;
  }

  const auto real_yaw = this->m_old_angles_.y;
  const auto fake_yaw = this->m_modified_angles_.y;

  auto yaw_delta = real_yaw - fake_yaw;
  math::helpers::NormalizeAngle(yaw_delta);

  if (std::fabsf(yaw_delta) < 0.01f) {
    return;
  }

  const auto forward_move = base->Forwardmove();
  const auto left_move = base->Leftmove();

  if (CONVAR("sv_quantize_movement_input")->Get<bool>() &&
      (forward_move != 0.0f || left_move != 0.0f)) {
    const auto quantized_forward = std::roundf(forward_move);
    const auto quantized_left = std::roundf(left_move);

    if (quantized_forward != 0.0f || quantized_left != 0.0f) {
      const auto intended_yaw =
          math::helpers::RadToDeg(std::atan2f(left_move, forward_move));
      const auto quantized_yaw = math::helpers::RadToDeg(
          std::atan2f(quantized_left, quantized_forward));
      yaw_delta += intended_yaw - quantized_yaw;
      math::helpers::NormalizeAngle(yaw_delta);
    }
  }

  const auto step = systems::g_input.AcquireSubtickStepSorted(
      base->MutableSubtickMoves(), 1.0f / 64.0f);
  if (!step) {
    return;
  }

  step->SetYawDelta(yaw_delta);
  step->SetPitchDelta(0.0f);
}

bool Misc::Antiaim::IsNearLadder(std::uintptr_t local_pawn) const {
  return false;  // ill do this lader.
}

namespace {

constexpr auto kDuckStandThreshold{0.75f};
constexpr auto kMaxPeekTicks{32};

math::Vector3 QuickpeekGroundSnap(std::uintptr_t skip_pawn,
                                  const math::Vector3& feet_pos) {
  const auto start = math::Vector3{feet_pos.x, feet_pos.y, feet_pos.z + 64.0f};
  const auto end = math::Vector3{feet_pos.x, feet_pos.y, feet_pos.z - 8192.0f};
  const auto tr = systems::g_tracing.Trace(start, end, skip_pawn);

  if (tr.fraction <= 0.0f || tr.fraction >= 0.997f) {
    return feet_pos;
  }

  auto out = tr.position;
  out.z += 1.0f;
  return out;
}

}  // namespace

float Misc::Duckpeek::GetDuckAmount(std::uintptr_t local_pawn) const {
  const auto movement_services = memory::Read<std::uintptr_t>(
      local_pawn + SCHEMA("C_BasePlayerPawn", "m_pMovementServices"_hash));

  if (!movement_services) {
    return 0.0f;
  }

  return memory::Read<float>(
      movement_services +
      SCHEMA("CCSPlayer_MovementServices", "m_flDuckAmount"_hash));
}

float Misc::Duckpeek::GetStandRate(std::uintptr_t local_pawn) const {
  const auto movement_services = memory::Read<std::uintptr_t>(
      local_pawn + SCHEMA("C_BasePlayerPawn", "m_pMovementServices"_hash));

  if (!movement_services) {
    return 0.0f;
  }

  const auto duck_speed = memory::Read<float>(
      movement_services +
      SCHEMA("CCSPlayer_MovementServices", "m_flDuckSpeed"_hash));

  return std::max(1.5f, duck_speed) * cstypes::kTickInterval;
}

bool Misc::Duckpeek::PlannedShotIsStanding(std::uintptr_t local_pawn) const {
  const auto& cfg = settings::g_combat.m_duckpeek;

  switch (cfg.shot_timing.value) {
    case settings::Combat::Duckpeek::ShotTiming::kFullStand:
      return true;
    case settings::Combat::Duckpeek::ShotTiming::kFixedDelay: {
      const auto delay = std::max(0, cfg.shot_delay.value);
      const auto ticks_left = std::max(0, delay - this->m_peek_ticks_);
      const auto amount =
          this->GetDuckAmount(local_pawn) -
          this->GetStandRate(local_pawn) * static_cast<float>(ticks_left);
      return amount <= kDuckStandThreshold;
    }
    case settings::Combat::Duckpeek::ShotTiming::kReleaseTick:
    default:
      return false;
  }
}

int Misc::Duckpeek::GetTicksUntilShot(std::uintptr_t local_pawn) const {
  const auto& cfg = settings::g_combat.m_duckpeek;

  switch (cfg.shot_timing.value) {
    case settings::Combat::Duckpeek::ShotTiming::kFullStand: {
      const auto rate = this->GetStandRate(local_pawn);
      if (rate <= 0.0f) {
        return 1;
      }

      const auto amount = this->GetDuckAmount(local_pawn);
      return static_cast<int>(std::ceil(amount / rate)) + 1;
    }
    case settings::Combat::Duckpeek::ShotTiming::kFixedDelay:
      return std::max(0, cfg.shot_delay.value - this->m_peek_ticks_) + 1;
    case settings::Combat::Duckpeek::ShotTiming::kReleaseTick:
    default:
      return 1;
  }
}

math::Vector3 Misc::Duckpeek::PredictShotVelocity(
    std::uintptr_t local_pawn, const math::Vector3& velocity) const {
  const auto ticks = this->GetTicksUntilShot(local_pawn);

  auto direction = velocity;
  direction.z = 0.0f;
  const auto initial_speed = direction.Length2d();

  if (ticks <= 0 || initial_speed < 1.0f) {
    return velocity;
  }

  const auto movement_services = memory::Read<std::uintptr_t>(
      local_pawn + SCHEMA("C_BasePlayerPawn", "m_pMovementServices"_hash));
  if (!movement_services) {
    return velocity;
  }

  const auto max_move_speed =
      memory::Read<float>(movement_services + SCHEMA("CPlayer_MovementServices",
                                                     "m_flMaxspeed"_hash));
  const auto duck_amount = memory::Read<float>(
      movement_services +
      SCHEMA("CCSPlayer_MovementServices", "m_flDuckAmount"_hash));
  const auto duck_speed = memory::Read<float>(
      movement_services +
      SCHEMA("CCSPlayer_MovementServices", "m_flDuckSpeed"_hash));

  const auto stand_rate = std::max(1.5f, duck_speed) * cstypes::kTickInterval;
  const auto& prestate = systems::g_prediction.Pre();
  const auto input =
      std::min(1.0f, std::sqrt(prestate.last_movement_impulses.x *
                                   prestate.last_movement_impulses.x +
                               prestate.last_movement_impulses.y *
                                   prestate.last_movement_impulses.y));

  const auto sv_friction = CONVAR("sv_friction")->Get<float>();
  const auto sv_stopspeed = CONVAR("sv_stopspeed")->Get<float>();
  const auto sv_accelerate = CONVAR("sv_accelerate")->Get<float>();
  const auto surface_friction = prestate.surface_friction;

  auto speed = initial_speed;

  for (auto i = 0; i < ticks; ++i) {
    const auto amount =
        std::max(0.0f, duck_amount - stand_rate * static_cast<float>(i + 1));
    const auto duck_scale = 1.0f - 0.66f * amount;
    const auto wish_speed =
        std::min(max_move_speed, input * max_move_speed * duck_scale);

    const auto control = std::max(speed, sv_stopspeed);
    speed = std::max(speed - sv_friction * surface_friction * control *
                                 cstypes::kTickInterval,
                     0.0f);

    const auto addspeed = wish_speed - speed;
    if (addspeed > 0.0f) {
      auto accel_speed = std::min(
          sv_accelerate * wish_speed * cstypes::kTickInterval, addspeed);
      accel_speed = std::min(accel_speed,
                             wish_speed * wish_speed * cstypes::kTickInterval);
      speed += accel_speed;
    }
  }

  return direction.Normalized() * speed;
}

Misc::Duckpeek::ShotDecision Misc::Duckpeek::PrepareShot(
    bool has_target, bool shot_viable, std::uintptr_t local_pawn) {
  const auto& cfg = settings::g_combat.m_duckpeek;

  if (this->m_state_ == State::kRecovering) {
    // The recovery window keeps the player standing after the peek shot.
    // It must not block further shots.
    return ShotDecision::kFire;
  }

  if (cfg.shot_timing.value ==
      settings::Combat::Duckpeek::ShotTiming::kReleaseTick) {
    if (this->m_state_ != State::kIdle) {
      // The release tick already passed without a shot.
      this->Abort();
      return ShotDecision::kHold;
    }

    if (!shot_viable) {
      return ShotDecision::kHold;
    }

    this->m_state_ = State::kPeeking;
    this->m_peek_ticks_ = 0;
    this->m_release_armed_ = true;
    return ShotDecision::kFirePeek;
  }

  if (this->m_state_ == State::kIdle) {
    if (!has_target) {
      return ShotDecision::kFire;
    }

    this->m_state_ = State::kPeeking;
    this->m_peek_ticks_ = 0;
    this->m_release_armed_ = true;
  }

  const auto ready =
      cfg.shot_timing.value ==
              settings::Combat::Duckpeek::ShotTiming::kFullStand
          ? this->GetDuckAmount(local_pawn) <= 0.0f
          : this->m_peek_ticks_ >= std::max(0, cfg.shot_delay.value);

  if (!ready || !shot_viable) {
    return ShotDecision::kHold;
  }

  return ShotDecision::kFirePeek;
}

void Misc::Duckpeek::OnFired() noexcept {
  this->m_state_ = State::kRecovering;
  this->m_reduck_ticks_ =
      std::max(0, settings::g_combat.m_duckpeek.reduck_delay.value);
}

void Misc::Duckpeek::OnShotSkipped() noexcept {
  if (this->m_state_ != State::kPeeking) {
    return;
  }

  if (settings::g_combat.m_duckpeek.shot_timing.value ==
      settings::Combat::Duckpeek::ShotTiming::kReleaseTick) {
    this->Abort();
  }
}

void Misc::Duckpeek::OnTargetLost() noexcept {
  if (this->m_state_ == State::kPeeking) {
    this->Abort();
  }
}

void Misc::Duckpeek::Abort() noexcept {
  this->m_state_ = State::kRecovering;
  this->m_reduck_ticks_ = 0;
  this->m_release_armed_ = false;
}

void Misc::Duckpeek::Reset() noexcept {
  this->m_state_ = State::kIdle;
  this->m_peek_ticks_ = 0;
  this->m_reduck_ticks_ = 0;
  this->m_release_armed_ = false;
}

void Misc::Duckpeek::OnCreateMove(systems::Input::Usercmd* cmd) {
  if (!cmd) {
    return;
  }

  const auto& cfg = settings::g_combat.m_duckpeek;

  if (!cfg.enabled.value) {
    this->Reset();
    return;
  }

  const auto local = systems::g_local.Get();
  if (!local.is_alive || !local.pawn || systems::g_local.IsInCinematic() ||
      systems::g_local.IsInTimeFreeze()) {
    this->Reset();
    return;
  }

  const auto weapon_type = g_shared.Ctx().weapon_type;
  if (weapon_type < cstypes::weapon_type::kPistol ||
      weapon_type > cstypes::weapon_type::kLmg) {
    this->Reset();
    return;
  }

  const auto on_ground = (systems::g_prediction.Pre().flags &
                          cstypes::entity_flags::kOnGround) != 0;
  if (!on_ground) {
    this->Reset();
    return;
  }

  switch (this->m_state_) {
    case State::kIdle:
      break;
    case State::kPeeking:
      ++this->m_peek_ticks_;
      if (this->m_peek_ticks_ > kMaxPeekTicks) {
        this->Abort();
      }
      break;
    case State::kRecovering:
      if (this->m_reduck_ticks_ > 0) {
        --this->m_reduck_ticks_;
      } else {
        this->m_state_ = State::kIdle;
      }
      break;
  }

  if (this->m_release_armed_) {
    this->m_release_armed_ = false;
    cmd->buttons.value &= ~cstypes::command_buttons::kInDuck;
    return;
  }

  if (this->m_state_ == State::kPeeking ||
      this->m_state_ == State::kRecovering) {
    cmd->buttons.value &= ~cstypes::command_buttons::kInDuck;
    return;
  }

  if (cfg.force_duck.value) {
    cmd->buttons.value |= cstypes::command_buttons::kInDuck;
  }
}

void Misc::Quickpeek::OnCreateMove(systems::Input::Usercmd* cmd) {
  if (!settings::g_combat.m_quickpeek.enabled.value) {
    this->Reset();
    return;
  }

  const auto local = systems::g_local.Get();
  const auto& ctx = g_shared.Ctx();

  if (ctx.weapon_type < cstypes::weapon_type::kPistol ||
      ctx.weapon_type > cstypes::weapon_type::kLmg) {
    this->Reset();
    return;
  }

  const auto base = cmd->csgo_user_cmd.MutableBase();
  constexpr auto kMovementCancelMask = static_cast<std::uintptr_t>(
      cstypes::command_buttons::kInForward | cstypes::command_buttons::kInBack |
      cstypes::command_buttons::kInMoveleft |
      cstypes::command_buttons::kInMoveright);
  const auto curr_movement_bits = cmd->buttons.value & kMovementCancelMask;

  const auto game_scene_node = memory::Read<std::uintptr_t>(
      local.pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
  const auto origin = memory::Read<math::Vector3>(
      game_scene_node + SCHEMA("CGameSceneNode", "m_vecAbsOrigin"_hash));

  if (this->m_saved_origin_.LengthSqr() < 0.001f) {
    this->m_saved_origin_ = QuickpeekGroundSnap(local.pawn, origin);
    this->m_should_retrack_ = false;
    this->m_fired_ = false;
    this->m_active_ = true;
    this->CreateParticle();
    this->m_prev_movement_bits_ = curr_movement_bits;
    return;
  }

  this->UpdateParticle();

  const auto distance = (origin - this->m_saved_origin_).Length2d();

  if (this->m_should_retrack_ &&
      (curr_movement_bits & ~this->m_prev_movement_bits_) != 0) {
    this->m_should_retrack_ = false;
  }

  if (this->m_should_retrack_ &&
      (systems::g_prediction.Pre().flags & cstypes::entity_flags::kOnGround)) {
    const auto velocity = memory::Read<math::Vector3>(
        local.pawn + SCHEMA("C_BaseEntity", "m_vecAbsVelocity"_hash));
    const auto speed = velocity.Length2d();

    if (distance < 5.0f && speed < 15.0f) {
      this->m_should_retrack_ = false;
      this->m_fired_ = false;
    } else if (distance < speed * 0.1f && speed > 15.0f) {
      const auto vel_angle = math::helpers::VectorToAngle(velocity * -1.0f);
      const auto yaw_diff = math::helpers::DegToRad(
          systems::g_input.GetViewAngles().y - vel_angle.y);

      base->SetForwardmove(std::cosf(yaw_diff));
      base->SetLeftmove(-std::sinf(yaw_diff));

      auto buttons = cmd->buttons.value;
      buttons &=
          ~static_cast<std::uintptr_t>(cstypes::command_buttons::kInForward |
                                       cstypes::command_buttons::kInBack |
                                       cstypes::command_buttons::kInMoveleft |
                                       cstypes::command_buttons::kInMoveright);

      if (base->Forwardmove() > 0.0f) {
        buttons |= cstypes::command_buttons::kInForward;
      } else if (base->Forwardmove() < 0.0f) {
        buttons |= cstypes::command_buttons::kInBack;
      }

      if (base->Leftmove() > 0.0f) {
        buttons |= cstypes::command_buttons::kInMoveleft;
      } else if (base->Leftmove() < 0.0f) {
        buttons |= cstypes::command_buttons::kInMoveright;
      }

      cmd->buttons.value = buttons;
    } else {
      const auto diff = this->m_saved_origin_ - origin;
      const auto angle_to_pos = math::helpers::VectorToAngle(diff);
      const auto yaw_diff = math::helpers::DegToRad(
          systems::g_input.GetViewAngles().y - angle_to_pos.y);

      base->SetForwardmove(std::cosf(yaw_diff));
      base->SetLeftmove(-std::sinf(yaw_diff));

      auto buttons = cmd->buttons.value;
      buttons &=
          ~static_cast<std::uintptr_t>(cstypes::command_buttons::kInForward |
                                       cstypes::command_buttons::kInBack |
                                       cstypes::command_buttons::kInMoveleft |
                                       cstypes::command_buttons::kInMoveright);

      if (base->Forwardmove() > 0.0f) {
        buttons |= cstypes::command_buttons::kInForward;
      } else if (base->Forwardmove() < 0.0f) {
        buttons |= cstypes::command_buttons::kInBack;
      }

      if (base->Leftmove() > 0.0f) {
        buttons |= cstypes::command_buttons::kInMoveleft;
      } else if (base->Leftmove() < 0.0f) {
        buttons |= cstypes::command_buttons::kInMoveright;
      }

      cmd->buttons.value = buttons;
    }
  }

  if ((cmd->buttons.value & cstypes::command_buttons::kInAttack) &&
      !g_rage.IsCockingRevolver()) {
    this->m_should_retrack_ = true;
    this->m_fired_ = true;
  }

  this->m_prev_movement_bits_ = curr_movement_bits;
}

void Misc::Quickpeek::ResetIfNeeded() {
  if (!this->m_active_) {
    return;
  }

  const auto local = systems::g_local.Get();
  if (!local.is_alive || !local.pawn) {
    this->Reset();
    return;
  }

  if (!settings::g_combat.m_quickpeek.enabled.value) {
    this->Reset();
  }
}

void Misc::Quickpeek::CreateParticle() {
  const auto particle_manager =
      memory::Read<std::uintptr_t>(addresses::globals::particle_manager);
  if (!particle_manager) {
    return;
  }

  constexpr auto kParticlePath{"particles/embedded/halo.vpcf"};

  if (!this->m_particle_loaded_) {
    struct BufferString {
      std::uint32_t m_unknown1{};
      std::uint32_t m_unknown2{0xc00000c8};

      union {
        std::uintptr_t m_str_ptr;
        std::uint8_t data[0xc8];
      };

      std::uintptr_t m_unknown3{0};
      std::uintptr_t m_unknown4{0};
    } buffer;

    memory::Call<void>(PATTERN(patterns::init_particle_path_buffer), &buffer,
                       kParticlePath);
    buffer.m_unknown4 = 'fcpv';
    memory::Call<void>(PATTERN(patterns::resource_system_precache),
                       addresses::globals::resource_system, &buffer, "");

    this->m_particle_loaded_ = true;
  }

  auto effect_index{kInvalidEffectIndex};
  memory::Call<int*>(PATTERN(patterns::particle_create_effect),
                     particle_manager, &effect_index, kParticlePath, 8, 0ll,
                     0ll, 0ll, 0);

  this->m_particle_effect_ = effect_index;

  if (effect_index == kInvalidEffectIndex) {
    return;
  }

  memory::Call<bool>(PATTERN(patterns::particle_set_control_point),
                     particle_manager, effect_index, 0, &this->m_saved_origin_,
                     0);
}

void Misc::Quickpeek::UpdateParticle() {
  if (this->m_particle_effect_ == kInvalidEffectIndex) {
    return;
  }

  const auto particle_manager =
      memory::Read<std::uintptr_t>(addresses::globals::particle_manager);
  if (!particle_manager) {
    return;
  }

  const auto& cfg = settings::g_combat.m_quickpeek;
  const auto& col = this->m_should_retrack_ ? cfg.retrack_color : cfg.color;
  const auto color = math::Vector3{static_cast<float>(col.value.r),
                                   static_cast<float>(col.value.g),
                                   static_cast<float>(col.value.b)};

  memory::Call<bool>(PATTERN(patterns::particle_set_control_point),
                     particle_manager, this->m_particle_effect_, 1, &color, 0);
  memory::Call<bool>(PATTERN(patterns::particle_set_control_point),
                     particle_manager, this->m_particle_effect_, 0,
                     &this->m_saved_origin_, 0);
}

void Misc::Quickpeek::ReleaseParticle() {
  if (this->m_particle_effect_ == kInvalidEffectIndex) {
    return;
  }

  const auto particle_manager =
      memory::Read<std::uintptr_t>(addresses::globals::particle_manager);
  if (particle_manager) {
    memory::Call<void>(PATTERN(patterns::particle_destroy_effect),
                       particle_manager, this->m_particle_effect_, true, true);
  }

  this->m_particle_effect_ = kInvalidEffectIndex;
}

void Misc::Quickpeek::Reset() {
  this->ReleaseParticle();
  this->m_saved_origin_ = {};
  this->m_should_retrack_ = false;
  this->m_fired_ = false;
  this->m_active_ = false;
  this->m_prev_movement_bits_ = 0;
}

void Misc::Autostop::OnCreateMove(systems::Input::Usercmd* cmd) {
  if (!features::combat::g_rage.ShouldStop()) {
    return;
  }

  const auto local = systems::g_local.Get();
  const auto movement_services = memory::Read<std::uintptr_t>(
      local.pawn + SCHEMA("C_BasePlayerPawn", "m_pMovementServices"_hash));
  const auto base = cmd->csgo_user_cmd.MutableBase();
  const auto& prestate = systems::g_prediction.Pre();
  const auto& ctx = g_shared.Ctx();

  if (!(prestate.flags & cstypes::entity_flags::kOnGround)) {
    return;
  }

  auto velocity = prestate.networked_velocity;
  auto speed = velocity.Length2d();

  if (speed <= 1.0f) {
    return;
  }

  const auto sv_friction = CONVAR("sv_friction")->Get<float>();
  const auto sv_stopspeed = CONVAR("sv_stopspeed")->Get<float>();
  const auto surface_friction = prestate.surface_friction;

  const auto control = std::fmaxf(speed, sv_stopspeed);
  const auto drop =
      control * sv_friction * surface_friction * cstypes::kTickInterval;
  const auto post_friction = std::fmaxf(speed - drop, 0.0f);

  if (post_friction > 0.0f) {
    velocity *= (post_friction / speed);
    speed = post_friction;
  } else {
    base->SetForwardmove(0.0f);
    base->SetLeftmove(0.0f);
    return;
  }

  if (speed < 2.0f) {
    base->SetForwardmove(0.0f);
    base->SetLeftmove(0.0f);
    return;
  }

  auto accel = CONVAR("sv_accelerate")->Get<float>();
  const auto accel_base = this->GetEffectiveAccelBase(
      local.pawn, movement_services, prestate.flags, ctx.weapon_max_speed);

  if (ctx.is_scoped) {
    const auto weapon_ratio = std::fminf(1.0f, ctx.weapon_max_speed / 250.0f);
    const auto v20 =
        std::fmaxf(250.0f,
                   memory::Read<float>(movement_services +
                                       SCHEMA("CPlayer_MovementServices",
                                              "m_flMaxspeed"_hash))) *
        weapon_ratio;
    const auto scoped_max = v20 * 0.52f;

    if (speed > scoped_max - 5.0f) {
      const auto t = 1.0f - std::fmaxf(0.0f, speed - (scoped_max - 5.0f)) /
                                std::fmaxf(0.01f, 5.0f);
      accel *= std::clamp(t, 0.0f, 1.0f);
    }
  }

  const auto wish_x = -velocity.x / speed;
  const auto wish_y = -velocity.y / speed;
  const auto accel_speed = std::fminf(
      accel * accel_base * surface_friction * cstypes::kTickInterval, speed);

  velocity.x += wish_x * accel_speed;
  velocity.y += wish_y * accel_speed;

  const auto move_magnitude =
      std::clamp(speed / ctx.weapon_max_speed, 0.0f, 1.0f);
  const auto yaw_rad =
      systems::g_input.GetViewAngles().y * (std::numbers::pi_v<float> / 180.0f);
  const auto sy = std::sinf(yaw_rad);
  const auto cy = std::cosf(yaw_rad);

  const auto forward_move =
      std::clamp((wish_x * cy + wish_y * sy) * move_magnitude, -1.0f, 1.0f);
  const auto left_move =
      std::clamp((wish_x * sy - wish_y * cy) * -move_magnitude, -1.0f, 1.0f);

  base->SetForwardmove(forward_move);
  base->SetLeftmove(left_move);

  const auto subtick_moves = base->MutableSubtickMoves();
  if (subtick_moves) {
    const auto step = systems::g_input.AcquireSubtickStep(subtick_moves);
    if (step) {
      step->SetButton(0);
      step->SetPressed(false);
      step->SetWhen(0.0f);
      step->SetAnalogForwardDelta(forward_move -
                                  prestate.last_movement_impulses.x);
      step->SetAnalogLeftDelta(left_move - prestate.last_movement_impulses.y);
    }
  }

  if (forward_move > 0.0f) {
    cmd->buttons.value |= cstypes::command_buttons::kInForward;
  } else if (forward_move < 0.0f) {
    cmd->buttons.value |= cstypes::command_buttons::kInBack;
  }

  if (left_move > 0.0f) {
    cmd->buttons.value |= cstypes::command_buttons::kInMoveleft;
  } else if (left_move < 0.0f) {
    cmd->buttons.value |= cstypes::command_buttons::kInMoveright;
  }
}

float Misc::Autostop::GetEffectiveAccelBase(std::uintptr_t local_pawn,
                                            std::uintptr_t movement_services,
                                            std::uint32_t flags,
                                            float max_weapon_speed) const {
  const auto max_speed_base =
      memory::Read<float>(movement_services + SCHEMA("CPlayer_MovementServices",
                                                     "m_flMaxspeed"_hash));
  const auto is_ducked = (flags & cstypes::entity_flags::kDucking) != 0;
  const auto duck_amount = memory::Read<float>(
      movement_services +
      SCHEMA("CCSPlayer_MovementServices", "m_flDuckAmount"_hash));
  const auto is_scoped = g_shared.Ctx().is_scoped;
  const auto is_ducking = is_ducked || duck_amount > 0.0f;
  const auto v19 = std::fmaxf(250.0f, max_speed_base);

  auto friction_scale{1.0f};

  if (CONVAR("sv_accelerate_use_weapon_speed")->Get<bool>()) {
    const auto weapon_ratio = std::fminf(1.0f, max_weapon_speed / 250.0f);

    if (!is_ducking && !is_scoped) {
      friction_scale = weapon_ratio;
    }
  }

  if (is_ducking) {
    friction_scale = std::fminf(0.34f, friction_scale);
  }

  auto accel_base = v19 * friction_scale;

  if (is_scoped && !is_ducking) {
    accel_base *= 0.52f;
  }

  return accel_base;
}

}  // namespace features::combat
