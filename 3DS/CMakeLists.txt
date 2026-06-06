set(PPSSPP_3DS_TITLE "PPSSPP")
set(PPSSPP_3DS_DESCRIPTION "PSP emulator for Nintendo 3DS")
set(PPSSPP_3DS_AUTHOR "PPSSPP Team")
set(PPSSPP_3DS_UNIQUE_ID "0xF5050")
set(PPSSPP_3DS_PRODUCT_CODE "CTR-H-PPSP")

if(NOT DEFINED ENV{DEVKITPRO})
	message(FATAL_ERROR "DEVKITPRO is not set. Build with devkitpro/devkitarm or devkitPro's shell.")
endif()

set(DEVKITPRO "$ENV{DEVKITPRO}")
set(CTRULIB "${DEVKITPRO}/libctru")
set(DEVKITPRO_TOOLS "${DEVKITPRO}/tools/bin")

find_program(CTR_3DSXTOOL 3dsxtool HINTS "${DEVKITPRO_TOOLS}" REQUIRED)
find_program(CTR_SMDHTOOL smdhtool HINTS "${DEVKITPRO_TOOLS}" REQUIRED)
find_program(CTR_MAKEROM makerom HINTS "${DEVKITPRO_TOOLS}" REQUIRED)
find_program(CTR_MKROMFS3DS mkromfs3ds HINTS "${DEVKITPRO_TOOLS}" REQUIRED)
find_program(PYTHON3_EXECUTABLE python3 REQUIRED)

set(PPSSPP_3DS_ICON "${CTRULIB}/default_icon.png")
if(NOT EXISTS "${PPSSPP_3DS_ICON}")
	message(FATAL_ERROR "Missing libctru default icon: ${PPSSPP_3DS_ICON}")
endif()

add_executable(PPSSPP
	main.cpp
	Platform3DS.c
	Platform3DS.h
	RuntimeStubs.cpp
)

target_compile_features(PPSSPP PRIVATE cxx_std_17)
target_include_directories(PPSSPP SYSTEM PRIVATE
	"${CTRULIB}/include"
	"${DEVKITPRO}/portlibs/3ds/include"
)
target_link_directories(PPSSPP PRIVATE
	"${CTRULIB}/lib"
	"${DEVKITPRO}/portlibs/3ds/lib"
)
target_compile_definitions(PPSSPP PRIVATE
	__3DS__
	_3DS
)
target_link_libraries(PPSSPP PRIVATE
	${LinkCommon}
	Common
	pthread
	ctru
	m
)

set_target_properties(PPSSPP PROPERTIES
	OUTPUT_NAME "PPSSPP"
	SUFFIX ".elf"
)

set(PPSSPP_3DS_SMDH "${CMAKE_CURRENT_BINARY_DIR}/PPSSPP.smdh")
set(PPSSPP_3DS_3DSX "${CMAKE_CURRENT_BINARY_DIR}/PPSSPP.3dsx")
set(PPSSPP_3DS_CIA "${CMAKE_CURRENT_BINARY_DIR}/PPSSPP.cia")
set(PPSSPP_3DS_ROMFS "${CMAKE_CURRENT_BINARY_DIR}/PPSSPP.romfs")
set(PPSSPP_3DS_LEVEL3_ROMFS "${CMAKE_CURRENT_BINARY_DIR}/PPSSPP.level3.romfs")

add_custom_command(
	OUTPUT "${PPSSPP_3DS_SMDH}"
	COMMAND "${CTR_SMDHTOOL}" --create
		"${PPSSPP_3DS_TITLE}"
		"${PPSSPP_3DS_DESCRIPTION}"
		"${PPSSPP_3DS_AUTHOR}"
		"${PPSSPP_3DS_ICON}"
		"${PPSSPP_3DS_SMDH}"
	DEPENDS "${PPSSPP_3DS_ICON}"
	VERBATIM
)
add_custom_target(PPSSPP_SMDH DEPENDS "${PPSSPP_3DS_SMDH}")

add_custom_command(
	OUTPUT "${PPSSPP_3DS_3DSX}"
	COMMAND "${CTR_3DSXTOOL}"
		"$<TARGET_FILE:PPSSPP>"
		"${PPSSPP_3DS_3DSX}"
		"--smdh=${PPSSPP_3DS_SMDH}"
		"--romfs=${CMAKE_BINARY_DIR}/assets"
	DEPENDS PPSSPP PPSSPP_SMDH
	VERBATIM
)

add_custom_command(
	OUTPUT "${PPSSPP_3DS_LEVEL3_ROMFS}"
	COMMAND "${CTR_MKROMFS3DS}"
		"${CMAKE_BINARY_DIR}/assets"
		"${PPSSPP_3DS_LEVEL3_ROMFS}"
	DEPENDS "${CMAKE_BINARY_DIR}/assets"
	VERBATIM
)

add_custom_command(
	OUTPUT "${PPSSPP_3DS_ROMFS}"
	COMMAND "${PYTHON3_EXECUTABLE}"
		"${CMAKE_SOURCE_DIR}/ci/3ds/wrap-romfs-ivfc.py"
		"${PPSSPP_3DS_LEVEL3_ROMFS}"
		"${PPSSPP_3DS_ROMFS}"
	DEPENDS "${PPSSPP_3DS_LEVEL3_ROMFS}" "${CMAKE_SOURCE_DIR}/ci/3ds/wrap-romfs-ivfc.py"
	VERBATIM
)
add_custom_target(PPSSPP_ROMFS DEPENDS "${PPSSPP_3DS_ROMFS}")

add_custom_command(
	OUTPUT "${PPSSPP_3DS_CIA}"
	COMMAND "${CTR_MAKEROM}"
		-f cia
		-o "${PPSSPP_3DS_CIA}"
		-rsf "${CMAKE_CURRENT_SOURCE_DIR}/PPSSPP.rsf"
		-target t
		-elf "$<TARGET_FILE:PPSSPP>"
		-icon "${PPSSPP_3DS_SMDH}"
		-romfs "${PPSSPP_3DS_ROMFS}"
		-desc app:4
		-DAPP_TITLE="${PPSSPP_3DS_TITLE}"
		-DAPP_PRODUCT_CODE="${PPSSPP_3DS_PRODUCT_CODE}"
		-DAPP_UNIQUE_ID="${PPSSPP_3DS_UNIQUE_ID}"
	DEPENDS PPSSPP PPSSPP_SMDH PPSSPP_ROMFS "${CMAKE_CURRENT_SOURCE_DIR}/PPSSPP.rsf"
	VERBATIM
)

add_custom_target(PPSSPP_3DSX ALL DEPENDS "${PPSSPP_3DS_3DSX}")
add_custom_target(PPSSPP_CIA ALL DEPENDS "${PPSSPP_3DS_CIA}")
