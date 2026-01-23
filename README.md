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
### Generated output
For a message `message user_info { string user_name = 1; }`, the plugin will generate:
```cpp
USTRUCT(BlueprintType)
struct FChatAction {
  GENERATED_BODY()

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
  FString ChatMessage;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
  int32 EmoteId;

};
```
Additionally, in a seperate conversion class, a conversion function will be generated
```cpp
FChatAction ProtoToUStructConverter::Convert(const ::ChatAction& In) {
  FChatAction Out;

  Out.ChatMessage = FString(UTF8_TO_TCHAR(In.chat_message().c_str()));
  Out.EmoteId = In.emote_id();
  return Out;
}
```
### Unreal Engine Macro Guards
Integrating gRPC and Protobuf into Unreal Engine is  difficult due to name collisions between Unreal's global macros (such as `verify`) and the standard C++ libraries used by gRPC. Additionally, UE and gRPC expect differnet warning flags, which must be adjusted. 

To get your project to compile, you can include a  **Guard Header**. 
> **Note:** This is just one way to achieve a successful build.
> 
#### Example Guard Header
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

#### Implementation via ForceIncludes
How I like to use the macro guard is to have all code touching the gRpc backend exist within an unreal plugin. Then in that plugin's build.cs file add: 
```c#
ForceIncludeFiles.Add("Path/To/MacroGuard.h");
```
This will inject this header into every compliation unit for the plugin. Then, within the plugin, have a class (I like to use a Game Instance Subsystem) ingest all the cpp proto messages, and forward events with the USTRUCT messages. This restricts the header guard to the plugin classes, preventing you from including it all over. 
