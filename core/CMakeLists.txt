qt_add_library(passcore STATIC
    src/Version.cpp
    src/db/Database.cpp
    src/db/Migrations.cpp
    src/domain/StudySession.cpp
    src/repo/SubjectRepository.cpp
    src/repo/TopicRepository.cpp
    src/repo/StrategyRepository.cpp
    src/repo/SessionRepository.cpp
    src/repo/EventRepository.cpp
    src/admin/SubjectAdminService.cpp
    src/calendar/LocalCalendarProvider.cpp
    src/calendar/CalendarService.cpp
    include/pass/domain/Topic.h
    include/pass/repo/TopicRepository.h
    include/pass/admin/SubjectAdminService.h
    src/repo/SyncStateRepository.cpp
    src/google/GoogleAuthService.cpp
    src/google/GoogleEventMapper.cpp
    src/google/GoogleCalendarClient.cpp
    src/google/GoogleSyncService.cpp
    include/pass/calendar/CalendarProvider.h
    include/pass/calendar/LocalCalendarProvider.h
    include/pass/calendar/CalendarService.h
    include/pass/google/TokenStore.h
    include/pass/google/GoogleAuthService.h
    include/pass/google/GoogleEventMapper.h
    include/pass/google/CalendarClient.h
    include/pass/google/GoogleCalendarClient.h
    include/pass/google/GoogleSyncService.h
    src/session/StrategyCatalog.cpp
    src/session/SessionTimerService.cpp
    src/stats/StatsService.cpp
    src/notes/NoteSerializer.cpp
    src/notes/VaultService.cpp
    src/notes/VaultWatcher.cpp
    src/settings/AppSettings.cpp
    src/command/CommandId.cpp
    src/command/CommandParser.cpp
    src/command/CommandProcessor.cpp
    src/sync/DataChangeNotifier.cpp
    src/sync/Tombstones.cpp
    src/sync/SyncSerializer.cpp
    src/sync/SyncExporter.cpp
    src/sync/SyncImporter.cpp
    src/sync/GitRunner.cpp
    src/sync/GitSyncService.cpp
    src/sync/GitSyncController.cpp
    include/pass/notes/VaultWatcher.h
    include/pass/command/CommandTypes.h
    include/pass/command/CommandId.h
    include/pass/command/CommandParser.h
    include/pass/command/CommandProcessor.h
    include/pass/session/SessionTimerService.h
    include/pass/sync/DataChangeNotifier.h
    include/pass/sync/Tombstones.h
    include/pass/sync/SyncSerializer.h
    include/pass/sync/SyncExporter.h
    include/pass/sync/SyncImporter.h
    include/pass/sync/GitRunner.h
    include/pass/sync/GitSyncService.h
    include/pass/sync/GitSyncController.h
)

# El almacén de tokens sobre el Administrador de credenciales es solo Windows.
if(WIN32)
    target_sources(passcore PRIVATE
        src/google/WinCredTokenStore.cpp
        include/pass/google/WinCredTokenStore.h
    )
endif()

target_include_directories(passcore PUBLIC include)

# Regla dura: passcore solo puede depender de Qt Core, Qt Sql, Qt Network y
# Qt NetworkAuth (nunca Widgets/Gui), para poder reutilizarlo detrás de un
# servidor REST en la futura versión web.
target_link_libraries(passcore PUBLIC Qt6::Core Qt6::Sql Qt6::Network Qt6::NetworkAuth)

# Almacén de tokens en el Administrador de credenciales de Windows (wincred):
# CredWriteW/CredReadW/CredDeleteW viven en advapi32 (DLL de sistema; el deploy
# no cambia). Solo en Windows.
if(WIN32)
    target_link_libraries(passcore PRIVATE advapi32)
endif()
