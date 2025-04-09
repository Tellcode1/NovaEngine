## \[TODO\]
*   [ ] dude, the vulkan and non vulkan parts are interacting too much. fix!
*   [ ] Add a render spec that contains all the data about rendering which the user can fetch at runtime.
*   [x] Move the renderer struct to the stack.
*   [ ] Add support for things like NOVA_GPU_BUFFER_USAGE_SINGLE_TIME, etc. Basically, make an abstraction over the vulkan type.
*   [ ] Add more error checking to vulkan functions.
*   [ ] Need to really change the renderer. It should be more akin to the ctext renderer.
*   [ ] need to remove support for strings or redo them entirely!
*   [ ] need to redo the allocator interface, very unclean

# \[LEGACY\]
### Changes
*   nv_gpu_buffer_type is now *_usage as it more reflects the purpose
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
