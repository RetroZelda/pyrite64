/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: localization (docs/todo/dialog/PLAN.md, sections 4.3 to 4.5). A project's locale folder on
// disk, and the scan that finds every key the project uses.
#pragma once
#include <string>
#include <vector>

#include "localization.h"
#include "locCompile.h"

namespace Project
{
  class Project;
}

namespace Project::Loc
{
  /** <project>/locale */
  std::string localeDir(const std::string &projectPath);

  /** Whether the project has a locale/locale.json. */
  bool hasLocalization(const std::string &projectPath);

  /**
   * Reads locale.json and every language's table. Returns false when the project has no
   * locale.json; problems in the files go to 'errors'.
   */
  bool load(const std::string &projectPath, Localization &out, std::vector<std::string> &errors);

  /** Writes locale.json and the tables of every stored (not pseudo) language, only the files that changed. */
  bool save(const std::string &projectPath, const Localization &loc);

  /**
   * Makes locale/ with a locale.json naming one source language, and its empty table.
   * Does nothing when the project already has one.
   */
  bool create(const std::string &projectPath, const std::string &sourceCode, const std::string &sourceName);

  /**
   * Every key the project uses:
   *   "key"_localized in src/user (comments are skipped),
   *   LocText fields of code components in scenes and prefabs,
   *   and string overrides on prefab instances that look like keys (a dot and a letter).
   * Dialog trees and the speakers list join in phase 3.
   */
  Uses scanProject(Project &project, std::vector<std::string> &errors);

  /** Every P64_LOC_VAR and P64_LOC_ARG in src/user. */
  VarDecls scanVariables(Project &project, std::vector<std::string> &errors);

  struct RefreshResult
  {
    std::vector<std::string> added{};  // keys new to the tables
    std::vector<std::string> unused{}; // keys nothing uses (not removed)
    std::vector<std::string> errors{};
    Uses uses{};
  };

  /** Scans the project and adds the missing keys to every table, then saves. */
  RefreshResult refresh(Project &project, Localization &loc);

  /**
   * The editor's copy of the open project's localization, read again when a file in locale/
   * changed on disk (checked at most every 2 s). Edits go through edit() and are saved by saveCached().
   */
  const Localization& cached(Project &project);
  Localization& edit(Project &project);
  bool saveCached(Project &project);
  /** The last scan of the cached project, made by refreshCached(). */
  const Uses& cachedUses(Project &project);
  RefreshResult refreshCached(Project &project);
  const std::vector<std::string>& cachedErrors(Project &project);
  /** Drops the copy, so the next cached() reads the files. */
  void invalidateCache();
}
