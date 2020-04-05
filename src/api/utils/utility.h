#pragma once

#include "enums.h"

#include <cmath>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include <vips/vips8>

namespace weserv {
namespace api {
namespace utils {

using enums::ImageType;
using enums::Output;
using enums::Position;
using vips::VImage;

#ifndef __has_builtin       // Optional of course.
#define __has_builtin(x) 0  // Compatibility with non-clang compilers.
#endif

/**
 * Performs a compile-time version check for libvips.
 */
#define VIPS_VERSION_AT_LEAST(major, minor, patch)                             \
    (((major) < VIPS_MAJOR_VERSION) ||                                         \
     ((major) == VIPS_MAJOR_VERSION && (minor) < VIPS_MINOR_VERSION) ||        \
     ((major) == VIPS_MAJOR_VERSION && (minor) == VIPS_MINOR_VERSION &&        \
      (patch) <= VIPS_MICRO_VERSION))

/**
 * Are pixel values in this image 16-bit integer?
 * @param interpretation The VipsInterpretation.
 * @return A bool indicating if the pixel values in this image are 16-bit.
 */
inline bool is_16_bit(const VipsInterpretation interpretation) {
    return interpretation == VIPS_INTERPRETATION_RGB16 ||
           interpretation == VIPS_INTERPRETATION_GREY16;
}

/**
 * Return the image alpha maximum. Useful for combining alpha bands. scRGB
 * images are 0 - 1 for image data, but the alpha is 0 - 255.
 * @param interpretation The VipsInterpretation.
 * @return The image alpha maximum.
 */
inline int maximum_image_alpha(const VipsInterpretation interpretation) {
    return is_16_bit(interpretation) ? 65535 : 255;
}

/**
 * Does this image have an embedded profile?
 * @param image The source image.
 * @return A bool indicating if this image have an embedded profile.
 */
inline bool has_profile(const VImage &image) {
    return image.get_typeof(VIPS_META_ICC_NAME) != 0;
}

/**
 * Does this image have a non-default density?
 * @param image The source image.
 * @return A bool indicating if this image have a non-default density.
 */
inline bool has_density(const VImage &image) {
    return image.xres() > 1.0;
}

/**
 * Get pixels/mm resolution as pixels/inch density.
 * @param image The source image.
 * @return pixels/inch density.
 */
inline int get_density(const VImage &image) {
    return static_cast<int>(std::round(image.xres() * 25.4));
}

/**
 * Multi-page images can have a page height. Fetch it, and sanity check it.
 * If page-height is not set, it defaults to the image height.
 * @param image The source image.
 * @return The image page height.
 */
inline int get_page_height(const VImage &image) {
    return vips_image_get_page_height(image.get_image());
}

/**
 * Get EXIF Orientation of image, if any.
 * @param image The source image.
 * @return EXIF Orientation.
 */
inline int exif_orientation(const VImage &image) {
    return image.get_typeof(VIPS_META_ORIENTATION) != 0
               ? image.get_int(VIPS_META_ORIENTATION)
               : 0;
}

/**
 * Ensures alpha channel, if missing.
 * @param image The source image.
 * @return An image with an alpha channel.
 */
inline VImage ensure_alpha(const VImage &image) {
    if (!image.has_alpha()) {
        std::vector<double> alpha;
        alpha.emplace_back(maximum_image_alpha(image.interpretation()));
        return image.bandjoin_const(alpha);
    }
    return image;
}

/**
 * Insert a line cache to prevent over-computation of
 * any previous operations in the pipeline.
 * @param image The source image.
 * @param tile_height Tile height in pixels
 * @return A new image.
 */
inline VImage line_cache(const VImage &image, const int tile_height) {
    return image.linecache(VImage::option()
                               ->set("tile_height", tile_height)
                               ->set("access", VIPS_ACCESS_SEQUENTIAL)
                               ->set("threaded", true));
}

/**
 * Calculate the rotation for the given angle.
 * @note Assumes that a positive angle is given which is a multiple of 90.
 * @return Rotation as VipsAngle.
 */
inline VipsAngle resolve_angle_rotation(const int angle) {
    switch (angle) {
        case 90:
            return VIPS_ANGLE_D90;
        case 180:
            return VIPS_ANGLE_D180;
        case 270:
            return VIPS_ANGLE_D270;
        default:  // LCOV_EXCL_START
            return VIPS_ANGLE_D0;
    }
    // LCOV_EXCL_STOP
}

/**
 * Determine image extension from the output enum.
 * @note The return value also defines which extension is allowed to
 *       pass on to the selected save operation.
 * @param output The output enum.
 * @return The image extension.
 */
inline std::string determine_image_extension(const Output &output) {
    switch (output) {
        case Output::Jpeg:
            return ".jpg";
        case Output::Webp:
            return ".webp";
        case Output::Tiff:
            return ".tiff";
        case Output::Gif:
            return ".gif";
        case Output::Png:
        default:
            return ".png";
    }
}

/**
 * Determine the output from the image type enum.
 * @param image_type The image type enum.
 * @return The image output.
 */
inline Output to_output(const ImageType &image_type) {
    switch (image_type) {
        case ImageType::Jpeg:
            return Output::Jpeg;
        case ImageType::Webp:
            return Output::Webp;
        case ImageType::Tiff:
            return Output::Tiff;
        case ImageType::Gif:
            return Output::Gif;
        case ImageType::Png:
        default:
            return Output::Png;
    }
}

/**
 * Determine image type from the name of the load operation.
 * @param loader The name of the load operation.
 * @return The image type.
 */
inline ImageType determine_image_type(const std::string &loader) {
    if (loader.rfind("VipsForeignLoadJpeg", 0) == 0) {
        return ImageType::Jpeg;
    } else if (loader.rfind("VipsForeignLoadPng", 0) == 0) {
        return ImageType::Png;
    } else if (loader.rfind("VipsForeignLoadWebp", 0) == 0) {
        return ImageType::Webp;
    } else if (loader.rfind("VipsForeignLoadTiff", 0) == 0) {
        return ImageType::Tiff;
    } else if (loader.rfind("VipsForeignLoadGif", 0) == 0) {
        return ImageType::Gif;
    } else if (loader.rfind("VipsForeignLoadSvg", 0) == 0) {
        return ImageType::Svg;
    } else if (loader.rfind("VipsForeignLoadPdf", 0) == 0) {
        return ImageType::Pdf;
    } else if (loader.rfind("VipsForeignLoadHeif", 0) == 0) {
        return ImageType::Heif;
    } else if (loader.rfind("VipsForeignLoadMagick", 0) == 0) {
        return ImageType::Magick;
    } else {  // LCOV_EXCL_START
        return ImageType::Unknown;
    }
    // LCOV_EXCL_STOP
}

/**
 * Does this loader support multiple pages?
 * @param loader The name of the load operation.
 * @return A bool indicating if this loader support multiple pages.
 */
inline bool image_loader_supports_page(const std::string &loader) {
    return loader.rfind("VipsForeignLoadPdf", 0) == 0 ||
           loader.rfind("VipsForeignLoadGif", 0) == 0 ||
           loader.rfind("VipsForeignLoadTiff", 0) == 0 ||
           loader.rfind("VipsForeignLoadWebp", 0) == 0 ||
           loader.rfind("VipsForeignLoadHeif", 0) == 0 ||
           loader.rfind("VipsForeignLoadMagick", 0) == 0;
}

/**
 * Provide a string identifier for the given image type.
 * @param image_type The image type enum.
 * @return A string identifier for the given image type.
 */
inline std::string image_type_id(const ImageType &image_type) {
    switch (image_type) {
        case ImageType::Jpeg:
            return "jpeg";
        case ImageType::Png:
            return "png";
        case ImageType::Webp:
            return "webp";
        case ImageType::Tiff:
            return "tiff";
        case ImageType::Gif:
            return "gif";
        case ImageType::Svg:
            return "svg";
        case ImageType::Pdf:
            return "pdf";
        case ImageType::Heif:
            return "heif";
        case ImageType::Magick:
            return "magick";
        case ImageType::Unknown:  // LCOV_EXCL_START
        default:
            return "unknown";
    }
    // LCOV_EXCL_STOP
}

/**
 * Does this image type support alpha channel?
 * @param image_type Image type to check.
 * @return A bool indicating if the alpha channel is supported.
 */
inline bool support_alpha_channel(const ImageType &image_type) {
    return image_type == ImageType::Png || image_type == ImageType::Webp ||
           image_type == ImageType::Tiff || image_type == ImageType::Gif;
}

/**
 * Calculate the (left, top) coordinates of the output image
 * within the input image, applying the given Position.
 * @param in_width In width.
 * @param in_height In height.
 * @param out_width Out width.
 * @param out_height Out height.
 * @param pos The given Position.
 * @return The (left, top) position as pair.
 */
inline std::pair<int, int>
calculate_position(const int in_width, const int in_height, const int out_width,
                   const int out_height, const Position pos) {
    int left = 0;
    int top = 0;
    switch (pos) {
        case Position::Top:
            left = (out_width - in_width) / 2;
            break;
        case Position::Right:
            left = out_width - in_width;
            top = (out_height - in_height) / 2;
            break;
        case Position::Bottom:
            left = (out_width - in_width) / 2;
            top = out_height - in_height;
            break;
        case Position::Left:
            top = (out_height - in_height) / 2;
            break;
        case Position::TopRight:
            left = out_width - in_width;
            break;
        case Position::BottomRight:
            left = out_width - in_width;
            top = out_height - in_height;
            break;
        case Position::BottomLeft:
            top = out_height - in_height;
            break;
        case Position::TopLeft:
            // Which is the default is 0,0 so we do not assign anything here
            break;
        default:
            // Centre
            left = (out_width - in_width) / 2;
            top = (out_height - in_height) / 2;
    }

    return std::make_pair(left, top);
}

/**
 * Convenient function to convert an image to a JSON representation.
 * @param image The source image.
 * @param image_type Image type of the image.
 * @return A JSON representation of the image.
 */
inline std::string image_to_json(const VImage &image,
                                 const ImageType &image_type) {
    std::ostringstream json;
    json << "{"
         << R"("format":")" << image_type_id(image_type) << "\","
         << R"("width":)" << image.width() << ","
         << R"("height":)" << image.height() << ","
         << R"("space":")"
         << vips_enum_nick(VIPS_TYPE_INTERPRETATION, image.interpretation())
         << "\","
         << R"("channels":)" << image.bands() << ","
         << R"("depth":")"
         << vips_enum_nick(VIPS_TYPE_BAND_FORMAT, image.format()) << "\",";
    if (has_density(image)) {
        json << R"("density":)" << get_density(image) << ",";
    }
    if (image.get_typeof("jpeg-chroma-subsample") != 0) {
        json << R"("chromaSubsampling":")"
             << image.get_string("jpeg-chroma-subsample") << "\",";
    }
    json << R"("isProgressive":)"
         << (image.get_typeof("interlaced") != 0 ? "true" : "false") << ",";
    if (image.get_typeof("palette-bit-depth") != 0) {
        json << R"("paletteBitDepth":)" << image.get_int("palette-bit-depth")
             << ",";
    }
    if (image.get_typeof(VIPS_META_N_PAGES) != 0) {
        json << R"("pages":)" << image.get_int(VIPS_META_N_PAGES) << ",";
    }
    if (image.get_typeof(VIPS_META_PAGE_HEIGHT) != 0) {
        json << R"("pageHeight":)" << image.get_int(VIPS_META_PAGE_HEIGHT)
             << ",";
    }
