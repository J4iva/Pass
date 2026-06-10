qt_add_executable(pass WIN32
    main.cpp
    MainWindow.h
    MainWindow.cpp
    views/StudyView.h
    views/StudyView.cpp
    widgets/SessionSetupDialog.h
    widgets/SessionSetupDialog.cpp
    widgets/TimerWidget.h
    widgets/TimerWidget.cpp
)

target_link_libraries(pass PRIVATE passcore Qt6::Widgets)
