// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/substitution_writer.h"

#include "tools/gn/build_settings.h"
#include "tools/gn/escape.h"
#include "tools/gn/filesystem_utils.h"
#include "tools/gn/output_file.h"
#include "tools/gn/settings.h"
#include "tools/gn/source_file.h"
#include "tools/gn/string_utils.h"
#include "tools/gn/substitution_list.h"
#include "tools/gn/substitution_pattern.h"
#include "tools/gn/target.h"

namespace {

// Sets the given directory string to the destination, trimming any trailing
// slash from the directory (SourceDirs and OutputFiles representing
// directories will end in a trailing slash). If the directory is empty,
// it will be replaced with a ".".
void SetDirOrDotWithNoSlash(const std::string& dir, std::string* dest) {
  if (!dir.empty() && dir[dir.size() - 1] == '/')
    dest->assign(dir.data(), dir.size() - 1);
  else
    dest->assign(dir);

  if (dest->empty())
    dest->push_back('.');
}

}  // namespace

const char kSourceExpansion_Help[] =
    "How Source Expansion Works\n"
    "\n"
    "  Source expansion is used for the action_foreach and copy target types\n"
    "  to map source file names to output file names or arguments.\n"
    "\n"
    "  To perform source expansion in the outputs, GN maps every entry in the\n"
    "  sources to every entry in the outputs list, producing the cross\n"
    "  product of all combinations, expanding placeholders (see below).\n"
    "\n"
    "  Source expansion in the args works similarly, but performing the\n"
    "  placeholder substitution produces a different set of arguments for\n"
    "  each invocation of the script.\n"
    "\n"
    "  If no placeholders are found, the outputs or args list will be treated\n"
    "  as a static list of literal file names that do not depend on the\n"
    "  sources.\n"
    "\n"
    "  See \"gn help copy\" and \"gn help action_foreach\" for more on how\n"
    "  this is applied.\n"
    "\n"
    "Placeholders\n"
    "\n"
    "  {{source}}\n"
    "      The name of the source file including directory (*). This will\n"
    "      generally be used for specifying inputs to a script in the\n"
    "      \"args\" variable.\n"
    "        \"//foo/bar/baz.txt\" => \"../../foo/bar/baz.txt\"\n"
    "\n"
    "  {{source_file_part}}\n"
    "      The file part of the source including the extension.\n"
    "        \"//foo/bar/baz.txt\" => \"baz.txt\"\n"
    "\n"
    "  {{source_name_part}}\n"
    "      The filename part of the source file with no directory or\n"
    "      extension. This will generally be used for specifying a\n"
    "      transformation from a source file to a destination file with the\n"
    "      same name but different extension.\n"
    "        \"//foo/bar/baz.txt\" => \"baz\"\n"
    "\n"
    "  {{source_dir}}\n"
    "      The directory (*) containing the source file with no\n"
    "      trailing slash.\n"
    "        \"//foo/bar/baz.txt\" => \"../../foo/bar\"\n"
    "\n"
    "  {{source_root_relative_dir}}\n"
    "      The path to the source file's directory relative to the source\n"
    "      root, with no leading \"//\" or trailing slashes. If the path is\n"
    "      system-absolute, (beginning in a single slash) this will just\n"
    "      return the path with no trailing slash. This value will always\n"
    "      be the same, regardless of whether it appears in the \"outputs\"\n"
    "      or \"args\" section.\n"
    "        \"//foo/bar/baz.txt\" => \"foo/bar\"\n"
    "\n"
    "  {{source_gen_dir}}\n"
    "      The generated file directory (*) corresponding to the source\n"
    "      file's path. This will be different than the target's generated\n"
    "      file directory if the source file is in a different directory\n"
    "      than the BUILD.gn file.\n"
    "        \"//foo/bar/baz.txt\" => \"gen/foo/bar\"\n"
    "\n"
    "  {{source_out_dir}}\n"
    "      The object file directory (*) corresponding to the source file's\n"
    "      path, relative to the build directory. this us be different than\n"
    "      the target's out directory if the source file is in a different\n"
    "      directory than the build.gn file.\n"
    "        \"//foo/bar/baz.txt\" => \"obj/foo/bar\"\n"
    "\n"
    "(*) Note on directories\n"
    "\n"
    "  Paths containing directories (except the source_root_relative_dir)\n"
    "  will be different depending on what context the expansion is evaluated\n"
    "  in. Generally it should \"just work\" but it means you can't\n"
    "  concatenate strings containing these values with reasonable results.\n"
    "\n"
    "  Details: source expansions can be used in the \"outputs\" variable,\n"
    "  the \"args\" variable, and in calls to \"process_file_template\". The\n"
    "  \"args\" are passed to a script which is run from the build directory,\n"
    "  so these directories will relative to the build directory for the\n"
    "  script to find. In the other cases, the directories will be source-\n"
    "  absolute (begin with a \"//\") because the results of those expansions\n"
    "  will be handled by GN internally.\n"
    "\n"
    "Examples\n"
    "\n"
    "  Non-varying outputs:\n"
    "    action(\"hardcoded_outputs\") {\n"
    "      sources = [ \"input1.idl\", \"input2.idl\" ]\n"
    "      outputs = [ \"$target_out_dir/output1.dat\",\n"
    "                  \"$target_out_dir/output2.dat\" ]\n"
    "    }\n"
    "  The outputs in this case will be the two literal files given.\n"
    "\n"
    "  Varying outputs:\n"
    "    action_foreach(\"varying_outputs\") {\n"
    "      sources = [ \"input1.idl\", \"input2.idl\" ]\n"
    "      outputs = [ \"{{source_gen_dir}}/{{source_name_part}}.h\",\n"
    "                  \"{{source_gen_dir}}/{{source_name_part}}.cc\" ]\n"
    "    }\n"
    "  Performing source expansion will result in the following output names:\n"
    "    //out/Debug/obj/mydirectory/input1.h\n"
    "    //out/Debug/obj/mydirectory/input1.cc\n"
    "    //out/Debug/obj/mydirectory/input2.h\n"
    "    //out/Debug/obj/mydirectory/input2.cc\n";

