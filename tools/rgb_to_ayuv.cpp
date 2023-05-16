#include <fstream>
#include <iostream>

#include <vector>

#include <stdio.h>

int main(int argc, char *argv[])
{
    if (argc != 3) {
        std::cerr << "usage: ./" << argv[0] << " <input> <output>" << std::endl;
        std::cerr << "   ex: ./" << argv[0] << " input.data output.ayuv" << std::endl;
        std::cerr << "" << std::endl;
        std::cerr << "input should be RGBA8888 (24-bit) raw image data file (no BMP with headers etc.)." << std::endl;
        std::cerr << " in gimp: export as .data (raw image data, choose RGB, alpha channel will be included.)" << std::endl;
        std::cerr << "output should be AYUV8888 (24-bit) yuv file." << std::endl;
        return 1;
    }

    std::string input(argv[1]);
    std::string output(argv[2]);

    std::ifstream file(input, std::ios::binary | std::ios::ate);
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> buffer(size);
    std::cout << "size = " << size << " / " << buffer.size() << std::endl;
    if (!file.read(buffer.data(), size)) {
        std::cerr << "Reading file failed." << std::endl;
        return 1;
    }
    std::cout << "size = " << size << " / " << buffer.size() << std::endl;

    for (size_t i=0; i<size; i+=4) {
        uint8_t& r = *((uint8_t *)(&buffer[i  ]));
        uint8_t& g = *((uint8_t *)(&buffer[i+1]));
        uint8_t& b = *((uint8_t *)(&buffer[i+2]));
        uint8_t& a = *((uint8_t *)(&buffer[i+3]));

        uint8_t y = (( 66 * r + 129 * g +  25 * b + 128) >> 8) + 16;
        uint8_t u = ((-38 * r -  74 * g + 112 * b + 128) >> 8) + 128;
        uint8_t v = ((112 * r -  94 * g -  18 * b + 128) >> 8) + 128;
        uint8_t a2 = a;

        // printf("ARGB: %d, %d, %d, %d\n", a, r, g, b);
        // printf("AYUV: %d, %d, %d, %d\n", a, y, u, v);

        r = a2;
        g = y;
        b = u;
        a = v;
    }

    std::ofstream file_out(output, std::ios::binary);
    file_out.write((char *)&buffer[0], buffer.size() * sizeof(char));

    return 0;
}
