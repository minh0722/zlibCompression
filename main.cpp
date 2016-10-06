#include "pch.h"
#include "Compressor.h"
#include "Decompressor.h"

std::chrono::time_point<std::chrono::steady_clock> t1;
std::chrono::time_point<std::chrono::steady_clock> t2;
long long duration;

#define CHRONO_BEGIN \
    t1 = std::chrono::high_resolution_clock::now();

#define CHRONO_END \
    t2 = std::chrono::high_resolution_clock::now(); \
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count(); \
    std::cout << "Time: " << duration << " milliseconds" << std::endl;

int main()
{
    Compressor compressor;

    //CHRONO_BEGIN;
    //compressor.compress(BIG_FILE_PATH, COMPRESSED_BIG_FILE);
    //CHRONO_END;

    Decompressor decompressor;
    //decompressor.decompress(COMPRESSED_BIG_FILE, DECOMPRESSED_BIG_FILE);

    return 0;
}