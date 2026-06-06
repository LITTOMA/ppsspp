set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR armv6k)

if(NOT DEFINED ENV{DEVKITPRO})
  message(FATAL_ERROR "DEVKITPRO is not set. Use devkitpro/devkitarm or source devkitPro's environment first.")
endif()

if(NOT DEFINED ENV{DEVKITARM})
  set(ENV{DEVKITARM} "$ENV{DEVKITPRO}/devkitARM")
endif()

set(DEVKITPRO "$ENV{DEVKITPRO}")
set(DEVKITARM "$ENV{DEVKITARM}")

if(WIN32)
  set(EXE_SUFFIX ".exe")
else()
  set(EXE_SUFFIX "")
endif()

set(CMAKE_C_COMPILER "${DEVKITARM}/bin/arm-none-eabi-gcc${EXE_SUFFIX}" CACHE FILEPATH "")
set(CMAKE_CXX_COMPILER "${DEVKITARM}/bin/arm-none-eabi-g++${EXE_SUFFIX}" CACHE FILEPATH "")
set(CMAKE_ASM_COMPILER "${DEVKITARM}/bin/arm-none-eabi-gcc${EXE_SUFFIX}" CACHE FILEPATH "")
set(CMAKE_AR "${DEVKITARM}/bin/arm-none-eabi-ar${EXE_SUFFIX}" CACHE FILEPATH "")
set(CMAKE_RANLIB "${DEVKITARM}/bin/arm-none-eabi-ranlib${EXE_SUFFIX}" CACHE FILEPATH "")
set(CMAKE_OBJCOPY "${DEVKITARM}/bin/arm-none-eabi-objcopy${EXE_SUFFIX}" CACHE FILEPATH "")
set(CMAKE_STRIP "${DEVKITARM}/bin/arm-none-eabi-strip${EXE_SUFFIX}" CACHE FILEPATH "")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_EXECUTABLE_SUFFIX ".elf")

set(CTRULIB "${DEVKITPRO}/libctru")
set(PORTLIBS_3DS "${DEVKITPRO}/portlibs/3ds")

set(CMAKE_FIND_ROOT_PATH
  "${PORTLIBS_3DS}"
  "${CTRULIB}"
  "${DEVKITARM}"
)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CTR_ARM_FLAGS "-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft")
set(CTR_SECTION_FLAGS "-ffunction-sections -fdata-sections")
set(CTR_COMMON_DEFINES "-DARM11 -D_3DS -D__3DS__")

set(CMAKE_C_FLAGS_INIT "${CTR_COMMON_DEFINES} ${CTR_ARM_FLAGS} ${CTR_SECTION_FLAGS} -mword-relocations")
set(CMAKE_CXX_FLAGS_INIT "${CTR_COMMON_DEFINES} ${CTR_ARM_FLAGS} ${CTR_SECTION_FLAGS} -mword-relocations")
set(CMAKE_ASM_FLAGS_INIT "${CTR_ARM_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-specs=3dsx.specs ${CTR_ARM_FLAGS} -Wl,--gc-sections")

include_directories(SYSTEM
  "${CTRULIB}/include"
  "${PORTLIBS_3DS}/include"
)

link_directories(
  "${CTRULIB}/lib"
  "${PORTLIBS_3DS}/lib"
)
