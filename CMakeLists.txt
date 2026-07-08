cmake_minimum_required(VERSION 3.25)

project(pass VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Sql Widgets Test Charts Network NetworkAuth)

# --- Endurecimiento (hardening) del binario -------------------------------
# Defensa en profundidad sobre el código que parsea entrada semi-confiable
# (JSON de sync entre dispositivos, JSON de Google, .md del vault). Solo
# GCC/Clang; sin -Werror para no romper el build por warnings preexistentes.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall -Wextra -Wformat -Wformat-security -fstack-protector-strong
        # _FORTIFY_SOURCE necesita optimización: solo en configuraciones no-Debug.
        $<$<NOT:$<CONFIG:Debug>>:-U_FORTIFY_SOURCE>
        $<$<NOT:$<CONFIG:Debug>>:-D_FORTIFY_SOURCE=2>
    )
    # Mitigaciones del enlazador. En MinGW activan ASLR/DEP y alta entropía de
    # direcciones en pass.exe; en otros GNU/ld se ignoran las no soportadas.
    if(WIN32)
        add_link_options(
            -Wl,--dynamicbase    # ASLR
            -Wl,--nxcompat       # DEP
            -Wl,--high-entropy-va # ASLR de 64 bits
        )
    endif()
endif()

enable_testing()

add_subdirectory(core)
add_subdirectory(app)
add_subdirectory(tests)
