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
#include "project/scene/prefab.h"
#include "project/events/eventGraph.h"
#include "project/events/eventUsages.h"
#include "utils/logger.h"

namespace
{
  std::string argProgPath{};
  bool argExperimental{false};
  int argScene{0};
  std::string argObject{};
  std::string argWindow{};
  bool argEdit{false};
  std::string argUiScript{};

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

  // Fork: the Events window's graph without the editor: print it, or draw and remove wires
  struct GraphArgs
  {
    int scene{0};
    std::string prefab{};
    std::string focus{};
    std::string from{};
    std::string to{};
    std::string event{};
  };

  bool runGraphCommand(const std::string &cmd, const GraphArgs &args)
  {
    namespace G = Project::Events::Graph;
    Project::Project project{argProgPath};
    Project::Project *prevProject = ctx.project;
    ctx.project = &project;
    bool res = false;
    try {
      std::unique_ptr<Project::Scene> scene{};
      std::shared_ptr<Project::Prefab> prefab{};
      std::string prefabPath{};
      Project::Object *root = nullptr;
      G::Kind kind = G::Kind::Scene;
      if(!args.prefab.empty()) {
        for(const auto &entry : project.getAssets().getTypeEntries(Project::FileType::PREFAB)) {
          if(entry.prefab && (entry.name == args.prefab || entry.path.ends_with("/" + args.prefab))) {
            prefab = entry.prefab;
            prefabPath = entry.path;
          }
        }
        if(!prefab) throw std::runtime_error("no prefab '" + args.prefab + "'");
        root = &prefab->obj;
        kind = G::Kind::Prefab;
      } else {
        scene = std::make_unique<Project::Scene>(args.scene, project.getPath());
        root = &scene->getRootObject();
      }

      uint32_t focus = 0;
      if(!args.focus.empty()) {
        auto model = G::collect(project, *root, kind);
        auto ref = G::parsePin(project, *root, model, args.focus + "|Object.Enable", true);
        if(!ref.node || !ref.path.empty()) throw std::runtime_error("no object '" + args.focus + "' to focus on");
        focus = ref.node;
      }
      auto model = G::collect(project, *root, kind, focus);

      if(cmd == "graph") {
        printf("graph: %s, creates %s:: events\n", kind == G::Kind::Scene ? "scene" : "prefab", model.ns.c_str());
        std::vector<const G::Node*> printed{};
        for(const auto &node : model.nodes) printed.push_back(&node);
        for(const auto &node : model.canvases) if(node.bound()) printed.push_back(&node);
        for(const auto *nodePtr : printed) {
          const auto &node = *nodePtr;
          printf("node %s%s%s%s%s\n", node.name.c_str(), node.instance ? (" (instance of " + node.prefab + ")").c_str() : (node.canvas ? " (UI canvas)" : ""),
                 node.builtin ? " (built-in)" : "", node.condition ? " (condition)" : "",
                 node.summary.empty() ? "" : (" [" + node.summary + "]").c_str());
          for(const auto &cat : node.categories) {
            for(const auto &pin : cat.pins) {
              if(pin.names.empty() && pin.muted.empty() && pin.problem.empty() && pin.locked.empty())continue;
              std::string names{};
              for(const auto &n : pin.names) names += " " + n;
              for(const auto &n : pin.locked) names += " " + n + " (trigger, from the script)";
              for(const auto &n : pin.muted) names += " (muted " + n + ")";
              printf("  %s %s:%s%s%s\n", pin.ref.input ? "in " : "out", G::describe(project, *root, pin.ref).c_str(), names.c_str(),
                     pin.problem.empty() ? "" : ("  PROBLEM: " + pin.problem).c_str(),
                     pin.hint.empty() ? "" : ("  NOTE: " + pin.hint).c_str());
            }
          }
        }
        for(const auto &wire : G::wires(model, {}, true, true)) {
          auto end = [&](const G::End &e) {
            switch(e.type) {
              case G::End::Type::Pin: return G::describe(project, *root, e.pin);
              case G::End::Type::Event: return std::string{"[event]"};
              case G::End::Type::Global: return std::string{"[Global]"};
              case G::End::Type::Scope: return std::string{"[scope]"};
            }
            return std::string{};
          };
          printf("wire %s -> %s : %s\n", end(wire.from).c_str(), end(wire.to).c_str(), wire.name.c_str());
        }
        res = true;
      } else {
        G::PinRef from{}, to{};
        if(!args.from.empty()) {
          from = G::parsePin(project, *root, model, args.from, false);
          if(!from.valid()) throw std::runtime_error("no output pin '" + args.from + "'");
        }
        if(!args.to.empty()) {
          to = G::parsePin(project, *root, model, args.to, true);
          if(!to.valid()) throw std::runtime_error("no input pin '" + args.to + "'");
        }
        if(cmd == "wire") {
          if(from.valid() && to.valid()) {
            auto name = G::connect(project, *root, model, from, to);
            printf("wired with %s\n", name.c_str());
            res = !name.empty();
          } else if(!args.event.empty() && (from.valid() || to.valid())) {
            res = G::bind(project, *root, model, from.valid() ? from : to, args.event);
            printf("%s %s\n", res ? "bound to" : "already bound to", args.event.c_str());
            res = true;
          } else {
            throw std::runtime_error("wire needs --from and --to, or one of them and --event");
          }
        } else {
          for(const auto &wire : G::wires(model, {}, true, true)) {
            const bool fromOk = from.valid() ? (wire.from.type == G::End::Type::Pin && wire.from.pin == from) : wire.from.type != G::End::Type::Pin;
            const bool toOk = to.valid() ? (wire.to.type == G::End::Type::Pin && wire.to.pin == to) : wire.to.type != G::End::Type::Pin;
            if(!fromOk || !toOk || (!args.event.empty() && wire.name != args.event))continue;
            res |= G::disconnect(project, *root, model, wire);
            printf("removed the wire of %s\n", wire.name.c_str());
          }
          if(!res && !args.event.empty() && (from.valid() || to.valid())) {
            res = G::unbind(project, *root, model, from.valid() ? from : to, args.event);
            if(res) printf("unbound %s\n", args.event.c_str());
          }
          if(!res) throw std::runtime_error("no such wire");
        }
        if(res) {
          if(scene) scene->save();
          if(prefab) prefab->save(prefabPath);
        }
      }
    } catch(const std::exception &e) {
      fprintf(stderr, "%s failed: %s\n", cmd.c_str(), e.what());
      res = false;
    }
    ctx.project = prevProject;
    return res;
  }

