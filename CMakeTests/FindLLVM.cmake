# This file only exists because LLVM's cmake files are broken.
# This affects both LLVM 3.4 and 3.5.
# Hopefully when they fix their cmake system we don't need this garbage.

include(CheckLibraryExists)

list(APPEND LLVM_CONFIG_EXECUTABLES "llvm-config")
list(APPEND LLVM_CONFIG_EXECUTABLES "llvm-config-3.5")
list(APPEND LLVM_CONFIG_EXECUTABLES "llvm-config-3.4")

foreach(LLVM_CONFIG_NAME ${LLVM_CONFIG_EXECUTABLES})
	find_program(LLVM_CONFIG_EXE NAMES ${LLVM_CONFIG_NAME})
	if (LLVM_CONFIG_EXE)
		execute_process(COMMAND ${LLVM_CONFIG_EXE} --version OUTPUT_VARIABLE LLVM_PACKAGE_VERSION
			OUTPUT_STRIP_TRAILING_WHITESPACE )
		if (${LLVM_PACKAGE_VERSION} VERSION_GREATER "3.3")
			execute_process(COMMAND ${LLVM_CONFIG_EXE} --includedir OUTPUT_VARIABLE LLVM_INCLUDE_DIRS
				OUTPUT_STRIP_TRAILING_WHITESPACE )
			execute_process(COMMAND ${LLVM_CONFIG_EXE} --ldflags OUTPUT_VARIABLE LLVM_LDFLAGS
				OUTPUT_STRIP_TRAILING_WHITESPACE )
			check_library_exists(LLVM-${LLVM_PACKAGE_VERSION} LLVMVerifyFunction "${LLVM_LDFLAGS}" HAVE_DYNAMIC_LLVM_${LLVM_PACKAGE_VERSION})
			if (HAVE_DYNAMIC_LLVM_${LLVM_PACKAGE_VERSION})
				set(LLVM_LIBRARIES "${LLVM_LDFLAGS} -lLLVM-${LLVM_PACKAGE_VERSION}")
				set(CMAKE_REQUIRED_LIBRARIES ${LLVM_LIBRARIES})
				CHECK_CXX_SOURCE_COMPILES(
					"#include <llvm-c/Disassembler.h>
					#include <llvm-c/Target.h>
					int main(int argc, char **argv)
					{
						LLVMInitializeAllTargetInfos();
						LLVMInitializeAllTargetMCs();
						LLVMInitializeAllDisassemblers();
						return 0;
					}"
					LLVM_FOUND)
				unset(CMAKE_REQUIRED_LIBRARIES)

				if (LLVM_FOUND)
					break()
				endif()
			endif()
		endif()
	endif()
endforeach()
