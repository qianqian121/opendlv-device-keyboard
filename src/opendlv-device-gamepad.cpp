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
#include <thread>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <GL/glx.h>
#include <gainput/gainput.h>

namespace {
enum Button
{
  ButtonLeft,
  ButtonQuit
};

const char* windowName = "Gainput basic sample";
const int width = 800;
const int height = 600;
}

int32_t main(int32_t argc, char **argv) {
    int32_t retCode{0};
    auto commandlineArguments = cluon::getCommandlineArguments(argc, argv);
    if ( (0 == commandlineArguments.count("cid")) ||
         (0 == commandlineArguments.count("device")) ||
         (0 == commandlineArguments.count("freq")) ||
         (0 == commandlineArguments.count("axis_leftright")) ||
         (0 == commandlineArguments.count("axis_updown")) ||
         (0 == commandlineArguments.count("acc_min")) ||
         (0 == commandlineArguments.count("acc_max")) ||
         (0 == commandlineArguments.count("dec_min")) ||
         (0 == commandlineArguments.count("dec_max")) ||
         (0 == commandlineArguments.count("steering_min")) ||
         (0 == commandlineArguments.count("steering_max")) ) {
        std::cerr << argv[0] << " interfaces with the given PS3 controller to emit ActuationRequest messages to an OD4Session." << std::endl;
        std::cerr << "Usage:   " << argv[0] << " --device=<PS3 controller device> --freq=<frequency in Hz>--acc_min=<minimum acceleration> --acc_max=<maximum acceleration> --dec_min=<minimum deceleration> --dec_max=<maximum deceleration> --steering_min=<minimum steering> --steering_max=<maximum steering> [--steering_max_rate=5.0] --cid=<OpenDaVINCI session> [--ps4] [--verbose]" << std::endl;
        std::cerr << "Example: " << argv[0] << " --device=/dev/input/js0 --axis_leftright=0 --axis_updown=4 --freq=100 --acc_min=0 --acc_max=50 --dec_min=0 --dec_max=-10 --steering_min=-10 --steering_max=10 --steering_max_rate=5.0 --cid=111" << std::endl;
        retCode = 1;
    }
    else {
        const int32_t MIN_AXES_VALUE = -32768;
        const uint32_t MAX_AXES_VALUE = 32767;

        const bool VERBOSE{commandlineArguments.count("verbose") != 0};
        const uint8_t AXIS_LEFTRIGHT = std::stoi(commandlineArguments["axis_leftright"]);
        const uint8_t AXIS_UPDOWN = std::stoi(commandlineArguments["axis_updown"]);
        const std::string DEVICE{commandlineArguments["device"]};

        const float FREQ = std::stof(commandlineArguments["freq"]);
        const float ACCELERATION_MIN = std::stof(commandlineArguments["acc_min"]);
        const float ACCELERATION_MAX = std::stof(commandlineArguments["acc_max"]);
        const float DECELERATION_MIN = std::stof(commandlineArguments["dec_min"]);
        const float DECELERATION_MAX = std::stof(commandlineArguments["dec_max"]);
        const float STEERING_MIN = std::stof(commandlineArguments["steering_min"]);
        const float STEERING_MAX = std::stof(commandlineArguments["steering_max"]);

        float const STEERING_MAX_RATE = (commandlineArguments.count("steering_max_rate") != 0) ? std::stof(commandlineArguments["steering_max_rate"]) : -1.0f;
        float const TS = 1.0f / FREQ;

      {
        static int attributeListDbl[] = {      GLX_RGBA,      GLX_DOUBLEBUFFER, /*In case single buffering is not supported*/      GLX_RED_SIZE,   1,      GLX_GREEN_SIZE, 1,      GLX_BLUE_SIZE,  1,
                                               None };

        Display* xDisplay = XOpenDisplay(0);
        if (xDisplay == 0)
        {
          std::cerr << "Cannot connect to X server." << std::endl;
          return -1;
        }

        Window root = DefaultRootWindow(xDisplay);

        XVisualInfo* vi = glXChooseVisual(xDisplay, DefaultScreen(xDisplay), attributeListDbl);
        assert(vi);

        GLXContext context = glXCreateContext(xDisplay, vi, 0, GL_TRUE);

        Colormap cmap = XCreateColormap(xDisplay, root,                   vi->visual, AllocNone);

        XSetWindowAttributes swa;
        swa.colormap = cmap;
        swa.event_mask = ExposureMask
                         | KeyPressMask | KeyReleaseMask
                         | PointerMotionMask | ButtonPressMask | ButtonReleaseMask;

        Window xWindow = XCreateWindow(
            xDisplay, root,
            0, 0, width, height, 0,
            CopyFromParent, InputOutput,
            CopyFromParent, CWEventMask,
            &swa
        );

        glXMakeCurrent(xDisplay, xWindow, context);

        XSetWindowAttributes xattr;
        xattr.override_redirect = False;
        XChangeWindowAttributes(xDisplay, xWindow, CWOverrideRedirect, &xattr);

        XMapWindow(xDisplay, xWindow);
        XStoreName(xDisplay, xWindow, windowName);

        // Setup Gainput
        gainput::InputManager manager;
        const gainput::DeviceId keyboardId = manager.CreateDevice<gainput::InputDeviceKeyboard>();

        gainput::InputMap map(manager);
        map.MapBool(ButtonLeft, keyboardId, gainput::KeyA);
        map.MapBool(ButtonQuit, keyboardId, gainput::KeyEscape);

        manager.SetDisplaySize(width, height);

        std::mutex valuesMutex;
            float acceleration{0};
            float steering{0};
            float targetSteering{0};
            float prevSteering{0};
            bool hasError{false};

            // Thread to read values.
            std::thread gamepadReadingThread([&AXIS_LEFTRIGHT,
                                              &AXIS_UPDOWN,
                                              &MIN_AXES_VALUE,
                                              &MAX_AXES_VALUE,
                                              &VERBOSE,
                                              &ACCELERATION_MIN,
                                              &ACCELERATION_MAX,
                                              &DECELERATION_MIN,
                                              &DECELERATION_MAX,
                                              &STEERING_MIN,
                                              &STEERING_MAX,
                                              &STEERING_MAX_RATE,
                                              &valuesMutex,
                                              &acceleration,
                                              &steering,
                                              &targetSteering,
                                              &hasError,
                                              &xDisplay,
                                              &manager,
                                              &map]() {

                while (!hasError) {
                    // Define timeout for select system call. The timeval struct must be
                    // reinitialized for every select call as it might be modified containing
                    // the actual time slept.

                    if (true) {
                        std::lock_guard<std::mutex> lck(valuesMutex);

                        while (!hasError) {
                          manager.Update();
                          XEvent event;
                          while (XPending(xDisplay))
                          {
                            XNextEvent(xDisplay, &event);
                            manager.HandleEvent(event);
                          }
                            float percent{0};
                          if (map.GetBoolWasDown(ButtonLeft)) {
                            percent++;
                              std::cout << "<<<<<<<" << std::endl;
                          }
                          if (map.GetBoolWasDown(ButtonQuit)) {
                            hasError = true;
                            break;
                          }
                          using namespace std::chrono_literals;

                          std::this_thread::sleep_for(1ms);
                        }
                    }
                }
            });

            // OD4Session to send values to.
            opendlv::proxy::ActuationRequest ar;
            cluon::OD4Session od4{static_cast<uint16_t>(std::stoi(commandlineArguments["cid"]))};
            if (od4.isRunning()) {
                od4.timeTrigger(FREQ, [&VERBOSE,
                                       &STEERING_MAX_RATE,
                                       &TS,
                                       &valuesMutex,
                                       &acceleration,
                                       &steering,
                                       &prevSteering,
                                       &targetSteering,
                                       &hasError,
                                       &ar,
                                       &od4](){
                    std::lock_guard<std::mutex> lck(valuesMutex);

                    if (STEERING_MAX_RATE > 0.0f) {
                        float inc = TS * STEERING_MAX_RATE;
                        float steeringRate = (steering - prevSteering) / TS;
                        if (steeringRate > STEERING_MAX_RATE) {
                            steering = prevSteering + inc;
                        } else if (steeringRate < -STEERING_MAX_RATE) {
                            steering = prevSteering - inc;
                        }
                    }

                    prevSteering = steering;
                    ar.acceleration(acceleration).steering(steering).isValid(!hasError);

                    if (VERBOSE) {
                        std::stringstream buffer;
                        ar.accept([](uint32_t, const std::string &, const std::string &) {},
                                 [&buffer](uint32_t, std::string &&, std::string &&n, auto v) { buffer << n << " = " << v << '\n'; },
                                 []() {});
                        std::cout << buffer.str() << std::endl;
                    }
                    od4.send(ar);

                    if (STEERING_MAX_RATE > 0.0f) {
                        steering = targetSteering;
                    }

                    // Determine whether to continue or not.
                    return !hasError;
                });

                // Send stop.
                ar.acceleration(0).steering(0).isValid(true);
                od4.send(ar);
            }

            // Stop thread.
            {
                std::lock_guard<std::mutex> lck(valuesMutex);
                hasError = true;
                gamepadReadingThread.join();
            }

            retCode = 0;
        }
    }
    return retCode;
}

