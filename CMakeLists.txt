cmake_minimum_required(VERSION 3.10)
project(OAPW CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Hochoptimierte Flags für den Raspberry Pi 5 (Cortex-A76) und 64-Bit DSP.
# -ffast-math und -ftree-vectorize beschleunigen die 192kHz Audio-Berechnung 
# (Biquad-Filter, FFT) durch Nutzung der ARM NEON SIMD-Befehlssätze extrem.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -mcpu=cortex-a76 -mtune=cortex-a76 -ffast-math -ftree-vectorize")

# Fallback: Falls das Projekt doch noch auf einem anderen System (z.B. RPi 4) 
# kompiliert werden soll, kommentiere die obige Zeile aus und nutze stattdessen diese:
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -mcpu=native -mtune=native -ffast-math -ftree-vectorize")

include_directories(include)
file(GLOB SOURCES "src/*.cpp")

add_executable(OAPW_Player ${SOURCES})

# Miniaudio-spezifische Verknüpfungen (dl) wurden entfernt.
# NEU: 'asound' für den direkten ALSA-Hardware-Zugriff.
# 'm' (Mathematik für DSP) und 'pthread' (für den Webserver & ALSA-Thread) bleiben bestehen.
target_link_libraries(OAPW_Player asound m pthread)
