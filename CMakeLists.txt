cmake_minimum_required(VERSION 3.0)
project(str_to_enum_trie VERSION 0.1)

add_executable(str_to_enum_trie
	src/main.c)

add_executable(example
	src/example.c
	example.trie.c)
target_include_directories(example PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

add_custom_command(
	OUTPUT example.trie.c example.trie.h
	COMMAND
	file=example.trie.c
	noupper=1
	nocase=1
	prefix=foo
	enum=bar
	./str_to_enum_trie
	<"${CMAKE_CURRENT_SOURCE_DIR}/test.example"
	MAIN_DEPENDENCY
	"${CMAKE_CURRENT_SOURCE_DIR}/test.example"
	DEPENDS
	str_to_enum_trie)
	
