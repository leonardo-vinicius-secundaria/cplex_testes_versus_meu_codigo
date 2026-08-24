#include "instance.hpp"

#include <fstream>
#include <utility>

bool readORLibraryInstance(const std::string& filePath, Instance& instance)
{
    std::ifstream input(filePath);
    if (!input) return false;

    Instance loaded;
    input >> loaded.elementCount >> loaded.setCount;

    loaded.setCosts.assign(loaded.setCount + 1, 0);
    for (int set = 1; set <= loaded.setCount; set++)
        input >> loaded.setCosts[set];

    loaded.elementsBySet.assign(loaded.setCount + 1, {});

    for (int element = 1; element <= loaded.elementCount; element++)
    {
        int coveringSetCount;
        input >> coveringSetCount;

        for (int i = 0; i < coveringSetCount; i++)
        {
            int set;
            input >> set;
            loaded.elementsBySet[set].push_back(element);
            loaded.incidenceCount++;
        }
    }

    if (!input) return false;

    instance = std::move(loaded);
    return true;
}
