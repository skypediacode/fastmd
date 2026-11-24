# Compiles a patched copy of md4c.c instead of the fetched one. The fetched tree
# stays pristine, so existing build directories and GIT_TAG updates need no
# special handling, and a patch that stops applying fails the configure step.
function(fastmd_patch_md4c patch_file)
    find_package(Git REQUIRED)
    set(upstream "${md4c_SOURCE_DIR}/src/md4c.c")
    set(work_dir "${CMAKE_BINARY_DIR}/md4c-patched/work")
    set(patched "${CMAKE_BINARY_DIR}/md4c-patched/md4c.c")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${patch_file}" "${upstream}")

    file(MAKE_DIRECTORY "${work_dir}")
    configure_file("${upstream}" "${work_dir}/md4c.c" COPYONLY)
    # The ceiling keeps git from treating the build directory as part of the
    # FastMD repository, where `git apply` would skip these paths.
    # The fetched checkout may have CRLF line endings.
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${CMAKE_BINARY_DIR}/md4c-patched"
            "${GIT_EXECUTABLE}" apply --ignore-whitespace --whitespace=nowarn "${patch_file}"
        WORKING_DIRECTORY "${work_dir}"
        RESULT_VARIABLE result
        ERROR_VARIABLE error)
    file(SHA256 "${upstream}" upstream_hash)
    file(SHA256 "${work_dir}/md4c.c" patched_hash)
    if(NOT result EQUAL 0 OR upstream_hash STREQUAL patched_hash)
        message(FATAL_ERROR "Could not apply ${patch_file} to ${upstream}: ${error}")
    endif()
    file(COPY_FILE "${work_dir}/md4c.c" "${patched}" ONLY_IF_DIFFERENT)

    set_target_properties(md4c PROPERTIES SOURCES "${patched};${md4c_SOURCE_DIR}/src/md4c.h")
    target_include_directories(md4c PRIVATE "${md4c_SOURCE_DIR}/src")
endfunction()