#if VIPS_VERSION_AT_LEAST(8, 9, 0)
    if (image.get_typeof("loop") != 0) {
        json << R"("loop":)" << image.get_int("loop") << ",";
    }
#else
    if (image.get_typeof("gif-loop") != 0) {
        json << R"("loop":)" << image.get_int("gif-loop") << ",";
    }
#endif
#if VIPS_VERSION_AT_LEAST(8, 9, 0)
    if (image.get_typeof("delay") != 0) {
        std::vector<int> delays = image.get_array_int("delay");

        json << R"("delay":[)";
        for (auto &delay : delays) {
            json << delay;
            if (&delay != &delays.back()) {
                json << ",";
            }
        }
        json << "],";
    }
#else
    if (image.get_typeof("gif-delay") != 0) {
        // libvips uses centiseconds (the GIF standard),
        // we use milliseconds for delays.
        json << R"("delay":[)" << image.get_int("gif-delay") * 10 << "],";
    }
#endif
    if (image.get_typeof("heif-primary") != 0) {
        json << R"("pagePrimary":)" << image.get_int("heif-primary") << ",";
    }

    json << R"("hasProfile":)" << (has_profile(image) ? "true" : "false") << ","
         << R"("hasAlpha":)" << (image.has_alpha() ? "true" : "false") << ","
         << R"("orientation":)" << exif_orientation(image) << "}";

    return json.str();
}

