/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#ifndef _clca7e_h_
#define _clca7e_h_

// class methods
#define NVCA7E_SET_NOTIFIER_CONTROL                                             (0x00000220)
#define NVCA7E_SET_NOTIFIER_CONTROL_MODE                                        0:0
#define NVCA7E_SET_NOTIFIER_CONTROL_MODE_WRITE                                  (0x00000000)
#define NVCA7E_SET_NOTIFIER_CONTROL_MODE_WRITE_AWAKEN                           (0x00000001)
#define NVCA7E_SET_SIZE                                                         (0x00000224)
#define NVCA7E_SET_SIZE_WIDTH                                                   15:0
#define NVCA7E_SET_SIZE_HEIGHT                                                  31:16
#define NVCA7E_SET_STORAGE                                                      (0x00000228)
#define NVCA7E_SET_STORAGE_BLOCK_HEIGHT                                         3:0
#define NVCA7E_SET_STORAGE_BLOCK_HEIGHT_NVD_BLOCK_HEIGHT_ONE_GOB                (0x00000000)
#define NVCA7E_SET_STORAGE_BLOCK_HEIGHT_NVD_BLOCK_HEIGHT_TWO_GOBS               (0x00000001)
#define NVCA7E_SET_STORAGE_BLOCK_HEIGHT_NVD_BLOCK_HEIGHT_FOUR_GOBS              (0x00000002)
#define NVCA7E_SET_STORAGE_BLOCK_HEIGHT_NVD_BLOCK_HEIGHT_EIGHT_GOBS             (0x00000003)
#define NVCA7E_SET_STORAGE_BLOCK_HEIGHT_NVD_BLOCK_HEIGHT_SIXTEEN_GOBS           (0x00000004)
#define NVCA7E_SET_STORAGE_BLOCK_HEIGHT_NVD_BLOCK_HEIGHT_THIRTYTWO_GOBS         (0x00000005)
#define NVCA7E_SET_PARAMS                                                       (0x0000022C)
#define NVCA7E_SET_PARAMS_FORMAT                                                7:0
#define NVCA7E_SET_PARAMS_FORMAT_I8                                             (0x0000001E)
#define NVCA7E_SET_PARAMS_FORMAT_R4G4B4A4                                       (0x0000002F)
#define NVCA7E_SET_PARAMS_FORMAT_R5G6B5                                         (0x000000E8)
#define NVCA7E_SET_PARAMS_FORMAT_A1R5G5B5                                       (0x000000E9)
#define NVCA7E_SET_PARAMS_FORMAT_R5G5B5A1                                       (0x0000002E)
#define NVCA7E_SET_PARAMS_FORMAT_A8R8G8B8                                       (0x000000CF)
#define NVCA7E_SET_PARAMS_FORMAT_X8R8G8B8                                       (0x000000E6)
#define NVCA7E_SET_PARAMS_FORMAT_A8B8G8R8                                       (0x000000D5)
#define NVCA7E_SET_PARAMS_FORMAT_X8B8G8R8                                       (0x000000F9)
#define NVCA7E_SET_PARAMS_FORMAT_A2R10G10B10                                    (0x000000DF)
#define NVCA7E_SET_PARAMS_FORMAT_A2B10G10R10                                    (0x000000D1)
#define NVCA7E_SET_PARAMS_FORMAT_R16_G16_B16_A16_NVBIAS                         (0x00000023)
#define NVCA7E_SET_PARAMS_FORMAT_R16_G16_B16_A16                                (0x000000C6)
#define NVCA7E_SET_PARAMS_FORMAT_RF16_GF16_BF16_AF16                            (0x000000CA)
#define NVCA7E_SET_PARAMS_FORMAT_Y8_U8__Y8_V8_N422                              (0x00000028)
#define NVCA7E_SET_PARAMS_FORMAT_U8_Y8__V8_Y8_N422                              (0x00000029)
#define NVCA7E_SET_PARAMS_FORMAT_Y8___U8V8_N444                                 (0x00000035)
#define NVCA7E_SET_PARAMS_FORMAT_Y8___U8V8_N422                                 (0x00000036)
#define NVCA7E_SET_PARAMS_FORMAT_Y8___V8U8_N420                                 (0x00000038)
#define NVCA7E_SET_PARAMS_FORMAT_Y8___U8___V8_N444                              (0x0000003A)
#define NVCA7E_SET_PARAMS_FORMAT_Y8___U8___V8_N420                              (0x0000003B)
#define NVCA7E_SET_PARAMS_FORMAT_Y10___U10V10_N444                              (0x00000055)
#define NVCA7E_SET_PARAMS_FORMAT_Y10___U10V10_N422                              (0x00000056)
#define NVCA7E_SET_PARAMS_FORMAT_Y10___V10U10_N420                              (0x00000058)
#define NVCA7E_SET_PARAMS_FORMAT_Y12___U12V12_N444                              (0x00000075)
#define NVCA7E_SET_PARAMS_FORMAT_Y12___U12V12_N422                              (0x00000076)
#define NVCA7E_SET_PARAMS_FORMAT_Y12___V12U12_N420                              (0x00000078)
#define NVCA7E_SET_PARAMS_CLAMP_BEFORE_BLEND                                    18:18
#define NVCA7E_SET_PARAMS_CLAMP_BEFORE_BLEND_DISABLE                            (0x00000000)
#define NVCA7E_SET_PARAMS_CLAMP_BEFORE_BLEND_ENABLE                             (0x00000001)
#define NVCA7E_SET_PARAMS_SWAP_UV                                               19:19
#define NVCA7E_SET_PARAMS_SWAP_UV_DISABLE                                       (0x00000000)
#define NVCA7E_SET_PARAMS_SWAP_UV_ENABLE                                        (0x00000001)
#define NVCA7E_SET_PARAMS_FMT_ROUNDING_MODE                                     22:22
#define NVCA7E_SET_PARAMS_FMT_ROUNDING_MODE_ROUND_TO_NEAREST                    (0x00000000)
#define NVCA7E_SET_PARAMS_FMT_ROUNDING_MODE_ROUND_DOWN                          (0x00000001)
#define NVCA7E_SET_PLANAR_STORAGE(b)                                            (0x00000230 + (b)*0x00000004)
#define NVCA7E_SET_PLANAR_STORAGE_PITCH                                         12:0
#define NVCA7E_SET_POINT_IN(b)                                                  (0x00000290 + (b)*0x00000004)
#define NVCA7E_SET_POINT_IN_X                                                   15:0
#define NVCA7E_SET_POINT_IN_Y                                                   31:16
#define NVCA7E_SET_SIZE_IN                                                      (0x00000298)
#define NVCA7E_SET_SIZE_IN_WIDTH                                                15:0
#define NVCA7E_SET_SIZE_IN_HEIGHT                                               31:16
#define NVCA7E_SET_SIZE_OUT                                                     (0x000002A4)
#define NVCA7E_SET_SIZE_OUT_WIDTH                                               15:0
#define NVCA7E_SET_SIZE_OUT_HEIGHT                                              31:16
#define NVCA7E_SET_PRESENT_CONTROL                                              (0x00000308)
#define NVCA7E_SET_PRESENT_CONTROL_MIN_PRESENT_INTERVAL                         3:0
#define NVCA7E_SET_PRESENT_CONTROL_BEGIN_MODE                                   6:4
#define NVCA7E_SET_PRESENT_CONTROL_BEGIN_MODE_NON_TEARING                       (0x00000000)
#define NVCA7E_SET_PRESENT_CONTROL_BEGIN_MODE_IMMEDIATE                         (0x00000001)
#define NVCA7E_SET_PRESENT_CONTROL_TIMESTAMP_MODE                               8:8
#define NVCA7E_SET_PRESENT_CONTROL_TIMESTAMP_MODE_DISABLE                       (0x00000000)
#define NVCA7E_SET_PRESENT_CONTROL_TIMESTAMP_MODE_ENABLE                        (0x00000001)
#define NVCA7E_SET_PRESENT_CONTROL_STEREO_MODE                                  13:12
#define NVCA7E_SET_PRESENT_CONTROL_STEREO_MODE_MONO                             (0x00000000)
#define NVCA7E_SET_PRESENT_CONTROL_STEREO_MODE_PAIR_FLIP                        (0x00000001)
#define NVCA7E_SET_PRESENT_CONTROL_STEREO_MODE_AT_ANY_FRAME                     (0x00000002)
#define NVCA7E_SET_ILUT_CONTROL                                                 (0x00000440)
#define NVCA7E_SET_ILUT_CONTROL_INTERPOLATE                                     0:0
#define NVCA7E_SET_ILUT_CONTROL_INTERPOLATE_DISABLE                             (0x00000000)
#define NVCA7E_SET_ILUT_CONTROL_INTERPOLATE_ENABLE                              (0x00000001)
#define NVCA7E_SET_ILUT_CONTROL_MIRROR                                          1:1
#define NVCA7E_SET_ILUT_CONTROL_MIRROR_DISABLE                                  (0x00000000)
#define NVCA7E_SET_ILUT_CONTROL_MIRROR_ENABLE                                   (0x00000001)
#define NVCA7E_SET_ILUT_CONTROL_MODE                                            3:2
#define NVCA7E_SET_ILUT_CONTROL_MODE_SEGMENTED                                  (0x00000000)
#define NVCA7E_SET_ILUT_CONTROL_MODE_DIRECT8                                    (0x00000001)
#define NVCA7E_SET_ILUT_CONTROL_MODE_DIRECT10                                   (0x00000002)
#define NVCA7E_SET_ILUT_CONTROL_SIZE                                            18:8
#define NVCA7E_SET_SURFACE_ADDRESS_HI_NOTIFIER                                  (0x00000650)
#define NVCA7E_SET_SURFACE_ADDRESS_HI_NOTIFIER_ADDRESS_HI                       31:0
#define NVCA7E_SET_SURFACE_ADDRESS_LO_NOTIFIER                                  (0x00000654)
#define NVCA7E_SET_SURFACE_ADDRESS_LO_NOTIFIER_ADDRESS_LO                       31:4
#define NVCA7E_SET_SURFACE_ADDRESS_LO_NOTIFIER_TARGET                           3:2
#define NVCA7E_SET_SURFACE_ADDRESS_LO_NOTIFIER_TARGET_IOVA                      (0x00000000)
#define NVCA7E_SET_SURFACE_ADDRESS_LO_NOTIFIER_TARGET_PHYSICAL_NVM              (0x00000001)
#define NVCA7E_SET_SURFACE_ADDRESS_LO_NOTIFIER_TARGET_PHYSICAL_PCI              (0x00000002)
#define NVCA7E_SET_SURFACE_ADDRESS_LO_NOTIFIER_TARGET_PHYSICAL_PCI_COHERENT     (0x00000003)
#define NVCA7E_SET_SURFACE_ADDRESS_LO_NOTIFIER_ENABLE                           0:0
#define NVCA7E_SET_SURFACE_ADDRESS_LO_NOTIFIER_ENABLE_DISABLE                   (0x00000000)
#define NVCA7E_SET_SURFACE_ADDRESS_LO_NOTIFIER_ENABLE_ENABLE                    (0x00000001)
#define NVCA7E_SET_SURFACE_ADDRESS_HI_ISO(b)                                    (0x00000658 + (b)*0x00000004)
#define NVCA7E_SET_SURFACE_ADDRESS_HI_ISO_ADDRESS_HI                            31:0
#define NVCA7E_SET_SURFACE_ADDRESS_LO_ISO(b)                                    (0x00000670 + (b)*0x00000004)
#define NVCA7E_SET_SURFACE_ADDRESS_LO_ISO_ADDRESS_LO                            31:4
#define NVCA7E_SET_SURFACE_ADDRESS_LO_ISO_TARGET                                3:2
#define NVCA7E_SET_SURFACE_ADDRESS_LO_ISO_TARGET_IOVA                           (0x00000000)
#define NVCA7E_SET_SURFACE_ADDRESS_LO_ISO_TARGET_PHYSICAL_NVM                   (0x00000001)
#define NVCA7E_SET_SURFACE_ADDRESS_LO_ISO_TARGET_PHYSICAL_PCI                   (0x00000002)
#define NVCA7E_SET_SURFACE_ADDRESS_LO_ISO_TARGET_PHYSICAL_PCI_COHERENT          (0x00000003)
#define NVCA7E_SET_SURFACE_ADDRESS_LO_ISO_KIND                                  1:1
#define NVCA7E_SET_SURFACE_ADDRESS_LO_ISO_KIND_PITCH                            (0x00000000)
#define NVCA7E_SET_SURFACE_ADDRESS_LO_ISO_KIND_BLOCKLINEAR                      (0x00000001)
#define NVCA7E_SET_SURFACE_ADDRESS_LO_ISO_ENABLE                                0:0
#define NVCA7E_SET_SURFACE_ADDRESS_LO_ISO_ENABLE_DISABLE                        (0x00000000)
#define NVCA7E_SET_SURFACE_ADDRESS_LO_ISO_ENABLE_ENABLE                         (0x00000001)
#define NVCA7E_SET_SURFACE_ADDRESS_HI_ILUT                                      (0x00000688)
#define NVCA7E_SET_SURFACE_ADDRESS_HI_ILUT_ADDRESS_HI                           31:0
#define NVCA7E_SET_SURFACE_ADDRESS_LO_ILUT                                      (0x0000068C)
#define NVCA7E_SET_SURFACE_ADDRESS_LO_ILUT_ADDRESS_LO                           31:4
#define NVCA7E_SET_SURFACE_ADDRESS_LO_ILUT_TARGET                               3:2
#define NVCA7E_SET_SURFACE_ADDRESS_LO_ILUT_TARGET_IOVA                          (0x00000000)
#define NVCA7E_SET_SURFACE_ADDRESS_LO_ILUT_TARGET_PHYSICAL_NVM                  (0x00000001)
#define NVCA7E_SET_SURFACE_ADDRESS_LO_ILUT_TARGET_PHYSICAL_PCI                  (0x00000002)
#define NVCA7E_SET_SURFACE_ADDRESS_LO_ILUT_TARGET_PHYSICAL_PCI_COHERENT         (0x00000003)
#define NVCA7E_SET_SURFACE_ADDRESS_LO_ILUT_ENABLE                               0:0
#define NVCA7E_SET_SURFACE_ADDRESS_LO_ILUT_ENABLE_DISABLE                       (0x00000000)
#define NVCA7E_SET_SURFACE_ADDRESS_LO_ILUT_ENABLE_ENABLE                        (0x00000001)

#endif // _clca7e_h
