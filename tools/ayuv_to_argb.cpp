#include <fstream>
#include <iostream>

#include <vector>

#include <stdio.h>

int main(int argc, char *argv[])
{
    if (argc != 3) {
        std::cerr << "usage: ./" << argv[0] << " <input> <output>" << std::endl;
        std::cerr << "   ex: ./" << argv[0] << " input.ayuv output.data" << std::endl;
        std::cerr << "" << std::endl;
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
        uint8_t& a = *((uint8_t *)(&buffer[i  ]));
        uint8_t& y = *((uint8_t *)(&buffer[i+1]));
        uint8_t& u = *((uint8_t *)(&buffer[i+2]));
        uint8_t& v = *((uint8_t *)(&buffer[i+3]));

        uint8_t r  = y +                       + (v - 128) *  1.40200;
        uint8_t g  = y + (u - 128) * -0.34414 + (v - 128) * -0.71414;
        uint8_t b  = y + (u - 128) *  1.77200;
        uint8_t a2 = a;

        a = r;
        y = g;
        u = b;
        v = a2;
    }

    std::ofstream file_out(output, std::ios::binary);
    file_out.write((char *)&buffer[0], buffer.size() * sizeof(char));

    return 0;
}
