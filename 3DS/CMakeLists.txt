set(PPSSPP_3DS_TITLE "PPSSPP")
set(PPSSPP_3DS_DESCRIPTION "PSP emulator for Nintendo 3DS")
set(PPSSPP_3DS_AUTHOR "PPSSPP Team")
set(PPSSPP_3DS_UNIQUE_ID "0xEC0DE")
set(PPSSPP_3DS_PRODUCT_CODE "CTR-P-PPSP")
set(PPSSPP_3DS_TITLE_VERSION "1030")

if(NOT DEFINED ENV{DEVKITPRO})
	message(FATAL_ERROR "DEVKITPRO is not set. Build with devkitpro/devkitarm or devkitPro's shell.")
endif()

set(DEVKITPRO "$ENV{DEVKITPRO}")
set(CTRULIB "${DEVKITPRO}/libctru")
set(DEVKITPRO_TOOLS "${DEVKITPRO}/tools/bin")

find_program(CTR_3DSXTOOL 3dsxtool HINTS "${DEVKITPRO_TOOLS}" REQUIRED)
find_program(CTR_BANNERTOOL bannertool HINTS "${DEVKITPRO_TOOLS}" REQUIRED)
find_program(CTR_MAKEROM makerom HINTS "${DEVKITPRO_TOOLS}" REQUIRED)
find_program(CTR_PICASSO picasso HINTS "${DEVKITPRO_TOOLS}" REQUIRED)
find_program(PYTHON3_EXECUTABLE python3 REQUIRED)

set(PPSSPP_3DS_ICON "${CMAKE_SOURCE_DIR}/icons/hicolor/48x48/apps/ppsspp.png")
if(NOT EXISTS "${PPSSPP_3DS_ICON}")
	message(FATAL_ERROR "Missing PPSSPP icon: ${PPSSPP_3DS_ICON}")
endif()
set(PPSSPP_3DS_BANNER_ICON "${CMAKE_SOURCE_DIR}/icons/hicolor/512x512/apps/ppsspp.png")
if(NOT EXISTS "${PPSSPP_3DS_BANNER_ICON}")
	message(FATAL_ERROR "Missing PPSSPP banner icon: ${PPSSPP_3DS_BANNER_ICON}")
endif()

