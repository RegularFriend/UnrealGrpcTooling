#include <map>
#include <string>
#include <algorithm>
#include <google/protobuf/compiler/code_generator.h>
#include <google/protobuf/compiler/plugin.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/io/printer.h>
#include <google/protobuf/io/zero_copy_stream.h>
#include <absl/strings/string_view.h>

using namespace google::protobuf;
using namespace google::protobuf::compiler;

class UnrealGenerator final : public CodeGenerator {
public:
    uint64_t GetSupportedFeatures() const override { return FEATURE_PROTO3_OPTIONAL; }

    static std::string ToPascalCase(absl::string_view input) {
        std::string result;
        bool bNextUpper = true;
        for (char c : input) {
            if (c == '_') {
                bNextUpper = true;
            } else {
                if (bNextUpper) {
                    result += static_cast<char>(toupper(static_cast<unsigned char>(c)));
                    bNextUpper = false;
                } else {
                    result += c;
                }
            }
        }
        return result;
    }

    // map of grpc types to UE types
    static std::string GetUEType(const FieldDescriptor *field) {
        static const std::map<FieldDescriptor::Type, std::string> type_map = {
            {FieldDescriptor::TYPE_DOUBLE, "double"}, {FieldDescriptor::TYPE_FLOAT, "float"},
            {FieldDescriptor::TYPE_INT64, "int64"}, {FieldDescriptor::TYPE_UINT64, "uint64"},
            {FieldDescriptor::TYPE_INT32, "int32"}, {FieldDescriptor::TYPE_BOOL, "bool"},
            {FieldDescriptor::TYPE_STRING, "FString"}
        };

        std::string base;
        if (field->type() == FieldDescriptor::TYPE_MESSAGE) {
            base = "F" + std::string(field->message_type()->name()); // Messages become structs which are prefixed with F
        } else if (field->type() == FieldDescriptor::TYPE_ENUM) {
            base = "E" + std::string(field->enum_type()->name()); // Unreal enums are prefixed with E
        } else if (type_map.contains(field->type())) {
            base = type_map.at(field->type()); // Default types dont have a special prefix.
        } else {
            base = "FString"; // if you really dont have any clue what you are, default to string
        }

        if (field->is_repeated()) return "TArray<" + base + ">"; // repeateds become TArrays
        if (field->has_presence()) return "TOptional<" + base + ">"; // Things without defaults become TOptionals
        return base;
    }

    bool Generate(const FileDescriptor *file, const std::string &parameter,
                  GeneratorContext *context, std::string *error) const override {

        auto base_name = std::string(file->name());
        if (const size_t last_dot = base_name.find_last_of('.'); last_dot != std::string::npos) {
            base_name = base_name.substr(0, last_dot);
        }

        const std::unique_ptr<io::ZeroCopyOutputStream> output(context->Open(base_name + ".generated.h"));
        io::Printer printer(output.get(), '$');

        // Writing the file header and includes
        std::map<std::string, std::string> template_params;
        template_params["pb"] = base_name;

        printer.Print(template_params, "#pragma once\n\n"
                                      "#include \"CoreMinimal.h\"\n"
                                      "#include \"$pb$.pb.h\"\n"
                                      "#include \"$pb$.generated.h\"\n\n");

        for (int i = 0; i < file->message_type_count(); i++) {
            const Descriptor *msg = file->message_type(i);
            std::map<std::string, std::string> MessageVars;
            MessageVars["name"] = std::string(msg->name());

            // Writing the USTRUCT declaration
            printer.Print(MessageVars, "USTRUCT(BlueprintType)\n"
                                       "struct FRIENDGRPC_API F$name$ {\n");
            printer.Indent();
            printer.Print("GENERATED_BODY()\n\n");

            // Writing the individual UPROPERTY fields
            for (int j = 0; j < msg->field_count(); j++) {
                const FieldDescriptor *field = msg->field(j);
                //skip synthetic oneof's. on the backend, optionals can be written using oneofs to track if they're set
                //but they are meant to stay invisible. logic to determine if they exist happens in the fromproto conversion function
                //when the corresponding TOptional is set or not.
                if (field->real_containing_oneof() == nullptr && field->containing_oneof() != nullptr) continue;

                std::map<std::string, std::string> property_params;
                property_params["type"] = GetUEType(field);
                property_params["FieldName"] = ToPascalCase(field->name());
                printer.Print(property_params, "UPROPERTY(VisibleAnywhere, BlueprintReadOnly)\n"
                                              "$type$ $FieldName$;\n\n");
            }

            // Writing the FromProto conversion function
            printer.Print(MessageVars, "static F$name$ FromProto(const ::$name$& InProto) {\n");
            printer.Indent();
            printer.Print(MessageVars, "F$name$ Out;\n");

            for (int j = 0; j < msg->field_count(); j++) {
                const FieldDescriptor *field = msg->field(j);
                std::map<std::string, std::string> mapping_params;
                mapping_params["FieldName"] = ToPascalCase(field->name());
                mapping_params["ProtoName"] = std::string(field->name());
                mapping_params["Type"] = (field->type() == FieldDescriptor::TYPE_MESSAGE)
                                             ? std::string(field->message_type()->name())
                                             : "";

                if (field->is_repeated()) {
                    printer.Print(mapping_params, "for (const auto& Element : InProto.$ProtoName$()) {\n");
                    printer.Indent();
                    if (field->type() == FieldDescriptor::TYPE_MESSAGE) {
                        printer.Print(mapping_params, "Out.$FieldName$.Add(F$Type$::FromProto(Element));\n");
                    } else if (field->type() == FieldDescriptor::TYPE_STRING) {
                        printer.Print(mapping_params, "Out.$FieldName$.Add(UTF8_TO_TCHAR(Element.c_str()));\n");
                    } else {
                        printer.Print(mapping_params, "Out.$FieldName$.Add(Element);\n");
                    }
                    printer.Outdent();
                    printer.Print("}\n");
                } else if (field->type() == FieldDescriptor::TYPE_MESSAGE) {
                    printer.Print(mapping_params, "if (InProto.has_$ProtoName$()) {\n"
                                                 "    Out.$FieldName$ = F$Type$::FromProto(InProto.$ProtoName$());\n"
                                                 "}\n");
                } else if (field->type() == FieldDescriptor::TYPE_STRING) {
                    printer.Print(mapping_params, "Out.$FieldName$ = UTF8_TO_TCHAR(InProto.$ProtoName$().c_str());\n");
                } else {
                    printer.Print(mapping_params, "Out.$FieldName$ = InProto.$ProtoName$();\n");
                }
            }

            printer.Print("return Out;\n");
            printer.Outdent();
            printer.Print("}\n");

            printer.Outdent();
            printer.Print("};\n\n");
        }
        return true;
    }
};

int main(int argc, char *argv[]) {
    const UnrealGenerator generator;
    return PluginMain(argc, argv, &generator);
}
