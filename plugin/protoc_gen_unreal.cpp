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

using namespace google::protobuf;
using namespace google::protobuf::compiler;

static constexpr std::string_view kUPropVisible = "UPROPERTY(VisibleAnywhere, BlueprintReadOnly)\n";
static constexpr std::string_view kUstructDeclaration = "USTRUCT(BlueprintType)\n";
static constexpr std::string_view kConverterClassName = "ProtoToUStructConverter";

class UnrealGenerator final : public CodeGenerator {
public:
    [[nodiscard]] uint64_t GetSupportedFeatures() const override { return FEATURE_PROTO3_OPTIONAL; }

    static std::string ToPascalCase(absl::string_view input) {
        std::string result;
        bool bNextUpper = true;
        for (const char c : input) {
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

    static std::string GetBaseUEType(const FieldDescriptor* field) {
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

    static std::string GetUEType(const FieldDescriptor* field) {
        if (field->is_map()) {
            const Descriptor* entry = field->message_type();
            return "TMap<" + GetBaseUEType(entry->FindFieldByName("key")) + ", " + GetBaseUEType(entry->FindFieldByName("value")) + ">";
        }
        std::string base = GetBaseUEType(field);
        if (field->is_repeated()) return "TArray<" + base + ">";
        if (field->has_presence()) return "TOptional<" + base + ">";
        return base;
    }

    static void GenerateEnum(const EnumDescriptor* enum_desc, io::Printer& printer) {
        printer.Print({{"n", std::string(enum_desc->name())}},
            "UENUM(BlueprintType)\nenum class E$n$ : uint8 {\n");
        printer.Indent();
        for (int j = 0; j < enum_desc->value_count(); j++) {
            printer.Print({{"v", ToPascalCase(enum_desc->value(j)->name())}, {"num", std::to_string(enum_desc->value(j)->number())}},
                "$v$ = $num$,\n");
        }
        printer.Outdent();
        printer.Print("};\n\n");
    }

    static void GenerateNestedEnums(const Descriptor* msg, io::Printer& printer) {
        if (msg->options().map_entry()) return;
        for (int i = 0; i < msg->enum_type_count(); i++)
            GenerateEnum(msg->enum_type(i), printer);
        for (int i = 0; i < msg->nested_type_count(); i++)
            GenerateNestedEnums(msg->nested_type(i), printer);
    }

    static void GenerateOneofEnum(const Descriptor *msg, io::Printer &printer, const std::string &msg_name) {
        for (int i = 0; i < msg->oneof_decl_count(); i++) {
            const OneofDescriptor* oneof = msg->oneof_decl(i);
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

    static void GenerateStruct(const Descriptor* msg, io::Printer& printer) {
        auto msg_name = std::string(msg->name());
        //oneof enum declaration is outside the struct. UE cannot declare UENUMS within struct bodies
        GenerateOneofEnum(msg, printer, msg_name);

        printer.Print({{"n", msg_name}, {"us", kUstructDeclaration}},
            "$us$struct F$n$ {\n");
        printer.Indent();
        printer.Print(
            "GENERATED_BODY()\n\n");
        for (int i = 0; i < msg->oneof_decl_count(); i++) {
            const OneofDescriptor* oneof = msg->oneof_decl(i);
            //check for synthetic oneofs
            if (oneof->field(0)->real_containing_oneof() == nullptr) continue;
            //add the oneof enums.
            std::string oneof_enum_name = msg_name + ToPascalCase(oneof->name());
            printer.Print({{"en", oneof_enum_name}, {"up", kUPropVisible.data()},{"sn", ToPascalCase(oneof->name())}},
                "$up$E$en$Type $sn$Type = E$en$Type::None;\n\n");
        }
        for (int j = 0; j < msg->field_count(); j++) {
            const FieldDescriptor* f = msg->field(j);
            if (f->real_containing_oneof() == nullptr && f->containing_oneof() != nullptr) continue;
            printer.Print({{"t", GetUEType(f)}, {"up", kUPropVisible.data()},{"n", ToPascalCase(f->name())}},
                          "$up$$t$ $n$;\n\n");
        }
        printer.Outdent();
        printer.Print("};\n");
    }

    //generate a static conversion function. this is wrapped in the plugin that converts the raw messages to
    static void GenerateStaticConversionFunction(const Descriptor* msg, io::Printer& printer, const std::string& name_space) {
        printer.Print({{"n", std::string(msg->name())}, {"ns", name_space}, {"cn", kConverterClassName}},
            "F$n$ $cn$::Convert(const $ns$$n$& In) {\n");
        printer.Indent();
        printer.Print({{"n", std::string(msg->name())}},
            "F$n$ Out;\n");

        for (int i = 0; i < msg->oneof_decl_count(); i++) {
            const OneofDescriptor* oneof = msg->oneof_decl(i);
            if (oneof->field(0)->real_containing_oneof() == nullptr) continue;
            std::string un_oneof = ToPascalCase(oneof->name());
            printer.Print({{"pn", std::string(oneof->name())}, {"un", un_oneof}, {"ns", name_space}, {"mn", std::string(msg->name())}},
                "switch (In.$pn$_case()) {\n");
            printer.Indent();
            for (int j = 0; j < oneof->field_count(); j++) {
                const FieldDescriptor* f = oneof->field(j);
                auto low_name = std::string(f->name());
                std::ranges::transform(low_name, low_name.begin(), ::tolower);
                std::map<std::string, std::string> string_vars = {
                    {"un_f", ToPascalCase(f->name())}, {"un_t", un_oneof}, {"pn_f", low_name},
                    {"ns", name_space}, {"mn", std::string(msg->name())}, {"cn", kConverterClassName.data()}, {"et", GetBaseUEType(f)}
                };
                printer.Print(string_vars,
                    "case $ns$$mn$::k$un_f$: \n");
                printer.Indent();
                if (f->type() == FieldDescriptor::TYPE_MESSAGE) printer.Print(string_vars,
                    "Out.$un_f$ = $cn$::Convert(In.$pn_f$());\n");
                else if (f->type() == FieldDescriptor::TYPE_STRING) printer.Print(string_vars,
                    "Out.$un_f$ = FString(UTF8_TO_TCHAR(In.$pn_f$().c_str()));\n");
                else if (f->type() == FieldDescriptor::TYPE_ENUM) printer.Print(string_vars,
                    "Out.$un_f$ = static_cast<$et$>(In.$pn_f$());\n");
                else printer.Print(string_vars,
                    "Out.$un_f$ = In.$pn_f$();\n");
                printer.Print(string_vars,
                    "Out.$un_t$Type = E$mn$$un_t$Type::$un_f$;\nbreak;\n");
                printer.Outdent();
            }
            printer.Print("default: break;\n}\n");
            printer.Outdent();
        }

        for (int j = 0; j < msg->field_count(); j++) {
            const FieldDescriptor* f = msg->field(j);
            if (f->containing_oneof() != nullptr && f->real_containing_oneof() == nullptr) continue;
            auto low_name = std::string(f->name());
            std::ranges::transform(low_name, low_name.begin(), ::tolower);
            std::map<std::string, std::string> printer_vars = {{"un", ToPascalCase(f->name())}, {"pn", low_name}, {"cn", kConverterClassName.data()}, {"et", GetBaseUEType(f)}};

            if (f->is_map()) {
                const FieldDescriptor* vf = f->message_type()->FindFieldByName("value");
                printer.Print(printer_vars,
                    "for (const auto& P : In.$pn$()) {\n");
                printer.Indent();
                if (vf->type() == FieldDescriptor::TYPE_MESSAGE) printer.Print(printer_vars,
                    "Out.$un$.Add(P.first, $cn$::Convert(P.second));\n");
                else if (vf->type() == FieldDescriptor::TYPE_STRING) printer.Print(printer_vars,
                    "Out.$un$.Add(P.first, FString(UTF8_TO_TCHAR(P.second.c_str())));\n");
                else if (vf->type() == FieldDescriptor::TYPE_ENUM) printer.Print({{"un", printer_vars["un"]}, {"vet", "E" + std::string(vf->enum_type()->name())}},
                    "Out.$un$.Add(P.first, static_cast<$vet$>(P.second));\n");
                else printer.Print(printer_vars,
                    "Out.$un$.Add(P.first, P.second);\n");
                printer.Outdent(); printer.Print("}\n");
            } else if (f->is_repeated()) {
                printer.Print(printer_vars,
                    "for (const auto& E : In.$pn$()) {\n");
                printer.Indent();
                if (f->type() == FieldDescriptor::TYPE_MESSAGE) printer.Print(printer_vars,
                    "Out.$un$.Add($cn$::Convert(E));\n");
                else if (f->type() == FieldDescriptor::TYPE_STRING) printer.Print(printer_vars,
                    "Out.$un$.Add(FString(UTF8_TO_TCHAR(E.c_str())));\n");
                else if (f->type() == FieldDescriptor::TYPE_ENUM) printer.Print(printer_vars,
                    "Out.$un$.Add(static_cast<$et$>(E));\n");
                else printer.Print(printer_vars, "Out.$un$.Add(E);\n");
                printer.Outdent(); printer.Print("}\n");
            } else if (f->type() == FieldDescriptor::TYPE_MESSAGE) printer.Print(printer_vars,
                "if (In.has_$pn$()) Out.$un$ = $cn$::Convert(In.$pn$());\n");
            else if (f->type() == FieldDescriptor::TYPE_STRING) printer.Print(printer_vars,
                "Out.$un$ = FString(UTF8_TO_TCHAR(In.$pn$().c_str()));\n");
            else if (f->type() == FieldDescriptor::TYPE_ENUM) printer.Print(printer_vars,
                "Out.$un$ = static_cast<$et$>(In.$pn$());\n");
            else printer.Print(printer_vars,
                "Out.$un$ = In.$pn$();\n");
        }
        printer.Print("return Out;\n");
        printer.Outdent(); printer.Print("}\n\n");
    }

    bool Generate(const FileDescriptor* file, const std::string& parameter, GeneratorContext* context, std::string* error) const override {
        auto base_filename = ToPascalCase(std::string(file->name()));
        if (base_filename.find_last_of('.') != std::string::npos) base_filename = base_filename.substr(0, base_filename.find_last_of('.'));
        std::string proto_ns = file->package().empty() ? "::" : "::" + std::string(file->package()) + "::";

        std::string enum_h = base_filename + "Enums.h";
        const std::unique_ptr<io::ZeroCopyOutputStream> e_out(context->Open(enum_h));
        io::Printer e_p(e_out.get(), '$');
        e_p.Print({{"b", base_filename}},
            "#pragma once\n#include \"CoreMinimal.h\"\n#include \"$b$Enums.generated.h\"\n\n");
        for (int i = 0; i < file->enum_type_count(); i++) GenerateEnum(file->enum_type(i), e_p);
        for (int i = 0; i < file->message_type_count(); i++) GenerateNestedEnums(file->message_type(i), e_p);

        for (int i = 0; i < file->message_type_count(); i++) {
            const Descriptor* msg = file->message_type(i);
            if (msg->options().map_entry()) continue;
            const std::unique_ptr<io::ZeroCopyOutputStream> m_out(context->Open("F" + std::string(msg->name()) + ".h"));
            io::Printer m_p(m_out.get(), '$');
            m_p.Print({{"eh", enum_h}},
                "#pragma once\n#include \"CoreMinimal.h\"\n#include \"$eh$\"\n");
            std::set<std::string> deps;
            for (int j = 0; j < msg->field_count(); j++) {
                const FieldDescriptor* f = msg->field(j);
                const Descriptor* target = (f->type() == FieldDescriptor::TYPE_MESSAGE) ? (f->is_map() ? f->message_type()->FindFieldByName("value")->message_type() : f->message_type()) : nullptr;
                if (target && target->name() != msg->name() && deps.insert(std::string(target->name())).second) m_p.Print("#include \"F$d$.h\"\n", "d",
                    std::string(target->name()));
            }
            m_p.Print({{"n", std::string(msg->name())}},
                "#include \"F$n$.generated.h\"\n\n");
            GenerateStruct(msg, m_p);
        }

        const std::unique_ptr<io::ZeroCopyOutputStream> ch_out(context->Open(base_filename + "Converter.h"));
        io::Printer converter_h_printer(ch_out.get(), '$');
        converter_h_printer.Print({{"b", base_filename}}, "#pragma once\n#include \"CoreMinimal.h\"\n#include \"$b$.pb.h\"\n");
        for (int i = 0; i < file->message_type_count(); i++) if (!file->message_type(i)->options().map_entry()) converter_h_printer.Print("#include \"F$n$.h\"\n", "n", std::string(file->message_type(i)->name()));
        converter_h_printer.Print({{"cn", kConverterClassName}}, "\nclass $cn$ {\n");
        converter_h_printer.Indent();
        for (int i = 0; i < file->message_type_count(); i++) if (!file->message_type(i)->options().map_entry()) converter_h_printer.Print({{"n", std::string(file->message_type(i)->name())}, {"ns", proto_ns}}, "static F$n$ Convert(const $ns$$n$& In);\n");
        converter_h_printer.Outdent(); converter_h_printer.Print("};\n");

        const std::unique_ptr<io::ZeroCopyOutputStream> cpp_out(context->Open(base_filename + "Converter.cpp"));
        io::Printer converter_cpp_printer(cpp_out.get(), '$');
        converter_cpp_printer.Print({{"b", base_filename}}, "#include \"$b$Converter.h\"\n#include \"$b$.pb.h\"\n");
        for (int i = 0; i < file->message_type_count(); i++) if (!file->message_type(i)->options().map_entry()) GenerateStaticConversionFunction(file->message_type(i), converter_cpp_printer, proto_ns);
        return true;
    }
};

int main(int argc, char *argv[]) {
    const UnrealGenerator generator;
    return PluginMain(argc, argv, &generator);
}
