# Only reached when the root CMakeLists.txt's BUILD_TESTING option is ON.
# Core/Quick/SerialBus/Network/Gui/Qml are already found by the root file;
# Test and QuickTest are new here.
find_package(Qt6 REQUIRED COMPONENTS Test QuickTest)

set(APP_SRC_DIR ${CMAKE_SOURCE_DIR})

# All C++ tests run headless (no real display needed) and hit only the model
# layer, never a window.
set(DASH_TEST_ENV "QT_QPA_PLATFORM=offscreen")

# ── canscaling: pure header, no Qt object / event loop needed ─────────
add_executable(tst_canscaling cpp/tst_canscaling.cpp)
target_include_directories(tst_canscaling PRIVATE ${APP_SRC_DIR})
target_link_libraries(tst_canscaling PRIVATE Qt6::Test)
add_test(NAME tst_canscaling COMMAND tst_canscaling)
set_tests_properties(tst_canscaling PROPERTIES ENVIRONMENT "${DASH_TEST_ENV}")

# ── candatamodel: CAN frame decode + RPM state machine ────────────────
add_executable(tst_candatamodel cpp/tst_candatamodel.cpp ${APP_SRC_DIR}/src/candatamodel.cpp)
target_include_directories(tst_candatamodel PRIVATE ${APP_SRC_DIR})
target_link_libraries(tst_candatamodel PRIVATE Qt6::Test Qt6::SerialBus)
add_test(NAME tst_candatamodel COMMAND tst_candatamodel)
set_tests_properties(tst_candatamodel PROPERTIES ENVIRONMENT "${DASH_TEST_ENV}")

# ── raceboxmodel: haversine + lap timing via onData() sequences ───────
add_executable(tst_raceboxmodel cpp/tst_raceboxmodel.cpp
    ${APP_SRC_DIR}/src/raceboxmodel.cpp
    ${APP_SRC_DIR}/src/logging.cpp
)
target_include_directories(tst_raceboxmodel PRIVATE ${APP_SRC_DIR})
target_link_libraries(tst_raceboxmodel PRIVATE Qt6::Test)
add_test(NAME tst_raceboxmodel COMMAND tst_raceboxmodel)
set_tests_properties(tst_raceboxmodel PROPERTIES ENVIRONMENT "${DASH_TEST_ENV}")

# ── updatemodel: mock-mode update state machine ───────────────────────
add_executable(tst_updatemodel cpp/tst_updatemodel.cpp
    ${APP_SRC_DIR}/src/updatemodel.cpp
    ${APP_SRC_DIR}/src/logging.cpp
)
target_include_directories(tst_updatemodel PRIVATE ${APP_SRC_DIR})
target_link_libraries(tst_updatemodel PRIVATE Qt6::Test Qt6::Network)
# Lets the test sanity-check countStageMarkers() against the real install.sh,
# catching a "regex doesn't actually match real syntax" class of bug that
# synthetic-only test input could miss.
target_compile_definitions(tst_updatemodel PRIVATE DASH_INSTALL_SH="${APP_SRC_DIR}/install.sh")
add_test(NAME tst_updatemodel COMMAND tst_updatemodel)
set_tests_properties(tst_updatemodel PROPERTIES ENVIRONMENT "${DASH_TEST_ENV}")

# ── trackmodel: filtering + finish-line fallback ──────────────────────
# Uses QStandardPaths::setTestModeEnabled() (see tst_trackmodel.cpp) to sandbox
# AppPaths-backed persistence away from the real user's data directory.
add_executable(tst_trackmodel cpp/tst_trackmodel.cpp
    ${APP_SRC_DIR}/src/trackmodel.cpp
    ${APP_SRC_DIR}/src/raceboxmodel.cpp
    ${APP_SRC_DIR}/src/logging.cpp
)
target_include_directories(tst_trackmodel PRIVATE ${APP_SRC_DIR})
target_link_libraries(tst_trackmodel PRIVATE Qt6::Test Qt6::Network)
add_test(NAME tst_trackmodel COMMAND tst_trackmodel)
set_tests_properties(tst_trackmodel PROPERTIES ENVIRONMENT "${DASH_TEST_ENV}")

# ── displaymodel: backlight discovery/scaling/persistence ─────────────
# Uses QStandardPaths::setTestModeEnabled() (see tst_displaymodel.cpp) to
# sandbox AppPaths-backed persistence, plus a fake sysfs backlight tree under
# a QTemporaryDir passed to DisplayModel's constructor seam.
add_executable(tst_displaymodel cpp/tst_displaymodel.cpp
    ${APP_SRC_DIR}/src/displaymodel.cpp
    ${APP_SRC_DIR}/src/logging.cpp
)
target_include_directories(tst_displaymodel PRIVATE ${APP_SRC_DIR})
target_link_libraries(tst_displaymodel PRIVATE Qt6::Test)
add_test(NAME tst_displaymodel COMMAND tst_displaymodel)
set_tests_properties(tst_displaymodel PROPERTIES ENVIRONMENT "${DASH_TEST_ENV}")

# ── QML (Qt Quick Test) ────────────────────────────────────────────────
# Test .qml files load the real qml/*.qml sources directly off disk via
# relative directory imports (not through the app's compiled qrc resources),
# so no dependency on ${RESOURCES} here. Qt6::Qml is needed directly for the
# QQmlEngine/QQmlContext mock-context-property setup in tst_qml_runner.cpp
# (DisplaySettings.qml reads its model as a bare context property).
add_executable(tst_qml_runner qml/tst_qml_runner.cpp)
target_link_libraries(tst_qml_runner PRIVATE Qt6::QuickTest Qt6::Qml)
add_test(NAME tst_qml_runner COMMAND tst_qml_runner -input ${CMAKE_CURRENT_SOURCE_DIR}/qml)
set_tests_properties(tst_qml_runner PROPERTIES ENVIRONMENT "${DASH_TEST_ENV}")
