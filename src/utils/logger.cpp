/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "logger.h"

#include <atomic>
#include <mutex>

namespace
{
  std::mutex mtx{};
  std::string buff{};
  std::atomic_bool logChanged{false};
  constexpr size_t MAX_BUFF_SIZE = 1024 * 100; // 100kb

  constinit Utils::Logger::LogOutputFunc outputFunc = nullptr;
  std::string logStripped{};

  std::string buildBuff{};
  std::atomic_bool buildLogChanged{false};
  std::vector<std::string> buildLines{};
}

void Utils::Logger::setOutput(LogOutputFunc outFunc) {
  outputFunc = outFunc;
}

void Utils::Logger::log(const std::string&msg, int level)
{
  std::lock_guard lock{mtx};
  switch(level) {
    default:
    case LEVEL_INFO:
      buff += "[INF] ";
      break;
    case LEVEL_WARN:
      buff += "[WRN] ";
      break;
    case LEVEL_ERROR:
      buff += "[ERR] ";
      break;
  }
  buff += msg + "\n";
  logChanged = true;

  if (outputFunc) {
    outputFunc(buff);
    buff = "";
  }
}

void Utils::Logger::logRaw(const std::string&msg, int level) {
  std::lock_guard lock{mtx};
  buff += msg;
  logChanged = true;

  if (outputFunc) {
    outputFunc(buff);
    buff = "";
  }
}

void Utils::Logger::clear() {
  std::lock_guard lock{mtx};
  buff = "";
  logChanged = true;
}

std::string Utils::Logger::getLog() {
  std::lock_guard lock{mtx};
  if (buff.length() > MAX_BUFF_SIZE) {
    buff = buff.substr(buff.length() - MAX_BUFF_SIZE);
    logChanged = true;
  }
  return buff;
}

void Utils::Logger::clearBuild() {
  std::lock_guard lock{mtx};
  buildBuff = "";
  buildLines.clear();
  buildLogChanged = true;
}

void Utils::Logger::appendBuild(const std::string &msg) {
  std::lock_guard lock{mtx};
  buildBuff += msg;
  buildLogChanged = true;
}

const std::vector<std::string>& Utils::Logger::getBuildLines()
{
  if (!buildLogChanged) return buildLines;

  std::string stripped;
  {
    std::lock_guard lock{mtx};
    bool inAnsi = false;
    for (char c : buildBuff) {
      if (c == '\x1b') {
        inAnsi = true;
      } else if (inAnsi && (c == 'm' || c == 'K')) {
        inAnsi = false;
      } else if (!inAnsi) {
        if (c < 32 && c != '\n' && c != '\t') c = '?';
        stripped += c;
      }
    }
  }

  buildLines.clear();
  std::string line;
  for (char c : stripped) {
    if (c == '\n') {
      buildLines.push_back(std::move(line));
      line.clear();
    } else {
      line += c;
    }
  }
  if (!line.empty()) buildLines.push_back(std::move(line));

  buildLogChanged = false;
  return buildLines;
}

const std::string& Utils::Logger::getLogStripped()
{
  if(!logChanged)return logStripped;
  auto log = getLog();

  logStripped = "";

  bool inAnsi = false;
  for(char c : log)
  {
    if(c == '\x1b') {
      inAnsi = true;
    } else if(inAnsi && (c == 'm' || c == 'K')) {
      inAnsi = false;
    } else if(!inAnsi)
    {
      if(c < 32 && c != '\n' && c != '\t') {
        c = '?';
      }
      logStripped += c;
    }
  }
  logChanged = false;
  return logStripped;
}
