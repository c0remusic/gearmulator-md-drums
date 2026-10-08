cmake_minimum_required(VERSION 3.15)

# MD Drums' engine side, without JUCE: finds the flash image, drives mdEngine as the plug-in does, and its
# synthLib::Device, which speaks the Machinedrum's MIDI.
project(mdDrums)

add_library(mdDrums STATIC
	mdDrumsDevice.cpp mdDrumsDevice.h
	mdDrumsEngine.cpp mdDrumsEngine.h
	mdDrumsMessages.cpp mdDrumsMessages.h
	mdDrumsTelemetry.h
)
target_include_directories(mdDrums PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mdDrums PUBLIC mdEngine mdProtocol synthLib)
set_property(TARGET mdDrums PROPERTY FOLDER "Elektron")

if(BUILD_TESTING)
	add_executable(mdDrumsEngineTest mdDrumsEngineTest.cpp)
	target_link_libraries(mdDrumsEngineTest PRIVATE mdDrums)
	add_test(NAME mdDrumsEngineTest COMMAND mdDrumsEngineTest)
	set_tests_properties(mdDrumsEngineTest PROPERTIES LABELS "FirmwareTest;AudioIo" SKIP_RETURN_CODE 77 TIMEOUT 120)
	set_property(TARGET mdDrumsEngineTest PROPERTY FOLDER "Elektron/test")

	# The Device driven by MIDI bytes alone, through synthLib::Plugin as the plug-in drives it
	add_executable(mdDrumsDeviceTest mdDrumsDeviceTest.cpp)
	target_link_libraries(mdDrumsDeviceTest PRIVATE mdDrums)
	add_test(NAME mdDrumsDeviceTest COMMAND mdDrumsDeviceTest)
	set_tests_properties(mdDrumsDeviceTest PROPERTIES LABELS "FirmwareTest;AudioIo;Midi" SKIP_RETURN_CODE 77 TIMEOUT 120)
	set_property(TARGET mdDrumsDeviceTest PROPERTY FOLDER "Elektron/test")
endif()
