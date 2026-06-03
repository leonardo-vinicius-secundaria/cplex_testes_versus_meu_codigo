#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <string>
#include <unordered_map>
#include <vector>

using namespace std;

struct Timer {
    using Clock = chrono::steady_clock;
    Clock::time_point startPoint = Clock::now();

    void reset() {
        startPoint = Clock::now();
    }

    double elapsed() const {
        return chrono::duration<double>(Clock::now() - startPoint).count();
    }
};

struct Instance {
    int m = 0;
    int n = 0;
    vector<int> cost;                // 1-based
    vector<vector<int>> setElements; // 1-based: elementos cobertos por cada conjunto
};

static bool fileExists(const string& filename) {
    ifstream in(filename);
    return static_cast<bool>(in);
}

static Instance readOrLibraryInstance(const string& filename) {
    ifstream in(filename);
    if (!in) {
        cerr << "Erro: nao foi possivel abrir o arquivo: " << filename << "\n";
        exit(1);
    }

    Instance inst;
    in >> inst.m >> inst.n;

    inst.cost.assign(inst.n + 1, 0);
    inst.setElements.assign(inst.n + 1, {});

    for (int j = 1; j <= inst.n; ++j) {
        in >> inst.cost[j];
    }

    for (int i = 1; i <= inst.m; ++i) {
        int k = 0;
        in >> k;
        for (int t = 0; t < k; ++t) {
            int setId = 0;
            in >> setId;
            if (setId < 1 || setId > inst.n) {
                cerr << "Erro: indice de subconjunto fora do intervalo: " << setId << "\n";
                exit(1);
            }
            inst.setElements[setId].push_back(i);
        }
    }

    for (int j = 1; j <= inst.n; ++j) {
        auto& elems = inst.setElements[j];
        sort(elems.begin(), elems.end());
        elems.erase(unique(elems.begin(), elems.end()), elems.end());
    }

    return inst;
}

struct ReducedInstance {
    int originalM = 0;
    int originalN = 0;
    vector<int> originalCost;        // 1-based
    vector<int> forcedSets;          // IDs originais
    vector<int> rowOriginalId;       // linha reduzida -> conjunto original
    vector<vector<int>> rowColumns;   // colunas reduzidas cobertas por cada linha
    vector<int> colOriginalId;       // coluna reduzida -> elemento original
    bool infeasible = false;
    string infeasibleReason;
    int removedEmpty = 0;
    int removedDuplicates = 0;
};

struct VectorHash {
    size_t operator()(const vector<int>& v) const {
        uint64_t h = 1469598103934665603ULL;
        for (int x : v) {
            h ^= static_cast<uint64_t>(x + 0x9e3779b9);
            h *= 1099511628211ULL;
        }
        return static_cast<size_t>(h);
    }
};

