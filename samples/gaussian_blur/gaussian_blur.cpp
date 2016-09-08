/***************************************************************************
 *
 *  Copyright (C) 2016 Codeplay Software Limited
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  For your convenience, a copy of the License has been included in this
 *  repository.
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  Codeplay's ComputeCpp SDK
 *
 *  gaussian_blur.cpp
 *
 *  Description:
 *    Sample code that blurs an image provided on the command line.
 *
 **************************************************************************/

/* This code implements a Gaussian blur filter, blurring a JPG or PNG image
 * from the command line. The original file is not modified. */

#include <CL/sycl.hpp>

#include <iostream>

/* These public-domain headers implement useful image reading and writing
 * functions. */
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

using namespace cl::sycl;
/* It is possible to refer to the enum name in these using statements, used
 * here to make referencing the members more convenient (for example). */
using co = cl::sycl::image_format::channel_order;
using ct = cl::sycl::image_format::channel_type;

/* Attempts to determine a good local size. The OpenCL implementation can
 * do the same, but the best way to *control* performance is to choose the
 * sizes. The method here is to choose the largest number, leq 64, which is
 * a power-of-two, and divides the global work size evenly. In this code,
 * it might prove most optimal to pad the image along one dimension so that
 * the local size could be 64, but this introduces other complexities. */
range<2> get_optimal_local_range(cl::sycl::range<2> globalSize,
                                 cl::sycl::device d) {
  range<2> optimalLocalSize;
  /* 64 is a good local size on GPU-like devices, as each compute unit is
   * made of many smaller processors. On non-GPU devices, 4 is a common vector
   * width. */
  if (d.is_gpu()) {
    optimalLocalSize = range<2>(64, 1);
  } else {
    optimalLocalSize = range<2>(4, 1);
  }
  /* Here, for each dimension, we make sure that it divides the global size
   * evenly. If it doesn't, we try the next lowest power of two. Eventually
   * it will reach one, if the global size has no power of two component. */
  for (int i = 0; i < 2; ++i) {
    while (globalSize[i] % optimalLocalSize[i]) {
      optimalLocalSize[i] = optimalLocalSize[i] >> 1;
    }
  }
  return optimalLocalSize;
}

int main(int argc, char *argv[]) {
  /* The image dimensions will be set by the library, as will the number of
   * channels. However, passing a number of channels will force the image
   * data to be returned in that format, regardless of what the original image
   * looked like. The header has a mapping from int values to types - 4 means
   * RGBA. */
  int inputWidth;
  int inputHeight;
  int inputChannels;
  /* The data is returned as an unsigned char *, but due to OpenCL
   * restrictions, we must use it as a void *. Data is deallocated on program
   * exit. */
  const int numChannels = 4;
  void *inputData = nullptr;
  void *outputData = nullptr;

  if (argc < 2) {
    std::cout
        << "Please provide a JPEG or PNG image as an argument to this program."
        << std::endl;
  }

  inputData = stbi_load(argv[1], &inputWidth, &inputHeight, &inputChannels,
                        numChannels);
  if (inputData == nullptr) {
    std::cout << "Failed to load image file (is argv[1] a valid image file?)"
              << std::endl;
    return 1;
  }
  outputData = new char[inputWidth * inputHeight * numChannels];

  /* This range represents the full amount of work to be done across the
   * image. We dispatch one thread per pixel. */
  range<2> imgRange(inputWidth, inputHeight);
  queue myQueue;
  {
    /* Images need a void * pointing to the data, and enums describing the
     * type of the image (since a void * carries no type information). It
     * also needs a range which descrbes the image's dimensions. */
    image<2> image_in(inputData, co::RGBA, ct::UNORM_INT8, imgRange);
    image<2> image_out(outputData, co::RGBA, ct::UNORM_INT8, imgRange);

    myQueue.submit([&](handler &cgh) {
      /* The nd_range contains the total work (as mentioned previously) as
       * well as the local work size (i.e. the number of threads in the local
       * group). Here, we attempt to find a range close to the device's
       * preferred size that also divides the global size neatly. */
      auto r = get_optimal_local_range(imgRange, myQueue.get_device());
      auto myRange = nd_range<2>(imgRange, r);
      /* Images still require accessors, like buffers, except the target is
       * always access::target::image. */
      accessor<float4, 2, access::mode::read, access::target::image> inPtr(
          image_in, cgh);
      accessor<float4, 2, access::mode::write, access::target::image> outPtr(
          image_out, cgh);
      /* The sampler is used to map user-provided co-ordinates to pixels in
       * the image. */
      sampler smpl(false, sampler_addressing_mode::clamp,
                   sampler_filter_mode::nearest);

      cgh.parallel_for<class GaussianKernel>(myRange, ([=](nd_item<2> itemID) {
        const int blendMask = 10;
        float4 newPixel = float4(0.0f, 0.0f, 0.0f, 0.0f);

        for (int x = -(blendMask / 2); x < (blendMask / 2); x++) {
          for (int y = -(blendMask / 2); y < (blendMask / 2); y++) {
            auto inputCoords =
                int2(itemID.get_global(0) + x, itemID.get_global(1) + y);
            newPixel += inPtr(smpl)[inputCoords];
          }
        }

        newPixel /= (float)(blendMask * blendMask);

        auto outputCoords = int2(itemID.get_global(0), itemID.get_global(1));

        outPtr[outputCoords] = newPixel;
      }));
    });
  }

  /* Attempt to change the name from x.png or x.jpg to x-blurred.png and so
   * on. If the code cannot find a '.', it simply appends "-blurred" to the
   * name. */
  std::string outputFilePath;
  std::string inputName(argv[1]);
  auto pos = inputName.find_last_of(".");
  if (pos == -1) {
    outputFilePath = inputName + "-blurred";
  } else {
    std::string ext = inputName.substr(pos, inputName.size() - pos);
    inputName.erase(pos, inputName.size());
    outputFilePath = inputName + "-blurred" + ext;
  }

  stbi_write_png(outputFilePath.c_str(), inputWidth, inputHeight, inputChannels,
                 outputData, 0);

  std::cout << "Image successfully blurred!\n";
  return 0;
}