  // Fork: the event browser without the editor: list every event and its uses, or rename one
  bool runEventsCommand(const std::string &cmd, const std::string &event, const std::string &renameTo)
  {
    namespace U = Project::Events::Usages;
    Project::Project project{argProgPath};
    Project::Project *prevProject = ctx.project;
    ctx.project = &project;
    bool res = false;
    try {
      if(cmd == "events") {
        std::string last{};
        for(const auto &use : U::collect(project, nullptr)) {
          const auto k = Project::Events::Graph::key(use.name);
          if(!event.empty() && k != Project::Events::Graph::key(event))continue;
          if(k != last) { printf("%s\n", k.c_str()); last = k; }
          const char* kind = use.kind == U::Use::Kind::Code ? "code  " : (use.input ? "listen" : "fire  ");
          printf("  %s %s\n", kind, use.where.c_str());
        }
        res = true;
      } else {
        if(event.empty() || renameTo.empty()) throw std::runtime_error("rename-event needs --event and --rename-to");
        if(auto problem = Project::Events::nameProblem(renameTo); !problem.empty()) throw std::runtime_error(problem);
        auto result = U::rename(project, nullptr, event, renameTo);
        printf("renamed %d bindings in %zu files\n", result.bindings, result.files.size());
        for(const auto &f : result.files) printf("  wrote %s\n", f.c_str());
        for(const auto &c : result.code) printf("  code still uses the old name: %s\n", c.c_str());
        res = result.bindings > 0;
      }
    } catch(const std::exception &e) {
      fprintf(stderr, "%s failed: %s\n", cmd.c_str(), e.what());
      res = false;
    }
    ctx.project = prevProject;
    return res;
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

const std::string& CLI::getStartWindow()
{
  return argWindow;
}

bool CLI::getStartEdit()
{
  return argEdit;
}

const std::string& CLI::getUiScript()
{
  return argUiScript;
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
    .choices("build", "clean", "migrate", "to-prefab", "unpack", "graph", "wire", "unwire", "events", "rename-event");

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

  prog.add_argument("--edit")
    .help("In the editor: edit the prefab of the instance given with --object")
    .default_value(false)
    .implicit_value(true);

  prog.add_argument("--ui-script")
    .help("In the editor: play mouse and keyboard steps from this file (see src/editor/uiScript.h)")
    .default_value(std::string{});

  prog.add_argument("--window")
    .help("In the editor: bring this window's tab to the front at start-up (e.g. Events)")
    .default_value(std::string{});

  // Fork: arguments of graph, wire and unwire
  prog.add_argument("--prefab")
    .help("Use this prefab's own graph instead of a scene's (graph, wire, unwire)")
    .default_value(std::string{});
  prog.add_argument("--focus")
    .help("Only this object, with object:: events (graph, wire, unwire)")
    .default_value(std::string{});
  prog.add_argument("--from")
    .help("Output pin, \"Object/Path|Component.Pin\" (wire, unwire)")
    .default_value(std::string{});
  prog.add_argument("--to")
    .help("Input pin, \"Object/Path|Component.Pin\" (wire, unwire)")
    .default_value(std::string{});
  prog.add_argument("--rename-to")
    .help("The new name (rename-event)")
    .default_value(std::string{});
  prog.add_argument("--event")
    .help("An event to bind one pin to, like an Event node in the window (wire, unwire)")
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
  argWindow = prog.get<std::string>("--window");
  argEdit = prog["--edit"] == true;
  argUiScript = prog.get<std::string>("--ui-script");

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
  else if (cmd == "graph" || cmd == "wire" || cmd == "unwire")
  {
    res = runGraphCommand(cmd, {prog.get<int>("--scene"), prog.get<std::string>("--prefab"), prog.get<std::string>("--focus"),
                                prog.get<std::string>("--from"), prog.get<std::string>("--to"), prog.get<std::string>("--event")});
  }
  else if (cmd == "events" || cmd == "rename-event")
  {
    res = runEventsCommand(cmd, prog.get<std::string>("--event"), prog.get<std::string>("--rename-to"));
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
