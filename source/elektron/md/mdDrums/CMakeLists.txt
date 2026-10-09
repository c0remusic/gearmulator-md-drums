cmake_minimum_required(VERSION 3.15)

# MD Drums' engine side, without JUCE: finds the flash image, drives mdEngine as the plug-in does, and its
# synthLib::Device, which speaks the Machinedrum's MIDI.
project(mdDrums)

add_library(mdDrums STATIC
	mdDrumsBank.cpp mdDrumsBank.h
	mdDrumsDevice.cpp mdDrumsDevice.h
	mdDrumsEngine.cpp mdDrumsEngine.h
	mdDrumsFilterResponse.cpp mdDrumsFilterResponse.h
	mdDrumsHitPreview.cpp mdDrumsHitPreview.h
	mdDrumsLfo.cpp mdDrumsLfo.h
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

	# What the editor's Filter and EQ and LFO screens compute: the LFO's port against the OS's routines, the response
	add_executable(mdDrumsScreensTest mdDrumsScreensTest.cpp)
	target_link_libraries(mdDrumsScreensTest PRIVATE mdDrums)
	add_test(NAME mdDrumsScreensTest COMMAND mdDrumsScreensTest)
	set_tests_properties(mdDrumsScreensTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 120)
	set_property(TARGET mdDrumsScreensTest PROPERTY FOLDER "Elektron/test")

	# The Kits' Bank: its file, its Slots, two instances on one file
	add_executable(mdDrumsBankTest mdDrumsBankTest.cpp)
	target_link_libraries(mdDrumsBankTest PRIVATE mdDrums)
	add_test(NAME mdDrumsBankTest COMMAND mdDrumsBankTest)
	set_tests_properties(mdDrumsBankTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 60)
	set_property(TARGET mdDrumsBankTest PROPERTY FOLDER "Elektron/test")

	# The Hit screen's preview (ticket 28): the Hit the Device would play first, the same each time, on a shared thread
	add_executable(mdDrumsHitPreviewTest mdDrumsHitPreviewTest.cpp)
	target_link_libraries(mdDrumsHitPreviewTest PRIVATE mdDrums)
	if(WIN32)
		target_link_libraries(mdDrumsHitPreviewTest PRIVATE psapi)
	endif()
	add_test(NAME mdDrumsHitPreviewTest COMMAND mdDrumsHitPreviewTest)
	set_tests_properties(mdDrumsHitPreviewTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 120)
	set_property(TARGET mdDrumsHitPreviewTest PROPERTY FOLDER "Elektron/test")

	# Measurement, not a gate: the voices split across threads (ParallelVoiceEngine) against the single voice DSP. Run by
	# hand (ticket 25 of the editor map)
	add_executable(mdDrumsParallelTest mdDrumsParallelTest.cpp)
	target_link_libraries(mdDrumsParallelTest PRIVATE mdDrums)
	set_property(TARGET mdDrumsParallelTest PROPERTY FOLDER "Elektron/test")

	# A Track's first Hit against the next ones, the OS's code and the voice DSP apart, on a cold engine and on one whose
	# machines were compiled ahead (ticket 31): the times are printed, the two engines' samples must be the same
	add_executable(mdDrumsFirstHitTest mdDrumsFirstHitTest.cpp)
	target_link_libraries(mdDrumsFirstHitTest PRIVATE mdDrums)
	add_test(NAME mdDrumsFirstHitTest COMMAND mdDrumsFirstHitTest)
	set_tests_properties(mdDrumsFirstHitTest PROPERTIES LABELS "FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 120)
	set_property(TARGET mdDrumsFirstHitTest PROPERTY FOLDER "Elektron/test")

	# Measurement, not a gate: what a second engine costs the Hit screen and the browser's preview (ticket 12)
	add_executable(mdDrumsPreviewTest mdDrumsPreviewTest.cpp)
	target_link_libraries(mdDrumsPreviewTest PRIVATE mdDrums)
	if(WIN32)
		target_link_libraries(mdDrumsPreviewTest PRIVATE psapi)
	endif()
	set_property(TARGET mdDrumsPreviewTest PROPERTY FOLDER "Elektron/test")
endif()