// static
void SubstitutionWriter::WriteWithNinjaVariables(
    const SubstitutionPattern& pattern,
    const EscapeOptions& escape_options,
    std::ostream& out) {
  // The result needs to be quoted as if it was one string, but the $ for
  // the inserted Ninja variables can't be escaped. So write to a buffer with
  // no quoting, and then quote the whole thing if necessary.
  EscapeOptions no_quoting(escape_options);
  no_quoting.inhibit_quoting = true;

  bool needs_quotes = false;
  std::string result;
  for (const auto& range : pattern.ranges()) {
    if (range.type == SUBSTITUTION_LITERAL) {
      result.append(EscapeString(range.literal, no_quoting, &needs_quotes));
    } else {
      result.append("${");
      result.append(kSubstitutionNinjaNames[range.type]);
      result.append("}");
    }
  }

  if (needs_quotes && !escape_options.inhibit_quoting)
    out << "\"" << result << "\"";
  else
    out << result;
}

// static
void SubstitutionWriter::GetListAsSourceFiles(
    const SubstitutionList& list,
    std::vector<SourceFile>* output) {
  for (const auto& pattern : list.list()) {
    CHECK(pattern.ranges().size() == 1 &&
          pattern.ranges()[0].type == SUBSTITUTION_LITERAL)
        << "The substitution pattern \""
        << pattern.AsString()
        << "\" was expected to be a literal with no {{substitutions}}.";
    const std::string& literal = pattern.ranges()[0].literal;
    CHECK(literal.size() >= 1 && literal[0] == '/')
        << "The result of the pattern \""
        << pattern.AsString()
        << "\" was not an absolute path.";
    output->push_back(SourceFile(literal));
  }
}

