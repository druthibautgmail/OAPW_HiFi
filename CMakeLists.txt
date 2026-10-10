cmake_minimum_required(VERSION 3.10)
project(OAPW_HiFi CXX) # Projektname auf HiFi angepasst

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Hochoptimierte Flags für den Raspberry Pi 5 (Cortex-A76).
# -ffast-math und -ftree-vectorize zwingen GCC dazu, unsere neuen float-Berechnungen
# (Biquad-Filter, FFT) direkt in parallele ARM NEON SIMD-Befehle zu übersetzen.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -mcpu=cortex-a76 -mtune=cortex-a76 -ffast-math -ftree-vectorize")

# Sauberer ALSA-Suchmechanismus für CMake unter Debian Trixie
find_package(ALSA REQUIRED)

# Ordnerstrukturen einbinden
include_directories(include ${ALSA_INCLUDE_DIRS})
file(GLOB SOURCES "src/*.cpp")

# Executable-Name auf die HiFi-Variante angepasst
add_executable(OAPW_Player_HiFi ${SOURCES})

# 'm' (Mathematik für DSP) und 'pthread' (für Webserver & Realtime-Audio) bleiben bestehen.
# ALSA wird nun absolut sauber über die CMake-Variable verlinkt.
target_link_libraries(OAPW_Player_HiFi ${ALSA_LIBRARIES} m pthread)
