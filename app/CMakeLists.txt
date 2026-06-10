qt_add_executable(pass WIN32
    main.cpp
    MainWindow.h
    MainWindow.cpp
    views/StudyView.h
    views/StudyView.cpp
    views/StatsView.h
    views/StatsView.cpp
    views/CalendarView.h
    views/CalendarView.cpp
    views/NotesView.h
    views/NotesView.cpp
    views/DashboardView.h
    views/DashboardView.cpp
    widgets/EventDialog.h
    widgets/EventDialog.cpp
    models/SessionTableModel.h
    models/SessionTableModel.cpp
    widgets/SessionSetupDialog.h
    widgets/SessionSetupDialog.cpp
    widgets/NewNoteDialog.h
    widgets/NewNoteDialog.cpp
    widgets/TimerWidget.h
    widgets/TimerWidget.cpp
    util/SubjectUtil.h
    util/SubjectUtil.cpp
)

target_link_libraries(pass PRIVATE passcore Qt6::Widgets Qt6::Charts)
