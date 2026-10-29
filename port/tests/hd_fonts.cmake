# Synthetic font tests use the real loader, compositor and generated SDK layout.
set(KHDAYS_FONT_TEST_DIR "${CMAKE_CURRENT_BINARY_DIR}/font-tests")
file(MAKE_DIRECTORY "${KHDAYS_FONT_TEST_DIR}/build/tests")
set(KHDAYS_FONT_TEST_TARGETS)
function(khdays_font_test name)
    set(target khdays-font-${name}-test)
    add_executable(${target} ${ARGN})
    target_link_libraries(${target} PRIVATE khdays-hud-settings)
    target_compile_options(${target} PRIVATE /W4 /WX
                           $<$<COMPILE_LANGUAGE:C>:/std:c17>
                           $<$<COMPILE_LANGUAGE:CXX>:/std:c++20 /EHsc>)
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    add_test(NAME khdays-font-${name} COMMAND ${target})
    set_tests_properties(khdays-font-${name} PROPERTIES WORKING_DIRECTORY "${KHDAYS_FONT_TEST_DIR}")
    set(KHDAYS_FONT_TEST_TARGETS ${KHDAYS_FONT_TEST_TARGETS} ${target} PARENT_SCOPE)
endfunction()

khdays_font_test(canvas tests/hd_text_test.c)
khdays_font_test(follow tests/ui_follow_test.c)
khdays_font_test(paths tests/font_paths_test.cpp runtime/mods/font_raster.cpp)
khdays_font_test(secondary tests/font_secondary_test.cpp runtime/mods/font_raster.cpp)
khdays_font_test(raster tests/font_raster_test.cpp runtime/mods/font_raster.cpp)
khdays_font_test(cache tests/font_raster_cache_test.cpp)
khdays_font_test(nftr ../assets/tests/test_font.cpp ../assets/src/font.cpp ../assets/src/message.cpp)
target_include_directories(khdays-font-nftr-test PRIVATE ../assets/include)

add_library(khdays-font-scope-impl OBJECT runtime/menus/camp.c)
target_compile_options(khdays-font-scope-impl PRIVATE /W1 /std:c17 /Gy /Gw)
khdays_font_test(scope tests/camp_font_scope_test.c $<TARGET_OBJECTS:khdays-font-scope-impl>)
target_link_options(khdays-font-scope-test PRIVATE /OPT:REF)

set(KHDAYS_FONT_LAYOUT_GEN "${KHDAYS_GEN}/rewritten/libs/nns/g2d/calls")
set(KHDAYS_FONT_LAYOUT_SOURCES
    "${KHDAYS_FONT_LAYOUT_GEN}/Text_DrawGlyph.c"
    "${KHDAYS_FONT_LAYOUT_GEN}/Text_DrawLine.c"
    "${KHDAYS_FONT_LAYOUT_GEN}/Text_DrawAlignedLines.c"
    "${KHDAYS_FONT_LAYOUT_GEN}/Text_AlignAnchor.c"
    "${KHDAYS_FONT_LAYOUT_GEN}/NNSi_G2dFontGetStringWidth.c"
    "${KHDAYS_FONT_LAYOUT_GEN}/NNSi_G2dFontGetTextRect.c"
    "${KHDAYS_GEN}/rewritten/src/engine/Obj_ForwardInnerPayload.c"
    "${KHDAYS_GEN}/rewritten/src/engine/Text_DrawWithShadow.c"
    "${KHDAYS_GEN}/rewritten/src/engine/Text_DrawDirectional_2.c")
add_library(khdays-font-layout-impl OBJECT ${KHDAYS_FONT_LAYOUT_SOURCES})
add_library(khdays-font-reflow-impl OBJECT ${KHDAYS_FONT_LAYOUT_SOURCES})
target_compile_options(khdays-font-reflow-impl PRIVATE /W1 /std:c17 /Zp4)
target_include_directories(khdays-font-reflow-impl BEFORE PRIVATE tests/font_reflow_compat compat "${KHDAYS_GEN}")
khdays_font_test(reflow tests/font_reflow_test.c $<TARGET_OBJECTS:khdays-font-reflow-impl>)

target_compile_options(khdays-font-layout-impl PRIVATE /W1 /std:c17 /Zp4)
target_include_directories(khdays-font-layout-impl PRIVATE compat "${KHDAYS_GEN}")
khdays_font_test(layout tests/font_layout_test.c $<TARGET_OBJECTS:khdays-font-layout-impl>)
target_include_directories(khdays-font-layout-test PRIVATE compat "${KHDAYS_GEN}")
add_custom_target(khdays-font-tests DEPENDS ${KHDAYS_FONT_TEST_TARGETS})

add_test(NAME khdays-font-graphics-only COMMAND khdays-font-canvas-test --graphics-only)
set_tests_properties(khdays-font-graphics-only PROPERTIES ENVIRONMENT "KHDAYS_HD_FONT_DISABLE=1"
                     WORKING_DIRECTORY "${KHDAYS_FONT_TEST_DIR}")
add_test(NAME khdays-font-hooks COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/font_scope_hooks_test.py")