static ReducedInstance preprocessExactCover(const Instance& inst) {
    ReducedInstance red;
    red.originalM = inst.m;
    red.originalN = inst.n;
    red.originalCost = inst.cost;

    vector<char> covered(inst.m + 1, 0);
    vector<char> alive(inst.n + 1, 1);
    vector<vector<int>> elemSets(inst.m + 1);

    for (int j = 1; j <= inst.n; ++j) {
        if (inst.setElements[j].empty()) {
            alive[j] = 0;
            ++red.removedEmpty;
            continue;
        }
        for (int e : inst.setElements[j]) {
            elemSets[e].push_back(j);
        }
    }

    auto killSetsTouchingCovered = [&](const vector<int>& elems) {
        for (int e : elems) {
            for (int j : elemSets[e]) {
                alive[j] = 0;
            }
        }
    };

    bool changed = true;
    while (changed) {
        changed = false;

        for (int e = 1; e <= inst.m; ++e) {
            if (covered[e]) {
                continue;
            }

            int onlySet = -1;
            int count = 0;

            for (int j : elemSets[e]) {
                if (!alive[j]) {
                    continue;
                }
                bool conflict = false;
                for (int x : inst.setElements[j]) {
                    if (covered[x]) {
                        conflict = true;
                        break;
                    }
                }
                if (conflict) {
                    alive[j] = 0;
                    changed = true;
                    continue;
                }

                onlySet = j;
                ++count;
                if (count > 1) {
                    break;
                }
            }

            if (count == 0) {
                red.infeasible = true;
                red.infeasibleReason = "elemento " + to_string(e) + " ficou sem subconjunto disponivel";
                return red;
            }

            if (count == 1) {
                red.forcedSets.push_back(onlySet);
                changed = true;

                vector<int> newlyCovered;
                newlyCovered.reserve(inst.setElements[onlySet].size());

                for (int x : inst.setElements[onlySet]) {
                    if (covered[x]) {
                        red.infeasible = true;
                        red.infeasibleReason = "subconjunto forcado conflita com elemento ja coberto";
                        return red;
                    }
                    covered[x] = 1;
                    newlyCovered.push_back(x);
                }

                killSetsTouchingCovered(newlyCovered);
            }
        }
    }

    vector<int> colMap(inst.m + 1, 0);
    for (int e = 1; e <= inst.m; ++e) {
        if (!covered[e]) {
            colMap[e] = static_cast<int>(red.colOriginalId.size()) + 1;
            red.colOriginalId.push_back(e);
        }
    }

    unordered_map<vector<int>, int, VectorHash> seenRows;
    for (int j = 1; j <= inst.n; ++j) {
        if (!alive[j]) {
            continue;
        }

        vector<int> cols;
        cols.reserve(inst.setElements[j].size());
        bool conflict = false;

        for (int e : inst.setElements[j]) {
            if (covered[e]) {
                conflict = true;
                break;
            }
            cols.push_back(colMap[e]);
        }

        if (conflict || cols.empty()) {
            continue;
        }

        sort(cols.begin(), cols.end());
        cols.erase(unique(cols.begin(), cols.end()), cols.end());

        auto inserted = seenRows.emplace(cols, j);
        if (!inserted.second) {
            ++red.removedDuplicates;
            continue;
        }

        red.rowOriginalId.push_back(j);
        red.rowColumns.push_back(cols);
    }

    vector<int> colCount(red.colOriginalId.size() + 1, 0);
    for (const auto& row : red.rowColumns) {
        for (int c : row) {
            ++colCount[c];
        }
    }

    for (size_t c = 1; c < colCount.size(); ++c) {
        if (colCount[c] == 0) {
            red.infeasible = true;
            red.infeasibleReason = "elemento " + to_string(red.colOriginalId[c - 1]) +
                                    " nao aparece na matriz reduzida";
            return red;
        }
    }

    return red;
}

class DLXSolver {
public:
    explicit DLXSolver(const ReducedInstance& red, double timeLimitSeconds)
        : red_(red), timeLimit_(timeLimitSeconds) {
        build();
        buildGreedyIncumbent();
    }

    bool solve() {
        timer_.reset();
        search(0, 0);
        solveTime_ = timer_.elapsed();
        found_ = (bestCost_ != numeric_limits<long long>::max());
        return found_;
    }

    const vector<int>& selectedRows() const {
        return bestSolution_;
    }

    double solveTime() const {
        return solveTime_;
    }

    long long nodes() const {
        return nodes_;
    }

    bool timedOut() const {
        return timedOut_;
    }

    long long bestCost() const {
        return bestCost_;
    }

private:
    struct Node {
        int left = 0;
        int right = 0;
        int up = 0;
        int down = 0;
        int col = 0;
        int row = 0;
    };

    const ReducedInstance& red_;
    double timeLimit_ = 0.0;
    Timer timer_;
    vector<Node> node_;
    vector<int> colSize_;
    vector<int> rowLen_;
    vector<int> rowCost_;
    vector<int> currentSolution_;
    vector<int> bestSolution_;
    long long nodes_ = 0;
    long long bestCost_ = numeric_limits<long long>::max();
    double solveTime_ = 0.0;
    bool found_ = false;
    bool timedOut_ = false;

    void build() {
        const int cols = static_cast<int>(red_.colOriginalId.size());
        node_.assign(cols + 1, {});
        colSize_.assign(cols + 1, 0);
        rowLen_.assign(red_.rowColumns.size() + 1, 0);
        rowCost_.assign(red_.rowColumns.size() + 1, 0);

        for (int c = 0; c <= cols; ++c) {
            node_[c].left = c - 1;
            node_[c].right = c + 1;
            node_[c].up = c;
            node_[c].down = c;
            node_[c].col = c;
        }
        node_[0].left = cols;
        node_[cols].right = 0;

        for (size_t r0 = 0; r0 < red_.rowColumns.size(); ++r0) {
            const int row = static_cast<int>(r0) + 1;
            const int origRow = red_.rowOriginalId[r0];
            rowLen_[row] = static_cast<int>(red_.rowColumns[r0].size());
            rowCost_[row] = red_.originalCost[origRow];

            int firstNode = -1;
            int prevNode = -1;

            for (int c : red_.rowColumns[r0]) {
                int idx = static_cast<int>(node_.size());
                node_.push_back({});
                node_[idx].col = c;
                node_[idx].row = row;

                node_[idx].down = c;
                node_[idx].up = node_[c].up;
                node_[node_[c].up].down = idx;
                node_[c].up = idx;
                ++colSize_[c];

                if (firstNode == -1) {
                    firstNode = idx;
                    prevNode = idx;
                    node_[idx].left = idx;
                    node_[idx].right = idx;
                } else {
                    node_[idx].left = prevNode;
                    node_[idx].right = firstNode;
                    node_[prevNode].right = idx;
                    node_[firstNode].left = idx;
                    prevNode = idx;
                }
            }
        }
    }

