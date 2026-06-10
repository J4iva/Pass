qt_add_executable(pass WIN32
    main.cpp
    MainWindow.h
    MainWindow.cpp
    views/StudyView.h
    views/StudyView.cpp
    views/StatsView.h
    views/StatsView.cpp
    models/SessionTableModel.h
    models/SessionTableModel.cpp
    widgets/SessionSetupDialog.h
    widgets/SessionSetupDialog.cpp
    widgets/TimerWidget.h
    widgets/TimerWidget.cpp
)

target_link_libraries(pass PRIVATE passcore Qt6::Widgets Qt6::Charts)
