#include <map>
#include <set>
#include <string>
#include <algorithm>
#include <google/protobuf/compiler/code_generator.h>
#include <google/protobuf/compiler/plugin.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/io/printer.h>
#include <google/protobuf/io/zero_copy_stream.h>
#include <absl/strings/string_view.h>

#include "absl/strings/str_replace.h"

using namespace google::protobuf;
using namespace google::protobuf::compiler;

static constexpr std::string_view kUPropVisible = "UPROPERTY(VisibleAnywhere, BlueprintReadOnly)\n";
static constexpr std::string_view kUstructDeclaration = "USTRUCT(BlueprintType)\n";
static constexpr std::string_view kGeneratedComment = "// THIS FILE IS GENERATED.\n";

class UnrealGenerator final : public CodeGenerator {
public:
    [[nodiscard]] uint64_t GetSupportedFeatures() const override { return FEATURE_PROTO3_OPTIONAL; }

    static std::string ToPascalCase(const absl::string_view input) {
        std::string result;
        bool bNextUpper = true;
        for (const char c: input) {
            if (c == '_') {
                bNextUpper = true;
            } else {
                if (bNextUpper) {
                    result += static_cast<char>(toupper(static_cast<unsigned char>(c)));
                    bNextUpper = false;
                } else {
                    result += static_cast<char>(tolower(static_cast<unsigned char>(c)));
                }
            }
        }
        return result;
    }

    static std::string GetBackingUEType(const FieldDescriptor *field) {
        static const std::map<FieldDescriptor::Type, std::string> type_map = {
            {FieldDescriptor::TYPE_DOUBLE, "double"}, {FieldDescriptor::TYPE_FLOAT, "float"},
            {FieldDescriptor::TYPE_INT64, "int64"}, {FieldDescriptor::TYPE_UINT64, "uint64"},
            {FieldDescriptor::TYPE_INT32, "int32"}, {FieldDescriptor::TYPE_BOOL, "bool"},
            {FieldDescriptor::TYPE_STRING, "FString"}
        };
        //enums and structs are special they are EFoo and FFoo respectively
        if (field->type() == FieldDescriptor::TYPE_MESSAGE) return "F" + std::string(field->message_type()->name());
        if (field->type() == FieldDescriptor::TYPE_ENUM) return "E" + std::string(field->enum_type()->name());

        if (type_map.contains(field->type())) return type_map.at(field->type());
        return "FString";
    }

    static std::string GetUEType(const FieldDescriptor *field) {
        if (field->is_map()) {
            const Descriptor *entry = field->message_type();
            return "TMap<" + GetBackingUEType(entry->FindFieldByName("key")) + ", " + GetBackingUEType(
                       entry->FindFieldByName("value")) + ">";
        }
        std::string base = GetBackingUEType(field);
        if (field->is_repeated()) return "TArray<" + base + ">";
        if (field->has_presence()) return "TOptional<" + base + ">";
        return base;
    }

    static void GenerateEnum(const EnumDescriptor *enum_desc, io::Printer &printer) {
        printer.Print({{"n", std::string(enum_desc->name())}},
                      "UENUM(BlueprintType)\nenum class E$n$ : uint8 {\n");
        printer.Indent();
        for (int j = 0; j < enum_desc->value_count(); j++) {
            printer.Print({
                              {"v", ToPascalCase(enum_desc->value(j)->name())},
                              {"num", std::to_string(enum_desc->value(j)->number())}
                          },
                          "$v$ = $num$,\n");
        }
        printer.Outdent();
        printer.Print("};\n\n");
    }

    static void GenerateAllEnumsInMessageTree(const Descriptor *msg, io::Printer &printer) {
        // We only want to generate enums for real messages.
        // Map entries are internal vars we should ignore.
        if (msg->options().map_entry()) return;
        for (int i = 0; i < msg->enum_type_count(); i++)
            GenerateEnum(msg->enum_type(i), printer);
        for (int i = 0; i < msg->nested_type_count(); i++)
            GenerateAllEnumsInMessageTree(msg->nested_type(i), printer);
    }

