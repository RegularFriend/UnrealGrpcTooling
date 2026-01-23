### UnrealGrpcTooling Guidelines

#### Build & Configuration
- **Toolchain**: Use **Visual Studio** (MSVC) for building. This is mandatory for Unreal Engine compatibility.
- **Build Type**: Always use **Release** builds (`-DCMAKE_BUILD_TYPE=Release`). Debug builds of gRPC often use incompatible flags (e.g., `_ITERATOR_DEBUG_LEVEL`) that cause linker errors in Unreal.
- **Installation**: Use the `install` target to gather all necessary binaries and headers into the `outputs/` directory.
  ```powershell
  cmake --build <build_dir> --target install --config Release
  ```

#### Testing
To verify the `protoc-gen-unreal` plugin:
1. Ensure the project is built and installed.
2. Run the following command (adjust paths as necessary):
   ```powershell
   ./outputs/windows/bin/protoc.exe `
       --plugin=protoc-gen-unreal=./outputs/windows/bin/protoc-gen-unreal.exe `
       --unreal_out=./test_output/ `
       --cpp_out=./test_output/ `
       -I ./path/to/protos your_file.proto
   ```
3. Check `./test_output/` for generated `F<MessageName>.h` and `<PackageName>Converter.h/cpp` files.

#### Dependency Rules
- **GRPC Folder**: **DO NOT** modify any files within the `grpc/` folder. This is a third-party dependency managed via submodules. Any changes should be made to the `plugin/` or `CMakeLists.txt` at the root, unless absolutely necessary and approved.

#### Development Information
- **Language Standard**: The project uses **C++20**.
- **Code Style**:
  - The plugin (`protoc-gen-unreal`) follows standard gRPC plugin patterns.
  - Generated Unreal types use **PascalCase** (e.g., `user_id` -> `UserId`).
  - Generated Unreal structs are prefixed with `F` (e.g., `FTestMessage`).
- **Unreal Compatibility**: If you encounter macro collisions (like `verify`), use a guard header as described in the `README.md`.
