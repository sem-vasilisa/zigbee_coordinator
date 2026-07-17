# Install script for directory: /home/goodbyte/projects/workspace_zigbee/zephyr/drivers

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "MinSizeRel")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "TRUE")
endif()

# Set default install directory permissions.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/home/goodbyte/ncs/toolchains/b77d8c1312/opt/zephyr-sdk/arm-zephyr-eabi/bin/arm-zephyr-eabi-objdump")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/goodbyte/projects/workspace_zigbee/zigbee-coordinator/build/zigbee-coordinator/zephyr/drivers/disk/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/goodbyte/projects/workspace_zigbee/zigbee-coordinator/build/zigbee-coordinator/zephyr/drivers/firmware/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/goodbyte/projects/workspace_zigbee/zigbee-coordinator/build/zigbee-coordinator/zephyr/drivers/interrupt_controller/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/goodbyte/projects/workspace_zigbee/zigbee-coordinator/build/zigbee-coordinator/zephyr/drivers/misc/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/goodbyte/projects/workspace_zigbee/zigbee-coordinator/build/zigbee-coordinator/zephyr/drivers/pcie/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/goodbyte/projects/workspace_zigbee/zigbee-coordinator/build/zigbee-coordinator/zephyr/drivers/usb/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/goodbyte/projects/workspace_zigbee/zigbee-coordinator/build/zigbee-coordinator/zephyr/drivers/usb_c/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/goodbyte/projects/workspace_zigbee/zigbee-coordinator/build/zigbee-coordinator/zephyr/drivers/clock_control/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/goodbyte/projects/workspace_zigbee/zigbee-coordinator/build/zigbee-coordinator/zephyr/drivers/console/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/goodbyte/projects/workspace_zigbee/zigbee-coordinator/build/zigbee-coordinator/zephyr/drivers/counter/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/goodbyte/projects/workspace_zigbee/zigbee-coordinator/build/zigbee-coordinator/zephyr/drivers/entropy/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/goodbyte/projects/workspace_zigbee/zigbee-coordinator/build/zigbee-coordinator/zephyr/drivers/flash/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/goodbyte/projects/workspace_zigbee/zigbee-coordinator/build/zigbee-coordinator/zephyr/drivers/gpio/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/goodbyte/projects/workspace_zigbee/zigbee-coordinator/build/zigbee-coordinator/zephyr/drivers/hwinfo/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/goodbyte/projects/workspace_zigbee/zigbee-coordinator/build/zigbee-coordinator/zephyr/drivers/ieee802154/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/goodbyte/projects/workspace_zigbee/zigbee-coordinator/build/zigbee-coordinator/zephyr/drivers/pinctrl/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/goodbyte/projects/workspace_zigbee/zigbee-coordinator/build/zigbee-coordinator/zephyr/drivers/sensor/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/goodbyte/projects/workspace_zigbee/zigbee-coordinator/build/zigbee-coordinator/zephyr/drivers/serial/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/goodbyte/projects/workspace_zigbee/zigbee-coordinator/build/zigbee-coordinator/zephyr/drivers/timer/cmake_install.cmake")
endif()