// static
void SubstitutionWriter::GetListAsOutputFiles(
    const Settings* settings,
    const SubstitutionList& list,
    std::vector<OutputFile>* output) {
  std::vector<SourceFile> output_as_sources;
  GetListAsSourceFiles(list, &output_as_sources);
  for (const auto& file : output_as_sources)
    output->push_back(OutputFile(settings->build_settings(), file));
}

// static
SourceFile SubstitutionWriter::ApplyPatternToSource(
      const Settings* settings,
      const SubstitutionPattern& pattern,
      const SourceFile& source) {
  std::string result_value = ApplyPatternToSourceAsString(
      settings, pattern, source);
  CHECK(!result_value.empty() && result_value[0] == '/')
      << "The result of the pattern \""
      << pattern.AsString()
      << "\" was not a path beginning in \"/\" or \"//\".";
  return SourceFile(SourceFile::SWAP_IN, &result_value);
}

// static
std::string SubstitutionWriter::ApplyPatternToSourceAsString(
    const Settings* settings,
    const SubstitutionPattern& pattern,
    const SourceFile& source) {
  std::string result_value;
  for (const auto& subrange : pattern.ranges()) {
    if (subrange.type == SUBSTITUTION_LITERAL) {
      result_value.append(subrange.literal);
    } else {
      result_value.append(
          GetSourceSubstitution(settings, source, subrange.type,
                                OUTPUT_ABSOLUTE, SourceDir()));
    }
  }
  return result_value;
}

// static
OutputFile SubstitutionWriter::ApplyPatternToSourceAsOutputFile(
    const Settings* settings,
    const SubstitutionPattern& pattern,
    const SourceFile& source) {
  SourceFile result_as_source = ApplyPatternToSource(settings, pattern, source);
  return OutputFile(settings->build_settings(), result_as_source);
}

// static
void SubstitutionWriter::ApplyListToSource(
    const Settings* settings,
    const SubstitutionList& list,
    const SourceFile& source,
    std::vector<SourceFile>* output) {
  for (const auto& item : list.list())
    output->push_back(ApplyPatternToSource(settings, item, source));
}

// static
void SubstitutionWriter::ApplyListToSourceAsString(
    const Settings* settings,
    const SubstitutionList& list,
    const SourceFile& source,
    std::vector<std::string>* output) {
  for (const auto& item : list.list())
    output->push_back(ApplyPatternToSourceAsString(settings, item, source));
}

// static
void SubstitutionWriter::ApplyListToSourceAsOutputFile(
    const Settings* settings,
    const SubstitutionList& list,
    const SourceFile& source,
    std::vector<OutputFile>* output) {
  for (const auto& item : list.list())
    output->push_back(ApplyPatternToSourceAsOutputFile(settings, item, source));
}

// static
void SubstitutionWriter::ApplyListToSources(
    const Settings* settings,
    const SubstitutionList& list,
    const std::vector<SourceFile>& sources,
    std::vector<SourceFile>* output) {
  output->clear();
  for (const auto& source : sources)
    ApplyListToSource(settings, list, source, output);
}

// static
void SubstitutionWriter::ApplyListToSourcesAsString(
    const Settings* settings,
    const SubstitutionList& list,
    const std::vector<SourceFile>& sources,
    std::vector<std::string>* output) {
  output->clear();
  for (const auto& source : sources)
    ApplyListToSourceAsString(settings, list, source, output);
}

// static
void SubstitutionWriter::ApplyListToSourcesAsOutputFile(
    const Settings* settings,
    const SubstitutionList& list,
    const std::vector<SourceFile>& sources,
    std::vector<OutputFile>* output) {
  output->clear();
  for (const auto& source : sources)
    ApplyListToSourceAsOutputFile(settings, list, source, output);
}

