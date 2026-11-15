set_target_properties(grpp_app PROPERTIES
    MACOSX_BUNDLE TRUE
    OUTPUT_NAME "Gridrunner++ Enhanced"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${GRPP_APPLE_BUNDLE_ID}"
    MACOSX_BUNDLE_BUNDLE_NAME "Gridrunner++ Enhanced"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    MACOSX_BUNDLE_BUNDLE_VERSION "1"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_LIST_DIR}/Info.plist.in"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${GRPP_APPLE_BUNDLE_ID}"
)
if(GRPP_APPLE_SIGNING_TEAM)
    set_target_properties(grpp_app PROPERTIES XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${GRPP_APPLE_SIGNING_TEAM}")
endif()

set(grpp_resource_root "${CMAKE_CURRENT_LIST_DIR}/../..")
file(GLOB_RECURSE grpp_bundle_assets CONFIGURE_DEPENDS "${grpp_resource_root}/assets/*")
file(GLOB grpp_bundle_licenses CONFIGURE_DEPENDS "${grpp_resource_root}/LICENSES/*")
foreach(file IN LISTS grpp_bundle_assets grpp_bundle_licenses)
    get_filename_component(parent "${file}" DIRECTORY)
    file(RELATIVE_PATH relative_parent "${grpp_resource_root}" "${parent}")
    set_source_files_properties("${file}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/${relative_parent}")
endforeach()
set(grpp_bundle_notices "${grpp_resource_root}/README.md" "${grpp_resource_root}/LICENSE.txt" "${grpp_resource_root}/THIRD-PARTY-NOTICES.txt")
set_source_files_properties(${grpp_bundle_notices} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
target_sources(grpp_app PRIVATE ${grpp_bundle_assets} ${grpp_bundle_licenses} ${grpp_bundle_notices})

if(GRPP_APPLE_IOS)
    target_compile_definitions(grpp_app PRIVATE GRPP_APPLE_IOS=1)
    target_compile_definitions(grpp_presentation PRIVATE GRPP_APPLE_IOS=1)
    set_target_properties(grpp_app PROPERTIES
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic)
else()
    target_compile_definitions(grpp_app PRIVATE GRPP_APPLE_ANGLE=1)
    target_include_directories(grpp_app PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
    target_compile_definitions(grpp_presentation PRIVATE GRPP_APPLE_ANGLE=1)
    set_target_properties(grpp_app PROPERTIES BUILD_RPATH "@executable_path/../Frameworks" INSTALL_RPATH "@executable_path/../Frameworks")
    add_custom_command(TARGET grpp_app POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            "-DAPP=$<TARGET_BUNDLE_DIR:grpp_app>"
            "-DANGLE_DIR=${GRPP_ANGLE_DIR}"
            "-DSDL_DIR=$<TARGET_FILE_DIR:SDL3::SDL3>"
            -P "${CMAKE_CURRENT_LIST_DIR}/Bundle.cmake"
        VERBATIM)
endif()
install(TARGETS grpp_app BUNDLE DESTINATION .)
