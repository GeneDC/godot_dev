cmake_minimum_required(VERSION 3.19)
project(godot_dev)

set(GD_ENGINE_VERSION "4.5")
set(GD_ENGINE_DIR "${CMAKE_SOURCE_DIR}/godot")
set(GD_PROJECT_DIR "${CMAKE_SOURCE_DIR}/project")
set(GD_CPP_DIR "${CMAKE_SOURCE_DIR}/godot-cpp")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_COLOR_DIAGNOSTICS ON)
set(CMAKE_MESSAGE_LOG_LEVEL STATUS)

# Define custom configuration types
set(CMAKE_CONFIGURATION_TYPES "dev_build;release_debug;release" CACHE STRING "" FORCE)

# Map custom configs to standard godot flags
set(CMAKE_MAP_IMPORTED_CONFIG_DEV_BUILD Debug)
set(CMAKE_MAP_IMPORTED_CONFIG_RELEASE_DEBUG RelWithDebInfo)

# Explicitly initialize the missing flag variables
set(CMAKE_C_FLAGS_DEV_BUILD ${CMAKE_C_FLAGS_DEBUG})
set(CMAKE_CXX_FLAGS_DEV_BUILD ${CMAKE_CXX_FLAGS_DEBUG})
set(CMAKE_EXE_LINKER_FLAGS_DEV_BUILD ${CMAKE_EXE_LINKER_FLAGS_DEBUG})
set(CMAKE_SHARED_LINKER_FLAGS_DEV_BUILD ${CMAKE_SHARED_LINKER_FLAGS_DEBUG})

set(CMAKE_C_FLAGS_RELEASE_DEBUG ${CMAKE_C_FLAGS_RELWITHDEBINFO})
set(CMAKE_CXX_FLAGS_RELEASE_DEBUG ${CMAKE_CXX_FLAGS_RELWITHDEBINFO})
set(CMAKE_EXE_LINKER_FLAGS_RELEASE_DEBUG ${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO})
set(CMAKE_SHARED_LINKER_FLAGS_RELEASE_DEBUG ${CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO})

if(MSVC)
    set(ENGINE_VSPROJ "${CMAKE_CURRENT_SOURCE_DIR}/${GD_ENGINE_DIR}/godot.vcxproj")

    if(EXISTS "${ENGINE_VSPROJ}")
        message(STATUS "Found Engine Project: Attaching to Solution")
        include_external_msproject(
            godot_engine 
            "${ENGINE_VSPROJ}"
			TYPE vcxproj
        )
		
		# MAP the Master Solution Config to the Engine Project Config
        set_target_properties(godot_engine PROPERTIES
            MAP_IMPORTED_CONFIG_DEV_BUILD "template_debug"
            MAP_IMPORTED_CONFIG_RELEASE_DEBUG "template_release"
			FOLDER "godot-engine"
        )
    else()
        message(WARNING "Godot Engine project not found at ${ENGINE_VSPROJ}. "
                        "Run SCons with 'vsproj=yes' in the engine dir to enable engine debugging.")
    endif()
endif()

message(STATUS "Adding godot-cpp subdirectory")
add_subdirectory(godot-cpp)

