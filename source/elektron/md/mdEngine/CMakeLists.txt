cmake_minimum_required(VERSION 3.15)

# The Machinedrum's engines without its ColdFire OS: Machinemodule's engine (sd88me/mpc-vst-machinedrum, AGPL-3.0),
# imported at 796ded6 with the changes listed in README.md, built on this repository's dsp56300 and mc68k.
project(mdEngine)

add_library(mdEngine STATIC
	engine/Dsp56.h
	engine/HostModel.cpp engine/HostModel.h
	engine/MachineRunner.cpp engine/MachineRunner.h
	engine/MasterEngine.cpp engine/MasterEngine.h
	engine/MdEngine.h
	engine/Mixer.cpp engine/Mixer.h
	engine/ParallelVoiceEngine.cpp engine/ParallelVoiceEngine.h
	engine/TrackFx.cpp engine/TrackFx.h
	engine/VoiceEngine.cpp engine/VoiceEngine.h
	tools/mdfw/Firmware.cpp tools/mdfw/Firmware.h
)
target_include_directories(mdEngine PUBLIC engine tools/mdfw ${CMAKE_CURRENT_SOURCE_DIR}/../../.. ${CMAKE_CURRENT_SOURCE_DIR}/../../../mc68k)
target_link_libraries(mdEngine PUBLIC dsp56kEmu 68kEmu)
set_property(TARGET mdEngine PROPERTY FOLDER "Elektron")