// static
void SubstitutionWriter::WriteNinjaVariablesForSource(
    const Settings* settings,
    const SourceFile& source,
    const std::vector<SubstitutionType>& types,
    const EscapeOptions& escape_options,
    std::ostream& out) {
  for (const auto& type : types) {
    // Don't write SOURCE since that just maps to Ninja's $in variable, which
    // is implicit in the rule. RESPONSE_FILE_NAME is written separately
    // only when writing target rules since it can never be used in any
    // other context (like process_file_template).
    if (type != SUBSTITUTION_SOURCE && type != SUBSTITUTION_RSP_FILE_NAME) {
      out << "  " << kSubstitutionNinjaNames[type] << " = ";
        EscapeStringToStream(
            out,
            GetSourceSubstitution(settings, source, type, OUTPUT_RELATIVE,
                                  settings->build_settings()->build_dir()),
            escape_options);
      out << std::endl;
    }
  }
}

// static
std::string SubstitutionWriter::GetSourceSubstitution(
    const Settings* settings,
    const SourceFile& source,
    SubstitutionType type,
    OutputStyle output_style,
    const SourceDir& relative_to) {
  std::string to_rebase;
  switch (type) {
    case SUBSTITUTION_SOURCE:
      if (source.is_system_absolute())
        return source.value();
      to_rebase = source.value();
      break;

    case SUBSTITUTION_SOURCE_NAME_PART:
      return FindFilenameNoExtension(&source.value()).as_string();

    case SUBSTITUTION_SOURCE_FILE_PART:
      return source.GetName();

    case SUBSTITUTION_SOURCE_DIR:
      if (source.is_system_absolute())
        return DirectoryWithNoLastSlash(source.GetDir());
      to_rebase = DirectoryWithNoLastSlash(source.GetDir());
      break;

    case SUBSTITUTION_SOURCE_ROOT_RELATIVE_DIR:
      if (source.is_system_absolute())
        return DirectoryWithNoLastSlash(source.GetDir());
      return RebasePath(
          DirectoryWithNoLastSlash(source.GetDir()), SourceDir("//"),
          settings->build_settings()->root_path_utf8());

    case SUBSTITUTION_SOURCE_GEN_DIR:
      to_rebase = DirectoryWithNoLastSlash(
          GetGenDirForSourceDir(settings, source.GetDir()));
      break;

    case SUBSTITUTION_SOURCE_OUT_DIR:
      to_rebase = DirectoryWithNoLastSlash(
          GetOutputDirForSourceDir(settings, source.GetDir()));
      break;

    default:
      NOTREACHED()
          << "Unsupported substitution for this function: "
          << kSubstitutionNames[type];
      return std::string();
  }

  // If we get here, the result is a path that should be made relative or
  // absolute according to the output_style. Other cases (just file name or
  // extension extraction) will have been handled via early return above.
  if (output_style == OUTPUT_ABSOLUTE)
    return to_rebase;
  return RebasePath(to_rebase, relative_to,
                    settings->build_settings()->root_path_utf8());
}

// static
OutputFile SubstitutionWriter::ApplyPatternToTargetAsOutputFile(
    const Target* target,
    const Tool* tool,
    const SubstitutionPattern& pattern) {
  std::string result_value;
  for (const auto& subrange : pattern.ranges()) {
    if (subrange.type == SUBSTITUTION_LITERAL) {
      result_value.append(subrange.literal);
    } else {
      std::string subst;
      CHECK(GetTargetSubstitution(target, subrange.type, &subst));
      result_value.append(subst);
    }
  }
  return OutputFile(result_value);
}

// static
void SubstitutionWriter::ApplyListToTargetAsOutputFile(
    const Target* target,
    const Tool* tool,
    const SubstitutionList& list,
    std::vector<OutputFile>* output) {
  for (const auto& item : list.list())
    output->push_back(ApplyPatternToTargetAsOutputFile(target, tool, item));
}

