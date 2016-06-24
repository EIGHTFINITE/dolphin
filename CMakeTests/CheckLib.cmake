include(FindPkgConfig OPTIONAL)

macro(_internal_message msg)
	if(NOT ${_is_quiet})
		message("${msg}")
	endif()
endmacro()

macro(check_lib var pc lib)
	set(_is_required 0)
	set(_is_quiet 0)
	set(_arg_list ${ARGN})
	foreach(_arg ${ARGN})
		if(_arg STREQUAL "REQUIRED")
			list(REMOVE_ITEM _arg_list "REQUIRED")
			set(_is_required 1)
		endif()
		if(_arg STREQUAL "QUIET")
			list(REMOVE_ITEM _arg_list "QUIET")
			set(_is_quiet 1)
		endif()
	endforeach()

	if(PKG_CONFIG_FOUND AND NOT ${var}_FOUND)
		pkg_search_module(${var} QUIET ${pc})
	endif()

	if(${var}_FOUND)
		include_directories(${${var}_INCLUDE_DIRS})
		# Make sure include directories for headers found using find_path below
		# are re-added when reconfiguring
		include_directories(${${var}_INCLUDE})
		_internal_message("${lib} found")
	else()
		find_library(${var} ${lib})
		if(_arg_list)
			find_path(${var}_INCLUDE ${_arg_list})
		else()
			set(${var}_INCLUDE FALSE)
		endif()
		if(${var} AND ${var}_INCLUDE)
			include_directories(${${var}_INCLUDE})
			_internal_message("${lib} found")
			set(${var}_FOUND 1 CACHE INTERNAL "")
		else()
			if(_is_required)
				message(FATAL_ERROR "${lib} is required but not found")
			else()
				_internal_message("${lib} not found")
			endif()
		endif()
	endif()
endmacro()

macro(check_libav)
	if(PKG_CONFIG_FOUND)
		pkg_check_modules(LIBAV libavcodec>=54.35.0 libavformat>=54.20.4
			libswscale>=2.1.1 libavutil>=52.3.0)
	else()
		# Attempt to find it through static means
		set(LIBAV_LDFLAGS avformat avcodec swscale avutil)
		set(CMAKE_REQUIRED_LIBRARIES ${LIBAV_LDFLAGS})
		CHECK_CXX_SOURCE_COMPILES(
			"extern \"C\" {
			#include <libavcodec/avcodec.h>
			#include <libavformat/avformat.h>
			#include <libavutil/mathematics.h>
			#include <libswscale/swscale.h>
			}
			int main(int argc, char **argv)
			{
				av_register_all();
				return 0;
			}"
			LIBAV_FOUND)
		unset(CMAKE_REQUIRED_LIBRARIES)
	endif()
	if(LIBAV_FOUND)
		message("libav/ffmpeg found, enabling AVI frame dumps")
		add_definitions(-DHAVE_LIBAV)
		include_directories(${LIBAV_INCLUDE_DIRS})
	else()
		message("libav/ffmpeg not found, disabling AVI frame dumps")
	endif()
endmacro()

