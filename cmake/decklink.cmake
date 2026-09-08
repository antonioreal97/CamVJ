include_guard(GLOBAL)

option(ATEMFX_ENABLE_DECKLINK "Build Windows DeckLink device discovery" OFF)
set(ATEMFX_DECKLINK_SDK_DIR "" CACHE PATH "External Blackmagic DeckLink SDK root")

function(atemfx_configure_decklink target)
    if(NOT ATEMFX_ENABLE_DECKLINK)
        target_sources(${target} PRIVATE src/decklink/decklink_discovery_stub.cpp)
        return()
    endif()

    if(NOT WIN32 OR NOT MSVC)
        message(FATAL_ERROR
            "ATEMFX_ENABLE_DECKLINK requires Windows with MSVC. "
            "Leave it OFF for the macOS development build.")
    endif()
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR NOT MSVC_CXX_ARCHITECTURE_ID STREQUAL "x64")
        message(FATAL_ERROR
            "DeckLink discovery currently supports MSVC x64 only. "
            "Use the x64 Native Tools Command Prompt; for Visual Studio generators, use -A x64.")
    endif()

    set(sdkIncludeDir "")
    foreach(candidate IN ITEMS
            "${ATEMFX_DECKLINK_SDK_DIR}/Win/include"
            "${ATEMFX_DECKLINK_SDK_DIR}/Win/Include")
        if(EXISTS "${candidate}/DeckLinkAPI.idl")
            set(sdkIncludeDir "${candidate}")
            break()
        endif()
    endforeach()
    if(NOT sdkIncludeDir OR NOT EXISTS "${sdkIncludeDir}/DeckLinkAPIVersion.h")
        message(FATAL_ERROR
            "Set ATEMFX_DECKLINK_SDK_DIR to an extracted Blackmagic DeckLink SDK root "
            "containing Win/include/DeckLinkAPI.idl and DeckLinkAPIVersion.h. "
            "The SDK is external and is not downloaded by this project.")
    endif()

    find_program(ATEMFX_MIDL_EXECUTABLE NAMES midl.exe midl
        DOC "Microsoft MIDL compiler from the Windows SDK")
    if(NOT ATEMFX_MIDL_EXECUTABLE)
        message(FATAL_ERROR
            "Microsoft MIDL was not found. Install the Windows SDK and configure/build "
            "from an x64 Native Tools Command Prompt, or set ATEMFX_MIDL_EXECUTABLE "
            "to midl.exe while keeping the MSVC compiler and SDK environment available.")
    endif()

    set(generatedDir "${CMAKE_CURRENT_BINARY_DIR}/decklink_sdk")
    set(generatedHeader "${generatedDir}/DeckLinkAPI.h")
    set(generatedIids "${generatedDir}/DeckLinkAPI_i.c")
    # The main IDL includes both current and versioned interfaces from this directory.
    file(GLOB sdkIdlFiles CONFIGURE_DEPENDS "${sdkIncludeDir}/*.idl")
    add_custom_command(
        OUTPUT "${generatedHeader}" "${generatedIids}"
        BYPRODUCTS "${generatedDir}/DeckLinkAPI.tlb"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${generatedDir}"
        COMMAND "${ATEMFX_MIDL_EXECUTABLE}" /nologo /env x64
            /I "${sdkIncludeDir}"
            /out "${generatedDir}"
            /h DeckLinkAPI.h /iid DeckLinkAPI_i.c /tlb DeckLinkAPI.tlb
            "${sdkIncludeDir}/DeckLinkAPI.idl"
        DEPENDS ${sdkIdlFiles}
        COMMENT "Generating DeckLink COM bindings"
        VERBATIM)
    add_custom_target(${target}_decklink_idl DEPENDS "${generatedHeader}" "${generatedIids}")
    add_dependencies(${target} ${target}_decklink_idl)

    # MIDL's GUID definitions support C++; keep the project C++-only.
    set_source_files_properties("${generatedIids}" PROPERTIES LANGUAGE CXX)
    target_sources(${target} PRIVATE
        src/decklink/decklink_discovery_win.cpp
        "${generatedHeader}" "${generatedIids}")
    target_include_directories(${target} PRIVATE "${generatedDir}" "${sdkIncludeDir}")
    target_link_libraries(${target} PRIVATE ole32 oleaut32)
    message(STATUS "DeckLink discovery: external SDK at ${ATEMFX_DECKLINK_SDK_DIR}")
endfunction()
