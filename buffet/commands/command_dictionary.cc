// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffet/commands/command_dictionary.h"

#include <base/values.h>

#include "buffet/commands/command_definition.h"
#include "buffet/commands/schema_constants.h"
#include "buffet/string_utils.h"

namespace buffet {

std::vector<std::string> CommandDictionary::GetCommandNamesByCategory(
    const std::string& category) const {
  std::vector<std::string> names;
  for (const auto& pair : definitions_) {
    if (pair.second->GetCategory() == category)
      names.push_back(pair.first);
  }
  return names;
}

bool CommandDictionary::LoadCommands(const base::DictionaryValue& json,
                                     const std::string& category,
                                     const CommandDictionary* base_commands,
                                     chromeos::ErrorPtr* error) {
  std::map<std::string, std::shared_ptr<const CommandDefinition>> new_defs;

  // |json| contains a list of nested objects with the following structure:
  // {"<pkg_name>": {"<cmd_name>": {"parameters": {object_schema}}, ...}, ...}
  // Iterate over packages
  base::DictionaryValue::Iterator package_iter(json);
  while (!package_iter.IsAtEnd()) {
    std::string package_name = package_iter.key();
    const base::DictionaryValue* package_value = nullptr;
    if (!package_iter.value().GetAsDictionary(&package_value)) {
      chromeos::Error::AddToPrintf(error, errors::commands::kDomain,
                                   errors::commands::kTypeMismatch,
                                   "Expecting an object for package '%s'",
                                   package_name.c_str());
      return false;
    }
    // Iterate over command definitions within the current package.
    base::DictionaryValue::Iterator command_iter(*package_value);
    while (!command_iter.IsAtEnd()) {
      std::string command_name = command_iter.key();
      if (command_name.empty()) {
        chromeos::Error::AddToPrintf(
            error, errors::commands::kDomain,
            errors::commands::kInvalidCommandName,
            "Unnamed command encountered in package '%s'",
            package_name.c_str());
        return false;
      }
      const base::DictionaryValue* command_value = nullptr;
      if (!command_iter.value().GetAsDictionary(&command_value)) {
        chromeos::Error::AddToPrintf(error, errors::commands::kDomain,
                                     errors::commands::kTypeMismatch,
                                     "Expecting an object for command '%s'",
                                     command_name.c_str());
        return false;
      }
      // Construct the compound command name as "pkg_name.cmd_name".
      std::string full_command_name = string_utils::Join('.', package_name,
                                                         command_name);
      // Get the "parameters" definition of the command and read it into
      // an object schema.
      const base::DictionaryValue* command_schema_def = nullptr;
      if (!command_value->GetDictionaryWithoutPathExpansion(
          commands::attributes::kCommand_Parameters, &command_schema_def)) {
        chromeos::Error::AddToPrintf(
            error, errors::commands::kDomain,
            errors::commands::kPropertyMissing,
            "Command definition '%s' is missing property '%s'",
            full_command_name.c_str(),
            commands::attributes::kCommand_Parameters);
        return false;
      }

      const ObjectSchema* base_def = nullptr;
      if (base_commands) {
        const CommandDefinition* cmd =
            base_commands->FindCommand(full_command_name);
        if (cmd)
          base_def = cmd->GetParameters().get();

        // If the base command dictionary was provided but the command was not
        // found in it, this must be a custom (vendor) command. GCD spec states
        // that all custom command names must begin with "_". Let's enforce
        // this rule here.
        if (!base_def) {
          if (command_name.front() != '_') {
            chromeos::Error::AddToPrintf(
                error, errors::commands::kDomain,
                errors::commands::kInvalidCommandName,
                "The name of custom command '%s' in package '%s'"
                " must start with '_'",
                command_name.c_str(), package_name.c_str());
            return false;
          }
        }
      }

      auto command_schema = std::make_shared<ObjectSchema>();
      if (!command_schema->FromJson(command_schema_def, base_def, error)) {
        chromeos::Error::AddToPrintf(error, errors::commands::kDomain,
                                     errors::commands::kInvalidObjectSchema,
                                     "Invalid definition for command '%s'",
                                     full_command_name.c_str());
        return false;
      }
      auto command_def = std::make_shared<CommandDefinition>(category,
                                                             command_schema);
      new_defs.insert(std::make_pair(full_command_name, command_def));

      command_iter.Advance();
    }
    package_iter.Advance();
  }

  // Verify that newly loaded command definitions do not override existing
  // definitions in another category. This is unlikely, but we don't want to let
  // one vendor daemon to define the same commands already handled by another
  // daemon on the same device.
  for (const auto& pair : new_defs) {
    auto iter = definitions_.find(pair.first);
    if (iter != definitions_.end()) {
        chromeos::Error::AddToPrintf(
            error, errors::commands::kDomain,
            errors::commands::kDuplicateCommandDef,
            "Definition for command '%s' overrides an earlier "
            "definition in category '%s'",
            pair.first.c_str(), iter->second->GetCategory().c_str());
        return false;
    }
  }

  // Now that we successfully loaded all the command definitions,
  // remove previous definitions of commands from the same category.
  std::vector<std::string> names = GetCommandNamesByCategory(category);
  for (const std::string& name : names)
    definitions_.erase(name);

  // Insert new definitions into the global map.
  definitions_.insert(new_defs.begin(), new_defs.end());
  return true;
}

std::unique_ptr<base::DictionaryValue> CommandDictionary::GetCommandsAsJson(
    bool full_schema, chromeos::ErrorPtr* error) const {
  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue);
  for (const auto& pair : definitions_) {
    std::unique_ptr<base::DictionaryValue> definition =
        pair.second->GetParameters()->ToJson(full_schema, error);
    if (!definition) {
      dict.reset();
      return dict;
    }
    auto cmd_name_parts = string_utils::SplitAtFirst(pair.first, '.');
    std::string package_name = cmd_name_parts.first;
    std::string command_name = cmd_name_parts.second;
    base::DictionaryValue* package = nullptr;
    if (!dict->GetDictionaryWithoutPathExpansion(package_name, &package)) {
      // If this is the first time we encounter this package, create a JSON
      // object for it.
      package = new base::DictionaryValue;
      dict->SetWithoutPathExpansion(package_name, package);
    }
    base::DictionaryValue* command_def = new base::DictionaryValue;
    command_def->SetWithoutPathExpansion(
        commands::attributes::kCommand_Parameters, definition.release());
    package->SetWithoutPathExpansion(command_name, command_def);
  }
  return dict;
}

const CommandDefinition* CommandDictionary::FindCommand(
    const std::string& command_name) const {
  auto pair = definitions_.find(command_name);
  return (pair != definitions_.end()) ? pair->second.get() : nullptr;
}

void CommandDictionary::Clear() {
  definitions_.clear();
}

}  // namespace buffet