// static
bool SubstitutionWriter::GetTargetSubstitution(
    const Target* target,
    SubstitutionType type,
    std::string* result) {
  switch (type) {
    case SUBSTITUTION_LABEL:
      // Only include the toolchain for non-default toolchains.
      *result = target->label().GetUserVisibleName(
          !target->settings()->is_default());
      break;
    case SUBSTITUTION_LABEL_NAME:
      *result = target->label().name();
      break;
    case SUBSTITUTION_ROOT_GEN_DIR:
      SetDirOrDotWithNoSlash(
          GetToolchainGenDirAsOutputFile(target->settings()).value(),
          result);
      break;
    case SUBSTITUTION_ROOT_OUT_DIR:
      SetDirOrDotWithNoSlash(
          target->settings()->toolchain_output_subdir().value(),
          result);
      break;
    case SUBSTITUTION_TARGET_GEN_DIR:
      SetDirOrDotWithNoSlash(
          GetTargetGenDirAsOutputFile(target).value(),
          result);
      break;
    case SUBSTITUTION_TARGET_OUT_DIR:
      SetDirOrDotWithNoSlash(
          GetTargetOutputDirAsOutputFile(target).value(),
          result);
      break;
    case SUBSTITUTION_TARGET_OUTPUT_NAME:
      *result = target->GetComputedOutputName();
      break;
    default:
      return false;
  }
  return true;
}

// static
std::string SubstitutionWriter::GetTargetSubstitution(
    const Target* target,
    SubstitutionType type) {
  std::string result;
  GetTargetSubstitution(target, type, &result);
  return result;
}

// static
OutputFile SubstitutionWriter::ApplyPatternToCompilerAsOutputFile(
    const Target* target,
    const SourceFile& source,
    const SubstitutionPattern& pattern) {
  OutputFile result;
  for (const auto& subrange : pattern.ranges()) {
    if (subrange.type == SUBSTITUTION_LITERAL) {
      result.value().append(subrange.literal);
    } else {
      result.value().append(
          GetCompilerSubstitution(target, source, subrange.type));
    }
  }
  return result;
}

// static
void SubstitutionWriter::ApplyListToCompilerAsOutputFile(
    const Target* target,
    const SourceFile& source,
    const SubstitutionList& list,
    std::vector<OutputFile>* output) {
  for (const auto& item : list.list())
    output->push_back(ApplyPatternToCompilerAsOutputFile(target, source, item));
}

// static
std::string SubstitutionWriter::GetCompilerSubstitution(
    const Target* target,
    const SourceFile& source,
    SubstitutionType type) {
  // First try the common tool ones.
  std::string result;
  if (GetTargetSubstitution(target, type, &result))
    return result;

  // Fall-through to the source ones.
  return GetSourceSubstitution(
      target->settings(), source, type, OUTPUT_RELATIVE,
      target->settings()->build_settings()->build_dir());
}

// static
OutputFile SubstitutionWriter::ApplyPatternToLinkerAsOutputFile(
    const Target* target,
    const Tool* tool,
    const SubstitutionPattern& pattern) {
  OutputFile result;
  for (const auto& subrange : pattern.ranges()) {
    if (subrange.type == SUBSTITUTION_LITERAL) {
      result.value().append(subrange.literal);
    } else {
      result.value().append(GetLinkerSubstitution(target, tool, subrange.type));
    }
  }
  return result;
}

// static
void SubstitutionWriter::ApplyListToLinkerAsOutputFile(
    const Target* target,
    const Tool* tool,
    const SubstitutionList& list,
    std::vector<OutputFile>* output) {
  for (const auto& item : list.list())
    output->push_back(ApplyPatternToLinkerAsOutputFile(target, tool, item));
}

// static
std::string SubstitutionWriter::GetLinkerSubstitution(
    const Target* target,
    const Tool* tool,
    SubstitutionType type) {
  // First try the common tool ones.
  std::string result;
  if (GetTargetSubstitution(target, type, &result))
    return result;

  // Fall-through to the linker-specific ones.
  switch (type) {
    case SUBSTITUTION_OUTPUT_EXTENSION:
      // Use the extension provided on the target if specified, otherwise
      // fall back on the default. Note that the target's output extension
      // does not include the dot but the tool's does.
      if (!target->output_extension_set())
        return tool->default_output_extension();
      if (target->output_extension().empty())
        return std::string();  // Explicitly set to no extension.
      return std::string(".") + target->output_extension();

    default:
      NOTREACHED();
      return std::string();
  }
}
