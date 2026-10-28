#include "../src/runtime/debug_tools.h"
#include <fstream>
#include <sstream>
#include <vector>
#include <iostream>

int main() {
    Config c; GyroState s;
    s.motion.ProcessMotion(1, 2, 3, 0, 0, 1, 0.01f);
    s.debugMx = 4; s.debugMy = -5;
    {
        GyroTrace disabled(false);
        GyroTrace trace(true);
        trace.record(0.6, 1, c, 1, 2, 3, 0, 0, 1, s);
        trace.record(0.6, 1, c, 1, 2, 3, 0, 0, 1, s);
        trace.record(600, 1, c, 1, 2, 3, 0, 0, 1, s);
        trace.record(0.6, 1, c, 1, 2, 3, 0, 0, 1, s); // must be ignored after limit
    }
    WIN32_FIND_DATAA data;
    HANDLE found = FindFirstFileA("gyro-trace-*.csv", &data);
    if (found == INVALID_HANDLE_VALUE) return 1;
    FindClose(found);
    std::ifstream file(data.cFileName);
    std::string header, line;
    std::getline(file, header);
    int rows = 0;
    while (std::getline(file, line)) {
        std::stringstream stream(line);
        std::vector<std::string> values;
        std::string value;
        while (std::getline(stream, value, ',')) values.push_back(value);
        if (values.size() != 21 || values[7] != "1" || values[8] != "2" ||
            values[9] != "3" || values[16] != "1" || values[17] != "2" ||
            values[18] != "3" || values[19] != "4" || values[20] != "-5") return 1;
        ++rows;
    }
    if (rows != 3) return 1;
    std::cout << "Gyro trace fields, flush and recording limit passed\n";
}
