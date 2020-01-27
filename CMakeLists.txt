cmake_minimum_required(VERSION 3.0)
project(str_to_enum_trie VERSION 0.1)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/add_module")
include(AddModule)

add_module(cstuff
  GIT 874ae5456e63ea82ddba9cca149600a71be06daf
  file://$ENV{HOME}/code/cstuff
  file://$ENV{HOME}/repo/cstuff
  git@github.com:cyisfor/cstuff.git
  https://github.com/cyisfor/cstuff.git)

add_cstuff()
add_executable(str_to_enum_trie
	src/main.c)
target_link_libraries(str_to_enum_trie PRIVATE cstuff)

function(str_to_enum_trie prefix source)
	set(options NOUPPER NOCASE NULL_TERMINATED)
	set(args PREFIX ENUM FILE OUTPUT_NAME)
	get_filename_component(source "${source}" ABSOLUTE BASE_DIR
	  "${CMAKE_CURRENT_SOURCE_DIR}")
	cmake_parse_arguments(PARSE_ARGV 2 O "${options}" "${args}" "")
	if(O_OUTPUT_NAME)
		set(output_name "${O_OUTPUT_NAME}")
	else()
		set(output_name "${prefix}")
	endif()
	get_filename_component(full_output "${output_name}" ABSOLUTE
		BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
	set(command "prefix=${prefix}" "file=${full_output}.c")
	if(O_NOUPPER)
		list(APPEND command "noupper=1")
	endif()
	if(O_NOCASE)
		list(APPEND command "nocase=1")
	endif()
	if(O_PREFIX)
		list(APPEND command "prefix=${O_PREFIX}")
	endif()
	if(O_ENUM)
		list(APPEND command "enum=${O_ENUM}")
	endif()
	add_custom_command(
	OUTPUT ${output_name}.c ${output_name}.h
	COMMAND
	${command}
	$<TARGET_FILE:str_to_enum_trie>
	<"${source}"
	WORKING_DIRECTORY
	"${CMAKE_CURRENT_SOURCE_DIR}"
	COMMAND_EXPAND_LISTS
	MAIN_DEPENDENCY
	"${source}"
	DEPENDS
	str_to_enum_trie)
endfunction()

if("${CMAKE_CURRENT_SOURCE_DIR}" STREQUAL "${CMAKE_SOURCE_DIR}")
str_to_enum_trie(foo test.example
	OUTPUT_NAME example.trie 
	NOUPPER NOCASE
	ENUM bar)

add_executable(example
	src/example.c
	example.trie.c)
target_include_directories(example PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
endif()
