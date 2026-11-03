# chesscoach_assert_no_qt(<target>): the module boundary in the build (CLAUDE.md, **Module
# boundaries**). core, pipeline and cli call it on each of their targets; at the end of the
# configure, once every target_link_libraries has run, it walks the target's link closure
# (LINK_LIBRARIES and INTERFACE_LINK_LIBRARIES, through every target it reaches) and fails the
# configure if any entry is a Qt target or a Qt library file. tools/gates.sh's boundary step and
# the boundary test (cmake/boundary_check.cmake) check the sources for the same rule.
include_guard(GLOBAL)

function(_chesscoach_check_no_qt target)
  set(pending "${target}")
  set(seen "")
  while(pending)
    list(POP_FRONT pending current)
    if(current IN_LIST seen)
      continue()
    endif()
    list(APPEND seen "${current}")
    foreach(property LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
      get_target_property(libraries "${current}" ${property})
      if(NOT libraries)
        continue()
      endif()
      foreach(library IN LISTS libraries)
        # $<LINK_ONLY:x> and similar wrappers name the library they wrap.
        string(REGEX REPLACE "^\\$<[A-Z_]+:(.*)>$" "\\1" library "${library}")
        if(library MATCHES "^Qt[0-9]*::" OR library MATCHES "(^|/)(lib)?Qt[0-9]*[A-Z][A-Za-z]*(\\.framework|\\.so|\\.dylib|\\.a|\\.lib)")
          message(FATAL_ERROR "module boundary: ${target} links Qt (${library}, through "
                              "${current}); core, pipeline and cli never depend on Qt "
                              "(CLAUDE.md, **Module boundaries**)")
        endif()
        if(TARGET "${library}")
          list(APPEND pending "${library}")
        endif()
      endforeach()
    endforeach()
  endwhile()
endfunction()

function(chesscoach_assert_no_qt target)
  if(NOT TARGET "${target}")
    message(FATAL_ERROR "chesscoach_assert_no_qt: no target ${target}")
  endif()
  # A deferred call's arguments are evaluated when it runs, at the root, so the target's name is
  # substituted now.
  cmake_language(EVAL CODE "cmake_language(DEFER DIRECTORY [[${CMAKE_SOURCE_DIR}]] CALL _chesscoach_check_no_qt [[${target}]])")
endfunction()
