/*
 * DLX V2 sem restart + fixing local em lote.
 *
 * O nucleo estrutural (headers, cover/uncover, arenas e construcao da
 * matriz) vem da versao estavel. Renomeamos apenas search/main da V1 para
 * manter este experimento isolado em um novo executavel.
 */
#define search searchV1NaoUsado
#define main mainV1NaoUsado
#include "../dlx.cpp"
#undef main
#undef search

namespace
{
constexpr int IG_MAX_ITERATIONS = 2000;
constexpr int IG_MAX_STALE = 500;
constexpr int IG_RCL_SIZE = 6;

struct IteratedGreedyResult
{
    int cost = INT_MAX;
    int iterations = 0;
    int improvements = 0;
};

// Workspace unico da heuristica. Nenhum destes arrays e criado novamente
// dentro das iteracoes do iterated greedy.
int igCoverage[MAXCOL];
int igGain[MAXROW];
int igCurrent[MAXSOL];
int igBest[MAXSOL];
int igPermutation[MAXSOL];
int igExclusiveElements[MAXCOL];
int igRcl[IG_RCL_SIZE];
unsigned char igSelected[MAXROW];
unsigned char igRemovedPosition[MAXSOL];

// Incidencia inversa em CSR, montada uma unica vez para atualizar os ganhos
// quando um elemento passa de descoberto para coberto.
int igElementOffsets[MAXCOL + 1];
int igElementSets[MAXNODE];
int igNextPosition[MAXCOL + 1];

// Reduced costs calculados uma unica vez para o fixing inicial da raiz.
double storedReducedCost[MAXROW];
int availableRowsV2 = 0;
int searchIncumbentImprovements = 0;
long long lastIncumbentNode = 0;
double lastIncumbentTime = 0.0;
long long totalLocalFixings = 0;

bool betterRatio(
    const Instance& instance,
    int leftSet,
    int rightSet
)
{
    if (rightSet < 0) return true;

    const long long left =
        1LL * instance.setCosts[leftSet] * igGain[rightSet];
    const long long right =
        1LL * instance.setCosts[rightSet] * igGain[leftSet];

    if (left != right) return left < right;
    if (instance.setCosts[leftSet] != instance.setCosts[rightSet])
        return instance.setCosts[leftSet] < instance.setCosts[rightSet];
    if (igGain[leftSet] != igGain[rightSet])
        return igGain[leftSet] > igGain[rightSet];
    return leftSet < rightSet;
}

void buildInverseIncidence(
    const Instance& instance,
    const vector<unsigned char>& activeSets
)
{
    fill(igElementOffsets, igElementOffsets + instance.elementCount + 2, 0);

    int activeIncidences = 0;
    for (int set = 1; set <= instance.setCount; set++)
    {
        if (!activeSets[set]) continue;
        for (int element : instance.elementsBySet[set])
        {
            igElementOffsets[element + 1]++;
            activeIncidences++;
        }
    }

    if (activeIncidences >= MAXNODE)
        throw runtime_error("Arena de incidencias da heuristica insuficiente");

    for (int element = 1; element <= instance.elementCount; element++)
        igElementOffsets[element + 1] += igElementOffsets[element];

    copy(
        igElementOffsets,
        igElementOffsets + instance.elementCount + 2,
        igNextPosition
    );

    for (int set = 1; set <= instance.setCount; set++)
    {
        if (!activeSets[set]) continue;
        for (int element : instance.elementsBySet[set])
            igElementSets[igNextPosition[element]++] = set;
    }
}

int removeRedundantIG(
    const Instance& instance,
    int& currentCount,
    int currentCost
)
{
    sort(igCurrent, igCurrent + currentCount, [&](int a, int b)
    {
        if (instance.setCosts[a] != instance.setCosts[b])
            return instance.setCosts[a] > instance.setCosts[b];
        return a > b;
    });

    for (int position = 0; position < currentCount; position++)
    {
        int set = igCurrent[position];
        if (!igSelected[set]) continue;

        bool redundant = true;
        for (int element : instance.elementsBySet[set])
        {
            if (igCoverage[element] == 1)
            {
                redundant = false;
                break;
            }
        }

        if (!redundant) continue;

        igSelected[set] = 0;
        currentCost -= instance.setCosts[set];
        for (int element : instance.elementsBySet[set])
            igCoverage[element]--;
    }

    int write = 0;
    for (int position = 0; position < currentCount; position++)
    {
        int set = igCurrent[position];
        if (igSelected[set]) igCurrent[write++] = set;
    }
    currentCount = write;
    return currentCost;
}

int improveOneForOneIG(
    const Instance& instance,
    const vector<unsigned char>& activeSets,
    int currentCount,
    int currentCost
)
{
    for (int position = 0; position < currentCount; position++)
    {
        int currentSet = igCurrent[position];
        int exclusiveCount = 0;

        for (int element : instance.elementsBySet[currentSet])
        {
            igCoverage[element]--;
            if (igCoverage[element] == 0)
                igExclusiveElements[exclusiveCount++] = element;
        }

        int replacement = -1;
        int replacementCost = instance.setCosts[currentSet];
        for (int candidate = 1; candidate <= instance.setCount; candidate++)
        {
            if (!activeSets[candidate] || igSelected[candidate]) continue;
            if (instance.setCosts[candidate] >= replacementCost) continue;

            bool coversAll = true;
            const auto& elements = instance.elementsBySet[candidate];
            for (int index = 0; index < exclusiveCount; index++)
            {
                if (!binary_search(
                        elements.begin(),
                        elements.end(),
                        igExclusiveElements[index]
                    ))
                {
                    coversAll = false;
                    break;
                }
            }

            if (coversAll)
            {
                replacement = candidate;
                replacementCost = instance.setCosts[candidate];
            }
        }

        if (replacement >= 0)
        {
            igSelected[currentSet] = 0;
            igSelected[replacement] = 1;
            igCurrent[position] = replacement;
            currentCost += replacementCost - instance.setCosts[currentSet];
            for (int element : instance.elementsBySet[replacement])
                igCoverage[element]++;
        }
        else
        {
            for (int element : instance.elementsBySet[currentSet])
                igCoverage[element]++;
        }
    }

    return currentCost;
}

IteratedGreedyResult runIteratedGreedy(
    const Instance& instance,
    const vector<unsigned char>& activeSets,
    vector<int>& selectedSets,
    int initialCost
)
{
    if (selectedSets.size() >= MAXSOL)
        throw runtime_error("MAXSOL insuficiente para iterated greedy");

    buildInverseIncidence(instance, activeSets);

    int bestCount = static_cast<int>(selectedSets.size());
    for (int i = 0; i < bestCount; i++)
        igBest[i] = selectedSets[i];

    IteratedGreedyResult result;
    result.cost = initialCost;

    mt19937 generator(0xC0FFEEu);
    int stale = 0;

    for (int iteration = 0;
         iteration < IG_MAX_ITERATIONS && stale < IG_MAX_STALE;
         iteration++)
    {
        result.iterations = iteration + 1;
        fill(igSelected, igSelected + instance.setCount + 1, 0);
        fill(igCoverage, igCoverage + instance.elementCount + 1, 0);
        fill(igRemovedPosition, igRemovedPosition + bestCount, 0);

        for (int i = 0; i < bestCount; i++)
            igPermutation[i] = i;

        int maximumDestruction = min(
            bestCount,
            max(1, min(14, bestCount / 4))
        );
        int destruction = 1;
        if (maximumDestruction > 1)
            destruction += iteration % maximumDestruction;
        destruction = min(destruction, maximumDestruction);

        for (int i = 0; i < destruction; i++)
        {
            uniform_int_distribution<int> distribution(i, bestCount - 1);
            int chosen = distribution(generator);
            swap(igPermutation[i], igPermutation[chosen]);
            igRemovedPosition[igPermutation[i]] = 1;
        }

        int currentCount = 0;
        int currentCost = 0;
        for (int i = 0; i < bestCount; i++)
        {
            if (igRemovedPosition[i]) continue;
            int set = igBest[i];
            igCurrent[currentCount++] = set;
            igSelected[set] = 1;
            currentCost += instance.setCosts[set];
            for (int element : instance.elementsBySet[set])
                igCoverage[element]++;
        }

        int uncovered = 0;
        for (int element = 1; element <= instance.elementCount; element++)
            uncovered += (igCoverage[element] == 0);

        for (int set = 1; set <= instance.setCount; set++)
        {
            int gain = 0;
            if (activeSets[set] && !igSelected[set])
            {
                for (int element : instance.elementsBySet[set])
                    gain += (igCoverage[element] == 0);
            }
            igGain[set] = gain;
        }

        bool repairFailed = false;
        while (uncovered > 0)
        {
            int rclCount = 0;
            fill(igRcl, igRcl + IG_RCL_SIZE, -1);

            for (int set = 1; set <= instance.setCount; set++)
            {
                if (!activeSets[set] || igSelected[set] || igGain[set] == 0)
                    continue;

                if (rclCount == IG_RCL_SIZE
                    && !betterRatio(
                        instance,
                        set,
                        igRcl[IG_RCL_SIZE - 1]
                    ))
                {
                    continue;
                }

                int position = min(rclCount, IG_RCL_SIZE - 1);
                while (position > 0
                       && betterRatio(instance, set, igRcl[position - 1]))
                {
                    if (position < IG_RCL_SIZE)
                        igRcl[position] = igRcl[position - 1];
                    position--;
                }

                if (position < IG_RCL_SIZE)
                {
                    igRcl[position] = set;
                    if (rclCount < IG_RCL_SIZE) rclCount++;
                }
            }

            if (rclCount == 0 || currentCount >= MAXSOL)
            {
                repairFailed = true;
                break;
            }

            int chosenPosition = 0;
            uniform_int_distribution<int> chance(0, 99);
            if (rclCount > 1 && chance(generator) >= 65)
            {
                uniform_int_distribution<int> alternative(1, rclCount - 1);
                chosenPosition = alternative(generator);
            }

            int chosenSet = igRcl[chosenPosition];
            igSelected[chosenSet] = 1;
            igCurrent[currentCount++] = chosenSet;
            currentCost += instance.setCosts[chosenSet];

            for (int element : instance.elementsBySet[chosenSet])
            {
                if (igCoverage[element] == 0)
                {
                    uncovered--;
                    for (int index = igElementOffsets[element];
                         index < igElementOffsets[element + 1];
                         index++)
                    {
                        int affectedSet = igElementSets[index];
                        if (igGain[affectedSet] > 0) igGain[affectedSet]--;
                    }
                }
                igCoverage[element]++;
            }
        }

        if (repairFailed)
        {
            stale++;
            continue;
        }

        currentCost = removeRedundantIG(
            instance,
            currentCount,
            currentCost
        );

        if (currentCost <= result.cost + 8)
        {
            currentCost = improveOneForOneIG(
                instance,
                activeSets,
                currentCount,
                currentCost
            );
            currentCost = removeRedundantIG(
                instance,
                currentCount,
                currentCost
            );
        }

        if (currentCost < result.cost)
        {
            result.cost = currentCost;
            bestCount = currentCount;
            copy(igCurrent, igCurrent + currentCount, igBest);
            result.improvements++;
            stale = 0;
        }
        else
        {
            stale++;
        }
    }

    fill(igCoverage, igCoverage + instance.elementCount + 1, 0);
    int verifiedCost = 0;
    for (int i = 0; i < bestCount; i++)
    {
        int set = igBest[i];
        verifiedCost += instance.setCosts[set];
        for (int element : instance.elementsBySet[set])
            igCoverage[element]++;
    }
    for (int element = 1; element <= instance.elementCount; element++)
    {
        if (igCoverage[element] == 0)
            throw runtime_error("Iterated greedy produziu solucao invalida");
    }
    if (verifiedCost != result.cost)
        throw runtime_error("Custo inconsistente no iterated greedy");

    selectedSets.assign(igBest, igBest + bestCount);
    return result;
}

void calculateStoredReducedCosts(int totalRows)
{
    for (int row = 1; row <= totalRows; row++)
        storedReducedCost[row] = rowCost[row];

    for (int node = nCols + 1; node < nodeCount; node++)
        storedReducedCost[rowID[node]] -= pesoDual[colID[node]];
}

void rebuildRootAvailability(int totalRows)
{
    fill(colSize, colSize + nCols + 1, 0);
    fill(menorCustoColuna, menorCustoColuna + nCols + 1, INT_MAX);

    availableRowsV2 = 0;
    for (int row = 1; row <= totalRows; row++)
        availableRowsV2 += !rowEliminada[row];

    for (int node = nCols + 1; node < nodeCount; node++)
    {
        int row = rowID[node];
        int column = colID[node];
        if (rowEliminada[row]) continue;

        colSize[column]++;
        menorCustoColuna[column] = min(
            menorCustoColuna[column],
            rowCost[row]
        );
    }
}

int applyStoredReducedCostFixing(int totalRows)
{
    int newlyFixed = 0;
    for (int row = 1; row <= totalRows; row++)
    {
        if (rowEliminada[row]) continue;
        if (!limiteNaoPodeMelhorar(
                lowerBoundRaiz + storedReducedCost[row]
            ))
        {
            continue;
        }

        rowEliminada[row] = true;
        newlyFixed++;
    }

    rebuildRootAvailability(totalRows);
    return newlyFixed;
}

}

