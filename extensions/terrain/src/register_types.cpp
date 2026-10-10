#include "register_types.h"

#include "chunk_node.h"
#include "chunk_generator.h"
#include "chunk_loader.h"
#include "chunk_viewer.h"
#include "collision_generator.h"
#include "mesh_generator.h"
#include "terrain_performance_monitor.h"
#include "thread_pool.h"

#include <client/TracyProfiler.hpp>
#include <gdextension_interface.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/godot.hpp>


using namespace godot;

static TerrainPerformanceMonitor* _terrain_performance_monitor_instance = nullptr;

static void initialize_gdextension_types(ModuleInitializationLevel p_level)
{
	if (p_level == MODULE_INITIALIZATION_LEVEL_CORE)
	{
#ifdef TRACY_MANUAL_LIFETIME
		// Use manual tracy lifetime to prevent it from hanging the app on shutdown.
		// Look into a different approach if adding tracy to more extensions.
		tracy::StartupProfiler();
#endif
	}
	else if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE)
	{
		GDREGISTER_CLASS(TerrainPerformanceMonitor)
		_terrain_performance_monitor_instance = memnew(TerrainPerformanceMonitor);
		_terrain_performance_monitor_instance->initialize();
	}

	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE)
	{
		return;
	}
	GDREGISTER_CLASS(ChunkNode)
	GDREGISTER_CLASS(ChunkGeneratorSettings)
	GDREGISTER_CLASS(ChunkGenerator)
	GDREGISTER_CLASS(ChunkLoader)
	GDREGISTER_CLASS(ChunkViewer)
	GDREGISTER_CLASS(CollisionGenerator)
	GDREGISTER_CLASS(MeshGeneratorSettings)
	GDREGISTER_CLASS(MeshGenerator)
	GDREGISTER_CLASS(ThreadPoolBase)
}

static void uninitialize_gdextension_types(ModuleInitializationLevel p_level)
{
	if (p_level == MODULE_INITIALIZATION_LEVEL_CORE)
	{
#ifdef TRACY_MANUAL_LIFETIME
		tracy::ShutdownProfiler();
#endif
	}
	else if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE)
	{
		_terrain_performance_monitor_instance->uninitialize();
		memdelete(_terrain_performance_monitor_instance);
		_terrain_performance_monitor_instance = nullptr;
	}

}

extern "C"
{
	// Initialization
	// GDE_ENTRY_NAME is set by CMake to <extension folder name>_library_init
	GDExtensionBool GDE_EXPORT GDE_ENTRY_NAME(GDExtensionInterfaceGetProcAddress p_get_proc_address, GDExtensionClassLibraryPtr p_library, GDExtensionInitialization* r_initialization)
	{
		GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
		init_obj.register_initializer(initialize_gdextension_types);
		init_obj.register_terminator(uninitialize_gdextension_types);
		init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

		return init_obj.init();
	}
}
