cmake_minimum_required(VERSION 3.15)

# MD Drums: the Machinedrum's engines without its OS (mdDrums, mdEngine) as an instrument plug-in on the TUS stack
# (ADR 0001). Its Device speaks the Machinedrum's MIDI (ADR 0002); its Controller comes from
# parameterDescriptions_mddrums.json and its skin is the v22 editor (skins/mdDrums), both written by hand. Its own name
# and codes: it never replaces md-mm's "Gearmulator MD".
project(mdDrumsPlugin VERSION ${CMAKE_PROJECT_VERSION})

set(SOURCES
	mdDrumsController.cpp mdDrumsController.h
	mdDrumsEditor.cpp mdDrumsEditor.h
	mdDrumsGlyph.cpp mdDrumsGlyph.h
	mdDrumsHitView.cpp mdDrumsHitView.h
	mdDrumsKnob.cpp mdDrumsKnob.h
	mdDrumsMeterView.cpp mdDrumsMeterView.h
	mdDrumsProcessor.cpp mdDrumsProcessor.h
	mdDrumsTrackView.cpp mdDrumsTrackView.h
	parameterDescriptions_mddrums.json
	skins/mdDrums/mdDrums.rml
	skins/mdDrums/mdDrums.rcss
)

set(ASSETS parameterDescriptions_mddrums.json)
addSkin("MD Drums" "MD Drums" "skins/mdDrums" "mdDrums.rml")
buildSkinHeader()

juce_add_binary_data(mdDrumsPlugin_BinaryData SOURCES ${ASSETS} ${ASSETS_SKINS})

juce_add_plugin(mdDrumsPlugin
	COMPANY_NAME "c0remusic"
	IS_SYNTH TRUE
	NEEDS_MIDI_INPUT TRUE
	NEEDS_MIDI_OUTPUT FALSE
	IS_MIDI_EFFECT FALSE
	EDITOR_WANTS_KEYBOARD_FOCUS TRUE
	COPY_PLUGIN_AFTER_BUILD FALSE
	PLUGIN_MANUFACTURER_CODE C0rm
	PLUGIN_CODE Mddr
	PRODUCTS_FOLDER "${GEARMULATOR_JUCE_PRODUCTS_ROOT}/$<CONFIG>"
	FORMATS ${juce_formats}
	PRODUCT_NAME "MD Drums"
	VST3_AUTO_MANIFEST TRUE
	BUNDLE_ID "local.c0remusic.mddrums"
	LV2URI "urn:c0remusic:mddrums"
)

set_property(TARGET mdDrumsPlugin PROPERTY ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shared-code/$<CONFIG>")

target_sources(mdDrumsPlugin PRIVATE ${SOURCES})
source_group("source" FILES ${SOURCES})
removeJuceDependencies(mdDrumsPlugin)
# What createJucePlugin (source/juce.cmake) defines for the stack, with MD Drums' identity
target_compile_definitions(mdDrumsPlugin PUBLIC
	PluginName="MD Drums"
	PluginVersionMajor=${CMAKE_PROJECT_VERSION_MAJOR}
	PluginVersionMinor=${CMAKE_PROJECT_VERSION_MINOR}
	PluginVersionPatch=${CMAKE_PROJECT_VERSION_PATCH}
	Plugin4CC="Mddr"
	JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
	JucePlugin_Lv2Uri="urn:c0remusic:mddrums")
target_link_libraries(mdDrumsPlugin PRIVATE mdDrumsPlugin_BinaryData mdDrums jucePluginEditorLib juce_plugin_modules)
set_property(TARGET mdDrumsPlugin PROPERTY FOLDER "Elektron")
set_property(TARGET mdDrumsPlugin_BinaryData PROPERTY FOLDER "Elektron")

if(BUILD_TESTING)
	add_executable(mdDrumsProcessorTest mdDrumsProcessorTest.cpp)
	target_link_libraries(mdDrumsProcessorTest PRIVATE mdDrumsPlugin mdDrumsPlugin_BinaryData mdDrums
		jucePluginEditorLib juceRmlUi juceUiLib juce_plugin_modules)
	target_compile_definitions(mdDrumsProcessorTest PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
	add_test(NAME mdDrumsProcessorTest COMMAND mdDrumsProcessorTest)
	set_tests_properties(mdDrumsProcessorTest PROPERTIES LABELS "FirmwareTest;AudioIo" SKIP_RETURN_CODE 77 TIMEOUT 180)
	set_property(TARGET mdDrumsProcessorTest PROPERTY FOLDER "Elektron/test")

	add_executable(mdDrumsSkinTest mdDrumsSkinTest.cpp)
	target_link_libraries(mdDrumsSkinTest PRIVATE mdDrumsPlugin mdDrumsPlugin_BinaryData mdDrums
		jucePluginEditorLib juceRmlUi juceUiLib juce_plugin_modules)
	target_compile_definitions(mdDrumsSkinTest PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
		MD_DRUMS_DESIGN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Design")
	add_test(NAME mdDrumsSkinTest COMMAND mdDrumsSkinTest)
	set_tests_properties(mdDrumsSkinTest PROPERTIES LABELS "FirmwareTest;Performance" SKIP_RETURN_CODE 77 TIMEOUT 180)
	set_property(TARGET mdDrumsSkinTest PROPERTY FOLDER "Elektron/test")
endif()
