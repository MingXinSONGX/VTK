vtk_module_test_data(
  Data/gs.bp/,REGEX:.*)
add_subdirectory(Cxx)
if (VTK_WRAP_PYTHON)
  vtk_module_test_data(
    Data/vtk-uns-grid-2.json
    Data/tris-blocks-time.bp
    Data/tris-blocks-time.bp.dir/,REGEX:.*
    Data/cartesian-attr.bp
    Data/cartesian-attr.bp.dir/,REGEX:.*
    Data/groups.bp/,REGEX:.*
    Data/gs.bp/,REGEX:.*)
  add_subdirectory(Python)
endif ()
