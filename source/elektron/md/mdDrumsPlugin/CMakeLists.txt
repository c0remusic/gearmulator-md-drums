cmake_minimum_required(VERSION 3.15)

# MD Drums: the Machinedrum's engines without its OS (mdDrums, mdEngine) as an instrument plug-in, with JUCE's
# generic editor. Its own name and codes: it never replaces md-mm's "Gearmulator MD".
juce_add_plugin(mdDrumsPlugin
	COMPANY_NAME "c0remusic"
	IS_SYNTH TRUE
	NEEDS_MIDI_INPUT TRUE
	NEEDS_MIDI_OUTPUT FALSE
	IS_MIDI_EFFECT FALSE
	EDITOR_WANTS_KEYBOARD_FOCUS FALSE
	COPY_PLUGIN_AFTER_BUILD FALSE
	PLUGIN_MANUFACTURER_CODE C0rm
	PLUGIN_CODE Mddr
	PRODUCTS_FOLDER "${GEARMULATOR_JUCE_PRODUCTS_ROOT}/$<CONFIG>"
	FORMATS ${juce_formats}
	PRODUCT_NAME "MD Drums"
	VST3_AUTO_MANIFEST TRUE
	BUNDLE_ID "local.c0remusic.mddrums"
)

set_property(TARGET mdDrumsPlugin PROPERTY ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shared-code/$<CONFIG>")

target_sources(mdDrumsPlugin PRIVATE mdDrumsProcessor.cpp mdDrumsProcessor.h)
removeJuceDependencies(mdDrumsPlugin)
target_compile_definitions(mdDrumsPlugin PUBLIC JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
target_link_libraries(mdDrumsPlugin PRIVATE mdDrums juce_plugin_modules)
set_property(TARGET mdDrumsPlugin PROPERTY FOLDER "Elektron")

if(BUILD_TESTING)
	add_executable(mdDrumsProcessorTest mdDrumsProcessorTest.cpp)
	target_link_libraries(mdDrumsProcessorTest PRIVATE mdDrumsPlugin mdDrums juce_plugin_modules)
	target_compile_definitions(mdDrumsProcessorTest PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
	add_test(NAME mdDrumsProcessorTest COMMAND mdDrumsProcessorTest)
	set_tests_properties(mdDrumsProcessorTest PROPERTIES LABELS "FirmwareTest;AudioIo" SKIP_RETURN_CODE 77 TIMEOUT 120)
	set_property(TARGET mdDrumsProcessorTest PROPERTY FOLDER "Elektron/test")
endif()
