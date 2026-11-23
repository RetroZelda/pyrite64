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
#include "project/scene/scene.h"
#include "utils/logger.h"

namespace
{
  std::string argProgPath{};
  bool argExperimental{false};
  int argScene{0};
  std::string argObject{};

  // Fork: an object of a scene by its names from the top, "Group/Door"
  std::shared_ptr<Project::Object> findByPath(Project::Scene &scene, const std::string &path)
  {
    Project::Object *node = &scene.getRootObject();
    std::shared_ptr<Project::Object> found{};
    size_t start = 0;
    while(start <= path.size()) {
      size_t end = path.find('/', start);
      if(end == std::string::npos) end = path.size();
      const auto name = path.substr(start, end - start);
      found = nullptr;
      for(auto &child : node->children) {
        if(child->name == name) { found = child; break; }
      }
      if(!found) return nullptr;
      node = found.get();
      start = end + 1;
    }
    return found;
  }

  // Fork: "To Prefab" and "Unpack" without the editor, for scripts and tests
  bool runPrefabCommand(const std::string &cmd, int sceneId, const std::string &objectPath, const std::string &dir)
  {
    Project::Project project{argProgPath};
    Project::Project *prevProject = ctx.project;
    ctx.project = &project;
    bool res = false;
    try {
      Project::Scene scene{sceneId, project.getPath()};
      auto obj = findByPath(scene, objectPath);
      if(!obj) {
        fprintf(stderr, "No object '%s' in scene %d\n", objectPath.c_str(), sceneId);
      } else if(cmd == "to-prefab") {
        if(obj->isPrefabInstance()) {
          fprintf(stderr, "'%s' already is a prefab instance\n", objectPath.c_str());
        } else {
          uint64_t uuid = scene.createPrefabFromObject(obj->uuid, dir);
          printf("Created prefab %llu from '%s'\n", (unsigned long long)uuid, objectPath.c_str());
          res = uuid != 0;
        }
      } else {
        if(!obj->isPrefabInstance()) {
          fprintf(stderr, "'%s' is not a prefab instance\n", objectPath.c_str());
        } else {
          scene.unpackPrefabInstance(obj->uuid);
          res = !obj->isPrefabInstance();
          printf("Unpacked '%s'\n", objectPath.c_str());
        }
      }
      if(res) scene.save();
    } catch(const std::exception &e) {
      fprintf(stderr, "%s failed: %s\n", cmd.c_str(), e.what());
      res = false;
    }
    ctx.project = prevProject;
    return res;
  }
}

const std::string& CLI::getProjectPath()
{
  return argProgPath;
}

bool CLI::isExperimentalEnabled()
{
  return argExperimental;
}

int CLI::getStartScene()
{
  return argScene;
}

const std::string& CLI::getStartObject()
{
  return argObject;
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
    .choices("build", "clean", "migrate", "to-prefab", "unpack");

  // Fork: arguments of to-prefab and unpack
  prog.add_argument("--scene")
    .help("Scene id (to-prefab, unpack; in the editor: the scene to open)")
    .default_value(0)
    .scan<'i', int>();
  prog.add_argument("--object")
    .help("Object path in the scene, names from the top separated by / (to-prefab, unpack; in the editor: "
          "the object to select, continuing into prefab instances)")
    .default_value(std::string{});
  prog.add_argument("--dir")
    .help("Folder under assets/ for the new prefab (to-prefab)")
    .default_value(std::string{});

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
  argScene = prog.get<int>("--scene");
  argObject = prog.get<std::string>("--object");

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
  else if (cmd == "to-prefab" || cmd == "unpack")
  {
    res = runPrefabCommand(cmd, prog.get<int>("--scene"), prog.get<std::string>("--object"), prog.get<std::string>("--dir"));
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