    static void GenerateOneofEnums(const Descriptor *msg, io::Printer &printer, const std::string &msg_name) {
        for (int i = 0; i < msg->oneof_decl_count(); i++) {
            const OneofDescriptor *oneof = msg->oneof_decl(i);
            //check for 'synthetic' oneofs. sometimes grpc creates a oneof as a backing field, those should not be converted.
            if (oneof->field(0)->real_containing_oneof() == nullptr) continue;
            std::string oneof_enum_name = msg_name + ToPascalCase(oneof->name());
            printer.Print({{"n", oneof_enum_name}}, "UENUM(BlueprintType)\nenum class E$n$Type : uint8 {\n");
            printer.Indent();
            printer.Print("None = 0,\n");
            for (int j = 0; j < oneof->field_count(); j++) {
                printer.Print({{"f", ToPascalCase(oneof->field(j)->name())}}, "$f$,\n");
            }
            printer.Outdent();
            printer.Print("};\n\n");
        }
    }

    static void GenerateUstruct(const Descriptor *msg, io::Printer &printer, const std::string &macro_with_space) {
        auto msg_name = std::string(msg->name());
        // oneof enum type declaration is done outside the USTRUCT
        GenerateOneofEnums(msg, printer, msg_name);

        // USTRUCT(BlueprintType)
        // FStruct FFoo
        printer.Print({{"n", msg_name}, {"us", kUstructDeclaration}, {"m", macro_with_space}},
                      "$us$struct $m$F$n$ {\n");
        printer.Indent();
        printer.Print("GENERATED_BODY()\n\n");

        // oneof enum member variables
        for (int i = 0; i < msg->oneof_decl_count(); i++) {
            const OneofDescriptor *oneof = msg->oneof_decl(i);
            //skip backing data oneofs
            if (oneof->field(0)->real_containing_oneof() == nullptr) continue;
            //add the oneof enums.
            std::string oneof_enum_name = msg_name + ToPascalCase(oneof->name());
            printer.Print(kUPropVisible.data());
            printer.Print({{"en", oneof_enum_name}, {"sn", ToPascalCase(oneof->name())}},
                          "E$en$Type $sn$Type = E$en$Type::None;\n\n");
        }
        for (int j = 0; j < msg->field_count(); j++) {
            const FieldDescriptor *f = msg->field(j);
            if (f->real_containing_oneof() == nullptr && f->containing_oneof() != nullptr) continue;
            printer.Print(kUPropVisible.data());
            printer.Print({{"t", GetUEType(f)},{"n", ToPascalCase(f->lowercase_name())}},
                          "$t$ $n${};\n\n");
        }
        printer.Outdent();
        printer.Print("};\n");
    }


    static std::string GetConversionExpression(const FieldDescriptor *field, const std::string &source_val) {
        switch (field->type()) {
            case FieldDescriptor::TYPE_MESSAGE: {
                return "FUnrealGrpcMarshaler::ToUnreal(" + source_val + ")";
            }
            case FieldDescriptor::TYPE_STRING:
                return "FString(UTF8_TO_TCHAR(" + source_val + ".c_str()))";
            case FieldDescriptor::TYPE_ENUM: //enums become UENUMs
                return "static_cast<" + GetBackingUEType(field) + ">(" + source_val + ")";
            default:
                return source_val; //primitives stay primitives
        }
    }

    static std::string GetToGrpcConversionExpression(const FieldDescriptor *field, const std::string &source_val) {
        switch (field->type()) {
            case FieldDescriptor::TYPE_MESSAGE: {
                return "FUnrealGrpcMarshaler::ToGrpc(" + source_val + ")";
            }
            case FieldDescriptor::TYPE_STRING:
                return "std::string(TCHAR_TO_UTF8(*" + source_val + "))";
            case FieldDescriptor::TYPE_ENUM:
                return "static_cast<::" + absl::StrReplaceAll(field->enum_type()->full_name(), {{".", "::"}}) + ">(" + source_val + ")";
            default:
                return source_val;
        }
    }

