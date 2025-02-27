## \[TODO\]

*   Add a render spec that contains all the data about rendering which the user can fetch at runtime.
*   Move the renderer struct to the stack.

## \[UNRELEASED\]

### Added

*   “type” system in ssl. Currenlty just a keyword or identifier or whatever you call that.
*   New object pool, untested, need some time to test that
*   Support for multithreading on all containers is now fully supported
*   Need to remove support for strings or redo them entirely!
*   Also need to redo the allocator interface, very unclean
*   New hash.h with two hash functions. More may be added later.
*   Added NV\_FALLTHROUGH
*   A minimal clang-tidy configuration; The project is now compliant to clang-tidy's basic checks.

### Fixed

*   Redid the hashmap container; Very much faster now.
*   Changed timer struct to nv\_timer\_t as it should have been.
*   The example's clock using the wrong format specifier.
*   All (most?) member variables now have the m\_ prefix
*   Fixed the CMakeLists using invalid compile option --fast-math (corrected to -ffast-math)
*   Add check to nvsm runtime cleanup for whether the shader module is NULL or not
*   Fix stupid bug where the result check function is being assigned to in pipeline.h

## \[VERSION 1.0\]

### Added

*   Add proto for ssl
*   Added multithreading support to hashmap
*   Added keywords to ssl in testing
*   Add new fontc\_load function to automatically handle baking (NEEDS FIXES FOR BROKEN FONT FILES!!!)

### Fixes

*   Fix hashmap allocating the size of the entire node for nothing but a pointer to the node
*   Improved the ctext fragment shader
*   Fixed queue priority variable going out of scope and corrupting the queue create info structure in vk.c
*   Updated example due to breaking changes from fontc.