# Force godot-cpp to output to its own folder instead of /build
set_target_properties(godot-cpp PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${GD_CPP_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY_DEV_BUILD "${GD_CPP_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${GD_CPP_DIR}/bin"
)

file(GLOB extension_dirs RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "extensions/*")

foreach(dir ${extension_dirs})
    get_filename_component(ext_name ${dir} NAME)
    
    set(current_ext_path "${CMAKE_CURRENT_SOURCE_DIR}/extensions/${ext_name}")
    set(current_ext_src "${CMAKE_CURRENT_SOURCE_DIR}/extensions/${ext_name}/src")

    if(IS_DIRECTORY ${current_ext_src})
        message(STATUS "Adding Extension Project: ${ext_name}")
        
		# Doc generation
		# Check if the doc_classes folder exists and has XMLs
		file(GLOB EXT_DOCS "${current_ext_path}/doc_classes/*.xml")

		if(EXT_DOCS)
			message(STATUS "Found Documentation for ${ext_name}: Generating C++ data...")
			
			# Only include docs for Editor/Debug builds
			if(CMAKE_BUILD_TYPE MATCHES "Debug|RelWithDebInfo|dev_build")
				
				# Use official godot-cpp helper (ensure your godot-cpp version is 4.3+)
				target_doc_sources(${ext_name}
					SOURCES ${EXT_DOCS}
					GEN_CPP "${current_ext_path}/src/gen/doc_data.gen.cpp"
				)
				
				# Ensure the generated folder is in the include path
				target_include_directories(${ext_name} PRIVATE "${current_ext_path}/src/gen")
			endif()
		endif()
		
        # Use the absolute path for GLOB to avoid confusion
        file(GLOB_RECURSE SOURCES 
            "${current_ext_src}/*.cpp" 
            "${current_ext_src}/*.hpp" 
            "${current_ext_src}/*.h"
        )

        if(SOURCES)
            add_library(${ext_name} SHARED ${SOURCES})

            # Organize the IDE View
            # Put the project in the "Extensions" folder
            set_target_properties(${ext_name} PROPERTIES FOLDER "Extensions")
            # Tell VS to mirror the actual folder structure on disk
            source_group(TREE "${current_ext_path}" FILES ${SOURCES})

            # Build settings
            target_link_libraries(${ext_name} PRIVATE godot-cpp)
            target_include_directories(${ext_name} PRIVATE ${current_ext_src})
			
			message(STATUS "Generating .gdextension for ${ext_name}")
    
			# Uses the folder name for the libs and entry symbol
			set(GD_EXT_FILE_CONTENT 
"[configuration]
entry_symbol = \"${ext_name}_library_init\"
compatibility_minimum = \"${GD_ENGINE_VERSION}\"

[libraries]
")
			
			if (WIN32)
				set(PLATFORM_NAME "windows")
			elseif (LINUX)
				set(PLATFORM_NAME "linux")
			else()
				message(WARNING "Unsupported platform!")
			endif()
			
			set(ARCH "x86_64")
			set(PRECISION "single")
			set(BUILD_CONFIGURATION "debug")
			string(APPEND GD_EXT_FILE_CONTENT "${PLATFORM_NAME}.${ARCH}.${PRECISION}.${BUILD_CONFIGURATION} = \"./${ext_name}/dev_build/${ext_name}.dll\"\n")
			set(BUILD_CONFIGURATION "release")
			string(APPEND GD_EXT_FILE_CONTENT "${PLATFORM_NAME}.${ARCH}.${PRECISION}.${BUILD_CONFIGURATION} = \"./${ext_name}/dev_build/${ext_name}.dll\"\n")

			# Copy the .gdextension file
			set(GD_EXT_FILE_SRC "${current_ext_path}/bin/${ext_name}.gdextension")
			set(GD_EXT_FILE_DEST "${GD_PROJECT_DIR}/bin/${ext_name}.gdextension")
			
			file(WRITE "${GD_EXT_FILE_SRC}" "${GD_EXT_FILE_CONTENT}")

			message(STATUS "Copying .gdextension for ${ext_name} to project")
			configure_file("${GD_EXT_FILE_SRC}" "${GD_EXT_FILE_DEST}" COPYONLY)
			
			# Deploy libs to project dir
			add_custom_command(TARGET ${ext_name} POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"${CMAKE_SOURCE_DIR}/build/bin/${ext_name}/dev_build/${ext_name}.dll"
				"${GD_PROJECT_DIR}/bin/${ext_name}/dev_build/${ext_name}.dll"
				COMMENT "Deploy DLL to Godot project..."
			)
			
			# Creates macro GDE_ENTRY_NAME based on the extension name
			target_compile_definitions(${ext_name} PRIVATE 
				GDE_ENTRY_NAME=${ext_name}_library_init
			)
			
            set_target_properties(${ext_name} PROPERTIES
                LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/bin/${ext_name}"
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/bin/${ext_name}"
                PREFIX ""
                FOLDER "extensions"
                VS_DEBUGGER_COMMAND "${GD_ENGINE_DIR}/bin/godot.windows.editor.dev.x86_64.exe"
                VS_DEBUGGER_COMMAND_ARGUMENTS "--editor --path \"${GD_PROJECT_DIR}\""
                VS_DEBUGGER_WORKING_DIRECTORY "${GD_PROJECT_DIR}"
            )
        endif()
    endif()	
endforeach()