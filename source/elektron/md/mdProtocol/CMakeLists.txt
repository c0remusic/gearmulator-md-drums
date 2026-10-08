cmake_minimum_required(VERSION 3.15)

# The Machinedrum's and Monomachine's MIDI messages without an emulator: the CC mapping, the machine table and the
# SysEx codecs (Kits, patterns, Globals, ASSIGN MACHINE, master effects, LFOs, Links and Chokes). mdLib and MD Drums
# link it; MD Drums cannot link mdLib (Musashi serves one CPU class per executable).
project(mdProtocol)

add_library(mdProtocol STATIC
	mdautomation.cpp mdautomation.h
	mdkit.cpp mdkit.h
	mdlfosettings.h
	mdmachines.cpp mdmachines.h
	mdsysexautomation.cpp mdsysexautomation.h
	mdsysexcodec.h
	mdtypes.h
)
target_include_directories(mdProtocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_property(TARGET mdProtocol PROPERTY FOLDER "Elektron")

if(BUILD_TESTING)
	# The factory Kits come from the flash image, read by mdEngine's firmware tools (no emulator runs)
	add_executable(mdProtocolKitTest mdProtocolKitTest.cpp)
	target_link_libraries(mdProtocolKitTest PRIVATE mdProtocol mdEngine)
	add_test(NAME mdProtocolKitTest COMMAND mdProtocolKitTest)
	set_tests_properties(mdProtocolKitTest PROPERTIES LABELS "UnitTest;FirmwareTest" SKIP_RETURN_CODE 77 TIMEOUT 60)
	set_property(TARGET mdProtocolKitTest PROPERTY FOLDER "Elektron/test")
endif()
