/*
 * Copyright (C) 2018  Christian Berger
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "cluon-complete.hpp"
#include "actuationrequestmessage.hpp"

#include <linux/joystick.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>

int32_t main(int32_t argc, char **argv) {
  int32_t retCode{0};
  auto commandlineArguments = cluon::getCommandlineArguments(argc, argv);
  if ((0 == commandlineArguments.count("cid"))) {
    std::cerr << argv[0]
              << " interfaces with the given PS3 controller to emit ActuationRequest messages to an OD4Session."
              << std::endl;
    std::cerr << "Usage:   " << argv[0] << " --cid=<OpenDaVINCI session> [--verbose]" << std::endl;
    std::cerr << "Example: " << argv[0] << " --cid=111" << std::endl;
    retCode = 1;
  } else {
    cluon::OD4Session od4{static_cast<uint16_t>(std::stoi(commandlineArguments["cid"]))};

    if (od4.isRunning()) {
      auto onActuationRequest = [](cluon::data::Envelope &&env) {
        // Now, we unpack the cluon::data::Envelope to get our message.
        auto msg = cluon::extractMessage<opendlv::proxy::ActuationRequest>(std::move(env));
        std::cout << "acceleration = " << msg.acceleration() << ", steering = " << msg.steering() << std::endl;
      };
      od4.dataTrigger(opendlv::proxy::ActuationRequest::ID(), onActuationRequest);
      while (1) {
        using namespace std::chrono_literals;
        std::this_thread::sleep_for(1s);
      }
      retCode = 0;
    }
  }
  return retCode;
}
