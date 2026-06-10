qt_add_library(passcore STATIC
    src/Version.cpp
    src/db/Database.cpp
    src/db/Migrations.cpp
    src/domain/StudySession.cpp
    src/repo/SubjectRepository.cpp
    src/repo/StrategyRepository.cpp
    src/repo/SessionRepository.cpp
    src/session/StrategyCatalog.cpp
    src/session/SessionTimerService.cpp
    src/stats/StatsService.cpp
    include/pass/session/SessionTimerService.h
)

target_include_directories(passcore PUBLIC include)

# Regla dura: passcore solo puede depender de Qt Core y Qt Sql (nunca Widgets),
# para poder reutilizarlo detrás de un servidor REST en la futura versión web.
target_link_libraries(passcore PUBLIC Qt6::Core Qt6::Sql)