    void cover(int c) {
        node_[node_[c].right].left = node_[c].left;
        node_[node_[c].left].right = node_[c].right;

        for (int i = node_[c].down; i != c; i = node_[i].down) {
            for (int j = node_[i].right; j != i; j = node_[j].right) {
                node_[node_[j].down].up = node_[j].up;
                node_[node_[j].up].down = node_[j].down;
                --colSize_[node_[j].col];
            }
        }
    }

    void uncover(int c) {
        for (int i = node_[c].up; i != c; i = node_[i].up) {
            for (int j = node_[i].left; j != i; j = node_[j].left) {
                ++colSize_[node_[j].col];
                node_[node_[j].down].up = j;
                node_[node_[j].up].down = j;
            }
        }

        node_[node_[c].right].left = c;
        node_[node_[c].left].right = c;
    }

    long long lowerBoundAdditional() const {
        int remainingCols = 0;
        int maxRowCover = 0;
        int minRowCost = numeric_limits<int>::max();
        long long maxMinCostPerColumn = 0;

        for (int c = node_[0].right; c != 0; c = node_[c].right) {
            ++remainingCols;

            int minCostForColumn = numeric_limits<int>::max();
            for (int rNode = node_[c].down; rNode != c; rNode = node_[rNode].down) {
                int row = node_[rNode].row;
                maxRowCover = max(maxRowCover, rowLen_[row]);
                minRowCost = min(minRowCost, rowCost_[row]);
                minCostForColumn = min(minCostForColumn, rowCost_[row]);
            }

            if (minCostForColumn == numeric_limits<int>::max()) {
                return numeric_limits<long long>::max() / 4;
            }
            maxMinCostPerColumn = max(maxMinCostPerColumn, static_cast<long long>(minCostForColumn));
        }

        if (remainingCols == 0) {
            return 0;
        }

        if (maxRowCover <= 0) {
            maxRowCover = 1;
        }
        if (minRowCost == numeric_limits<int>::max()) {
            return numeric_limits<long long>::max() / 4;
        }

        long long rowsNeeded = (remainingCols + maxRowCover - 1) / maxRowCover;
        long long lbByRows = rowsNeeded * static_cast<long long>(minRowCost);

        return max(lbByRows, maxMinCostPerColumn);
    }

    int chooseColumn() const {
        int best = -1;
        int bestSize = numeric_limits<int>::max();

        for (int c = node_[0].right; c != 0; c = node_[c].right) {
            if (colSize_[c] < bestSize) {
                best = c;
                bestSize = colSize_[c];
                if (bestSize <= 1) {
                    break;
                }
            }
        }
        return best;
    }

    bool rowCompatibleWithChosenColumns(int rowNode, const vector<char>& covered) const {
        for (int j = node_[rowNode].right; j != rowNode; j = node_[j].right) {
            int c = node_[j].col;
            if (covered[c]) {
                return false;
            }
        }
        return true;
    }

