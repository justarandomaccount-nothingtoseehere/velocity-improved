#include <cstring>
#include <mutex>
#include <unordered_set>
#include <vector>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>
#include <signatures/game_addresses.hpp>
namespace features::changer {
	namespace detail {
		constexpr std::size_t player_inventory_offset{ 0x528D0 };
		constexpr std::size_t item_list_offset{ 0x4D0 };
		constexpr std::size_t item_list_cached_offset{ 0x4CF };
		constexpr std::size_t maximum_views{ 8192 };
		constexpr std::uint64_t fake_item_id_base{ 0xf000000000000000ull };
		[[nodiscard]] std::uint64_t make_fake_item_id( std::int16_t def_index, int paint_kit_id )
		{
			return fake_item_id_base |
				( static_cast< std::uint64_t >( paint_kit_id & 0xFFFF ) << 16 ) |
				static_cast< std::uint64_t >( static_cast< std::uint16_t >( def_index ) );
		}
	}
	std::uint32_t inventory::state_hash( ) const
	{
		auto hash{ 2166136261u };
		const auto mix = [ &hash ]( std::uint32_t value )
			{
				hash ^= value;
				hash *= 16777619u;
			};
		const auto& inv = settings::g_changer.m_inventory;
		mix( inv.replace.value );
		mix( inv.configured.value );
		mix( inv.music_kits.value );
		mix( inv.collectibles.value );
		mix( inv.stickers.value );
		mix( inv.patches.value );
		mix( inv.graffiti.value );
		mix( inv.cases.value );
		mix( inv.keys.value );
		mix( inv.tools.value );
		mix( inv.charms.value );
		mix( inv.all_guns.value );
		mix( inv.all_knives.value );
		mix( inv.all_gloves.value );
		mix( inv.all_agents.value );
		mix( static_cast< std::uint32_t >( settings::g_changer.agents.ct_def ) );
		mix( static_cast< std::uint32_t >( settings::g_changer.agents.t_def ) );
		mix( static_cast< std::uint32_t >( g_econ_item_system.item_defs( ).size( ) ) );
		mix( static_cast< std::uint32_t >( settings::g_changer.skins.data.size( ) ) );
		for ( const auto& [ def, skin ] : settings::g_changer.skins.data )
		{
			mix( static_cast< std::uint32_t >( def ) );
			mix( static_cast< std::uint32_t >( skin.paint_kit_id ) );
			mix( static_cast< std::uint32_t >( skin.seed ) );
			auto wear{ std::uint32_t{} };
			std::memcpy( &wear, &skin.wear, sizeof( wear ) );
			mix( wear );
		}
		return hash;
	}
	bool inventory::wants( const econ_item_system::item_def& def ) const
	{
		const auto& inv = settings::g_changer.m_inventory;
		switch ( def.category )
		{
		case econ_item_system::item_category::gun:
			return inv.all_guns.value;
		case econ_item_system::item_category::knife:
			return inv.all_knives.value;
		case econ_item_system::item_category::glove:
			return inv.all_gloves.value;
		case econ_item_system::item_category::agent:
			return inv.all_agents.value;
		case econ_item_system::item_category::music_kit:
			return inv.music_kits.value;
		case econ_item_system::item_category::collectible:
			return inv.collectibles.value;
		case econ_item_system::item_category::sticker:
			return inv.stickers.value;
		case econ_item_system::item_category::patch:
			return inv.patches.value;
		case econ_item_system::item_category::graffiti:
			return inv.graffiti.value;
		case econ_item_system::item_category::case_:
			return inv.cases.value;
		case econ_item_system::item_category::key:
			return inv.keys.value;
		case econ_item_system::item_category::tool:
			return inv.tools.value;
		case econ_item_system::item_category::charm:
			return inv.charms.value;
		default:
			return false;
		}
	}
	std::uint64_t inventory::ensure_view( const spec& s )
	{
		if ( s.def_index <= 0 )
		{
			return 0;
		}
		const auto manager = addresses::globals::inventory_manager;
		if ( !manager )
		{
			return 0;
		}
		const auto id = detail::make_fake_item_id( s.def_index, s.paint_kit_id );
		auto view = memory::call<std::uintptr_t>(
			PATTERN( patterns::inventory_find_item_view ),
			manager,
			static_cast< std::uint16_t >( s.def_index ),
			static_cast< std::uint32_t >( s.paint_kit_id ),
			static_cast< std::uint8_t >( 0 )
		);
		if ( !view )
		{
			view = memory::call<std::uintptr_t>(
				PATTERN( patterns::create_econ_item_view ),
				manager,
				static_cast< std::uint16_t >( s.def_index ),
				static_cast< std::uint32_t >( s.paint_kit_id ),
				static_cast< std::uint8_t >( 0 ),
				static_cast< char >( 1 )
			);
		}
		if ( !view )
		{
			return 0;
		}
		if ( s.paint_kit_id != 0 )
		{
			const auto set_attribute = PATTERN( patterns::econ_item_view_set_attribute );
			memory::call<void>( set_attribute, view, "set item texture prefab", static_cast< float >( s.paint_kit_id ) );
			memory::call<void>( set_attribute, view, "set item texture seed", static_cast< float >( s.seed ) );
			memory::call<void>( set_attribute, view, "set item texture wear", s.wear > 0.0f ? s.wear : 0.01f );
			memory::call<void>( PATTERN( patterns::econ_item_view_invalidate_description ), view );
		}
		return id;
	}
	void inventory::rebuild( )
	{
		const auto manager = addresses::globals::inventory_manager;
		if ( !manager )
		{
			return;
		}
		const auto player_inventory = memory::read<std::uintptr_t>( manager + detail::player_inventory_offset );
		if ( !player_inventory )
		{
			return;
		}
		this->m_manager = manager;
		this->m_player_inventory = player_inventory;
		auto& econ = g_econ_item_system;
		std::vector<entry> views{};
		std::vector<spec> specs{};
		views.reserve( 512 );
		specs.reserve( 64 );
		std::unordered_set<std::uint64_t> seen{};
		seen.reserve( 512 );
		const auto total = [ & ]( )
			{
				return views.size( ) + specs.size( );
			};
		if ( settings::g_changer.m_inventory.configured.value )
		{
			for ( const auto& [ def_index, skin ] : settings::g_changer.skins.data )
			{
				const auto* def = econ.find_def( def_index );
				if ( !def || total( ) >= detail::maximum_views )
				{
					continue;
				}
				if (
					def->category != econ_item_system::item_category::gun &&
					def->category != econ_item_system::item_category::knife &&
					def->category != econ_item_system::item_category::glove
					)
				{
					continue;
				}
				const auto id = detail::make_fake_item_id( def->def_index, skin.paint_kit_id );
				if ( !seen.insert( id ).second )
				{
					continue;
				}
				specs.push_back( { def->def_index, skin.paint_kit_id, skin.seed, skin.wear } );
			}
			for ( const auto def_index : { settings::g_changer.agents.ct_def, settings::g_changer.agents.t_def } )
			{
				const auto* def = econ.find_def( def_index );
				if ( !def || def->category != econ_item_system::item_category::agent || total( ) >= detail::maximum_views )
				{
					continue;
				}
				const auto id = detail::make_fake_item_id( def->def_index, 0 );
				if ( !seen.insert( id ).second )
				{
					continue;
				}
				specs.push_back( { def->def_index, 0, 0, 0.0f } );
			}
		}
		for ( const auto& def : econ.item_defs( ) )
		{
			if ( !this->wants( def ) || def.def_index <= 0 || total( ) >= detail::maximum_views )
			{
				continue;
			}
			const auto id = detail::make_fake_item_id( def.def_index, 0 );
			if ( !seen.insert( id ).second )
			{
				continue;
			}
			views.push_back( { id } );
		}
#if defined( DEV )
		const auto count = views.size( ) + specs.size( );
#endif
		{
			std::lock_guard lock( this->m_mutex );
			this->m_views.swap( views );
			this->m_specs.swap( specs );
		}
#if defined( DEV )
		logging::console::print( "[inventory] rebuilt with {} views", count );
#endif
	}
	void inventory::on_frame_stage_notify( )
	{
		const auto enabled = settings::g_changer.m_inventory.enabled.value;
		if ( enabled != this->m_enabled )
		{
			this->m_enabled = enabled;
			this->m_invalidate = true;
			if ( enabled )
			{
				this->m_state = 0;
			}
		}
		if ( !enabled )
		{
			return;
		}
		const auto manager = addresses::globals::inventory_manager;
		const auto player_inventory = manager ? memory::read<std::uintptr_t>( manager + detail::player_inventory_offset ) : 0;
		const auto state = this->state_hash( );
		if ( state == this->m_state && manager == this->m_manager && player_inventory == this->m_player_inventory )
		{
			return;
		}
		this->m_state = state;
		this->m_invalidate = true;
		this->rebuild( );
	}
	bool inventory::prepare_refresh( std::uintptr_t panel )
	{
		if ( !panel )
		{
			return false;
		}
		const auto invalidate = this->m_invalidate.exchange( false );
		const auto cached = memory::read<std::uint8_t>( panel + detail::item_list_cached_offset ) != 0;
		if ( invalidate )
		{
			memory::write<std::uint8_t>( panel + detail::item_list_cached_offset, 0 );
		}
		return invalidate || !cached;
	}
	void inventory::on_refresh( std::uintptr_t panel )
	{
		if ( !settings::g_changer.m_inventory.enabled.value || !panel )
		{
			return;
		}
		std::vector<entry> views{};
		std::vector<spec> specs{};
		{
			std::lock_guard lock( this->m_mutex );
			views = this->m_views;
			specs = this->m_specs;
		}
		if ( views.empty( ) && specs.empty( ) )
		{
			return;
		}
		const auto add = PATTERN( patterns::inventory_add_display_item );
		if ( !add )
		{
			return;
		}
		std::unordered_set<std::uint64_t> existing{};
		if ( settings::g_changer.m_inventory.replace.value )
		{
			memory::write<int>( panel + detail::item_list_offset, 0 );
		}
		else
		{
			const auto count = memory::read<int>( panel + detail::item_list_offset );
			const auto array = memory::read<std::uintptr_t>( panel + detail::item_list_offset + 8 );
			if ( count > 0 && count < 1000000 && array )
			{
				existing.reserve( static_cast< std::size_t >( count ) );
				for ( auto i = 0; i < count; i++ )
				{
					existing.insert( memory::read<std::uint64_t>( array + static_cast< std::size_t >( i ) * 8 ) );
				}
			}
		}
		std::unordered_set<std::uint64_t> written{};
		const auto append = [ & ]( std::uint64_t id )
			{
				if ( !id || existing.contains( id ) || !written.insert( id ).second )
				{
					return;
				}
				memory::call<void>( add, panel + detail::item_list_offset, &id );
			};
		for ( const auto& s : specs )
		{
			append( this->ensure_view( s ) );
		}
		for ( const auto& v : views )
		{
			append( v.id );
		}
	}
}
