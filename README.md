# DELIA UI APP #

DELIA UI app for the Raspberry Pi (64-bit).

### Building the DELIA UI app ###

To clone the DELIA UI app the --recurse-submodules flag must be used:

$ git clone --recurse-submodules https://github.com/Melbourne-Instruments/delia_ui.git

The DELIA UI app uses CMake as its build system. A generate script is also provided for convenient setup. Simply running ./generate with no arguments in the root of DELIA UI will setup a build folder containing a Release configuration and a Debug configuration. CMake arguments can be passed through the generate script using the --cmake-args flag. Those arguments will then be added to both configurations.

To cross compile for the Raspberry Pi, the Melbourne Instruments SDK *must* be used.
It is recommended to install the SDK in the /opt folder on your Ubuntu PC.

Once this has been done, source the environment script to set-up the build environment, for example:

$ source /opt/delia/1.0.0/environment-setup-cortexa72-elk-linux

Note: The build options are specified in common.h. Please check to make sure these are set correctly for your configuration.

### Dependencies ###

  * GRPC: version 1.36.4

  * RapidJSON

  * spdlog
  
  * Melbourne Instruments RPi RTDM Audio Driver

---
Copyright 2023-2024 Melbourne Instruments, Australia.
