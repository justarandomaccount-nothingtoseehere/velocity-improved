#include "pch/pch.h"

#include "core/features/features.h"
#include "core/settings.h"
#include "core/systems/systems.h"
#include "protection/game_addresses.h"
#include "utilities/cstypes.h"
#include "utilities/math/math.h"

namespace features::movement {

namespace {

constexpr auto kFirstSubtickSlot{1.0f / 64.0f};

}  // namespace

void Slidewalk::OnCreateMove(systems::Input::Usercmd* cmd) const {
  const auto& cfg = settings::g_movement.m_slidewalk;
  if (!cfg.enabled.value) {
    return;
  }

  if (!CONVAR("sv_subtick_movement_view_angles")->Get<bool>()) {
    return;
  }

  const auto base = cmd->csgo_user_cmd.MutableBase();
  if (!base) {
    return;
  }

  const auto real_angles = systems::g_input.GetViewAngles();
  const auto slide = cfg.angle;
  if (std::fabsf(slide) < 0.01f) {
    return;
  }

  const auto render_mode =
      cfg.mode.value == settings::Movement::Slidewalk::Mode::kRender;

  if (render_mode &&
      (cmd->buttons.value &
       static_cast<std::uintptr_t>(cstypes::command_buttons::kInAttack))) {
    return;
  }

  auto yaw_delta = slide;
  math::helpers::NormalizeAngle(yaw_delta);

  if (render_mode) {
    auto render_yaw = real_angles.y + slide;
    math::helpers::NormalizeAngle(render_yaw);
    base->MutableViewangles()->SetY(render_yaw);

    yaw_delta = -slide;
    math::helpers::NormalizeAngle(yaw_delta);
  }

  if (std::fabsf(yaw_delta) < 0.01f) {
    return;
  }

  const auto step = systems::g_input.AcquireSubtickStepSorted(
      base->MutableSubtickMoves(), kFirstSubtickSlot);
  if (!step) {
    return;
  }

  step->SetYawDelta(yaw_delta);
  step->SetPitchDelta(0.0f);
}

}  // namespace features::movement
