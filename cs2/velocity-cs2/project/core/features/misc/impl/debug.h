#ifndef VELOCITY_CORE_FEATURES_MISC_IMPL_DEBUG_H_
#define VELOCITY_CORE_FEATURES_MISC_IMPL_DEBUG_H_

#include "core/settings.h"
#include "core/systems/systems.h"
#include "external/xdraw/xdraw.hpp"

namespace features::misc::impl {

class Debug {
 public:
  void OnRender(xdraw::draw_list& draw_list);
  void OnFrameStageNotify();

  [[nodiscard]] float GetDrawScale() const { return this->m_draw_scale_; }

 private:
  void RenderBoneRevealer(xdraw::draw_list& draw_list,
                          const systems::Local::Snapshot& local);
  void RenderPoseParameters(xdraw::draw_list& draw_list,
                            const systems::Local::Snapshot& local);
  void RenderSpeedDisplay(xdraw::draw_list& draw_list,
                          const systems::Local::Snapshot& local);
  void RenderBoneGrid(xdraw::draw_list& draw_list,
                      const systems::Local::Snapshot& local);

  float m_draw_scale_{1.0f};
  float m_fps_{60.0f};
  std::chrono::steady_clock::time_point m_last_fps_update_{};
};

}  // namespace features::misc::impl

#endif  // VELOCITY_CORE_FEATURES_MISC_IMPL_DEBUG_H_
