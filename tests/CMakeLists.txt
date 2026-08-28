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

# ── optimallap: shared optimal-lap rule, free functions, no Qt object ─
add_executable(tst_optimallap cpp/tst_optimallap.cpp ${APP_SRC_DIR}/src/optimallap.cpp)
target_include_directories(tst_optimallap PRIVATE ${APP_SRC_DIR})
target_link_libraries(tst_optimallap PRIVATE Qt6::Test)
add_test(NAME tst_optimallap COMMAND tst_optimallap)
set_tests_properties(tst_optimallap PROPERTIES ENVIRONMENT "${DASH_TEST_ENV}")

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

# ── sessionmodel: delete/persist/regroup on session records ───────────
# Seeds sessions.json directly (bypassing the lap-completion signal chain
# saveCurrentSession() depends on); uses QStandardPaths::setTestModeEnabled()
# to sandbox AppPaths-backed persistence away from the real user's sessions.
add_executable(tst_sessionmodel cpp/tst_sessionmodel.cpp
    ${APP_SRC_DIR}/src/sessionmodel.cpp
    ${APP_SRC_DIR}/src/candatamodel.cpp
    ${APP_SRC_DIR}/src/raceboxmodel.cpp
    ${APP_SRC_DIR}/src/trackmodel.cpp
    ${APP_SRC_DIR}/src/optimallap.cpp
    ${APP_SRC_DIR}/src/logging.cpp
)
target_include_directories(tst_sessionmodel PRIVATE ${APP_SRC_DIR})
target_link_libraries(tst_sessionmodel PRIVATE Qt6::Test Qt6::SerialBus Qt6::Network)
add_test(NAME tst_sessionmodel COMMAND tst_sessionmodel)
set_tests_properties(tst_sessionmodel PROPERTIES ENVIRONMENT "${DASH_TEST_ENV}")

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

# ── logbuffermodel: Device Log buffering, snapshot, and flood safety ──
# Uses QStandardPaths::setTestModeEnabled() to sandbox dash.log away from the
# real user data dir. handlerNeverBlocksUnderConcurrentFlood() is the regression
# guard for the UI freeze: it fails if file I/O is put back inside the logging
# critical section.
add_executable(tst_logbuffermodel cpp/tst_logbuffermodel.cpp
    ${APP_SRC_DIR}/src/logbuffermodel.cpp
)
target_include_directories(tst_logbuffermodel PRIVATE ${APP_SRC_DIR})
target_link_libraries(tst_logbuffermodel PRIVATE Qt6::Test)
add_test(NAME tst_logbuffermodel COMMAND tst_logbuffermodel)
set_tests_properties(tst_logbuffermodel PROPERTIES ENVIRONMENT "${DASH_TEST_ENV}")

# ── dashconfig: RPM limiter rescale, position grid, persistence ───────
# Uses QStandardPaths::setTestModeEnabled() to sandbox AppPaths-backed
# persistence away from the real user's dashboard.conf.
add_executable(tst_dashconfig cpp/tst_dashconfig.cpp
    ${APP_SRC_DIR}/src/dashconfig.cpp
)
target_include_directories(tst_dashconfig PRIVATE ${APP_SRC_DIR})
target_link_libraries(tst_dashconfig PRIVATE Qt6::Test)
add_test(NAME tst_dashconfig COMMAND tst_dashconfig)
set_tests_properties(tst_dashconfig PROPERTIES ENVIRONMENT "${DASH_TEST_ENV}")

# ── uplinkspool: store-and-forward FIFO durability ────────────────────
# The guarantees that make the uplink safe over a lossy LTE link: FIFO order,
# survival across a restart, deletion only on PUBACK, bounded growth. Uses a
# QTemporaryDir rather than QStandardPaths test mode because UplinkSpool takes
# an explicit path seam (setDatabasePath) for exactly this.
add_executable(tst_uplinkspool cpp/tst_uplinkspool.cpp
    ${APP_SRC_DIR}/src/uplinkspool.cpp
    ${APP_SRC_DIR}/src/logging.cpp
)
target_include_directories(tst_uplinkspool PRIVATE ${APP_SRC_DIR})
target_link_libraries(tst_uplinkspool PRIVATE Qt6::Test Qt6::Sql)
add_test(NAME tst_uplinkspool COMMAND tst_uplinkspool)
set_tests_properties(tst_uplinkspool PROPERTIES ENVIRONMENT "${DASH_TEST_ENV}")

