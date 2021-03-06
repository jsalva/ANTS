## This file should be placed in the root directory of your project.
## Then modify the CMakeLists.txt file in the root directory of your
## project to incorporate the testing dashboard.
## # The following are required to uses Dart and the Cdash dashboard
##   enable_testing()
##   include(CTest)
set(CTEST_PROJECT_NAME "ANTS")
set(CTEST_NIGHTLY_START_TIME "00:00:00 EST")

set(CTEST_DROP_METHOD "http")
set(CTEST_DROP_SITE "picsl.upenn.edu")
set(CTEST_DROP_LOCATION "/cdash/submit.php?project=ANTS")
set(CTEST_DROP_SITE_CDASH TRUE)
