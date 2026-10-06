/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#if CONFIG_MUSE_BOARD_M5STACK_ATOMS3R && CONFIG_MUSE_REPLY_IMAGES
#define MUSE_ATOM_IMAGE_POLICY "[Request source: ATOMS3R. For image-generation requests from this device: generate a square image and export an actual 128x128 pixel PNG or baseline JPEG file. If needed, resize locally. Read and base64-encode the file using code, then call display.draw_base64 with data_base64 (max 49152 characters). Do not upload it to public storage or call remote-storage upload-file. Do not put base64 in spoken replies. These image rules apply only to ATOMS3R requests; answer other requests normally.] "
#else
#define MUSE_ATOM_IMAGE_POLICY ""
#endif