    // Generate the conversion functions from cpp proto to USTRUCT proto.
    static void GenerateConversionFunction(const Descriptor *msg, io::Printer &printer,
                                            const std::string &name_space) {
        const std::string message_name = std::string(msg->name());

        std::map<std::string, std::string> msg_vars = {
            {"mn", message_name},
            {"ns", name_space}
        };

        // Explicitly mark as static method in FUnrealGrpcMarshaler
        printer.Print(msg_vars, "F$mn$ FUnrealGrpcMarshaler::ToUnreal(const $ns$$mn$& In) {\n");
        printer.Indent();
        printer.Print(msg_vars, "F$mn$ Out;\n\n");

        //  ONEOF CONVERSION
        for (int i = 0; i < msg->oneof_decl_count(); i++) {
            const OneofDescriptor *oneof = msg->oneof_decl(i);
            if (oneof->field(0)->real_containing_oneof() == nullptr) continue;

            // OneOf-level vars: Inherit message constants and add specific OneOf names
            std::map<std::string, std::string> oneof_vars = msg_vars;
            oneof_vars["osn"] = oneof->name(); // Oneof Source Name
            oneof_vars["utn"] = ToPascalCase(oneof->name()); // Unreal Type Name

            printer.Print(oneof_vars, "switch (In.$osn$_case()) {\n");
            printer.Indent();

            for (int j = 0; j < oneof->field_count(); j++) {
                const FieldDescriptor *current_field_desc = oneof->field(j);

                // Field-level vars: Inherit everything above
                std::map<std::string, std::string> field_vars = oneof_vars;
                field_vars["unf"] = ToPascalCase(current_field_desc->lowercase_name());
                field_vars["val"] = GetConversionExpression(current_field_desc, "In." + std::string(current_field_desc->lowercase_name()) + "()");

                printer.Print(field_vars,
                              "case $ns$$mn$::k$unf$:\n"
                              "    Out.$unf$ = $val$;\n"
                              "    Out.$utn$Type = E$mn$$utn$Type::$unf$;\n"
                              "    break;\n");
            }
            printer.Print("default: break;\n}\n");
            printer.Outdent();
        }

        // FIELD CONVERSIONS
        for (int i = 0; i < msg->field_count(); i++) {
            const FieldDescriptor *current_field_desc = msg->field(i);
            // Skip fields that are part of a real oneof (handled above)
            if (current_field_desc->containing_oneof() && !current_field_desc->real_containing_oneof()) continue;

            std::map<std::string, std::string> field_vars = msg_vars;
            field_vars["unf"] = ToPascalCase(current_field_desc->lowercase_name());
            field_vars["psn"] = std::string(current_field_desc->lowercase_name()); // Proto Source Name

            if (current_field_desc->is_map()) {
                const FieldDescriptor *val_field = current_field_desc->message_type()->FindFieldByName("value");
                field_vars["ce"] = GetConversionExpression(val_field, "Kvp.second");
                printer.Print(field_vars,
                              "for (const auto& Kvp : In.$psn$()) {\n"
                              "    Out.$unf$.Add(Kvp.first, $ce$);\n"
                              "}\n");
            } else if (current_field_desc->is_repeated()) {
                field_vars["ce"] = GetConversionExpression(current_field_desc, "Element");
                printer.Print(field_vars,
                              "for (const auto& Element : In.$psn$()) {\n"
                              "    Out.$unf$.Add($ce$);\n"
                              "}\n");
            } else {
                std::string accessor = "In." + std::string(current_field_desc->lowercase_name()) + "()";
                field_vars["ce"] = GetConversionExpression(current_field_desc, accessor);
                //only set if it has a value or default value. TOptional will be null otherwise.
                if (current_field_desc->type() == FieldDescriptor::TYPE_MESSAGE) {
                    printer.Print(field_vars, "if (In.has_$psn$()) Out.$unf$ = $ce$;\n");
                } else {
                    printer.Print(field_vars, "Out.$unf$ = $ce$;\n");
                }
            }
        }

        printer.Print("return Out;\n");
        printer.Outdent();
        printer.Print("}\n\n");
    }

