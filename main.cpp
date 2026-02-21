#include "nfc_eink.hpp"
#include "dither.hpp"
#include "image.hpp"

#include <iostream>
#include <string>
#include <cstring>

static void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " <image_path> [options]\n"
              << "       " << prog << " --clear\n"
              << "       " << prog << " --info\n"
              << "\n"
              << "NFC E-Paper Image Uploader (Santek EZ Sign 2.9\" 4-color, C++ / libnfc)\n"
              << "\n"
              << "Options:\n"
              << "  --bg <black|white>       Background color (default: black)\n"
              << "  --dither <atkinson|none>  Dithering algorithm (default: atkinson)\n"
              << "  --resize <fit|cover>     Resize mode (default: fit)\n"
              << "  --clear                  Clear the screen to white\n"
              << "  --info                   Display device information\n"
              << "  --help                   Show this help message\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    // Parse arguments
    std::string image_path;
    std::string bg_name = "black";
    std::string dither_name = "atkinson";
    std::string resize_mode = "fit";
    bool do_clear = false;
    bool do_info = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--clear") {
            do_clear = true;
        } else if (arg == "--info") {
            do_info = true;
        } else if (arg == "--bg" && i + 1 < argc) {
            bg_name = argv[++i];
        } else if (arg == "--dither" && i + 1 < argc) {
            dither_name = argv[++i];
        } else if (arg == "--resize" && i + 1 < argc) {
            resize_mode = argv[++i];
        } else if (arg[0] != '-') {
            image_path = arg;
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    }

    // Determine background color
    Color bg_color = {0, 0, 0};  // default: black
    if (bg_name == "white") {
        bg_color = {255, 255, 255};
    } else if (bg_name == "red") {
        bg_color = {255, 0, 0};
    } else if (bg_name == "yellow") {
        bg_color = {255, 255, 0};
    } else if (bg_name != "black") {
        std::cerr << "Unknown background color: " << bg_name << std::endl;
        return 1;
    }

    try {
        NfcEinkCard card;
        card.connect();

        const auto& info = card.device_info();
        int w = info.width;
        int h = info.height;

        if (do_info) {
            std::cout << "Serial No:  " << info.serial_number << std::endl;
            std::cout << "Screen:     " << w << "x" << h << std::endl;
            std::cout << "Colors:     " << info.num_colors() << std::endl;
            std::cout << "Bits/pixel: " << info.bits_per_pixel << std::endl;
            return 0;
        }

        if (do_clear) {
            std::cout << "Clearing display..." << std::endl;
            // All white (index 1)
            std::vector<std::vector<int>> pixels(h, std::vector<int>(w, 1));
            card.send_image(pixels);
            std::cout << "Refreshing display..." << std::endl;
            card.refresh();
            std::cout << "Done!" << std::endl;
            return 0;
        }

        if (image_path.empty()) {
            std::cerr << "Error: Please specify an image file." << std::endl;
            print_usage(argv[0]);
            return 1;
        }

        // Load and process image
        std::cout << "Loading: " << image_path << std::endl;
        std::cout << "Options: bg=" << bg_name << ", dither=" << dither_name
                  << ", resize=" << resize_mode << std::endl;

        auto rgb = load_and_resize_image(image_path.c_str(), w, h, bg_color, resize_mode);

        // Dither
        std::vector<std::vector<int>> pixels;
        if (dither_name == "atkinson") {
            pixels = dither_atkinson(rgb, w, h);
        } else if (dither_name == "none") {
            pixels = dither_none(rgb, w, h);
        } else {
            std::cerr << "Unknown dither method: " << dither_name << std::endl;
            return 1;
        }

        // Send
        std::cout << "Sending image..." << std::endl;
        card.send_image(pixels);
        std::cout << "Refreshing display..." << std::endl;
        card.refresh();
        std::cout << "Done!" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
