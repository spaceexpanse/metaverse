# Copyright (c) Martin Schweiger
# Licensed under the MIT License

set(CELBODY "Sun")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Modules/Celbody)

add_library(${CELBODY} SHARED
	${CELBODY}.cpp
)

add_dependencies(${CELBODY}
	Vsop87
	${SpaceXpanseTgt}
	SpaceXpansesdk
)

target_include_directories(${CELBODY}
	PUBLIC ${CMAKE_SOURCE_DIR}/SpaceXpansesdk/include
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(${CELBODY}
	${VSOP87_LIB}
	${SPACEXPANSE_LIB}
	${SPACEXPANSE_SDK_LIB}
)

add_custom_command(
	TARGET ${CELBODY} PRE_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/Config/ ${CMAKE_BINARY_DIR}/Config	
)

add_custom_command(
	TARGET ${CELBODY} PRE_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/Config/${CELBODY}/Data/
	COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/../Data/Vsop87E_sun.dat ${CMAKE_BINARY_DIR}/Config/${CELBODY}/Data/Vsop87E.dat
)

set_target_properties(${CELBODY}
	PROPERTIES
	FOLDER Celbody/Vsop87
)

#Installation
install(TARGETS
	${CELBODY}
	RUNTIME
	DESTINATION ${SPACEXPANSE_INSTALL_CELBODY_DIR}
)
install(DIRECTORY Config
	DESTINATION ${SPACEXPANSE_INSTALL_ROOT_DIR}
)
install(FILES ../Data/Vsop87E_sun.dat
	DESTINATION ${SPACEXPANSE_INSTALL_ROOT_DIR}/Config/${CELBODY}/Data
	RENAME Vsop87E.dat
)