// ============================================================
// searchV2() — uma unica execucao da arvore, sem restart.
// ============================================================
void searchV2(int k, int MAX_K, int custoAtual)
{
    if (limiteTempoAtingido) return;

    totalNos++;
    if ((totalNos == 1 || (totalNos & 0x3FFLL) == 0)
        && elapsedTimerWall() >= limiteTempoBusca)
    {
        limiteTempoAtingido = true;
        return;
    }

    const double limiteDoNo = custoAtual + lowerBoundAtivo;
    if (limiteNaoPodeMelhorar(limiteDoNo))
    {
        totalPodas++;
        totalPodasLowerBound++;
        return;
    }

    if (rightN[header] == header)
    {
        if (custoAtual < bestCost)
        {
            bestCost = custoAtual;
            bestSolSize = solSize;
            for (int i = 0; i < solSize; i++)
                bestSol[i] = rowID[solution[i]];
            searchIncumbentImprovements++;
            lastIncumbentNode = totalNos;
            lastIncumbentTime = elapsedTimerWall();
        }
        return;
    }

    if (k >= MAX_K) return;

    int c = chooseColumn();
    if (colSize[c] == 0)
    {
        totalPodas++;
        totalPodasColunaSemLinha++;
        return;
    }

    const double lowerBoundRestante = max(
        lowerBoundAtivo,
        static_cast<double>(menorCustoColuna[c])
    );
    if (limiteNaoPodeMelhorar(custoAtual + lowerBoundRestante))
    {
        totalPodas++;
        totalPodasMenorCusto++;
        return;
    }

    const int inicioCandidatos = topoArenaCandidatos;
    for (int node = downN[c]; node != c; node = downN[node])
    {
        int row = rowID[node];
        if (rowEliminada[row] || forbiddenRow[row] || usedInBranch[row])
            continue;

        int novasCoberturas = 1;
        double pesoDualCoberto = pesoDual[c];
        for (int j = rightN[node]; j != node; j = rightN[j])
        {
            if (!colunaAtiva(colID[j])) continue;
            novasCoberturas++;
            pesoDualCoberto += pesoDual[colID[j]];
        }

        const double limiteFilho = custoAtual
                                  + rowCost[row]
                                  + lowerBoundAtivo
                                  - pesoDualCoberto;
        const double custoReduzido = rowCost[row] - pesoDualCoberto;

        if (topoArenaCandidatos >= MAXNODE)
            throw runtime_error("Arena de candidatos insuficiente");
        arenaCandidatos[topoArenaCandidatos++] = {
            node,
            row,
            novasCoberturas,
            custoReduzido,
            limiteFilho
        };
    }

    const int fimCandidatos = topoArenaCandidatos;

    // Uma linha cujo bound do filho ja nao melhora o incumbente nao pode
    // participar de nenhuma solucao melhor em toda esta subarvore. Move essas
    // linhas para o fim e as proibe antes de explorar qualquer filho, evitando
    // que reaparecam nos descendentes de candidatos anteriores.
    const int inicioFixados = static_cast<int>(partition(
        arenaCandidatos + inicioCandidatos,
        arenaCandidatos + fimCandidatos,
        [](const Candidato& candidato)
        {
            return !limiteNaoPodeMelhorar(candidato.limiteFilho);
        }
    ) - arenaCandidatos);

    for (int index = inicioFixados; index < fimCandidatos; index++)
    {
        proibirLinha(arenaCandidatos[index].node);
        totalPodas++;
        totalPodasLowerBoundFilho++;
        totalLocalFixings++;
    }

    if (inicioFixados == inicioCandidatos)
    {
        for (int index = fimCandidatos - 1;
             index >= inicioFixados;
             index--)
        {
            liberarLinha(arenaCandidatos[index].node);
        }
        topoArenaCandidatos = inicioCandidatos;
        return;
    }

    sort(
        arenaCandidatos + inicioCandidatos,
        arenaCandidatos + inicioFixados,
        [](const Candidato& a, const Candidato& b)
        {
            const double left = a.custoReduzido * b.novasCoberturas;
            const double right = b.custoReduzido * a.novasCoberturas;
            if (abs(left - right) > BOUND_EPSILON) return left < right;

            const long long costLeft =
                1LL * rowCost[a.row] * b.novasCoberturas;
            const long long costRight =
                1LL * rowCost[b.row] * a.novasCoberturas;
            if (costLeft != costRight) return costLeft < costRight;
            if (rowCost[a.row] != rowCost[b.row])
                return rowCost[a.row] < rowCost[b.row];
            if (a.novasCoberturas != b.novasCoberturas)
                return a.novasCoberturas > b.novasCoberturas;
            return a.row < b.row;
        }
    );

    const double lowerBoundAntesDaColuna = lowerBoundAtivo;
    cover(c);
    colSize[c]--;

    int candidatosProcessados = 0;
    for (int index = inicioCandidatos; index < inicioFixados; index++)
    {
        if (limiteTempoAtingido) break;

        const Candidato& candidato = arenaCandidatos[index];
        int r = candidato.node;

        if (limiteNaoPodeMelhorar(candidato.limiteFilho))
        {
            totalPodas++;
            totalPodasLowerBoundFilho++;
            proibirLinha(r);
            candidatosProcessados++;
            continue;
        }

        const int novoCusto = custoAtual + rowCost[rowID[r]];
        if (novoCusto >= bestCost)
        {
            totalPodas++;
            proibirLinha(r);
            candidatosProcessados++;
            continue;
        }

        usedInBranch[rowID[r]] = true;
        solution[solSize++] = r;

        const double lowerBoundAntesDoFilho = lowerBoundAtivo;
        const int inicioColunasCobertas = topoArenaColunasCobertas;
        for (int j = rightN[r]; j != r; j = rightN[j])
        {
            if (!colunaAtiva(colID[j])) continue;
            if (topoArenaColunasCobertas >= MAXCOL)
                throw runtime_error("Arena de colunas cobertas insuficiente");

            colSize[colID[j]]--;
            cover(colID[j]);
            arenaColunasCobertas[topoArenaColunasCobertas++] = colID[j];
        }

        searchV2(k + 1, MAX_K, novoCusto);

        for (int i = topoArenaColunasCobertas - 1;
             i >= inicioColunasCobertas;
             i--)
        {
            uncover(arenaColunasCobertas[i]);
            colSize[arenaColunasCobertas[i]]++;
        }
        topoArenaColunasCobertas = inicioColunasCobertas;
        lowerBoundAtivo = lowerBoundAntesDoFilho;

        solSize--;
        usedInBranch[rowID[r]] = false;
        proibirLinha(r);
        candidatosProcessados++;
    }

    for (int i = candidatosProcessados - 1; i >= 0; i--)
        liberarLinha(arenaCandidatos[inicioCandidatos + i].node);

    topoArenaCandidatos = inicioCandidatos;
    colSize[c]++;
    uncover(c);
    lowerBoundAtivo = lowerBoundAntesDaColuna;

    for (int index = fimCandidatos - 1; index >= inicioFixados; index--)
        liberarLinha(arenaCandidatos[index].node);
}