    void buildGreedyIncumbent() {
        if (red_.rowColumns.empty()) {
            bestCost_ = 0;
            bestSolution_.clear();
            return;
        }

        vector<char> covered(static_cast<size_t>(red_.colOriginalId.size()) + 1, 0);
        vector<char> chosen(red_.rowColumns.size() + 1, 0);
        vector<int> solution;
        long long cost = 0;

        while (true) {
            int bestCol = -1;
            int bestSize = numeric_limits<int>::max();

            for (size_t c = 1; c <= red_.colOriginalId.size(); ++c) {
                if (covered[c]) {
                    continue;
                }
                int count = 0;
                for (size_t r0 = 0; r0 < red_.rowColumns.size(); ++r0) {
                    int row = static_cast<int>(r0) + 1;
                    if (chosen[row]) {
                        continue;
                    }
                    if (binary_search(red_.rowColumns[r0].begin(), red_.rowColumns[r0].end(), static_cast<int>(c))) {
                        ++count;
                    }
                }
                if (count < bestSize) {
                    bestSize = count;
                    bestCol = static_cast<int>(c);
                }
            }

            if (bestCol == -1) {
                break;
            }

            int bestRow = -1;
            int bestRowCost = numeric_limits<int>::max();

            for (size_t r0 = 0; r0 < red_.rowColumns.size(); ++r0) {
                int row = static_cast<int>(r0) + 1;
                if (chosen[row]) {
                    continue;
                }
                const auto& cols = red_.rowColumns[r0];
                if (!binary_search(cols.begin(), cols.end(), bestCol)) {
                    continue;
                }

                bool conflict = false;
                for (int c : cols) {
                    if (covered[c]) {
                        conflict = true;
                        break;
                    }
                }
                if (conflict) {
                    continue;
                }

                if (rowCost_[row] < bestRowCost) {
                    bestRowCost = rowCost_[row];
                    bestRow = row;
                }
            }

            if (bestRow == -1) {
                return;
            }

            chosen[bestRow] = 1;
            solution.push_back(bestRow);
            cost += rowCost_[bestRow];

            for (int c : red_.rowColumns[bestRow - 1]) {
                covered[c] = 1;
            }
        }

        if (solution.size() == red_.colOriginalId.size()) {
            // Não garante exatidão de seleção por número de linhas,
            // mas garante cobertura sem conflito.
            bestCost_ = cost;
            bestSolution_ = solution;
        }
    }

    void search(int depth, long long currentCost) {
        (void)depth;
        ++nodes_;

        if ((nodes_ & 4095LL) == 0 && timeLimit_ > 0.0 && timer_.elapsed() >= timeLimit_) {
            timedOut_ = true;
            return;
        }

        if (currentCost >= bestCost_) {
            return;
        }

        if (node_[0].right == 0) {
            if (currentCost < bestCost_) {
                bestCost_ = currentCost;
                bestSolution_ = currentSolution_;
                found_ = true;
            }
            return;
        }

        long long lbExtra = lowerBoundAdditional();
        if (currentCost + lbExtra >= bestCost_) {
            return;
        }

        int c = chooseColumn();
        if (c == -1 || colSize_[c] == 0) {
            return;
        }

        cover(c);

        vector<int> candidates;
        candidates.reserve(static_cast<size_t>(colSize_[c]));
        for (int rNode = node_[c].down; rNode != c; rNode = node_[rNode].down) {
            candidates.push_back(rNode);
        }

        sort(candidates.begin(), candidates.end(), [&](int a, int b) {
            int ra = node_[a].row;
            int rb = node_[b].row;
            if (rowCost_[ra] != rowCost_[rb]) {
                return rowCost_[ra] < rowCost_[rb];
            }
            if (rowLen_[ra] != rowLen_[rb]) {
                return rowLen_[ra] > rowLen_[rb];
            }
            return red_.rowOriginalId[ra - 1] < red_.rowOriginalId[rb - 1];
        });

        for (int rNode : candidates) {
            if (timedOut_) {
                break;
            }

            int row = node_[rNode].row;
            long long nextCost = currentCost + static_cast<long long>(rowCost_[row]);
            if (nextCost >= bestCost_) {
                continue;
            }

            currentSolution_.push_back(row);

            for (int j = node_[rNode].right; j != rNode; j = node_[j].right) {
                cover(node_[j].col);
            }

            if (currentCost + static_cast<long long>(rowCost_[row]) < bestCost_) {
                search(depth + 1, nextCost);
            }

            for (int j = node_[rNode].left; j != rNode; j = node_[j].left) {
                uncover(node_[j].col);
            }

            currentSolution_.pop_back();

            if (timedOut_) {
                break;
            }
        }

        uncover(c);
    }
};

static bool validatePartition(const Instance& inst, const vector<int>& selected, string& message) {
    vector<int> count(inst.m + 1, 0);

    for (int setId : selected) {
        if (setId < 1 || setId > inst.n) {
            message = "subconjunto selecionado fora do intervalo";
            return false;
        }
        for (int e : inst.setElements[setId]) {
            ++count[e];
        }
    }

    for (int e = 1; e <= inst.m; ++e) {
        if (count[e] != 1) {
            message = "elemento " + to_string(e) + " foi coberto " + to_string(count[e]) + " vez(es)";
            return false;
        }
    }

    message = "todos os elementos foram cobertos exatamente uma vez";
    return true;
}

