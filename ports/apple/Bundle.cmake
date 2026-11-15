# Embed dependencies in the development bundle; sign the completed bundle afterwards.
if(NOT APP OR NOT ANGLE_DIR OR NOT SDL_DIR)
    message(FATAL_ERROR "Bundle preparation requires APP, ANGLE_DIR and SDL_DIR")
endif()
include(BundleUtilities)
fixup_bundle("${APP}" "${ANGLE_DIR}/libEGL.dylib;${ANGLE_DIR}/libGLESv2.dylib" "${ANGLE_DIR};${SDL_DIR}")
