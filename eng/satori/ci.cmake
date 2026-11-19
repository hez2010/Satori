# jit and alljits install the same files. Finish the jit install before alljits.
cmake_language(DEFER CALL add_dependencies alljits jit)
