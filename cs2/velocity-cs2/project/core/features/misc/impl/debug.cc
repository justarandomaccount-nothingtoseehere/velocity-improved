#define NOMINMAX
#include "pch/pch.h"

#include "core/features/misc/impl/debug.h"

#include "core/rendering/rendering.h"
#include "core/systems/systems.h"
#include "utilities/math/math.h"

namespace features::misc::impl {

void Debug::OnRender(xdraw::draw_list& draw_list) {
  if (!settings::g_misc.m_debug.enabled) {
    return;
  }

  const auto& local = systems::g_local.Get();
  if (!local.IsValid()) {
    return;
  }

  const auto scale = settings::g_misc.m_debug.speed_scale;
  const auto draw_scale = 1.0f / scale;

  if (settings::g_misc.m_debug.show_bone_revealer) {
    RenderBoneRevealer(draw_list, local);
  }

  if (settings::g_misc.m_debug.show_pose_params) {
    RenderPoseParameters(draw_list, local);
  }

  if (settings::g_misc.m_debug.show_speed) {
    RenderSpeedDisplay(draw_list, local);
  }

  if (settings::g_misc.m_debug.show_bone_ids) {
    RenderBoneGrid(draw_list, local);
  }
}

void Debug::OnFrameStageNotify() {
  const auto now = std::chrono::steady_clock::now();
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::seconds>(now - m_last_fps_update_)
          .count();

  if (elapsed >= 1.0f) {
    m_fps_ = m_fps_ * 0.9f + (60.0f / static_cast<float>(elapsed)) * 0.1f;
    m_last_fps_update_ = now;
  }
}

void Debug::RenderBoneRevealer(xdraw::draw_list& draw_list,
                               const systems::Local::Snapshot& local) {
  const auto skeleton = systems::g_bones.GetSkeleton(local);

  for (int bone_id = settings::g_misc.m_debug.bone_id_min;
       bone_id <= settings::g_misc.m_debug.bone_id_max; ++bone_id) {
    if (bone_id >= skeleton.size()) {
      continue;
    }

    const auto& bone_data = skeleton[bone_id];

    const auto screen_pos = systems::g_view.WorldToScreen(bone_data.position);
    if (!screen_pos.has_value()) {
      continue;
    }

    auto yaw_norm = bone_data.rotation.y;
    math::NormalizeAngle(yaw_norm);
    const auto color = yaw_norm < 0.5f ? xdraw::color{255, 255, 255, 255}
                                       : xdraw::color{255, 0, 0, 255};

    const auto bone_pos = screen_pos.value();

    const auto radius = settings::g_misc.m_debug.speed_scale * 5.0f;

    const auto outline_width = settings::g_misc.m_debug.speed_scale * 2.0f;

    draw_list.circle_filled(bone_pos.x, bone_pos.y, radius, color);

    if (settings::g_misc.m_debug.show_bone_ids) {
      const auto text_pos = bone_pos.x - 10.0f;
      draw_list.text("{}", text_pos, bone_pos.y - radius - 5.0f, 0.8f,
                     xdraw::color{255, 255, 255, 255},
                     static_cast<int>(bone_id));
    }
  }
}

void Debug::RenderPoseParameters(xdraw::draw_list& draw_list,
                                 const systems::Local::Snapshot& local) {
  const auto screen_center_x = xdraw::viewport_size().width * 0.5f;
  const auto screen_center_y = xdraw::viewport_size().height * 0.5f;

  const auto& skeleton = systems::g_bones.GetSkeleton(local);

  const auto head_bone_id =
      static_cast<size_t>(systems::cstypes::bone_ids::kHead);

  if (head_bone_id < skeleton.size()) {
    const auto& head_bone = skeleton[head_bone_id];

    const auto yaw = head_bone.rotation.y;
    const auto pitch = head_bone.rotation.x;
    const auto roll = head_bone.rotation.z;

    auto yaw_norm = yaw;
    auto pitch_norm = pitch;
    auto roll_norm = roll;
    math::NormalizeAngle(yaw_norm);
    math::NormalizeAngle(pitch_norm);
    math::NormalizeAngle(roll_norm);

    const auto screen_pos = systems::g_view.WorldToScreen(head_bone.position);
    if (!screen_pos.has_value()) {
      return;
    }

    const auto pose_text =
        std::format("Pitch: {:.1f}°, Yaw: {:.1f}°, Roll: {:.1f}°",
                    pitch_norm * 180.0f, yaw_norm * 180.0f, roll_norm * 180.0f);

    const auto text_size = draw_list.text_size(pose_text.c_str(), 1.0f);

    const auto text_x = screen_pos->x - text_size.width * 0.5f;
    const auto text_y = screen_pos->y - 30.0f;

    draw_list.text(pose_text.c_str(), text_x, text_y, 1.0f,
                   xdraw::color{173, 192, 255, 255});

    const auto arrow_size = 20.0f;

    const auto arrow_x = screen_pos->x;
    const auto arrow_y = screen_pos->y - 20.0f;

    draw_list.triangle(arrow_x, arrow_y, arrow_x - arrow_size / 2,
                       arrow_y + arrow_size, arrow_x + arrow_size / 2,
                       arrow_y + arrow_size, xdraw::color{173, 192, 255, 255});
  }
}

void Debug::RenderSpeedDisplay(xdraw::draw_list& draw_list,
                               const systems::Local::Snapshot& local) {
  const auto& velocity = local.velocity;

  const auto speed = math::Vector3(velocity.x, velocity.y, velocity.z).Length();

  const auto speed_scaled = speed * settings::g_misc.m_debug.speed_scale;

  const auto screen_x = xdraw::viewport_size().width * 0.5f;
  const auto screen_y = xdraw::viewport_size().height * 0.5f;

  const auto speed_text =
      std::format("Speed: {:.1f} units/s (FPS: {:.0f})", speed_scaled, m_fps_);

  const auto text_size = draw_list.text_size(speed_text.c_str(), 1.0f);

  const auto text_x = screen_x - text_size.width * 0.5f;
  const auto text_y = screen_y - 40.0f;

  draw_list.text(speed_text.c_str(), text_x, text_y, 1.0f,
                 xdraw::color{173, 192, 255, 255});
}

void Debug::RenderBoneGrid(xdraw::draw_list& draw_list,
                           const systems::Local::Snapshot& local) {
  const auto skeleton = systems::g_bones.GetSkeleton(local);

  for (size_t i = 0; i < skeleton.size(); ++i) {
    const auto& bone_data = skeleton[i];

    const auto screen_pos = systems::g_view.WorldToScreen(bone_data.position);
    if (!screen_pos.has_value()) {
      continue;
    }

    const auto radius = settings::g_misc.m_debug.speed_scale * 2.0f;

    auto yaw_norm = bone_data.rotation.y;
    math::NormalizeAngle(yaw_norm);
    const auto bone_color = yaw_norm < 0.5f ? xdraw::color{173, 192, 255, 85}
                                            : xdraw::color{255, 0, 0, 85};

    draw_list.circle_filled(screen_pos->x, screen_pos->y, radius, bone_color);
  }
}

}  // namespace features::misc::impl