int main(int argc, char** argv) {
    string filename;
    double timeLimit = 0.0;

    if (argc >= 2) {
        filename = argv[1];
    } else {
        const vector<string> defaultPaths = {
            "../scp41/instancias/scp41.txt",
            "scp41/instancias/scp41.txt",
            "cplex_testes_versus_meu_codigo/scp41/instancias/scp41.txt"
        };
        for (const string& path : defaultPaths) {
            if (fileExists(path)) {
                filename = path;
                break;
            }
        }
        if (filename.empty()) {
            filename = "../scp41/instancias/scp41.txt";
        }
    }

    if (argc >= 3) {
        timeLimit = stod(argv[2]);
    }

    cout << "============================================\n";
    cout << " DLX - Set Partition / Exact Cover\n";
    cout << " Instancia: " << filename << "\n";
    cout << "============================================\n\n";

    Timer totalTimer;
    Instance inst = readOrLibraryInstance(filename);
    ReducedInstance red = preprocessExactCover(inst);

    cout << "Elementos originais      : " << inst.m << "\n";
    cout << "Subconjuntos originais   : " << inst.n << "\n";
    cout << "Subconjuntos vazios rem. : " << red.removedEmpty << "\n";
    cout << "Duplicatas removidas     : " << red.removedDuplicates << "\n";
    cout << "Subconjuntos forcados    : " << red.forcedSets.size() << "\n";
    cout << "Elementos restantes      : " << red.colOriginalId.size() << "\n";
    cout << "Linhas DLX restantes     : " << red.rowColumns.size() << "\n\n";

    if (red.infeasible) {
        double totalTime = totalTimer.elapsed();
        cout << "Particao encontrada: NAO\n";
        cout << "Motivo             : " << red.infeasibleReason << "\n";
        cout << fixed << setprecision(6);
        cout << "Tempo total (s)    : " << totalTime << "\n";
        return 2;
    }

    vector<int> selected = red.forcedSets;
    bool found = false;
    double solveTime = 0.0;
    long long nodes = 0;
    bool timedOut = false;
    long long bestCost = 0;

    if (red.colOriginalId.empty()) {
        found = true;
    } else {
        DLXSolver solver(red, timeLimit);
        found = solver.solve();
        solveTime = solver.solveTime();
        nodes = solver.nodes();
        timedOut = solver.timedOut();
        bestCost = solver.bestCost();

        if (found) {
            for (int reducedRow : solver.selectedRows()) {
                selected.push_back(red.rowOriginalId[reducedRow - 1]);
            }
        }
    }

    sort(selected.begin(), selected.end());

    string validationMessage;
    bool valid = found && validatePartition(inst, selected, validationMessage);
    if (!found) {
        validationMessage = timedOut
            ? "busca interrompida pelo limite de tempo"
            : "busca exaustiva nao encontrou exact cover";
    }

    double totalTime = totalTimer.elapsed();

    cout << "============================================\n";
    cout << " RESULTADO\n";
    cout << "============================================\n";
    cout << "Particao encontrada: " << (valid ? "SIM" : "NAO") << "\n";
    if (timedOut) {
        cout << "Status             : TIME_LIMIT\n";
    } else {
        cout << "Status             : " << (valid ? "FEASIBLE" : "INFEASIBLE") << "\n";
    }
    cout << "Validacao          : " << validationMessage << "\n";
    cout << "Nos de busca       : " << nodes << "\n";
    cout << fixed << setprecision(6);
    cout << "Tempo DLX (s)      : " << solveTime << "\n";
    cout << "Wall-clock solve(s): " << solveTime << "\n";
    cout << "Tempo total (s)    : " << totalTime << "\n";

    if (valid) {
        int totalCost = 0;
        for (int setId : selected) {
            totalCost += inst.cost[setId];
        }
        cout << "Num. subconjuntos  : " << selected.size() << "\n";
        cout << "Custo informativo  : " << totalCost << "\n";
        if (bestCost > 0) {
            cout << "Melhor custo interno: " << bestCost << "\n";
        }
        cout << "\nSubconjuntos selecionados (1-based):\n";
        for (int setId : selected) {
            cout << setId << ' ';
        }
        cout << "\n";
    }

    return valid ? 0 : 2;
}