# ── uplinkmodel: framing, sid/seq, mono, online-vs-spool decision ─────
# Runs against MockCloudClient, so no broker and no background thread. The lap
# timer is driven into Running through RaceBoxModel's real gate-crossing path
# rather than a back door, so the session lifecycle under test is the one the
# driver actually sees.
add_executable(tst_uplinkmodel cpp/tst_uplinkmodel.cpp
    # Both are header-only Q_OBJECTs, so they must be listed for AUTOMOC to
    # generate their meta-objects — otherwise the signals link as undefined.
    ${APP_SRC_DIR}/src/icloudclient.h
    ${APP_SRC_DIR}/src/mockcloudclient.h
    ${APP_SRC_DIR}/src/uplinkmodel.cpp
    ${APP_SRC_DIR}/src/uplinkspool.cpp
    ${APP_SRC_DIR}/src/cloudconfig.cpp
    ${APP_SRC_DIR}/src/candatamodel.cpp
    ${APP_SRC_DIR}/src/raceboxmodel.cpp
    ${APP_SRC_DIR}/src/trackmodel.cpp
    ${APP_SRC_DIR}/src/logging.cpp
)
target_include_directories(tst_uplinkmodel PRIVATE ${APP_SRC_DIR})
target_link_libraries(tst_uplinkmodel PRIVATE Qt6::Test Qt6::Sql Qt6::SerialBus Qt6::Network)
add_test(NAME tst_uplinkmodel COMMAND tst_uplinkmodel)
set_tests_properties(tst_uplinkmodel PROPERTIES ENVIRONMENT "${DASH_TEST_ENV}")

# ── cloudconfig: credential storage, 0600 permissions, no leakage ─────
# Uses QStandardPaths::setTestModeEnabled() to sandbox cloud.conf away from the
# real user's paired credential.
add_executable(tst_cloudconfig cpp/tst_cloudconfig.cpp
    ${APP_SRC_DIR}/src/cloudconfig.cpp
    ${APP_SRC_DIR}/src/logging.cpp
)
target_include_directories(tst_cloudconfig PRIVATE ${APP_SRC_DIR})
target_link_libraries(tst_cloudconfig PRIVATE Qt6::Test)
add_test(NAME tst_cloudconfig COMMAND tst_cloudconfig)
set_tests_properties(tst_cloudconfig PROPERTIES ENVIRONMENT "${DASH_TEST_ENV}")

# ── timemodel: region/zone splitting, offset formatting, geolocation ──
# validation, and the set-ntp-before-set-time ordering guarantee. The ordering
# test constructs TimeModel with mockMode=false against fake timedatectl/nmcli
# binaries on PATH (see tst_timemodel.cpp) rather than the real system tools —
# mock mode's setManualDateTime never shells out at all, so it can't
# demonstrate that ordering. Links RaceBoxModel because TimeModel calls
# through a RaceBoxModel* for the GPS time source (unused, passed as nullptr,
# in every test here).
add_executable(tst_timemodel cpp/tst_timemodel.cpp
    ${APP_SRC_DIR}/src/timemodel.cpp
    ${APP_SRC_DIR}/src/raceboxmodel.cpp
    ${APP_SRC_DIR}/src/logging.cpp
)
target_include_directories(tst_timemodel PRIVATE ${APP_SRC_DIR})
target_link_libraries(tst_timemodel PRIVATE Qt6::Test Qt6::Network)
add_test(NAME tst_timemodel COMMAND tst_timemodel)
set_tests_properties(tst_timemodel PROPERTIES ENVIRONMENT "${DASH_TEST_ENV}")

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