int main(int argc, char* argv[])
{
    const string instancePath = argc > 1
        ? argv[1]
        : "../scpa1/scpa1.txt";

    if (argc > 2)
    {
        limiteTempoBusca = stod(argv[2]);
        if (limiteTempoBusca <= 0.0)
        {
            cerr << "Limite de tempo deve ser positivo.\n";
            return 1;
        }
    }

    Instance instance;
    if (!readORLibraryInstance(instancePath, instance))
    {
        cerr << "Erro ao ler a instancia: " << instancePath << "\n";
        return 1;
    }

    if (instance.elementCount >= MAXCOL
        || instance.setCount >= MAXROW
        || instance.incidenceCount + instance.elementCount + 1 >= MAXNODE)
    {
        cerr << "Instancia excede os limites estaticos da V2.\n";
        return 1;
    }

    const auto preparationStart = chrono::steady_clock::now();
    const clock_t preparationCpuStart = clock();

    vector<unsigned char> activeSets;
    const int dominatedRows = removeDominatedSets(instance, activeSets);

    vector<int> incumbentSolution;
    const int chvatalUB = chvatal(instance, activeSets, incumbentSolution);
    const int localSearchUB = improveUpperBound(
        instance,
        activeSets,
        incumbentSolution
    );

    const IteratedGreedyResult iteratedGreedy = runIteratedGreedy(
        instance,
        activeSets,
        incumbentSolution,
        localSearchUB
    );
    const int initialUB = iteratedGreedy.cost;

    LagrangianResult lagrangian = optimizeLagrangianDual(
        instance,
        activeSets,
        initialUB
    );

    initDLX(instance.elementCount);
    for (int set = 1; set <= instance.setCount; set++)
    {
        rowCost[set] = instance.setCosts[set];
        if (!activeSets[set])
        {
            rowEliminada[set] = true;
            continue;
        }
        for (int element : instance.elementsBySet[set])
            addNode(set, element);
    }

    carregarPesosDuais(lagrangian.dualWeights);

    bestCost = initialUB;
    bestSolSize = static_cast<int>(incumbentSolution.size());
    for (int i = 0; i < bestSolSize; i++)
        bestSol[i] = incumbentSolution[i];

    calculateStoredReducedCosts(instance.setCount);
    const int initialFixings = applyStoredReducedCostFixing(instance.setCount);

    const double preparationWall = chrono::duration<double>(
        chrono::steady_clock::now() - preparationStart
    ).count();
    const double preparationCPU = static_cast<double>(
        clock() - preparationCpuStart
    ) / CLOCKS_PER_SEC;

    cout << "\nInstancia carregada — DLX V2 sem restart + fixing local!\n";
    cout << "Arquivo                  : " << instancePath << "\n";
    cout << "Elementos (colunas)      : " << instance.elementCount << "\n";
    cout << "Conjuntos (linhas)       : " << instance.setCount << "\n";
    cout << "Incidencias              : " << instance.incidenceCount << "\n";
    cout << "Linhas dominadas         : " << dominatedRows << "\n";
    cout << "Upper Bound Chvatal      : " << chvatalUB << "\n";
    cout << "UB apos busca local      : " << localSearchUB << "\n";
    cout << "Iteracoes iter. greedy   : " << iteratedGreedy.iterations << "\n";
    cout << "Melhorias iter. greedy   : " << iteratedGreedy.improvements << "\n";
    cout << "UB apos iter. greedy     : " << initialUB << "\n";
    cout << "Iteracoes subgrad.       : " << lagrangian.iterations << "\n";
    cout << fixed << setprecision(4);
    cout << "LB Lagrangiano           : " << lagrangian.lagrangianBound << "\n";
    cout << "LB dual factivel         : " << lowerBoundRaiz << "\n";
    cout << defaultfloat;
    cout << "Linhas eliminadas RC     : " << initialFixings << "\n";
    cout << "Linhas restantes na busca: " << availableRowsV2 << "\n";
    if (isfinite(limiteTempoBusca))
        cout << "Limite busca (s)         : " << limiteTempoBusca << "\n";
    cout << "Modo                     : Set Cover / DLX V2 sem restart + fixing local\n\n";

    startTimer();

    searchV2(0, instance.elementCount, 0);

    stopTimer();

    const double searchWall = totalTimerWall();
    const double searchCPU = totalTimerCPU();
    const double totalWallAfterRead = preparationWall + searchWall;
    const double totalCPUAfterRead = preparationCPU + searchCPU;

    cout << "\nResultado final:\n";
    cout << "Status DLX: "
         << (limiteTempoAtingido ? "TimeLimit" : "Optimal") << "\n";
    if (bestSolSize == 0)
    {
        cout << "Nenhuma solucao encontrada.\n";
    }
    else
    {
        cout << "Custo minimo: " << bestCost << "\n";
        cout << "Numero de conjuntos: " << bestSolSize << "\n";
        cout << "Linhas escolhidas: ";
        for (int i = 0; i < bestSolSize; i++)
            cout << "L" << bestSol[i] << " ";
        cout << "\n";
    }

    cout << "\nEstatisticas:\n";
    cout << "  Nos explorados          : " << totalNos << "\n";
    cout << "  Podas                   : " << totalPodas << "\n";
    cout << "  Podas por LB dual       : " << totalPodasLowerBound << "\n";
    cout << "  Podas por LB do filho   : " << totalPodasLowerBoundFilho << "\n";
    cout << "  Podas por menor custo   : " << totalPodasMenorCusto << "\n";
    cout << "  Podas sem linha         : " << totalPodasColunaSemLinha << "\n";
    cout << "  Linhas finais na busca  : " << availableRowsV2 << "\n";
    cout << "  Melhorias UB na busca   : " << searchIncumbentImprovements << "\n";
    cout << "  Ultimo UB no no         : " << lastIncumbentNode << "\n";
    cout << "  Fixings locais em lote  : " << totalLocalFixings << "\n";
    cout << fixed << setprecision(6);
    cout << "  Tempo do ultimo UB (s)  : " << lastIncumbentTime << "\n";
    cout << defaultfloat;

    cout << fixed << setprecision(9);
    cout << "Tempo preparacao apos leitura (s): " << preparationWall << "\n";
    cout << "Tempo busca (s)                 : " << searchWall << "\n";
    cout << "Tempo total apos leitura (s)    : " << totalWallAfterRead << "\n";
    cout << "CPU preparacao apos leitura (s) : " << preparationCPU << "\n";
    cout << "CPU busca (s)                   : " << searchCPU << "\n";
    cout << "CPU total apos leitura (s)      : " << totalCPUAfterRead << "\n";
    cout << defaultfloat;

    printTimingSummary(instancePath);
    return 0;
}
