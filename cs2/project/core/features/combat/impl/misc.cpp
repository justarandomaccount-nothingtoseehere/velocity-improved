#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/fnv1a.hpp>
#include <utilities/logging/logging.hpp>
#include <utilities/random/random.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <signatures/game_addresses.hpp>
namespace features::combat {
	namespace {
		constexpr int jitter_step_count( settings::combat::antiaim::jitter_mode mode )
		{
			switch ( mode )
			{
			case settings::combat::antiaim::jitter_mode::two_way:
				return 2;
			case settings::combat::antiaim::jitter_mode::three_way:
				return 3;
			case settings::combat::antiaim::jitter_mode::five_way:
				return 5;
			case settings::combat::antiaim::jitter_mode::eight_way:
				return 8;
			default:
				return 0;
			}
		}
		float jitter_offset( settings::combat::antiaim::jitter_mode mode, float angle, int command_number, int ticks, bool center )
		{
			const auto steps = jitter_step_count( mode );
			if ( steps < 2 )
			{
				return 0.0f;
			}
			const auto hold = std::max( ticks, 1 );
			const auto step = ( command_number / hold ) % steps;
			if ( steps == 2 )
			{
				return step == 0 ? -angle : angle;
			}
			const auto t = static_cast< float >( step ) / static_cast< float >( steps - 1 );
			return ( center ? ( t * 2.0f - 1.0f ) : t ) * angle;
		}
		float triangle_wave( float phase, float period )
		{
			auto value = std::fmod( phase, period );
			if ( value < 0.0f )
			{
				value += period;
			}
			const auto half = period * 0.5f;
			return value > half ? period - value : value;
		}
	}
	void misc::antiaim::on_create_move( systems::input::usercmd* cmd )
	{
		this->m_antiaim_active = false;
		auto& cfg = settings::g_combat.m_antiaim;
		if ( !cfg.enabled.value || !cmd )
		{
			return;
		}
		if ( systems::g_local.is_in_cinematic( ) )
		{
			return;
		}
		if ( cfg.disablers[ 0 ] && systems::g_local.is_in_time_freeze( ) )
		{
			return;
		}
		if ( cfg.manual_forward.value )
		{
			cfg.manual_left.value = false;
			cfg.manual_left.bind.active = false;
			cfg.manual_right.value = false;
			cfg.manual_right.bind.active = false;
		}
		else if ( cfg.manual_left.value && cfg.manual_right.value )
		{
			cfg.manual_right.value = false;
			cfg.manual_right.bind.active = false;
		}
		this->m_yaw_side = cfg.manual_left.value ? -1 : ( cfg.manual_right.value ? 1 : 0 );
		const auto local = systems::g_local.get( );
		if ( !local.is_alive || !local.pawn )
		{
			return;
		}
		const auto base = cmd->csgo_user_cmd.mutable_base( );
		if ( !base )
		{
			return;
		}
		const auto view_angles = systems::g_input.get_view_angles( );
		const auto& ctx = g_shared.ctx( );
		if ( cmd->buttons.value & cstypes::command_buttons::in_use )
		{
			return;
		}
		if ( ctx.weapon_type == cstypes::weapon_type::grenade )
		{
			if ( memory::read<float>( ctx.weapon + SCHEMA( "C_BaseCSGrenade", "m_fThrowTime"_hash ) ) > 0.0f )
			{
				return;
			}
		}
		const auto move_type = memory::read<int>( local.pawn + SCHEMA( "C_BaseEntity", "m_nActualMoveType"_hash ) );
		if ( move_type == cstypes::move_type::ladder || move_type == cstypes::move_type::noclip )
		{
			return;
		}
		if ( this->is_near_ladder( local.pawn ) )
		{
			return;
		}
		const auto enemy = this->find_nearest_enemy( local );
		const auto need_backstab = cfg.disablers[ 1 ] || cfg.jitter_disablers[ 2 ];
		const auto threat = need_backstab ? this->find_backstab_threat( local, view_angles ) : enemy_info{};
		const auto threat_override = cfg.disablers[ 1 ] ? threat : enemy_info{};
		const auto on_shot = ( cmd->buttons.value & cstypes::command_buttons::in_attack ) != 0;
		const auto jitter_disabled = ( cfg.jitter_disablers[ 0 ] && systems::g_local.is_in_time_freeze( ) ) || ( cfg.jitter_disablers[ 1 ] && on_shot ) || ( cfg.jitter_disablers[ 2 ] && threat.valid );
		const auto jitter_active = cfg.yaw_jitter.value && cfg.yaw_jitter_mode.value != settings::combat::antiaim::jitter_mode::off && !jitter_disabled;
		const auto spin_active = cfg.yaw_spin.value && !( cfg.freestanding.value && cfg.freestanding_disable_modifiers.value );
		const auto command_number = static_cast< int >( cmd->command_number );
		const auto time = ctx.current_time;
		this->m_old_angles = view_angles;
		this->m_antiaim_active = true;
		this->m_modified_angles = this->m_old_angles;
		this->m_modified_angles.x = this->get_pitch( this->m_old_angles.x, command_number, time, jitter_active, spin_active );
		this->m_modified_angles.y = this->get_yaw( this->m_old_angles, local, enemy, threat_override, cmd, jitter_active );
		this->m_last_command = cmd->command_number;
		math::helpers::normalize_angles( this->m_modified_angles );
		base->mutable_viewangles( )->set_x( this->m_modified_angles.x );
		base->mutable_viewangles( )->set_y( this->m_modified_angles.y );
		base->mutable_viewangles( )->set_z( this->m_modified_angles.z );
		this->m_should_correct = true;
		this->correct_movement( cmd );
	}
	void misc::antiaim::on_render( xdraw::draw_list& draw_list ) const
	{
		if ( !settings::g_combat.m_antiaim.enabled.value || !settings::g_combat.m_antiaim.direction_indicator.value )
		{
			return;
		}
		if ( !this->m_antiaim_active )
		{
			return;
		}
		if ( !systems::g_frame_data.valid( ) )
		{
			return;
		}
		const auto origin = systems::g_frame_data.origin( );
		const auto aa_yaw_rad = this->m_indicator_yaw * ( std::numbers::pi_v<float> / 180.0f );
		constexpr auto radius{ 28.0f };
		constexpr auto feet_offset{ -2.0f };
		constexpr auto arc_sweep_deg{ 60.0f };
		constexpr auto arc_segments{ 48 };
		constexpr auto max_thickness{ 3.0f };
		const auto& cfg = settings::g_combat.m_antiaim;
		const auto& color = cfg.direction_indicator_color;
		const auto base = math::vector3{ origin.x, origin.y, origin.z + feet_offset };
		const auto half_sweep = ( arc_sweep_deg * 0.5f ) * ( std::numbers::pi_v<float> / 180.0f );
		const auto start_angle = aa_yaw_rad - half_sweep;
		const auto end_angle = aa_yaw_rad + half_sweep;
		const auto angle_step = ( end_angle - start_angle ) / static_cast< float >( arc_segments );
		std::vector<math::vector2> pts;
		pts.reserve( arc_segments + 1 );
		for ( auto i = 0; i <= arc_segments; ++i )
		{
			const auto angle = start_angle + angle_step * static_cast< float >( i );
			const auto world_pt = math::vector3
			{
				base.x + std::cosf( angle ) * radius,
				base.y + std::sinf( angle ) * radius,
				base.z
			};
			const auto sp = systems::g_view.project( world_pt );
			if ( !systems::g_view.projection_valid( sp ) )
			{
				return;
			}
			pts.push_back( { sp.x, sp.y } );
		}
		if ( pts.size( ) < 2 )
		{
			return;
		}
		const auto total = static_cast< float >( pts.size( ) - 1 );
		const auto fade_at = [ ]( std::size_t idx, float total ) -> float
			{
				const auto frac = static_cast< float >( idx ) / total;
				const auto edge = 1.0f - std::fabsf( frac - 0.5f ) * 2.0f;
				return edge * edge * edge * ( edge * ( edge * 6.0f - 15.0f ) + 10.0f );
			};
		if ( cfg.direction_indicator_glow )
		{
			auto& glow = xdraw::get_glow( );
			for ( auto i = 0ull; i + 1 < pts.size( ); ++i )
			{
				const auto f0 = fade_at( i, total );
				const auto f1 = fade_at( i + 1, total );
				const auto ga0 = static_cast< std::uint8_t >( static_cast< float >( color.value.a ) * cfg.direction_indicator_glow_strength * std::fmaxf( f0, 0.05f ) );
				const auto ga1 = static_cast< std::uint8_t >( static_cast< float >( color.value.a ) * cfg.direction_indicator_glow_strength * std::fmaxf( f1, 0.05f ) );
				const auto thickness = ( max_thickness + 2.0f ) * ( ( f0 + f1 ) * 0.5f * 0.85f + 0.15f );
				const float seg[ ]{ pts[ i ].x, pts[ i ].y, pts[ i + 1 ].x, pts[ i + 1 ].y };
				const xdraw::color cols[ ]{ { color.value.r, color.value.g, color.value.b, ga0 }, { color.value.r, color.value.g, color.value.b, ga1 } };
				glow.polyline_gradient( seg, cols, false, thickness );
			}
		}
		for ( auto i = 0ull; i + 1 < pts.size( ); ++i )
		{
			const auto f0 = fade_at( i, total );
			const auto f1 = fade_at( i + 1, total );
			const auto a0 = static_cast< std::uint8_t >( color.value.a * std::fmaxf( f0, 0.05f ) );
			const auto a1 = static_cast< std::uint8_t >( color.value.a * std::fmaxf( f1, 0.05f ) );
			const auto thickness = max_thickness * ( ( f0 + f1 ) * 0.5f * 0.85f + 0.15f );
			const float seg[ ]{ pts[ i ].x, pts[ i ].y, pts[ i + 1 ].x, pts[ i + 1 ].y };
			const xdraw::color cols[ ]{ { color.value.r, color.value.g, color.value.b, a0 }, { color.value.r, color.value.g, color.value.b, a1 } };
			draw_list.polyline_gradient( seg, cols, false, thickness );
		}
	}
	float misc::antiaim::get_pitch( float view_pitch, int command_number, float time, bool yaw_jitter_active, bool yaw_spin_active )
	{
		auto& cfg = settings::g_combat.m_antiaim;
		auto pitch = view_pitch;
		switch ( cfg.pitch.value )
		{
		case settings::combat::antiaim::pitch_mode::down:
			pitch = 89.0f;
			break;
		case settings::combat::antiaim::pitch_mode::up:
			pitch = -89.0f;
			break;
		case settings::combat::antiaim::pitch_mode::custom:
			pitch = cfg.pitch_custom;
			break;
		default:
			break;
		}
		pitch += cfg.pitch_offset;
		if ( cfg.pitch_jitter.value )
		{
			pitch += jitter_offset( cfg.pitch_jitter_mode, cfg.pitch_jitter_angle, command_number, cfg.pitch_jitter_ticks, cfg.pitch_jitter_center.value );
		}
		if ( cfg.pitch_spin.value )
		{
			const auto delta = this->m_last_command != 0 ? std::clamp( static_cast< int >( command_number - this->m_last_command ), 0, 8 ) : 1;
			this->m_spin_pitch = std::fmod( this->m_spin_pitch + static_cast< float >( cfg.pitch_spin_speed ) * static_cast< float >( delta ), 356.0f );
			pitch = -89.0f + triangle_wave( this->m_spin_pitch, 356.0f );
		}
		else
		{
			this->m_spin_pitch = 0.0f;
		}
		if ( cfg.pitch_sway.value )
		{
			const auto speed = static_cast< float >( cfg.pitch_sway_speed );
			pitch += std::sinf( time * 2.0f * std::numbers::pi_v<float> * speed ) * static_cast< float >( cfg.pitch_sway_amount );
		}
		if ( cfg.pitch_random.value )
		{
			const auto amount = static_cast< float >( cfg.pitch_random_amount );
			pitch += random::floating( -amount, amount );
		}
		if ( yaw_jitter_active && cfg.override_pitch_on_jitter.value )
		{
			pitch = cfg.override_pitch;
		}
		if ( yaw_spin_active && cfg.override_pitch_on_spin.value )
		{
			pitch = cfg.override_pitch;
		}
		return std::clamp( pitch, -89.0f, 89.0f );
	}
	float misc::antiaim::get_yaw( const math::vector3& view_angles, const systems::local::snapshot& local, const enemy_info& enemy, const enemy_info& threat, systems::input::usercmd* cmd, bool jitter_active )
	{
		auto& cfg = settings::g_combat.m_antiaim;
		const auto origin = this->local_origin( local.pawn );
		auto yaw = view_angles.y - 180.0f;
		switch ( cfg.yaw.value )
		{
		case settings::combat::antiaim::yaw_mode::local_view:
			yaw = view_angles.y - 180.0f;
			break;
		case settings::combat::antiaim::yaw_mode::enemy_view:
			if ( enemy.valid )
			{
				yaw = math::helpers::calculate_angle( origin, enemy.origin ).y;
			}
			break;
		case settings::combat::antiaim::yaw_mode::manual:
			yaw = cfg.yaw_manual;
			break;
		}
		if ( cfg.manual_forward.value )
		{
			yaw = view_angles.y;
		}
		else if ( this->m_yaw_side < 0 )
		{
			yaw -= 90.0f;
		}
		else if ( this->m_yaw_side > 0 )
		{
			yaw += 90.0f;
		}
		if ( threat.valid )
		{
			yaw = math::helpers::calculate_angle( origin, threat.origin ).y;
			this->m_indicator_yaw = math::helpers::normalize_yaw( yaw );
			return yaw;
		}
		const auto modifiers = !( cfg.freestanding.value && cfg.freestanding_disable_modifiers.value );
		if ( cfg.freestanding.value )
		{
			yaw += 90.0f * static_cast< float >( this->choose_freestanding_side( local, view_angles, enemy ) );
		}
		if ( cfg.auto_adjust.value == settings::combat::antiaim::auto_adjust_mode::static_offset )
		{
			yaw += cfg.auto_adjust_amount;
		}
		else if ( cfg.auto_adjust.value == settings::combat::antiaim::auto_adjust_mode::geometry && enemy.valid )
		{
			const auto amount = static_cast< float >( cfg.auto_adjust_amount );
			const auto offset = math::helpers::normalize_yaw( enemy.eye_angles.y - yaw );
			yaw += std::clamp( offset, -amount, amount );
		}
		if ( modifiers )
		{
			yaw += cfg.yaw_offset;
			if ( jitter_active )
			{
				yaw += jitter_offset( cfg.yaw_jitter_mode, cfg.yaw_jitter_angle, static_cast< int >( cmd->command_number ), cfg.yaw_jitter_ticks, cfg.yaw_jitter_center.value );
			}
			if ( cfg.yaw_spin.value )
			{
				const auto delta = this->m_last_command != 0 ? std::clamp( static_cast< int >( cmd->command_number - this->m_last_command ), 0, 8 ) : 1;
				this->m_spin_yaw = std::fmod( this->m_spin_yaw + static_cast< float >( cfg.yaw_spin_speed ) * static_cast< float >( delta ), 360.0f );
				yaw += this->m_spin_yaw;
			}
			if ( cfg.yaw_sway.value )
			{
				const auto time = g_shared.ctx( ).current_time;
				yaw += std::sinf( time * 2.0f * std::numbers::pi_v<float> * static_cast< float >( cfg.yaw_sway_speed ) ) * static_cast< float >( cfg.yaw_sway_amount );
			}
			if ( cfg.yaw_random.value )
			{
				const auto amount = static_cast< float >( cfg.yaw_random_amount );
				yaw += random::floating( -amount, amount );
			}
		}
		else
		{
			this->m_spin_yaw = 0.0f;
		}
		if ( cfg.mouse_override.value )
		{
			const auto strength = std::clamp( static_cast< float >( cfg.mouse_override_strength ) / 100.0f, 0.0f, 1.0f );
			const auto mouse_base = cmd->csgo_user_cmd.base( );
			const auto dx = mouse_base ? static_cast< float >( mouse_base->m_mousedx ) : 0.0f;
			const auto dy = mouse_base ? static_cast< float >( mouse_base->m_mousedy ) : 0.0f;
			const auto forwardness = std::clamp( -dy / 50.0f, -1.0f, 1.0f ) * strength;
			const auto side = std::clamp( dx / 50.0f, -1.0f, 1.0f ) * 90.0f * strength;
			yaw += math::helpers::normalize_yaw( view_angles.y - yaw ) * forwardness;
			yaw += side;
		}
		this->m_indicator_yaw = math::helpers::normalize_yaw( yaw );
		return yaw;
	}
	misc::antiaim::enemy_info misc::antiaim::find_nearest_enemy( const systems::local::snapshot& local ) const
	{
		enemy_info out{};
		const auto origin = this->local_origin( local.pawn );
		if ( origin.length_sqr( ) == 0.0f )
		{
			return out;
		}
		const auto players = systems::g_entities.get_by_type( systems::entities::type::player );
		for ( const auto& p : players )
		{
			if ( !p.ptr || p.ptr == local.controller )
			{
				continue;
			}
			if ( !memory::read<bool>( p.ptr + SCHEMA( "CCSPlayerController", "m_bPawnIsAlive"_hash ) ) )
			{
				continue;
			}
			const auto pawn_handle = memory::read<std::uint32_t>( p.ptr + SCHEMA( "CBasePlayerController", "m_hPawn"_hash ) );
			const auto pawn = systems::g_entities.lookup( pawn_handle );
			if ( !pawn || pawn == local.pawn )
			{
				continue;
			}
			if ( !local.is_this_other_team( memory::read<int>( pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) ) ) )
			{
				continue;
			}
			if ( memory::read<int>( pawn + SCHEMA( "C_BaseEntity", "m_iHealth"_hash ) ) <= 0 )
			{
				continue;
			}
			if ( memory::read<bool>( pawn + SCHEMA( "C_CSPlayerPawn", "m_bGunGameImmunity"_hash ) ) )
			{
				continue;
			}
			const auto enemy_origin = this->local_origin( pawn );
			if ( enemy_origin.length_sqr( ) == 0.0f )
			{
				continue;
			}
			const auto distance = ( enemy_origin - origin ).length_sqr( );
			if ( out.valid && distance >= out.distance )
			{
				continue;
			}
			out.pawn = pawn;
			out.origin = enemy_origin;
			out.eye_angles = memory::read<math::vector3>( pawn + SCHEMA( "C_CSPlayerPawn", "m_angEyeAngles"_hash ) );
			out.distance = distance;
			out.valid = true;
		}
		return out;
	}
	misc::antiaim::enemy_info misc::antiaim::find_backstab_threat( const systems::local::snapshot& local, const math::vector3& view_angles ) const
	{
		enemy_info out{};
		const auto origin = this->local_origin( local.pawn );
		if ( origin.length_sqr( ) == 0.0f )
		{
			return out;
		}
		math::vector3 forward{}, left{};
		math::helpers::angle_vectors_left( view_angles, &forward, &left, nullptr );
		forward.z = 0.0f;
		if ( forward.length_sqr( ) == 0.0f )
		{
			return out;
		}
		forward.normalize( );
		const auto players = systems::g_entities.get_by_type( systems::entities::type::player );
		for ( const auto& p : players )
		{
			if ( !p.ptr || p.ptr == local.controller )
			{
				continue;
			}
			if ( !memory::read<bool>( p.ptr + SCHEMA( "CCSPlayerController", "m_bPawnIsAlive"_hash ) ) )
			{
				continue;
			}
			const auto pawn_handle = memory::read<std::uint32_t>( p.ptr + SCHEMA( "CBasePlayerController", "m_hPawn"_hash ) );
			const auto pawn = systems::g_entities.lookup( pawn_handle );
			if ( !pawn || pawn == local.pawn )
			{
				continue;
			}
			if ( !local.is_this_other_team( memory::read<int>( pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) ) ) )
			{
				continue;
			}
			if ( memory::read<int>( pawn + SCHEMA( "C_BaseEntity", "m_iHealth"_hash ) ) <= 0 )
			{
				continue;
			}
			const auto enemy_origin = this->local_origin( pawn );
			auto delta = enemy_origin - origin;
			delta.z = 0.0f;
			const auto length = delta.length( );
			if ( length < 1.0f || length > 300.0f )
			{
				continue;
			}
			if ( forward.dot( delta / length ) > -0.25f )
			{
				continue;
			}
			const auto sight = systems::g_tracing.trace( enemy_origin + math::vector3{ 0.0f, 0.0f, 64.0f }, origin + math::vector3{ 0.0f, 0.0f, 64.0f }, local.pawn );
			if ( sight.fraction < 0.9f )
			{
				continue;
			}
			if ( out.valid && length >= out.distance )
			{
				continue;
			}
			out.pawn = pawn;
			out.origin = enemy_origin;
			out.distance = length;
			out.valid = true;
		}
		return out;
	}
	int misc::antiaim::choose_freestanding_side( const systems::local::snapshot& local, const math::vector3& view_angles, const enemy_info& enemy ) const
	{
		if ( !enemy.valid )
		{
			return 0;
		}
		const auto origin = this->local_origin( local.pawn );
		if ( origin.length_sqr( ) == 0.0f )
		{
			return 0;
		}
		const auto enemy_angles = math::helpers::calculate_angle( origin, enemy.origin );
		const auto relative = math::helpers::normalize_yaw( enemy_angles.y - view_angles.y );
		auto side = relative > 0.0f ? -1 : 1;
		math::vector3 forward{}, right{};
		math::helpers::angle_vectors_2d( view_angles.y, forward, right );
		const auto start = origin + math::vector3{ 0.0f, 0.0f, 64.0f };
		constexpr auto reach{ 48.0f };
		const auto right_trace = systems::g_tracing.trace( start, start + right * reach, local.pawn );
		const auto left_trace = systems::g_tracing.trace( start, start - right * reach, local.pawn );
		const auto right_blocked = right_trace.fraction < 0.95f;
		const auto left_blocked = left_trace.fraction < 0.95f;
		if ( right_blocked != left_blocked )
		{
			side = right_blocked ? 1 : -1;
		}
		else if ( right_blocked && left_blocked )
		{
			side = right_trace.fraction <= left_trace.fraction ? 1 : -1;
		}
		return side;
	}
	math::vector3 misc::antiaim::local_origin( std::uintptr_t local_pawn ) const
	{
		if ( !local_pawn )
		{
			return {};
		}
		const auto node = memory::read<std::uintptr_t>( local_pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		if ( !node )
		{
			return {};
		}
		return memory::read<math::vector3>( node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
	}
		void misc::antiaim::correct_movement( systems::input::usercmd* cmd )
	{
		if ( !this->m_should_correct )
		{
			return;
		}
		this->m_should_correct = false;
		const auto base = cmd->csgo_user_cmd.mutable_base( );
		const auto forward_move = base->forwardmove( );
		const auto side_move = base->leftmove( );
		if ( forward_move == 0.0f && side_move == 0.0f )
		{
			return;
		}
		math::vector3 new_forward{}, new_left{};
		math::helpers::angle_vectors_left( this->m_modified_angles, &new_forward, &new_left, nullptr );
		math::vector3 old_forward{}, old_left{};
		math::helpers::angle_vectors_left( this->m_old_angles, &old_forward, &old_left, nullptr );
		new_forward.z = 0.0f; new_left.z = 0.0f;
		old_forward.z = 0.0f; old_left.z = 0.0f;
		new_forward.normalize( );
		new_left.normalize( );
		old_forward.normalize( );
		old_left.normalize( );
		const auto intent = old_forward * forward_move + old_left * -side_move;
		const auto intent_len = intent.length( );
		if ( intent_len == 0.0f )
		{
			return;
		}
		const auto intent_dir = intent / intent_len;
		const auto corrected_forward = new_forward.dot( intent_dir ) * intent_len;
		const auto corrected_side = -new_left.dot( intent_dir ) * intent_len;
		base->set_forwardmove( std::clamp( corrected_forward, -1.0f, 1.0f ) );
		base->set_leftmove( std::clamp( corrected_side, -1.0f, 1.0f ) );
		if ( systems::g_prediction.pre( ).flags & cstypes::entity_flags::on_ground )
		{
			auto buttons = cmd->buttons.value;
			buttons &= ~static_cast<std::uintptr_t>( cstypes::command_buttons::in_forward | cstypes::command_buttons::in_back | cstypes::command_buttons::in_moveleft | cstypes::command_buttons::in_moveright );
			if ( base->forwardmove( ) > 0.0f )
			{
				buttons |= cstypes::command_buttons::in_forward;
			}
			else if ( base->forwardmove( ) < 0.0f )
			{
				buttons |= cstypes::command_buttons::in_back;
			}
			if ( base->leftmove( ) > 0.0f )
			{
				buttons |= cstypes::command_buttons::in_moveleft;
			}
			else if ( base->leftmove( ) < 0.0f )
			{
				buttons |= cstypes::command_buttons::in_moveright;
			}
			cmd->buttons.value = buttons;
		}
	}
	bool misc::antiaim::is_near_ladder( std::uintptr_t local_pawn ) const
	{
		return false;
	}
	namespace {
		math::vector3 quickpeek_ground_snap( std::uintptr_t skip_pawn, const math::vector3& feet_pos )
		{
			const auto start = math::vector3{ feet_pos.x, feet_pos.y, feet_pos.z + 64.0f };
			const auto end = math::vector3{ feet_pos.x, feet_pos.y, feet_pos.z - 8192.0f };
			const auto tr = systems::g_tracing.trace( start, end, skip_pawn );
			if ( tr.fraction <= 0.0f || tr.fraction >= 0.997f )
			{
				return feet_pos;
			}
			auto out = tr.position;
			out.z += 1.0f;
			return out;
		}
	}
	void misc::duckpeek::on_create_move( systems::input::usercmd* cmd )
	{
		this->m_fake_stand_active = false;
		if ( !cmd || !settings::g_combat.m_duckpeek.enabled.value )
		{
			this->m_was_active = false;
			return;
		}
		const auto local = systems::g_local.get( );
		if ( !local.is_alive || !local.pawn || systems::g_local.is_in_cinematic( ) || systems::g_local.is_in_time_freeze( ) )
		{
			this->m_was_active = false;
			return;
		}
		this->m_was_active = true;
		if ( g_rage.should_release_duck_for_shot( ) )
		{
			cmd->buttons.value &= ~cstypes::command_buttons::in_duck;
			this->m_fake_stand_active = true;
			return;
		}
		cmd->buttons.value |= cstypes::command_buttons::in_duck;
		if ( g_rage.duckpeek_wants_reduck( ) )
		{
			g_rage.clear_duckpeek_reduck( );
		}
	}
	void misc::duckpeek::on_override_view( std::uintptr_t view_setup )
	{
		(void)view_setup;
		if ( !settings::g_combat.m_duckpeek.enabled.value )
		{
			this->m_was_active = false;
			this->m_fake_stand_active = false;
		}
	}
	void misc::quickpeek::on_create_move( systems::input::usercmd* cmd )
	{
		if ( !settings::g_combat.m_quickpeek.enabled.value )
		{
			this->reset( );
			return;
		}
		const auto local = systems::g_local.get( );
		const auto& ctx = g_shared.ctx( );
		if ( ctx.weapon_type < cstypes::weapon_type::pistol || ctx.weapon_type > cstypes::weapon_type::lmg )
		{
			this->reset( );
			return;
		}
		const auto base = cmd->csgo_user_cmd.mutable_base( );
		constexpr auto movement_cancel_mask = static_cast< std::uintptr_t >( cstypes::command_buttons::in_forward | cstypes::command_buttons::in_back | cstypes::command_buttons::in_moveleft | cstypes::command_buttons::in_moveright );
		const auto curr_movement_bits = cmd->buttons.value & movement_cancel_mask;
		const auto game_scene_node = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		const auto origin = memory::read<math::vector3>( game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
		if ( this->m_saved_origin.length_sqr( ) < 0.001f )
		{
			this->m_saved_origin = quickpeek_ground_snap( local.pawn, origin );
			this->m_should_retrack = false;
			this->m_fired = false;
			this->m_active = true;
			this->create_particle( );
			this->m_prev_movement_bits = curr_movement_bits;
			return;
		}
		this->update_particle( );
		const auto distance = ( origin - this->m_saved_origin ).length_2d( );
		if ( this->m_should_retrack && ( curr_movement_bits & ~this->m_prev_movement_bits ) != 0 )
		{
			this->m_should_retrack = false;
		}
		if ( this->m_should_retrack && ( systems::g_prediction.pre( ).flags & cstypes::entity_flags::on_ground ) )
		{
			const auto velocity = memory::read<math::vector3>( local.pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_hash ) );
			const auto speed = velocity.length_2d( );
			if ( distance < 5.0f && speed < 15.0f )
			{
				this->m_should_retrack = false;
				this->m_fired = false;
			}
			else if ( distance < speed * 0.1f && speed > 15.0f )
			{
				const auto vel_angle = math::helpers::vector_to_angle( velocity * -1.0f );
				const auto yaw_diff = math::helpers::deg_to_rad( base->viewangles( )->y( ) - vel_angle.y );
				base->set_forwardmove( std::cosf( yaw_diff ) );
				base->set_leftmove( -std::sinf( yaw_diff ) );
				auto buttons = cmd->buttons.value;
				buttons &= ~static_cast< std::uintptr_t >( cstypes::command_buttons::in_forward | cstypes::command_buttons::in_back | cstypes::command_buttons::in_moveleft | cstypes::command_buttons::in_moveright );
				if ( base->forwardmove( ) > 0.0f )
				{
					buttons |= cstypes::command_buttons::in_forward;
				}
				else if ( base->forwardmove( ) < 0.0f )
				{
					buttons |= cstypes::command_buttons::in_back;
				}
				if ( base->leftmove( ) > 0.0f )
				{
					buttons |= cstypes::command_buttons::in_moveleft;
				}
				else if ( base->leftmove( ) < 0.0f )
				{
					buttons |= cstypes::command_buttons::in_moveright;
				}
				cmd->buttons.value = buttons;
			}
			else
			{
				const auto diff = this->m_saved_origin - origin;
				const auto angle_to_pos = math::helpers::vector_to_angle( diff );
				const auto yaw_diff = math::helpers::deg_to_rad( base->viewangles( )->y( ) - angle_to_pos.y );
				base->set_forwardmove( std::cosf( yaw_diff ) );
				base->set_leftmove( -std::sinf( yaw_diff ) );
				auto buttons = cmd->buttons.value;
				buttons &= ~static_cast< std::uintptr_t >( cstypes::command_buttons::in_forward | cstypes::command_buttons::in_back | cstypes::command_buttons::in_moveleft | cstypes::command_buttons::in_moveright );
				if ( base->forwardmove( ) > 0.0f )
				{
					buttons |= cstypes::command_buttons::in_forward;
				}
				else if ( base->forwardmove( ) < 0.0f )
				{
					buttons |= cstypes::command_buttons::in_back;
				}
				if ( base->leftmove( ) > 0.0f )
				{
					buttons |= cstypes::command_buttons::in_moveleft;
				}
				else if ( base->leftmove( ) < 0.0f )
				{
					buttons |= cstypes::command_buttons::in_moveright;
				}
				cmd->buttons.value = buttons;
			}
		}
		if ( ( cmd->buttons.value & cstypes::command_buttons::in_attack ) && !g_rage.is_cocking_revolver( ) )
		{
			this->m_should_retrack = true;
			this->m_fired = true;
		}
		this->m_prev_movement_bits = curr_movement_bits;
	}
	void misc::quickpeek::reset_if_needed( )
	{
		if ( !this->m_active )
		{
			return;
		}
		const auto local = systems::g_local.get( );
		if ( !local.is_alive || !local.pawn )
		{
			this->reset( );
			return;
		}
		if ( !settings::g_combat.m_quickpeek.enabled.value )
		{
			this->reset( );
		}
	}
	void misc::quickpeek::create_particle( )
	{
		const auto particle_manager = memory::read<std::uintptr_t>( addresses::globals::particle_manager );
		if ( !particle_manager )
		{
			return;
		}
		constexpr auto particle_path{ "particles/embedded/halo.vpcf" };
		if ( !this->m_particle_loaded )
		{
			struct buffer_string
			{
				std::uint32_t m_unknown1{};
				std::uint32_t m_unknown2{ 0xc00000c8 };
				union
				{
					std::uintptr_t m_str_ptr;
					std::uint8_t data[ 0xc8 ];
				};
				std::uintptr_t m_unknown3{ 0 };
				std::uintptr_t m_unknown4{ 0 };
			} buffer;
			memory::call<void>(PATTERN (patterns::init_particle_path_buffer), &buffer, particle_path );
			buffer.m_unknown4 = 'fcpv';
			memory::call<void>(PATTERN (patterns::resource_system_precache), addresses::globals::resource_system, &buffer, "" );
			this->m_particle_loaded = true;
		}
		auto effect_index{ invalid_effect_index };
		memory::call<int*>(PATTERN (patterns::particle_create_effect), particle_manager, &effect_index, particle_path, 8, 0ll, 0ll, 0ll, 0 );
		this->m_particle_effect = effect_index;
		if ( effect_index == invalid_effect_index )
		{
			return;
		}
		memory::call<bool>(PATTERN (patterns::particle_set_control_point), particle_manager, effect_index, 0, &this->m_saved_origin, 0 );
	}
	void misc::quickpeek::update_particle( )
	{
		if ( this->m_particle_effect == invalid_effect_index )
		{
			return;
		}
		const auto particle_manager = memory::read<std::uintptr_t>( addresses::globals::particle_manager );
		if ( !particle_manager )
		{
			return;
		}
		const auto& cfg = settings::g_combat.m_quickpeek;
		const auto& col = this->m_should_retrack ? cfg.retrack_color : cfg.color;
		const auto color = math::vector3{ static_cast< float >( col.value.r ), static_cast< float >( col.value.g ), static_cast< float >( col.value.b ) };
		memory::call<bool>(PATTERN (patterns::particle_set_control_point), particle_manager, this->m_particle_effect, 1, &color, 0 );
		memory::call<bool>(PATTERN (patterns::particle_set_control_point), particle_manager, this->m_particle_effect, 0, &this->m_saved_origin, 0 );
	}
	void misc::quickpeek::release_particle( )
	{
		if ( this->m_particle_effect == invalid_effect_index )
		{
			return;
		}
		const auto particle_manager = memory::read<std::uintptr_t>( addresses::globals::particle_manager );
		if ( particle_manager )
		{
			memory::call<void>(PATTERN (patterns::particle_destroy_effect), particle_manager, this->m_particle_effect, true, true );
		}
		this->m_particle_effect = invalid_effect_index;
	}
	void misc::quickpeek::reset( )
	{
		this->release_particle( );
		this->m_saved_origin = {};
		this->m_should_retrack = false;
		this->m_fired = false;
		this->m_active = false;
		this->m_prev_movement_bits = 0;
	}
	void misc::autostop::on_create_move( systems::input::usercmd* cmd )
	{
		if ( !features::combat::g_rage.should_stop( ) && !features::combat::g_legit.should_stop( ) )
		{
			return;
		}
		const auto local = systems::g_local.get( );
		const auto movement_services = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_hash ) );
		const auto base = cmd->csgo_user_cmd.mutable_base( );
		const auto& prestate = systems::g_prediction.pre( );
		const auto& ctx = g_shared.ctx( );
		if ( !( prestate.flags & cstypes::entity_flags::on_ground ) )
		{
			return;
		}
		auto velocity = prestate.networked_velocity;
		auto speed = velocity.length_2d( );
		if ( speed <= 1.0f )
		{
			return;
		}
		const auto sv_friction = CONVAR ("sv_friction")->get<float>( );
		const auto sv_stopspeed = CONVAR ("sv_stopspeed")->get<float>( );
		const auto surface_friction = prestate.surface_friction;
		const auto control = std::fmaxf( speed, sv_stopspeed );
		const auto drop = control * sv_friction * surface_friction * cstypes::tick_interval;
		const auto post_friction = std::fmaxf( speed - drop, 0.0f );
		if ( post_friction > 0.0f )
		{
			velocity *= ( post_friction / speed );
			speed = post_friction;
		}
		else
		{
			base->set_forwardmove( 0.0f );
			base->set_leftmove( 0.0f );
			return;
		}
		if ( speed < 2.0f )
		{
			base->set_forwardmove( 0.0f );
			base->set_leftmove( 0.0f );
			return;
		}
		auto accel = CONVAR ("sv_accelerate")->get<float>( );
		const auto accel_base = this->get_effective_accel_base( local.pawn, movement_services, prestate.flags, ctx.weapon_max_speed );
		if ( ctx.is_scoped )
		{
			const auto weapon_ratio = std::fminf( 1.0f, ctx.weapon_max_speed / 250.0f );
			const auto v20 = std::fmaxf( 250.0f, memory::read<float>( movement_services + SCHEMA( "CPlayer_MovementServices", "m_flMaxspeed"_hash ) ) ) * weapon_ratio;
			const auto scoped_max = v20 * 0.52f;
			if ( speed > scoped_max - 5.0f )
			{
				const auto t = 1.0f - std::fmaxf( 0.0f, speed - ( scoped_max - 5.0f ) ) / std::fmaxf( 0.01f, 5.0f );
				accel *= std::clamp( t, 0.0f, 1.0f );
			}
		}
		const auto wish_x = -velocity.x / speed;
		const auto wish_y = -velocity.y / speed;
		const auto accel_speed = std::fminf( accel * accel_base * surface_friction * cstypes::tick_interval, speed );
		velocity.x += wish_x * accel_speed;
		velocity.y += wish_y * accel_speed;
		const auto move_magnitude = std::clamp( speed / ctx.weapon_max_speed, 0.0f, 1.0f );
		const auto yaw_rad = base->viewangles( )->y( ) * ( std::numbers::pi_v<float> / 180.0f );
		const auto sy = std::sinf( yaw_rad );
		const auto cy = std::cosf( yaw_rad );
		const auto forward_move = std::clamp( ( wish_x * cy + wish_y * sy ) * move_magnitude, -1.0f, 1.0f );
		const auto left_move = std::clamp( ( wish_x * sy - wish_y * cy ) * -move_magnitude, -1.0f, 1.0f );
		base->set_forwardmove( forward_move );
		base->set_leftmove( left_move );
		const auto subtick_moves = base->mutable_subtick_moves( );
		if ( subtick_moves )
		{
			const auto step = systems::g_input.acquire_subtick_step( subtick_moves );
			if ( step )
			{
				step->set_button( 0 );
				step->set_pressed( false );
				step->set_when( 0.0f );
				step->set_analog_forward_delta( forward_move - prestate.last_movement_impulses.x );
				step->set_analog_left_delta( left_move - prestate.last_movement_impulses.y );
			}
		}
		if ( forward_move > 0.0f )
		{
			cmd->buttons.value |= cstypes::command_buttons::in_forward;
		}
		else if ( forward_move < 0.0f )
		{
			cmd->buttons.value |= cstypes::command_buttons::in_back;
		}
		if ( left_move > 0.0f )
		{
			cmd->buttons.value |= cstypes::command_buttons::in_moveleft;
		}
		else if ( left_move < 0.0f )
		{
			cmd->buttons.value |= cstypes::command_buttons::in_moveright;
		}
	}
	float misc::autostop::get_effective_accel_base( std::uintptr_t local_pawn, std::uintptr_t movement_services, std::uint32_t flags, float max_weapon_speed ) const
	{
		const auto max_speed_base = memory::read<float>( movement_services + SCHEMA( "CPlayer_MovementServices", "m_flMaxspeed"_hash ) );
		const auto is_ducked = ( flags & 4 ) != 0;
		const auto ducking_state = memory::read<bool>( movement_services + SCHEMA( "CPlayer_MovementServices", "m_bDucking"_hash ) );
		const auto is_scoped = g_shared.ctx( ).is_scoped;
		const auto is_ducking = is_ducked || ducking_state;
		const auto v19 = std::fmaxf( 250.0f, max_speed_base );
		auto friction_scale{ 1.0f };
		if (CONVAR ("sv_accelerate_use_weapon_speed")->get<bool>( ) )
		{
			const auto weapon_ratio = std::fminf( 1.0f, max_weapon_speed / 250.0f );
			if ( !is_ducking && !is_scoped )
			{
				friction_scale = weapon_ratio;
			}
		}
		if ( is_ducking )
		{
			friction_scale = std::fminf( 0.34f, friction_scale );
		}
		auto accel_base = v19 * friction_scale;
		if ( is_scoped && !is_ducking )
		{
			accel_base *= 0.52f;
		}
		return accel_base;
	}
}
