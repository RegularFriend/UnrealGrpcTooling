# Friend gRPC Tools
gRpc Tooling for Unreal Engine. 
---

## Contents
* **Unreal-Friendly gRPC Build**: Custom CMake configuration designed to build gRPC/Protobuf with the flags required for Unreal Engine compatibility
* **Custom Protoc Plugin**: Includes `protoc-gen-unreal`, a specialized protoc generator that creates:
    * `USTRUCT` wrappers for Protobuf messages.
    * Automatic **PascalCase** conversion for field names (e.g., `user_id` becomes `UserId`).
    * Built-in `FromProto()` conversion functions to bridge gRPC C++ objects and Unreal types.
---

## Getting Started

### 1. Clone the Repository
Because this project uses submodules for gRPC, you must clone recursively:
```bash
git clone --recursive https://github.com/RegularFriend/UnrealGrpcTooling.git
```

### 2. Build the tooling
Clion/Visual Studio Build
1.  **Toolchain Setup**: Ensure your Toolchain is set to **Visual Studio** (required for Unreal Engine compatibility), and that you are making a release build (grpc debug builds have some incomptable flags)
2.  **Initialize Submodule**: If the `grpc` folder is empty, run `git submodule update --init --recursive` in the terminal. If you cloned recursively, you can skip this step.
3.  **Run Install**: Run the **Install** target (**Build > Install**).
4.  **Verify Output**: Everything should be in the outputs folder. 

Cli Build
```bash
# 1. Configure the project
# Release is chosen via --config
cmake -B build -DCMAKE_BUILD_TYPE=Release

# 2. Build and Install
# This compiles the plugin and copies binaries, public includes, and library to the /outputs folder
cmake --build build --target install --config Release
```

## Usage in Unreal
### Generating Protos
The command to generate Unreal compatable proto structs using the included plugin is:

```bash
protoc.exe -I=<PROTO_SRC_DIR> \
    --plugin=protoc-gen-unreal=<PATH_TO_PLUGIN> \
    --unreal_out=[api_name=YOUR_API_NAME:]<OUT_DIR> \
    <INPUT_FILE>.proto
```
#### Key Flags

- `-I`: Specifies the directory in which to search for imports.
- `--plugin=protoc-gen-unreal=...`: Path to the `protoc-gen-unreal.exe` binary.
- `--unreal_out=...`: The output directory for generated Unreal code.
    - `api_name=FOO_API:` (Optional) Adds a module API macro (e.g., `FOO_API`) to the generated unreal classes.
- `<INPUT_FILE>.proto`: The path to the input `.proto` file.
#### Generated output
* Default grpc generated cpp files suffixed with .pb.cc and .pb.h
* An enums header containing UENUM versions of all defined enums.
* A .h file for each message containing a USTRUCT equavalent
* A Marshaller.h and .cpp files containing static conversion functions for each method. Both from grpc -> Unreal and Unreal -> grpc.
### Including Generated Protos in an Unreal Project
The .pb.cc and .ph.h output will not be able to be included directly into an Unreal project. Google and Epic Games use different compiler and warning flags, which will cause errors if you try to directly include them. My suggestion is to encapsulate all references to these classes in a plugin, and include a MacroGuard.h for all the plugin classes. Then have a singular class recieve and convert all messages from Unreal style protos -> google style protos to shadow the problematic includes. 
> **Note:** This is just one suggestion on how to integrate the grpc generated output into an Unreal project. It is by no means the only way.
> 
#### Example Guard Header
This guard header should have all the needed warnings diabled to allow compilation. 
```cpp
#pragma once

/**
 * Prevents name collisions between Unreal Engine macros and gRPC/Protobuf headers.
 * Also suppresses common warnings triggered by third-party headers in Unreal.
 * Force-included via FriendGrpc.Build.cs.
 */

#ifdef _MSC_VER
// Warning C4668: 'Macro' is not defined as a preprocessor macro, replacing with '0' for '#if/#elif'
// Protobuf uses port_def.inc/port_undef.inc which can leave macros undefined between header inclusions.
	#pragma warning(disable: 4668)
// Warning C4800 : Implicit conversion from 'const google::protobuf::OneofDescriptor *' to bool. Possible information loss
	#pragma warning(disable: 4800)
// Warning C4458 : Hides internal member. micro_string.h does this->size = size all over the place.  
	#pragma warning(disable: 4458)
#endif

//these exist so you can call #pragma pop_macro("name") within an individual file to have them re-allign
#pragma push_macro("verify")

//undef conflicts
#undef verify
```

#### Force Includes
If you are doing this at a plugin level, you can easily add this to all plugins via the build.cs's ForceIncludeFiles variable. 
```c#
ForceIncludeFiles.Add("Path/To/MacroGuard.h");
```
