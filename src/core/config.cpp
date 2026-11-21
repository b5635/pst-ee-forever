// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/config.h"

#include <fstream>
#include <sstream>

#include "core/log.h"

namespace pstf {

const IniMap& Config() {
  static const IniMap ini = [] {
    std::ifstream f(std::string(DataDir()) + "\\pst_forever.ini", std::ios::binary);
    if (!f) {
      Log("config: pst_forever.ini not found, all modules off");
      return IniMap{};
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return ParseIni(ss.str());
  }();
  return ini;
}

}  // namespace pstf