/**
 * Escape a string by replacing certain special characters.
 * @param s The string to escape.
 * @return The escaped string.
 */
inline std::string escape_string(const std::string &s) {  // LCOV_EXCL_START
    std::ostringstream o;
    for (char c : s) {
        switch (c) {
            case '\x00':
                o << "\\u0000";
                break;
            case '\x01':
                o << "\\u0001";
                break;
            case '\x0a':
                o << "\\n";
                break;
            case '\x1f':
                o << "\\u001f";
                break;
            case '\x22':
                o << "\\\"";
                break;
            case '\x5c':
                o << "\\\\";
                break;
            default:
                o << c;
        }
    }

    return o.str();
}
// LCOV_EXCL_STOP

/**
 * Template function which takes a strongly typed enumerator
 * and returns its value as a compile-time constant.
 */
template <typename E>
constexpr inline typename std::underlying_type<E>::type underlying_value(E e) {
    return static_cast<typename std::underlying_type<E>::type>(e);
}

/**
 * Handle an integer multiplication overflow.
 * @param a Value of the expression on the left hand side.
 * @param b Value of the expression on the right hand side.
 * @param result Pointer to the product of the two values.
 * @return A bool indicating whether the operation overflowed.
 */
inline bool mul_overflow(int a, int b, int *result) {
#if __GNUC__ >= 5 || __has_builtin(__builtin_smul_overflow)
    return __builtin_smul_overflow(a, b, result);
#else
    int64_t t = static_cast<int64_t>(a) * b;
    *result = static_cast<int>(t);
    return t > std::numeric_limits<int>::max() ||
           t < std::numeric_limits<int>::min();
#endif
}

}  // namespace utils
}  // namespace api
}  // namespace weserv
