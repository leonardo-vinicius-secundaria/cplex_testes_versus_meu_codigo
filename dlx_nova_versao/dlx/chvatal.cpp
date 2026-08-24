#include "chvatal.hpp"

#include <algorithm>
#include <climits>
#include <utility>
#include <vector>

namespace
{
int removeRedundantSets(
    const Instance& instance,
    std::vector<int>& selectedSets
)
{
    std::sort(
        selectedSets.begin(),
        selectedSets.end(),
        [&](int a, int b)
        {
            return instance.setCosts[a] > instance.setCosts[b];
        }
    );

    std::vector<int> coverageCount(instance.elementCount + 1, 0);
    for (int set : selectedSets)
        for (int element : instance.elementsBySet[set])
            coverageCount[element]++;

    std::vector<int> keptSets;
    int totalCost = 0;

    for (int set : selectedSets)
    {
        bool redundant = true;
        for (int element : instance.elementsBySet[set])
        {
            if (coverageCount[element] == 1)
            {
                redundant = false;
                break;
            }
        }

        if (redundant)
        {
            for (int element : instance.elementsBySet[set])
                coverageCount[element]--;
        }
        else
        {
            keptSets.push_back(set);
            totalCost += instance.setCosts[set];
        }
    }

    selectedSets = std::move(keptSets);
    return totalCost;
}
}

int chvatal(
    const Instance& instance,
    const std::vector<unsigned char>& activeSets,
    std::vector<int>& selectedSets
)
{
    std::vector<bool> covered(instance.elementCount + 1, false);
    std::vector<bool> selected(instance.setCount + 1, false);

    int coveredCount = 0;
    int totalCost = 0;
    selectedSets.clear();

    while (coveredCount < instance.elementCount)
    {
        int bestSet = -1;
        int bestNewCoverage = 0;

        for (int set = 1; set <= instance.setCount; set++)
        {
            if (!activeSets[set] || selected[set]) continue;

            int newCoverage = 0;
            for (int element : instance.elementsBySet[set])
                if (!covered[element]) newCoverage++;

            if (newCoverage == 0) continue;

            bool betterRatio = bestSet == -1
                || 1LL * instance.setCosts[set] * bestNewCoverage
                 < 1LL * instance.setCosts[bestSet] * newCoverage;

            bool sameRatio = bestSet != -1
                && 1LL * instance.setCosts[set] * bestNewCoverage
                 == 1LL * instance.setCosts[bestSet] * newCoverage;

            if (betterRatio || (sameRatio && set < bestSet))
            {
                bestSet = set;
                bestNewCoverage = newCoverage;
            }
        }

        if (bestSet == -1) return INT_MAX;

        selected[bestSet] = true;
        selectedSets.push_back(bestSet);
        totalCost += instance.setCosts[bestSet];

        for (int element : instance.elementsBySet[bestSet])
        {
            if (!covered[element])
            {
                covered[element] = true;
                coveredCount++;
            }
        }
    }

    return totalCost;
}

int improveUpperBound(
    const Instance& instance,
    const std::vector<unsigned char>& activeSets,
    std::vector<int>& selectedSets
)
{
    removeRedundantSets(instance, selectedSets);

    bool improved = true;
    while (improved)
    {
        improved = false;

        std::vector<int> coverageCount(instance.elementCount + 1, 0);
        std::vector<bool> inSolution(instance.setCount + 1, false);

        for (int set : selectedSets)
        {
            inSolution[set] = true;
            for (int element : instance.elementsBySet[set])
                coverageCount[element]++;
        }

        for (std::size_t position = 0; position < selectedSets.size(); position++)
        {
            int currentSet = selectedSets[position];

            for (int element : instance.elementsBySet[currentSet])
                coverageCount[element]--;

            std::vector<int> uncoveredElements;
            for (int element : instance.elementsBySet[currentSet])
                if (coverageCount[element] == 0)
                    uncoveredElements.push_back(element);

            int replacement = -1;
            int replacementCost = instance.setCosts[currentSet];

            for (int candidate = 1; candidate <= instance.setCount; candidate++)
            {
                if (!activeSets[candidate]) continue;
                if (inSolution[candidate]) continue;
                if (instance.setCosts[candidate] >= replacementCost) continue;

                bool coversEverything = true;
                for (int element : uncoveredElements)
                {
                    const auto& covered = instance.elementsBySet[candidate];
                    if (!std::binary_search(covered.begin(), covered.end(), element))
                    {
                        coversEverything = false;
                        break;
                    }
                }

                if (coversEverything)
                {
                    replacement = candidate;
                    replacementCost = instance.setCosts[candidate];
                }
            }

            if (replacement != -1)
            {
                inSolution[currentSet] = false;
                inSolution[replacement] = true;
                selectedSets[position] = replacement;

                for (int element : instance.elementsBySet[replacement])
                    coverageCount[element]++;

                improved = true;
            }
            else
            {
                for (int element : instance.elementsBySet[currentSet])
                    coverageCount[element]++;
            }
        }

        if (improved)
            removeRedundantSets(instance, selectedSets);
    }

    int totalCost = 0;
    for (int set : selectedSets)
        totalCost += instance.setCosts[set];
    return totalCost;
}
