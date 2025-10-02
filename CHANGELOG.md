## [Unreleased]

### Added
- 

### Changed
- moved most things to stack.
- refactored allocator interface has been.
- restructured entire project. very clean :3

### Fixed
- nv_tiimer bug returning negative values (only returned nanoseconds part before).
- many vulkan initialization issues
- camera uniform buffers have improperly aligned variables.


# \[LEGACY\]
### Changes
*   removed ssl. Too much of a hassle for me to care about anymore. Maybe some day.
*   iris_buffer_type is now *_usage as it more reflects the purpose
*   Renamed dynarray.h to list.h, much easier to type, more clear what the hell it is.
*   We will now be using SDL_mutex instead of pthread for compatibility and because SDL is of superior intellect.
*   Removed m_ prefix from all variables. It makes the project less readable.
*   Entirely redid nvsm. Maybe I should break off the databasing code into a new container?
*   Modularize everything.
*   Added “type” system in ssl. Currenlty just a keyword or identifier or whatever you call that.
*   Added new object pool, untested, need some time to test that
*   Added support for multithreading on all containers
*   Added new hash.h with two hash functions. More may be added later.
*   Added NV\_FALLTHROUGH
*   Add proto for ssl
*   Added multithreading support to hashmap
*   Added keywords to ssl in testing
*   Add new fontc\_load function to automatically handle baking (NEEDS FIXES FOR BROKEN FONT FILES!!!)*   Added a minimal clang-tidy configuration; The project is now compliant to clang-tidy's basic checks.
*   Offloaded image reading/writing to SDL_image. We do not need more responsibilities.
*   Improved error handling a little in the renderer initialization.
*   The core path now runs even if no memory is available. The program exits safely.
*   Heavily improved nvsm. Now onto fontc and the common containers. The containers should be defined enough so that they can be incorporated into the standard
*   Moved containers into the standard library
*   Moved common/ directly into the standard library
*   Add support for a glyph table for unicode characters and fast indexing
*   Add multithreading support for all standard containers
*   Also fix inconsistencies of deviating from size\_t
*   Remove stupid things from the code like ctext labels, err.h, cvar.h or whatever
*   Add the m\_ prefix to ALL member variables and adopt a good coding standard
*   removed m\_ prefix you moron
*   (Possibly?) Add a clang-tidy that doesn't destroy the codebase
*   Break core.c into two files: core.c and ext.c, the latter containing the common/ sources

### Fixed
*   Fixed incorrect calculating of highest power of input in itoa.
*   init_size in list.h has now been replaced with init_capacity, same purpose, better name.
*   Fixed program options stupidly using opt->type instead of opt->value.
*   Fixed the debug messenger creation function using nvvk_context.instance which is not initialized by the time the function is called.
*   Fixed vulkan dependant code not compiling on 32 bit because I was using NULL instead of VK_NULL_HANDLE.
*   Redid the hashmap container; Very much faster now.
*   Changed timer struct to nv\_timer\_t as it should have been.
*   The example's clock using the wrong format specifier.
*   All (most?) member variables now have the m\_ prefix
*   Fixed the CMakeLists using invalid compile option --fast-math (corrected to -ffast-math)
*   Add check to nvsm runtime cleanup for whether the shader module is NULL or not
*   Fix stupid bug where the result check function is being assigned to in pipeline.h
*   Fix hashmap allocating the size of the entire node for nothing but a pointer to the node
*   Improved the ctext fragment shader
*   Fixed queue priority variable going out of scope and corrupting the queue create info structure in vk.c
*   Updated example due to breaking changes from fontc.
