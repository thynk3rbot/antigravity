#ifndef COMMAND_MANAGER_H
#define COMMAND_MANAGER_H

#include "../config.h"
#include <Arduino.h>

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

  int getPinFromName(const String &name);

  // Helper to get interface name string
  static const char *interfaceName(CommInterface ifc);

private:
  CommandManager() {}
};

#endif // COMMAND_MANAGER_H
