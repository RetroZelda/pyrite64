/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "pyrite64_version.h"
#include "cli.h"
#include "argparse/argparse.hpp"
#include "build/projectBuilder.h"
#include "context.h"
#include "project/project.h"
#include "project/scene/migration.h"
#include "utils/logger.h"

namespace
{
  std::string argProgPath{};
  bool argExperimental{false};
}

const std::string& CLI::getProjectPath()
{
  return argProgPath;
}

bool CLI::isExperimentalEnabled()
{
  return argExperimental;
}

CLI::Result CLI::run(int argc, char** argv)
{
  argparse::ArgumentParser prog{"pyrite64", PYRITE_VERSION};
  prog.add_argument("--cli")
   .help("Run in CLI mode (no GUI)")
   .default_value(false)
   .implicit_value(true);

  prog.add_argument("--cmd")
    .help("Command to run")
    .choices("build", "clean", "migrate");

  prog.add_argument("--experimental")
    .help("Enable experimental features (may cause instability / break projects)")
    .default_value(false)
    .implicit_value(true);

  prog.add_argument("project")
    .default_value("")
    .help("Path to project file (.p64proj)")
  ;

  argProgPath = {};
  try {
    prog.parse_args(argc, argv);
  }
  catch (const std::exception& err) {
    std::cerr << err.what() << std::endl;
    std::cerr << prog;
    return Result::ERROR;
  }

  argExperimental = prog["--experimental"] == true;
  argProgPath = prog.get<std::string>("project");

  if (prog["--cli"] == false) {
    return Result::GUI;
  }

  auto cmd = prog.get<std::string>("--cmd");

  Utils::Logger::setOutput([](const std::string &msg) {
    fputs(msg.c_str(), stdout);
  });

  printf("Pyrite64 - CLI\n");
  bool res = false;

  if (cmd == "build") {
    printf("Building project: %s\n", argProgPath.c_str());
    try {
      res = Build::buildProject(argProgPath);
    } catch(const std::exception &e) {
      fprintf(stderr, "Build failed: %s\n", e.what());
      res = false;
    }
  }
  else if (cmd == "migrate")
  {
    // Fork: headless version of the editor's project-update dialog (builds refuse outdated projects)
    printf("Migrating project: %s\n", argProgPath.c_str());
    Project::Project project{argProgPath};
    // prefab instances are resolved through the global project, like in a build
    Project::Project *prevProject = ctx.project;
    ctx.project = &project;
    auto scan = Project::Migration::scanProject(project);
    if (scan.empty()) {
      printf("Project is up to date\n");
    } else {
      printf("Updating %s (prefab/emitter units per meter: %g)\n",
        Project::Migration::describe(scan).c_str(), (double)scan.legacyUnitsPerMeter);
      if (scan.countOf(Project::Migration::DocType::PREFAB) > 0 || scan.countOf(Project::Migration::DocType::EMITTER) > 0) {
        project.conf.legacyUnitsPerMeter = scan.legacyUnitsPerMeter;
        project.saveConfig();
      }
      Project::Migration::apply(project, scan);
    }
    ctx.project = prevProject;
    res = true;
  }
  else if (cmd == "clean")
  {
    printf("Cleaning project: %s\n", argProgPath.c_str());
    Project::Project project{argProgPath};
    res = Build::cleanProject(project, {
      .code = true,
      .assets = true,
      .engine = true,
      .engineSrc = true
    });
  }

  return res ? Result::SUCCESS : Result::ERROR;
}