    // Generate the conversion functions from USTRUCT proto to cpp proto.
    static void GenerateToGrpcConversionFunction(const Descriptor *msg, io::Printer &printer,
                                                  const std::string &name_space) {
        const auto message_name = std::string(msg->name());

        std::map<std::string, std::string> msg_vars = {
            {"mn", message_name},
            {"ns", name_space}
        };

        printer.Print(msg_vars, "$ns$$mn$ FUnrealGrpcMarshaler::ToGrpc(const F$mn$& In) {\n");
        printer.Indent();
        printer.Print(msg_vars, "$ns$$mn$ Out;\n\n");

        //  ONEOF CONVERSION
        for (int i = 0; i < msg->oneof_decl_count(); i++) {
            const OneofDescriptor *oneof = msg->oneof_decl(i);
            if (oneof->field(0)->real_containing_oneof() == nullptr) continue;

            std::map<std::string, std::string> oneof_vars = msg_vars;
            oneof_vars["osn"] = oneof->name(); // oneof source name
            oneof_vars["utn"] = ToPascalCase(oneof->name()); // unreal type name

            printer.Print(oneof_vars, "switch (In.$utn$Type) {\n");
            printer.Indent();

            for (int j = 0; j < oneof->field_count(); j++) {
                const FieldDescriptor *current_field_desc = oneof->field(j);

                std::map<std::string, std::string> field_vars = oneof_vars;
                field_vars["unf"] = ToPascalCase(current_field_desc->lowercase_name()); // unreal field name
                field_vars["psn"] = std::string(current_field_desc->lowercase_name()); // proto source name

                if (current_field_desc->type() == FieldDescriptor::TYPE_MESSAGE) {
                    field_vars["val"] = GetToGrpcConversionExpression(current_field_desc, "In." + field_vars["unf"] + ".GetValue()");
                    printer.Print(field_vars,
                                  "case E$mn$$utn$Type::$unf$:\n"
                                  "    *Out.mutable_$psn$() = $val$;\n"
                                  "    break;\n");
                } else {
                    field_vars["val"] = GetToGrpcConversionExpression(current_field_desc, "In." + field_vars["unf"]);
                    printer.Print(field_vars,
                                  "case E$mn$$utn$Type::$unf$:\n"
                                  "    Out.set_$psn$($val$);\n"
                                  "    break;\n");
                }
            }
            printer.Print("default: break;\n}\n");
            printer.Outdent();
        }

        // FIELD CONVERSIONS
        for (int i = 0; i < msg->field_count(); i++) {
            const FieldDescriptor *current_field_desc = msg->field(i);
            if (current_field_desc->containing_oneof() && !current_field_desc->real_containing_oneof()) continue;

            std::map<std::string, std::string> field_vars = msg_vars;
            field_vars["unf"] = ToPascalCase(current_field_desc->lowercase_name()); // unreal field name
            field_vars["psn"] = std::string(current_field_desc->lowercase_name()); // proto source name

            if (current_field_desc->is_map()) {
                const FieldDescriptor *key_field = current_field_desc->message_type()->FindFieldByName("key");
                const FieldDescriptor *val_field = current_field_desc->message_type()->FindFieldByName("value");

                field_vars["ke"] = GetToGrpcConversionExpression(key_field, "Kvp.Key");
                field_vars["ve"] = GetToGrpcConversionExpression(val_field, "Kvp.Value");

                printer.Print(field_vars,
                              "for (const auto& Kvp : In.$unf$) {\n"
                              "    (*Out.mutable_$psn$())[$ke$] = $ve$;\n"
                              "}\n");
            } else if (current_field_desc->is_repeated()) {
                field_vars["ce"] = GetToGrpcConversionExpression(current_field_desc, "Element");
                if (current_field_desc->type() == FieldDescriptor::TYPE_MESSAGE) {
                    printer.Print(field_vars,
                                  "for (const auto& Element : In.$unf$) {\n"
                                  "    *Out.add_$psn$() = $ce$;\n"
                                  "}\n");
                } else {
                    printer.Print(field_vars,
                                  "for (const auto& Element : In.$unf$) {\n"
                                  "    Out.add_$psn$($ce$);\n"
                                  "}\n");
                }
            } else {
                if (current_field_desc->type() == FieldDescriptor::TYPE_MESSAGE) {
                    field_vars["val"] = GetToGrpcConversionExpression(current_field_desc, "In." + field_vars["unf"] + ".GetValue()");
                    printer.Print(field_vars, "if (In.$unf$.IsSet()) *Out.mutable_$psn$() = $val$;\n");
                } else {
                    field_vars["val"] = GetToGrpcConversionExpression(current_field_desc, "In." + field_vars["unf"]);
                    printer.Print(field_vars, "Out.set_$psn$($val$);\n");
                }
            }
        }

        printer.Print("return Out;\n");
        printer.Outdent();
        printer.Print("}\n\n");
    }

