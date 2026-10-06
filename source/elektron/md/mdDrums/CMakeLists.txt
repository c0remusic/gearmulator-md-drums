cmake_minimum_required(VERSION 3.15)

# MD Drums' engine side, without JUCE: finds the flash image and drives mdEngine as the plug-in does.
project(mdDrums)

add_library(mdDrums STATIC mdDrumsEngine.cpp mdDrumsEngine.h)
target_include_directories(mdDrums PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mdDrums PUBLIC mdEngine)
set_property(TARGET mdDrums PROPERTY FOLDER "Elektron")

if(BUILD_TESTING)
	add_executable(mdDrumsEngineTest mdDrumsEngineTest.cpp)
	target_link_libraries(mdDrumsEngineTest PRIVATE mdDrums)
	add_test(NAME mdDrumsEngineTest COMMAND mdDrumsEngineTest)
	set_tests_properties(mdDrumsEngineTest PROPERTIES LABELS "FirmwareTest;AudioIo" SKIP_RETURN_CODE 77 TIMEOUT 120)
	set_property(TARGET mdDrumsEngineTest PROPERTY FOLDER "Elektron/test")
endif()
