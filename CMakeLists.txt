cmake_minimum_required(VERSION 3.0)
project(str_to_enum_trie VERSION 0.1)

add_executable(str_to_enum_trie
	src/main.c)

add_executable(example
	src/example.c
	example.trie.c)
target_include_directories(example PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

function(str_to_enum_trie output_name source)
	cmake_parse_arguments(PARSE_ARGV 2 O "NOUPPER;NOCASE" "PREFIX;ENUM" "")
	get_filename_component(full_output "${output_name}" ABSOLUTE
		BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
	set(command "file=${full_output}.c")
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
	list(APPEND command  "${CMAKE_CURRENT_BINARY_DIR}/str_to_enum_trie")
	add_custom_command(
	OUTPUT ${output_name}.c ${output_name}.h
	COMMAND
	${command} <"${source}"
	WORKING_DIRECTORY
	"${CMAKE_CURRENT_SOURCE_DIR}"
	COMMAND_EXPAND_LISTS
	MAIN_DEPENDENCY
	"${source}"
	DEPENDS
	str_to_enum_trie)
endfunction()

str_to_enum_trie(example.trie test.example
	NOUPPER NOCASE
	PREFIX foo
	ENUM bar)

