## UNRELEASED

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