#include "pch/pch.h"

#include "core/features/features.h"
#include "core/settings.h"
#include "core/systems/systems.h"
#include "utilities/cstypes.h"

namespace features::movement {

void StaticLegs::OnCreateMove(systems::Input::Usercmd* cmd) {
  this->m_active_this_tick_ = false;

  if (!settings::g_movement.static_legs.value || !cmd) {
    return;
  }

  const auto base = cmd->csgo_user_cmd.MutableBase();
  if (!base) {
    return;
  }

  const auto local = systems::g_local.Get();
  if (!local.pawn) {
    return;
  }

  const auto& prestate = systems::g_prediction.Pre();
  if (!(prestate.flags & cstypes::entity_flags::kOnGround)) {
    return;
  }

  if (cmd->buttons.value & cstypes::command_buttons::kInJump) {
    return;
  }

  const auto forward_move = base->Forwardmove();
  const auto left_move = base->Leftmove();

  if (std::fabsf(forward_move) < 0.0001f && std::fabsf(left_move) < 0.0001f) {
    return;
  }

  const auto step = systems::g_input.AcquireSubtickStepSorted(
      base->MutableSubtickMoves(), 1.0f);
  if (!step) {
    return;
  }

  step->SetButton(0);
  step->SetPressed(false);
  step->SetAnalogForwardDelta(-forward_move);
  step->SetAnalogLeftDelta(-left_move);

  this->m_active_this_tick_ = true;
}

}  // namespace features::movement
