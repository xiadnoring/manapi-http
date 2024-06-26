To perform static linking of gmplib in your C++ program using CMake, you can follow these steps:

1. **Specify the gmplib library path**:
   ```cmake
   set(GMP_LIBRARY_PATH /path/to/gmp/lib)
   ```

2. **Specify the gmplib include path**:
   ```cmake
   set(GMP_INCLUDE_PATH /path/to/gmp/include)
   ```

3. **Link against the gmplib library**:
   ```cmake
   target_link_libraries(${PROJECT_NAME} ${GMP_LIBRARY_PATH}/libgmp.a)
   ```

4. **Include the gmplib headers**:
   ```cmake
   include_directories(${GMP_INCLUDE_PATH})
   ```

Here is a complete example of a `CMakeLists.txt` file:
```cmake
cmake_minimum_required(VERSION 3.10)

project(MyProject)

set(GMP_LIBRARY_PATH /path/to/gmp/lib)
set(GMP_INCLUDE_PATH /path/to/gmp/include)

add_executable(${PROJECT_NAME} main.cpp)

target_link_libraries(${PROJECT_NAME} ${GMP_LIBRARY_PATH}/libgmp.a)
include_directories(${GMP_INCLUDE_PATH})
```

In this example, replace `/path/to/gmp/lib` and `/path/to/gmp/include` with the actual paths to the gmplib library and include directories on your system.

### Explanation

- `set(GMP_LIBRARY_PATH /path/to/gmp/lib)` and `set(GMP_INCLUDE_PATH /path/to/gmp/include)` specify the paths to the gmplib library and include directories.
- `target_link_libraries(${PROJECT_NAME} ${GMP_LIBRARY_PATH}/libgmp.a)` links the executable against the static gmplib library.
- `include_directories(${GMP_INCLUDE_PATH})` includes the gmplib headers in the project.

### Notes

- Make sure to replace `/path/to/gmp/lib` and `/path/to/gmp/include` with the actual paths to the gmplib library and include directories on your system.
- This example assumes that the gmplib library is named `libgmp.a`. If your library has a different name, adjust the `target_link_libraries` command accordingly.
- If you are using a different compiler or platform, you may need to adjust the library name or linking command accordingly.

By following these steps, you should be able to statically link gmplib into your C++ program using CMake.