set(PPSSPP_3DS_GPU_SHADER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/gpu3ds_textured.v.pica")
set(PPSSPP_3DS_GPU_SHADER_BIN "${CMAKE_CURRENT_BINARY_DIR}/gpu3ds_textured.shbin")
set(PPSSPP_3DS_GPU_SHADER_C "${CMAKE_CURRENT_BINARY_DIR}/gpu3ds_textured_shbin.c")
set(PPSSPP_3DS_GPU_SHADER_H "${CMAKE_CURRENT_BINARY_DIR}/gpu3ds_textured_shbin.h")

add_custom_command(
	OUTPUT "${PPSSPP_3DS_GPU_SHADER_BIN}"
	COMMAND "${CTR_PICASSO}" -o "${PPSSPP_3DS_GPU_SHADER_BIN}" "${PPSSPP_3DS_GPU_SHADER_SOURCE}"
	DEPENDS "${PPSSPP_3DS_GPU_SHADER_SOURCE}"
	VERBATIM
)

add_custom_command(
	OUTPUT "${PPSSPP_3DS_GPU_SHADER_C}" "${PPSSPP_3DS_GPU_SHADER_H}"
	COMMAND "${PYTHON3_EXECUTABLE}"
		"${CMAKE_SOURCE_DIR}/ci/3ds/bin2c.py"
		"${PPSSPP_3DS_GPU_SHADER_BIN}"
		"${PPSSPP_3DS_GPU_SHADER_C}"
		"${PPSSPP_3DS_GPU_SHADER_H}"
		gpu3ds_textured_shbin
	DEPENDS "${PPSSPP_3DS_GPU_SHADER_BIN}" "${CMAKE_SOURCE_DIR}/ci/3ds/bin2c.py"
	VERBATIM
)

add_executable(PPSSPP
	main.cpp
	GPU3DS.c
	GPU3DS.h
	GraphicsContext3DS.cpp
	GraphicsContext3DS.h
	ImDebuggerStubs.cpp
	Platform3DS.c
	Platform3DS.h
	RuntimeStubs.cpp
	"${PPSSPP_3DS_GPU_SHADER_C}"
)

target_compile_features(PPSSPP PRIVATE cxx_std_17)
target_include_directories(PPSSPP SYSTEM PRIVATE
	"${CMAKE_CURRENT_BINARY_DIR}"
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
	IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS
)
target_link_libraries(PPSSPP PRIVATE
	"-Wl,--start-group"
	${LinkCommon}
	native
	Common
	"-Wl,--end-group"
	pthread
	citro2d
	citro3d
	ctru
	m
)

set_target_properties(PPSSPP PROPERTIES
	OUTPUT_NAME "PPSSPP"
	SUFFIX ".elf"
)

set(PPSSPP_3DS_SMDH "${CMAKE_CURRENT_BINARY_DIR}/PPSSPP.smdh")
set(PPSSPP_3DS_BANNER_PNG "${CMAKE_CURRENT_BINARY_DIR}/PPSSPP.banner.png")
set(PPSSPP_3DS_BANNER_WAV "${CMAKE_CURRENT_BINARY_DIR}/PPSSPP.silence.wav")
set(PPSSPP_3DS_BANNER "${CMAKE_CURRENT_BINARY_DIR}/PPSSPP.bnr")
set(PPSSPP_3DS_3DSX "${CMAKE_CURRENT_BINARY_DIR}/PPSSPP.3dsx")
set(PPSSPP_3DS_CIA "${CMAKE_CURRENT_BINARY_DIR}/PPSSPP.cia")

add_custom_command(
	OUTPUT "${PPSSPP_3DS_SMDH}"
	COMMAND "${CTR_BANNERTOOL}" makesmdh
		-s "${PPSSPP_3DS_TITLE}"
		-l "${PPSSPP_3DS_DESCRIPTION}"
		-p "${PPSSPP_3DS_AUTHOR}"
		-i "${PPSSPP_3DS_ICON}"
		-r regionfree
		-f visible,recordusage,new3ds
		-o "${PPSSPP_3DS_SMDH}"
	DEPENDS "${PPSSPP_3DS_ICON}"
	VERBATIM
)
add_custom_target(PPSSPP_SMDH DEPENDS "${PPSSPP_3DS_SMDH}")

add_custom_command(
	OUTPUT "${PPSSPP_3DS_BANNER_PNG}" "${PPSSPP_3DS_BANNER_WAV}"
	COMMAND "${PYTHON3_EXECUTABLE}"
		"${CMAKE_SOURCE_DIR}/ci/3ds/make-3ds-banner-assets.py"
		--icon "${PPSSPP_3DS_BANNER_ICON}"
		--banner-png "${PPSSPP_3DS_BANNER_PNG}"
		--silent-wav "${PPSSPP_3DS_BANNER_WAV}"
	DEPENDS "${PPSSPP_3DS_BANNER_ICON}" "${CMAKE_SOURCE_DIR}/ci/3ds/make-3ds-banner-assets.py"
	VERBATIM
)

add_custom_command(
	OUTPUT "${PPSSPP_3DS_BANNER}"
	COMMAND "${CTR_BANNERTOOL}" makebanner
		-i "${PPSSPP_3DS_BANNER_PNG}"
		-a "${PPSSPP_3DS_BANNER_WAV}"
		-o "${PPSSPP_3DS_BANNER}"
	DEPENDS "${PPSSPP_3DS_BANNER_PNG}" "${PPSSPP_3DS_BANNER_WAV}"
	VERBATIM
)
add_custom_target(PPSSPP_BANNER DEPENDS "${PPSSPP_3DS_BANNER}")

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
	OUTPUT "${PPSSPP_3DS_CIA}"
	COMMAND "${CTR_MAKEROM}"
		-f cia
		-o "${PPSSPP_3DS_CIA}"
		-rsf "${CMAKE_CURRENT_SOURCE_DIR}/PPSSPP.rsf"
		-target p
		-ignoresign
		-ver "${PPSSPP_3DS_TITLE_VERSION}"
		-elf "$<TARGET_FILE:PPSSPP>"
		-icon "${PPSSPP_3DS_SMDH}"
		-banner "${PPSSPP_3DS_BANNER}"
		-exefslogo
		-DAPP_TITLE="${PPSSPP_3DS_TITLE}"
		-DAPP_PRODUCT_CODE="${PPSSPP_3DS_PRODUCT_CODE}"
		-DAPP_UNIQUE_ID="${PPSSPP_3DS_UNIQUE_ID}"
		-DAPP_ROMFS=${CMAKE_BINARY_DIR}/assets
	DEPENDS PPSSPP PPSSPP_SMDH PPSSPP_BANNER "${CMAKE_CURRENT_SOURCE_DIR}/PPSSPP.rsf" "${CMAKE_BINARY_DIR}/assets"
	VERBATIM
)

add_custom_target(PPSSPP_3DSX ALL DEPENDS "${PPSSPP_3DS_3DSX}")
add_custom_target(PPSSPP_CIA ALL DEPENDS "${PPSSPP_3DS_CIA}")