    // Collect dependencies to #include.
    // Includes instead of forward declarations to support UE container types.
    static void GenerateDependencyIncludes(const Descriptor *msg, io::Printer &printer) {
        std::set<std::string> dependencies;
        for (int j = 0; j < msg->field_count(); j++) {
            const FieldDescriptor *field = msg->field(j);
            if (field->type() != FieldDescriptor::TYPE_MESSAGE) continue;
            const Descriptor *referenced_msg = (field->is_map()
                                                    ? field->message_type()->FindFieldByName("value")->message_type()
                                                    : field->message_type());

            if (referenced_msg && referenced_msg->name() != msg->name()) {
                if (dependencies.insert(std::string(referenced_msg->name())).second) {
                    printer.Print("#include \"F$d$.h\"\n", "d", std::string(referenced_msg->name()));
                }
            }
        }
    }

    bool Generate(const FileDescriptor *file, const std::string &parameter, GeneratorContext *context,
                  std::string *error) const override {
        std::string api_macro = "";
        std::vector<std::pair<std::string, std::string>> options;
        google::protobuf::compiler::ParseGeneratorParameter(parameter, &options);
        for (const auto& opt : options) {
            if (opt.first == "api_name") {
                api_macro = opt.second;
            }
        }
        std::string macro_with_space = api_macro.empty() ? "" : api_macro + " ";

        auto base_filename = ToPascalCase(std::string(file->name()));
        if (base_filename.find_last_of('.') != std::string::npos)
            base_filename = base_filename.substr(
                0, base_filename.find_last_of('.'));
        // Convert protobuf package to cpp namespace. eg game.data -> ::game::data::
        std::string proto_namespace = "::";
        if (!file->package().empty()) {
            proto_namespace = "::" + absl::StrReplaceAll(file->package(), {{".", "::"}}) + "::";
        }

        // UENUM header generation
        // This file contains all UENUM definitions found in the proto file and its nested messages.
        std::string enum_header = base_filename + "Enums.h";
        const std::unique_ptr<io::ZeroCopyOutputStream> enum_header_stream(context->Open(enum_header));
        io::Printer enum_printer(enum_header_stream.get(), '$');
        enum_printer.Print({{"b", enum_header}, {"gc", kGeneratedComment}},
                           "$gc$\n"
                           "#pragma once\n"
                           "#include \"CoreMinimal.h\"\n");
        for (int i = 0; i < file->enum_type_count(); i++) {
            GenerateEnum(file->enum_type(i), enum_printer);
        }
        for (int i = 0; i < file->message_type_count(); i++) {
            GenerateAllEnumsInMessageTree(file->message_type(i), enum_printer);
        }

        // USTRUCT generation
        // Generate a header for each message type.
        for (int i = 0; i < file->message_type_count(); i++) {
            const Descriptor *msg = file->message_type(i);
            // Skip internal protobuf map-helper messages
            if (msg->options().map_entry()) continue;

            const std::string struct_filename = "F" + std::string(msg->name()) + ".h";
            const std::unique_ptr<io::ZeroCopyOutputStream> struct_stream(context->Open(struct_filename));
            io::Printer struct_printer(struct_stream.get(), '$');

            //comment, pragma, base includes
            struct_printer.Print({{"gc", kGeneratedComment},{"eh", enum_header}},
                                 "$gc$\n"
                                 "#pragma once\n"
                                 "#include \"CoreMinimal.h\"\n"
                                 "#include \"$eh$\"\n");
            //all dependency includes
            GenerateDependencyIncludes(msg, struct_printer);
            //generated include is last
            struct_printer.Print({{"n", std::string(msg->name())}},
                                 "#include \"F$n$.generated.h\"\n\n");
            //write struct content
            GenerateUstruct(msg, struct_printer, macro_with_space);
        }

        // Marshaller Header Generation
        const std::string marshaller_header = base_filename + "Marshaller.h";
        const std::unique_ptr<io::ZeroCopyOutputStream> marshaller_header_stream(context->Open(marshaller_header));
        io::Printer marshaller_header_printer(marshaller_header_stream.get(), '$');
        marshaller_header_printer.Print({{"gc", kGeneratedComment}},
                             "$gc$\n"
                             "#pragma once\n"
                             "#include \"CoreMinimal.h\"\n");

        for (int i = 0; i < file->message_type_count(); i++) {
            if (!file->message_type(i)->options().map_entry()) {
                marshaller_header_printer.Print("#include \"F$n$.h\"\n", "n", std::string(file->message_type(i)->name()));
            }
        }
        marshaller_header_printer.Print("\n");

        // Forward declare all proto messages in the header to avoid leaking full pb.h
        // We use full names with namespaces
        for (int i = 0; i < file->message_type_count(); i++) {
            const Descriptor *msg = file->message_type(i);
            if (msg->options().map_entry()) continue;

            size_t last_dot = msg->full_name().find_last_of('.');
            if (last_dot != std::string::npos) {
                std::string ns = absl::StrReplaceAll(msg->full_name().substr(0, last_dot), {{".", "::"}});
                marshaller_header_printer.Print("namespace $ns$ { class $n$; }\n",
                                     "ns", ns,
                                     "n", msg->name());
            } else {
                marshaller_header_printer.Print("class $n$;\n", "n", msg->name());
            }
        }

        marshaller_header_printer.Print("\nclass $m$FUnrealGrpcMarshaler {\npublic:\n", "m", macro_with_space);
        marshaller_header_printer.Indent();

        for (int i = 0; i < file->message_type_count(); i++) {
            const Descriptor *msg = file->message_type(i);
            if (msg->options().map_entry()) continue;

            std::string full_name = "::" + absl::StrReplaceAll(msg->full_name(), {{".", "::"}});
            marshaller_header_printer.Print("static F$n$ ToUnreal(const $fn$& In);\n",
                                 "fn", full_name,
                                 "n", msg->name());
            marshaller_header_printer.Print("static $fn$ ToGrpc(const F$n$& In);\n",
                                 "fn", full_name,
                                 "n", msg->name());
        }

        marshaller_header_printer.Outdent();
        marshaller_header_printer.Print("};\n");

        // Marshall Function Generation. This is one massive static class with functions to convert all proto cpp messages to ustructs
        // Generate a static function per message to convert from backing cpp type to USTRUCT type
        const std::string marshaller_cpp = base_filename + "Marshaller.cpp";
        const std::unique_ptr<io::ZeroCopyOutputStream> marshaller_cpp_stream(context->Open(marshaller_cpp));
        io::Printer marshaller_cpp_printer(marshaller_cpp_stream.get(), '$');
        // Default includes and pragma
        std::string proto_header = std::string(file->name());
        size_t last_dot_file = proto_header.find_last_of('.');
        if (last_dot_file != std::string::npos) {
            proto_header = proto_header.substr(0, last_dot_file);
        }
        proto_header += ".pb.h";

        marshaller_cpp_printer.Print({{"gc", kGeneratedComment},{"ch", marshaller_header}, {"ph", proto_header}},
                                    "$gc$\n"
                                    "#include \"$ch$\"\n"
                                    "#include \"$ph$\"\n");

        for (int i = 0; i < file->message_type_count(); i++) {
            if (!file->message_type(i)->options().map_entry()) {
                marshaller_cpp_printer.Print("#include \"F$n$.h\"\n", "n", std::string(file->message_type(i)->name()));
            }
        }
        marshaller_cpp_printer.Print("\n");

        for (int i = 0; i < file->message_type_count(); i++) {
            //skip backing map entries.
            if (!file->message_type(i)->options().map_entry()) {
                GenerateConversionFunction(file->message_type(i), marshaller_cpp_printer, proto_namespace);
                GenerateToGrpcConversionFunction(file->message_type(i), marshaller_cpp_printer, proto_namespace);
            }
        }
        return true;
    }
};

int main(int argc, char *argv[]) {
    const UnrealGenerator generator;
    return PluginMain(argc, argv, &generator);
}
