#include <core/settings.hpp>
#include "../../rendering.hpp"
namespace rendering {
	namespace detail {
		constexpr const char* hitbox_names[ ]{ "head", "chest", "stomach", "arms", "legs", "feet" };
		constexpr const char* pitch_items[ ]{ "none", "down", "up", "custom" };
		constexpr const char* yaw_items[ ]{ "local view", "enemy view", "manual" };
		constexpr const char* auto_adjust_items[ ]{ "off", "geometry", "static" };
		constexpr const char* jitter_items[ ]{ "off", "2-way", "3-way", "5-way", "8-way" };
		constexpr const char* disabler_items[ ]{ "freeze time", "anti-backstab" };
		constexpr const char* jitter_disabler_items[ ]{ "freeze time", "on shot", "anti-backstab" };
		constexpr const char* leg_movement_items[ ]{ "off", "sliding" };
	}
	void menu::draw_ragebot( float group_w ) const
	{
		auto& s = settings::g_combat;
		auto& rb = s.m_ragebot;
		auto& aa = s.m_antiaim;
		auto& aa_other = settings::g_misc.m_antiaim_other;
		auto& qp = s.m_quickpeek;
		auto& dp = s.m_duckpeek;
		auto& zb = s.m_zeusbot;
		auto& kb = s.m_knifebot;
		auto& autos = s.m_autos;
		auto& wg = rb.groups[ this->m_subtab ];
		const auto wx = this->m_x;
		const auto wy = this->m_y;
		const auto content_x = wx + tokens::gap + tokens::sidebar_w + tokens::gap;
		const auto body_y = wy + tokens::gap + tokens::subtab_bar_h + tokens::gap;
		const auto content_w = this->m_w - tokens::gap * 2.0f - tokens::sidebar_w - tokens::gap;
		const auto col_w = ( content_w - tokens::gap ) * 0.5f;
		const auto right_x = content_x + col_w + tokens::gap;
		const auto body_h = this->m_h - ( body_y - wy ) - tokens::gap;
		xui::layout::set_cursor( content_x - wx, body_y - wy );
		if ( xui::begin_child( "##ragebot_aimbot", col_w ) )
		{
			xui::checkbox( "enabled", rb.enabled );
			xui::checkbox( "silent", rb.silent );
			xui::checkbox( "nospread", rb.no_spread );
			xui::checkbox( "autostop", wg.autostop );
			if ( this->m_subtab == 4 )
			{
				xui::checkbox( "autoscope", autos.scope );
			}
			xui::slider_float( "fov", wg.max_fov, 1.0f, 180.0f, "%.0f°" );
			xui::slider_int( "hitchance", wg.hitchance, 0, 100, "%d%%" );
			xui::slider_int( "mindamage", wg.min_damage, 1, 125, "%d" );
			xui::checkbox( "hitchance override", wg.hitchance_override );
			if ( xui::begin_popup( "##hitchance_popup", 220.0f ) )
			{
				xui::slider_int( "value##hc", wg.hitchance_override_value, 1, 100, "%d%%" );
				xui::end_popup( );
			}
			xui::checkbox( "mindamage override", wg.min_damage_override );
			if ( xui::begin_popup( "##mindamage_popup", 220.0f ) )
			{
				xui::slider_int( "value##md", wg.min_damage_override_value, 0, 130, "%d" );
				xui::end_popup( );
			}
			xui::end_child( );
		}
		if ( xui::begin_child( "##ragebot_extras", col_w ) )
		{
			xui::checkbox( "dynamic point scale", wg.dynamic_pointscale );
			xui::slider_float( "pointscale", wg.pointscale, 0.0f, 100.0f, "%.0f%%" );
			xui::multicombo( "hitboxes", wg.hitboxes, detail::hitbox_names, 6 );
			xui::end_child( );
		}
		if ( xui::begin_child( "##ragebot_otherbots", col_w ) )
		{
			xui::checkbox( "auto revolver", autos.revolver );
			xui::checkbox( "zeusbot", zb.enabled );
			if ( xui::begin_popup( "##zb_settings", 220.0f ) )
			{
				xui::slider_float( "max fov##zb", zb.max_fov, 1, 180, "%.0f°" );
				xui::checkbox( "drop after##zb", zb.drop_after );
				xui::end_popup( );
			}
			xui::checkbox( "knifebot", kb.enabled );
			if ( xui::begin_popup( "##kb_settings", 220.0f ) )
			{
				xui::slider_float( "max fov##kb", kb.max_fov, 1, 180, "%.0f°" );
				xui::end_popup( );
			}
			xui::end_child( );
		}
		if ( xui::begin_child( "##ragebot_peek", col_w ) )
		{
			xui::checkbox( "quick peek assist", qp.enabled );
			if ( xui::begin_popup( "##qp_colors", 220.0f ) )
			{
				xui::color_picker( "base color##qp", qp.color );
				xui::color_picker( "retracting color##qp", qp.retrack_color );
				xui::end_popup( );
			}
			xui::checkbox( "duck peek assist", dp.enabled );
			xui::end_child( );
		}
		xui::layout::set_cursor( right_x - wx, body_y - wy );
		if ( xui::begin_child( "##ragebot_antiaim", col_w, body_h, true ) )
		{
			xui::checkbox( "anti aim", aa.enabled );
			xui::combo( "pitch", aa.pitch.value, detail::pitch_items, 4 );
			if ( xui::begin_popup( "##aa_pitch", 220.0f ) )
			{
				if ( aa.pitch.value == settings::combat::antiaim::pitch_mode::custom )
				{
					xui::slider_float( "custom##aa_pitch", aa.pitch_custom, -89.0f, 89.0f, "%.0f°" );
				}
				xui::slider_float( "offset##aa_pitch", aa.pitch_offset, -89.0f, 89.0f, "%.0f°" );
				xui::checkbox( "jitter##aa_pitch", aa.pitch_jitter );
				xui::combo( "jitter mode##aa_pitch", aa.pitch_jitter_mode.value, detail::jitter_items, 5 );
				xui::slider_float( "jitter angle##aa_pitch", aa.pitch_jitter_angle, 1.0f, 89.0f, "%.0f°" );
				xui::slider_int( "jitter ticks##aa_pitch", aa.pitch_jitter_ticks, 1, 8, "%d" );
				xui::checkbox( "jitter center##aa_pitch", aa.pitch_jitter_center );
				xui::checkbox( "spin##aa_pitch", aa.pitch_spin );
				xui::slider_float( "spin speed##aa_pitch", aa.pitch_spin_speed, 1.0f, 90.0f, "%.0f°" );
				xui::checkbox( "sway##aa_pitch", aa.pitch_sway );
				xui::slider_float( "sway amount##aa_pitch", aa.pitch_sway_amount, 1.0f, 89.0f, "%.0f°" );
				xui::slider_float( "sway speed##aa_pitch", aa.pitch_sway_speed, 0.1f, 10.0f, "%.1f" );
				xui::checkbox( "random##aa_pitch", aa.pitch_random );
				xui::slider_float( "random amount##aa_pitch", aa.pitch_random_amount, 1.0f, 89.0f, "%.0f°" );
				xui::end_popup( );
			}
			xui::combo( "yaw", aa.yaw.value, detail::yaw_items, 3 );
			if ( xui::begin_popup( "##aa_yaw", 220.0f ) )
			{
				if ( aa.yaw.value == settings::combat::antiaim::yaw_mode::manual )
				{
					xui::slider_float( "manual yaw##aa_yaw", aa.yaw_manual, -180.0f, 180.0f, "%.0f°" );
				}
				xui::checkbox( "force left##aa_yaw", aa.manual_left );
				xui::checkbox( "force right##aa_yaw", aa.manual_right );
				xui::checkbox( "force forward##aa_yaw", aa.manual_forward );
				xui::end_popup( );
			}
			xui::combo( "yaw auto adjust", aa.auto_adjust.value, detail::auto_adjust_items, 3 );
			if ( xui::begin_popup( "##aa_auto", 220.0f ) )
			{
				xui::slider_float( "amount##aa_auto", aa.auto_adjust_amount, 1.0f, 180.0f, "%.0f°" );
				xui::end_popup( );
			}
			xui::checkbox( "freestanding", aa.freestanding );
			if ( xui::begin_popup( "##aa_freestanding", 220.0f ) )
			{
				xui::checkbox( "disable modifiers##aa_freestanding", aa.freestanding_disable_modifiers );
				xui::end_popup( );
			}
			xui::checkbox( "mouse override", aa.mouse_override );
			if ( xui::begin_popup( "##aa_mouse", 220.0f ) )
			{
				xui::slider_float( "strength##aa_mouse", aa.mouse_override_strength, 1.0f, 100.0f, "%.0f%%" );
				xui::end_popup( );
			}
			xui::multicombo( "disablers", aa.disablers, detail::disabler_items, 2 );
			xui::checkbox( "hide onshot", aa.hide_shots );
			xui::checkbox( "direction indicator", aa.direction_indicator );
			if ( xui::begin_popup( "##aa_indicator", 220.0f ) )
			{
				xui::color_picker( "color##aa_ind", aa.direction_indicator_color );
				xui::checkbox( "glow##aa_ind", aa.direction_indicator_glow );
				xui::slider_float( "glow strength##aa_ind", aa.direction_indicator_glow_strength, 0.1f, 1.0f, "%.2f" );
				xui::end_popup( );
			}
			xui::text( "modifiers", tokens::col_text_dim );
			xui::checkbox( "jitter", aa.yaw_jitter );
			if ( xui::begin_popup( "##aa_jitter", 220.0f ) )
			{
				xui::combo( "mode##aa_jitter", aa.yaw_jitter_mode.value, detail::jitter_items, 5 );
				xui::slider_float( "angle##aa_jitter", aa.yaw_jitter_angle, 1.0f, 180.0f, "%.0f°" );
				xui::slider_int( "ticks##aa_jitter", aa.yaw_jitter_ticks, 1, 8, "%d" );
				xui::checkbox( "center##aa_jitter", aa.yaw_jitter_center );
				xui::multicombo( "disablers##aa_jitter", aa.jitter_disablers, detail::jitter_disabler_items, 3 );
				xui::end_popup( );
			}
			xui::checkbox( "spin", aa.yaw_spin );
			if ( xui::begin_popup( "##aa_spin", 220.0f ) )
			{
				xui::slider_float( "speed##aa_spin", aa.yaw_spin_speed, 1.0f, 90.0f, "%.0f°" );
				xui::end_popup( );
			}
			xui::checkbox( "sway", aa.yaw_sway );
			if ( xui::begin_popup( "##aa_sway", 220.0f ) )
			{
				xui::slider_float( "amount##aa_sway", aa.yaw_sway_amount, 1.0f, 90.0f, "%.0f°" );
				xui::slider_float( "speed##aa_sway", aa.yaw_sway_speed, 0.1f, 10.0f, "%.1f" );
				xui::end_popup( );
			}
			xui::checkbox( "random", aa.yaw_random );
			if ( xui::begin_popup( "##aa_random", 220.0f ) )
			{
				xui::slider_float( "amount##aa_random", aa.yaw_random_amount, 1.0f, 90.0f, "%.0f°" );
				xui::end_popup( );
			}
			xui::slider_float( "yaw offset", aa.yaw_offset, -180.0f, 180.0f, "%.0f°" );
			xui::checkbox( "override pitch on jitter", aa.override_pitch_on_jitter );
			xui::checkbox( "override pitch on spin", aa.override_pitch_on_spin );
			if ( xui::begin_popup( "##aa_override_pitch", 220.0f ) )
			{
				xui::slider_float( "pitch##aa_override", aa.override_pitch, -89.0f, 89.0f, "%.0f°" );
				xui::end_popup( );
			}
			xui::text( "other", tokens::col_text_dim );
			xui::combo( "leg movement", aa_other.leg_movement.value, detail::leg_movement_items, 2 );
			xui::checkbox( "remove breathing", aa_other.remove_breathing );
			xui::checkbox( "fast weapon animations", aa_other.fast_weapon_animations );
			xui::end_child( );
		}
	}
}
