# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.22

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:

#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:

# Disable VCS-based implicit rules.
% : %,v

# Disable VCS-based implicit rules.
% : RCS/%

# Disable VCS-based implicit rules.
% : RCS/%,v

# Disable VCS-based implicit rules.
% : SCCS/s.%

# Disable VCS-based implicit rules.
% : s.%

.SUFFIXES: .hpux_make_needs_suffix_list

# Command-line flag to silence nested $(MAKE).
$(VERBOSE)MAKESILENT = -s

#Suppress display of executed commands.
$(VERBOSE).SILENT:

# A target that is always out of date.
cmake_force:
.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /usr/bin/cmake

# The command to remove a file.
RM = /usr/bin/cmake -E rm -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /home/void/Documents/Raft/myRaft

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /home/void/Documents/Raft/myRaft/build

# Include any dependencies generated for this target.
include example/fiberExample/CMakeFiles/test_hook.dir/depend.make
# Include any dependencies generated by the compiler for this target.
include example/fiberExample/CMakeFiles/test_hook.dir/compiler_depend.make

# Include the progress variables for this target.
include example/fiberExample/CMakeFiles/test_hook.dir/progress.make

# Include the compile flags for this target's objects.
include example/fiberExample/CMakeFiles/test_hook.dir/flags.make

example/fiberExample/CMakeFiles/test_hook.dir/test_hook.cpp.o: example/fiberExample/CMakeFiles/test_hook.dir/flags.make
example/fiberExample/CMakeFiles/test_hook.dir/test_hook.cpp.o: ../example/fiberExample/test_hook.cpp
example/fiberExample/CMakeFiles/test_hook.dir/test_hook.cpp.o: example/fiberExample/CMakeFiles/test_hook.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/void/Documents/Raft/myRaft/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building CXX object example/fiberExample/CMakeFiles/test_hook.dir/test_hook.cpp.o"
	cd /home/void/Documents/Raft/myRaft/build/example/fiberExample && /usr/bin/g++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -MD -MT example/fiberExample/CMakeFiles/test_hook.dir/test_hook.cpp.o -MF CMakeFiles/test_hook.dir/test_hook.cpp.o.d -o CMakeFiles/test_hook.dir/test_hook.cpp.o -c /home/void/Documents/Raft/myRaft/example/fiberExample/test_hook.cpp

example/fiberExample/CMakeFiles/test_hook.dir/test_hook.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing CXX source to CMakeFiles/test_hook.dir/test_hook.cpp.i"
	cd /home/void/Documents/Raft/myRaft/build/example/fiberExample && /usr/bin/g++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /home/void/Documents/Raft/myRaft/example/fiberExample/test_hook.cpp > CMakeFiles/test_hook.dir/test_hook.cpp.i

example/fiberExample/CMakeFiles/test_hook.dir/test_hook.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling CXX source to assembly CMakeFiles/test_hook.dir/test_hook.cpp.s"
	cd /home/void/Documents/Raft/myRaft/build/example/fiberExample && /usr/bin/g++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /home/void/Documents/Raft/myRaft/example/fiberExample/test_hook.cpp -o CMakeFiles/test_hook.dir/test_hook.cpp.s

# Object files for target test_hook
test_hook_OBJECTS = \
"CMakeFiles/test_hook.dir/test_hook.cpp.o"

# External object files for target test_hook
test_hook_EXTERNAL_OBJECTS =

../bin/test_hook: example/fiberExample/CMakeFiles/test_hook.dir/test_hook.cpp.o
../bin/test_hook: example/fiberExample/CMakeFiles/test_hook.dir/build.make
../bin/test_hook: ../lib/libfiber_lib.a
../bin/test_hook: example/fiberExample/CMakeFiles/test_hook.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/home/void/Documents/Raft/myRaft/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking CXX executable ../../../bin/test_hook"
	cd /home/void/Documents/Raft/myRaft/build/example/fiberExample && $(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/test_hook.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
example/fiberExample/CMakeFiles/test_hook.dir/build: ../bin/test_hook
.PHONY : example/fiberExample/CMakeFiles/test_hook.dir/build

example/fiberExample/CMakeFiles/test_hook.dir/clean:
	cd /home/void/Documents/Raft/myRaft/build/example/fiberExample && $(CMAKE_COMMAND) -P CMakeFiles/test_hook.dir/cmake_clean.cmake
.PHONY : example/fiberExample/CMakeFiles/test_hook.dir/clean

example/fiberExample/CMakeFiles/test_hook.dir/depend:
	cd /home/void/Documents/Raft/myRaft/build && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/void/Documents/Raft/myRaft /home/void/Documents/Raft/myRaft/example/fiberExample /home/void/Documents/Raft/myRaft/build /home/void/Documents/Raft/myRaft/build/example/fiberExample /home/void/Documents/Raft/myRaft/build/example/fiberExample/CMakeFiles/test_hook.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : example/fiberExample/CMakeFiles/test_hook.dir/depend

