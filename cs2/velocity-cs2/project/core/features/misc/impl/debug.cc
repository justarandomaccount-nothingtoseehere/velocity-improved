#include "pch/pch.h"

#include "core/features/features.h"
#include "core/settings.h"
#include "core/systems/systems.h"
#include "utilities/math/math.h"

namespace features::misc {

void Debug::OnRender(xdraw::draw_list& draw_list) {
  auto& cfg = settings::g_misc.m_debug;

  if (!cfg.enabled) {
    return;
  }

  const auto local = systems::g_local.Get();
  if (!local.IsValid() || !local.ViewPawn() || !systems::g_view.HasCamera()) {
    return;
  }

  if (cfg.show_pose_params) {
    this->RenderPoseParameters(draw_list, local);
  }

  if (cfg.show_speed) {
    this->RenderSpeedDisplay(draw_list, local);
  }

  if (cfg.show_bone_revealer || cfg.show_bone_ids) {
    this->RenderBoneRevealer(draw_list, local);
  }

  if (cfg.show_bone_revealer) {
    this->RenderBoneGrid(draw_list, local);
  }
}

void Debug::OnFrameStageNotify() {
  const auto now = std::chrono::steady_clock::now();
  const auto elapsed =
      std::chrono::duration<float>(now - this->m_last_fps_update_).count();

  if (elapsed >= 1.0f) {
    this->m_fps_ = static_cast<float>(this->m_frames_) / elapsed;
    this->m_frames_ = 0;
    this->m_last_fps_update_ = now;
  }

  ++this->m_frames_;
}

void Debug::RenderBoneRevealer(xdraw::draw_list& draw_list,
                               const systems::Local::Snapshot& local) {
  auto& cfg = settings::g_misc.m_debug;

  const auto skeleton = systems::g_bones.GetSkeleton(local.ViewPawn());

  const auto radius = 4.0f * cfg.speed_scale;

  const auto min_id = std::clamp<int>(cfg.bone_id_min, 0,
                                      static_cast<int>(skeleton.size()) - 1);
  const auto max_id = std::clamp<int>(cfg.bone_id_max, 0,
                                      static_cast<int>(skeleton.size()) - 1);

  for (auto bone_id = min_id; bone_id <= max_id; ++bone_id) {
    const auto screen = systems::g_view.ProjectFull(skeleton[bone_id].position);

    if (!screen.on_screen || screen.w <= 0.0f) {
      continue;
    }

    if (cfg.show_bone_revealer) {
      draw_list.circle_filled(screen.screen.x, screen.screen.y, radius,
                              xdraw::color{173, 192, 255, 200});
    }

    if (cfg.show_bone_ids) {
      const auto label = std::format("{}", bone_id);
      const auto [tw, th] = xdraw::measure_text(label);

      draw_list.text(screen.screen.x - tw * 0.5f, screen.screen.y - radius - th,
                     label, xdraw::color{235, 238, 248, 255});
    }
  }
}

void Debug::RenderPoseParameters(xdraw::draw_list& draw_list,
                                 const systems::Local::Snapshot& local) {
  const auto& cfg = settings::g_misc.m_debug;

  static constexpr int kBones[]{
      cstypes::bone_ids::kHead,
      cstypes::bone_ids::kNeck,
      cstypes::bone_ids::kPelvis,
  };

  const auto skeleton = systems::g_bones.GetSkeleton(local.ViewPawn());

  const auto source_id = std::clamp<int>(
      cfg.bone_id_source, 0, static_cast<int>(std::size(kBones)) - 1);
  const auto& bone = skeleton[kBones[source_id]];

  const auto yaw =
      std::fmod(math::helpers::RadToDeg(bone.rotation.y) + 360.0f, 360.0f);
  const auto pitch =
      std::fmod(math::helpers::RadToDeg(bone.rotation.x) + 360.0f, 360.0f);
  const auto roll =
      std::fmod(math::helpers::RadToDeg(bone.rotation.z) + 360.0f, 360.0f);

  const auto pose_text =
      std::format("{:.1f} / {:.1f} / {:.1f}", pitch, yaw, roll);

  const auto [tw, th] = xdraw::measure_text(pose_text);

  const auto screen = systems::g_view.ProjectFull(bone.position);
  if (!screen.on_screen || screen.w <= 0.0f) {
    return;
  }

  const auto tx = screen.screen.x - tw * 0.5f;
  const auto ty = screen.screen.y - 30.0f;

  draw_list.text(tx, ty, pose_text, xdraw::color{173, 192, 255, 255});
}

void Debug::RenderSpeedDisplay(xdraw::draw_list& draw_list,
                               const systems::Local::Snapshot& local) {
  const auto& cfg = settings::g_misc.m_debug;

  const auto& prestate = systems::g_prediction.Pre();
  const auto speed = prestate.velocity.Length2d();
  const auto scaled_speed = speed * cfg.speed_scale;

  const auto speed_text = std::format("{:.1f} u/s", scaled_speed);

  const auto [tw, th] = xdraw::measure_text(speed_text);

  const auto [screen_w, screen_h] = xdraw::viewport_size();
  const auto cx = static_cast<float>(screen_w) * 0.5f;
  const auto cy = static_cast<float>(screen_h) * 0.5f;

  draw_list.text(cx - tw * 0.5f, cy + 60.0f, speed_text,
                 xdraw::color{173, 192, 255, 255});
}

void Debug::RenderBoneGrid(xdraw::draw_list& draw_list,
                           const systems::Local::Snapshot& local) {
  const auto skeleton = systems::g_bones.GetSkeleton(local.ViewPawn());

  for (auto bone_id = 0u; bone_id < skeleton.size(); ++bone_id) {
    const auto screen = systems::g_view.ProjectFull(skeleton[bone_id].position);

    if (!screen.on_screen || screen.w <= 0.0f) {
      continue;
    }

    draw_list.line(screen.screen.x - 3.0f, screen.screen.y,
                   screen.screen.x + 3.0f, screen.screen.y,
                   xdraw::color{173, 192, 255, 255}, 1.0f);
    draw_list.line(screen.screen.x, screen.screen.y - 3.0f, screen.screen.x,
                   screen.screen.y + 3.0f, xdraw::color{173, 192, 255, 255},
                   1.0f);
  }
}

}  // namespace features::misc
