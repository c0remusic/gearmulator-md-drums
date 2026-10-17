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
	mdDrumsKitFiles.cpp mdDrumsKitFiles.h
	mdDrumsLfo.cpp mdDrumsLfo.h
	mdDrumsMessages.cpp mdDrumsMessages.h
	mdDrumsTelemetry.h
)
target_include_directories(mdDrums PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mdDrums PUBLIC mdEngine mdProtocol synthLib)
set_property(TARGET mdDrums PROPERTY FOLDER "Elektron")

if(BUILD_TESTING)
	# An executable of MD Drums' engine side, built from TARGET_NAME.cpp, with what mdDrumsTestSupport.h uses: psapi
	# for the process's memory on Windows
	function(add_md_drums_executable TARGET_NAME)
		add_executable(${TARGET_NAME} ${TARGET_NAME}.cpp)
		target_link_libraries(${TARGET_NAME} PRIVATE mdDrums)
		if(WIN32)
			target_link_libraries(${TARGET_NAME} PRIVATE psapi)
		endif()
		set_property(TARGET ${TARGET_NAME} PROPERTY FOLDER "Elektron/test")
	endfunction()

	# The same as a test ctest runs: TEST_LABELS a list ("FirmwareTest;AudioIo"), TEST_TIMEOUT in seconds, exit 77 a
	# skip (no flash image)
	function(add_md_drums_test TARGET_NAME TEST_LABELS TEST_TIMEOUT)
		add_md_drums_executable(${TARGET_NAME})
		add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})
		set_tests_properties(${TARGET_NAME} PROPERTIES LABELS "${TEST_LABELS}" SKIP_RETURN_CODE 77
			TIMEOUT ${TEST_TIMEOUT})
	endfunction()

	add_md_drums_test(mdDrumsEngineTest "FirmwareTest;AudioIo" 120)

	# The Device driven by MIDI bytes alone, through synthLib::Plugin as the plug-in drives it. With --measure, by hand,
	# it also times the whole Device with and without its Telemetry
	add_md_drums_test(mdDrumsDeviceTest "FirmwareTest;AudioIo;Midi" 120)

	# What the editor's Filter and EQ and LFO screens compute: the LFO's port against the OS's routines, the response
	add_md_drums_test(mdDrumsScreensTest "FirmwareTest" 120)

	# The Kits' Bank: its file, its Slots, two instances on one file
	add_md_drums_test(mdDrumsBankTest "FirmwareTest" 60)

	# The Hit screen's preview (ticket 28): the Hit the Device would play first, the same each time, on a shared thread
	add_md_drums_test(mdDrumsHitPreviewTest "FirmwareTest" 120)

	# Measurement, not a gate: the voices split across threads (ParallelVoiceEngine) against the single voice DSP. Run by
	# hand (ticket 25 of the editor map)
	add_md_drums_executable(mdDrumsParallelTest)

	# A Track's first Hit against the next ones, the OS's code and the voice DSP apart, on a cold engine and on one whose
	# machines were compiled ahead (ticket 31): the times are printed, the two engines' samples must be the same
	add_md_drums_test(mdDrumsFirstHitTest "FirmwareTest" 120)

	# Measurement, not a gate: what a second engine costs the Hit screen and the browser's preview (ticket 12)
	add_md_drums_executable(mdDrumsPreviewTest)
endif()
