# FriendGrpcSDK
gRpc sdk for Unreal Engine. Maintained by Friend.
---

## Features
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
git clone --recursive [https://github.com/YourUsername/FriendGrpcSDK.git](https://github.com/YourUsername/FriendGrpcSDK.git)
```

### 2. Build the tooling
Clion/Visual Studio Build
1.  **Toolchain Setup**: Ensure your Toolchain is set to **Visual Studio** (required for Unreal Engine compatibility), and that you are making a release build (grpc debug builds have some incomptable flags)
2.  **Initialize Submodule**: If the `grpc` folder is empty, run `git submodule update --init --recursive` in the terminal. If you cloned recursively, you can skip this step.
3.  **Run Install**: Run the **Install** target (**Build > Install**).
4.  **Verify Output**: Everything should be in the outputs folder. 

Cli Build
```bash
# 1. Create a build directory
mkdir build
cd build

# 2. Configure the project
# Use -DCMAKE_BUILD_TYPE=Release because gRPC debug builds are often incompatible with Unreal
cmake -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release ..

# 3. Build and Install
# This compiles the plugin and copies binaries to the /outputs folder
cmake --build . --target install --config Release
```

## Usage in Unreal
### Generating Code
You can run the unreal generator by passing it in as a plugin when you generate protos. For example.
```bash
protoc --plugin=protoc-gen-unreal=./outputs/bin/protoc-gen-unreal.exe \
       --unreal_out=./YourProject/Source/YourModule/Public/ \
       --cpp_out=./YourProject/Source/YourModule/Private/ \
       -I ./protos your_file.proto
```
### Generated output Example
Generated Output Example
For a message like `message user_info { string user_name = 1; }`, the plugin generates a PascalCase compatible USTRUCT:

```cpp
USTRUCT(BlueprintType)
struct FRIENDGRPC_API FUserInfo {
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    FString UserName;

    /**
     * Converts a standard gRPC C++ protobuf object into this Unreal USTRUCT.
     */
    static FUserInfo FromProto(const ::user_info& InProto);
};
```
### Unreal Engine Macro Guards
Integrating gRPC and Protobuf into Unreal Engine is  difficult due to name collisions between Unreal's global macros (such as `verify`) and the standard C++ libraries used by gRPC. Additionally, UE and gRPC expect differnet warning flags, which must be adjusted. To get your project to compile, you can include a  **Guard Header**. 
> **Note:** This is just one way to achieve a successful build.
#### Example Guard Header
```cpp
#pragma once

/**
 * Prevents name collisions between Unreal Engine macros and gRPC/Protobuf headers.
 * Also suppresses common warnings triggered by third-party headers in Unreal.
 */

#ifdef _MSC_VER
// 	// Error C4668: 'macro' is not defined as a preprocessor macro, replacing with '0' for '#if/#elif'
// 	// Protobuf uses port_def.inc/port_undef.inc which can leave macros undefined between header inclusions.
	#pragma warning(disable: 4668)
// 	//Error C4800 : Implicit conversion from 'const google::protobuf::OneofDescriptor *' to bool. Possible information loss
	#pragma warning(disable: 4800)
#endif

//call #pragma pop_macro("name") within an individual file to re-align to project level settings. 
#pragma push_macro("verify")

//undef conflicts
#undef verify
```

#### Implementation via ForceIncludes
One way to set this up is to have all code touching the gRpc backend exist within an unreal plugin. Then in that plugin's build.cs file add: 
```c#
ForceIncludeFiles.Add("Path/To/MacroGuard.h");
```
This will inject this header into every compliation unit for the plugin. 
