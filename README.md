# libfbx
Framebuffer for terminal made with C++.

## Roadmap
[ ] Simple framebuffer functions (print, read)
[ ] Save and load framebuffers or screen areas
[ ] tty compatibility
[ ] Handle different framebuffers at the same time 
[ ] Add advanced functions for console (size, position, special parameters)
[ ] Make Windows open-source

## How to use fbx
You can use this library using CMake's CPM.

```cmake
CPMAddPackage("gh:eLiteEs/libfbx@1.0.0")
target_link_libraries(app PRIVATE fbx::fbx)
```

## License
fbx uses MIT License. You can read it at [LICENSE](LICENSE) file.

