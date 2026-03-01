#ifndef COMMAND_MANAGER_H
#define COMMAND_MANAGER_H

#include "../config.h"
#include <Arduino.h>
#include <functional>
#include <map>

typedef std::function<void(const String &, CommInterface)> CommandHandler;

class CommandManager {
public:
  static CommandManager &getInstance() {
    static CommandManager instance;
    return instance;
  }

  // Main entry point for ANY text command from ANY interface
  void handleCommand(const String &fullCmd, CommInterface source);
  void executeLocalCommand(const String &subCmd, CommInterface source);
  void restoreHardwareState();

  void registerCommand(const String &cmd, CommandHandler handler);

  int getPinFromName(const String &name);

  // Helper to get interface name string
  static const char *interfaceName(CommInterface ifc);

private:
  CommandManager();
  void initRegistry();
  std::map<String, CommandHandler> _commandRegistry;
};

#endif // COMMAND_MANAGER_H
