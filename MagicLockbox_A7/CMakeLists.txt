#  Copyright (c) Microsoft Corporation. All rights reserved.
#  Licensed under the MIT License.

CMAKE_MINIMUM_REQUIRED(VERSION 3.8)
PROJECT(MagicLockbox_A7 C)

# Create executable
ADD_EXECUTABLE(${PROJECT_NAME} main.c epoll_timerfd_utilities.c i2c.c device_twin.c magicKey.c parson.c lsm6dso_reg.c azure_iot_utilities.c libs/platform_basic_func.c libs/Seeed_3D_touch_mgc3030.c)
TARGET_INCLUDE_DIRECTORIES(${PROJECT_NAME} PUBLIC ${AZURE_SPHERE_API_SET_DIR}/usr/include/azureiot)
TARGET_COMPILE_DEFINITIONS(${PROJECT_NAME} PUBLIC AZURE_IOT_HUB_CONFIGURED)
TARGET_LINK_LIBRARIES(${PROJECT_NAME} m azureiot applibs pthread gcc_s c)

# Add MakeImage post-build command
INCLUDE("${AZURE_SPHERE_MAKE_IMAGE_FILE}")
