#ifndef INSTANCE_HPP
#define INSTANCE_HPP

#include <string>
#include <vector>

struct Instance
{
    int elementCount = 0;
    int setCount = 0;
    int incidenceCount = 0;

    // Vetores indexados a partir de 1 para coincidir com a OR-Library.
    std::vector<int> setCosts;
    std::vector<std::vector<int>> elementsBySet;
};

bool readORLibraryInstance(const std::string& filePath, Instance& instance);

#endif
