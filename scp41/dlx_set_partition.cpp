// ============================================================
//  DLX — Set Partition / Exact Cover com custos
//  Adaptado das instancias OR-Library SCP (formato Beasley 1987)
//
//  Diferenca essencial em relacao ao Set Cover:
//    Set Cover      : cada elemento deve aparecer em >= 1 conjunto.
//    Set Partition  : cada elemento deve aparecer em == 1 conjunto.
//
//  Por isso este codigo usa o DLX classico de exact cover: ao
//  escolher um conjunto, todos os elementos desse conjunto sao
//  cobertos e todos os outros conjuntos que tocam esses elementos
//  ficam indisponiveis no ramo atual.
//
//  Formato do arquivo de entrada:
//    Linha 1 : m  n       (elementos, conjuntos)
//    Prox.   : n custos   (um por conjunto, em multiplas linhas)
//    Para cada elemento i (1..m):
//        k_i              (quantos conjuntos cobrem o elemento i)
//        j_1 j_2 ... j_k  (indices desses conjuntos, 1-indexados)
// ============================================================

#include <bits/stdc++.h>
#include <chrono>
#include <ctime>
#include <iomanip>
using namespace std;

struct Timer
{
    using Clock = chrono::steady_clock;
    Clock::time_point wallStart;
    clock_t cpuStart{};

    void start()
    {
        wallStart = Clock::now();
        cpuStart = clock();
    }
    double elapsedWall() const
    {
        return chrono::duration<double>(Clock::now() - wallStart).count();
    }
    double elapsedCPU() const
    {
        return static_cast<double>(clock() - cpuStart) / CLOCKS_PER_SEC;
    }
};

Timer gTimer;

const int MAXNODE = 200000;
const int MAXCOL  = 5001;
const int MAXROW  = 5001;
const int MAXSOL  = 5001;

double TIME_LIMIT_SEC = 3600.0;
const long long LOG_EVERY = 100000LL;

int leftN[MAXNODE], rightN[MAXNODE], upN[MAXNODE], downN[MAXNODE];
int rowID[MAXNODE], colID[MAXNODE];
int colSize[MAXCOL];
int rowCost[MAXROW];

int header = 0;
int nodeCount = 0;
int nElems = 0;
int nSets = 0;

int solution[MAXSOL];
int solSize = 0;
int bestCost = INT_MAX;
int bestSol[MAXSOL];
int bestSolSize = 0;

long long totalNos = 0;
long long totalPodas = 0;
bool stopSearch = false;
bool provedOptimal = false;

double u_star[MAXCOL];
double lbActiveSum = 0.0;
double rootLB = 0.0;

struct Instance
{
    int m = 0;
    int n = 0;
    vector<int> cost;                 // 1-based
    vector<vector<int>> coverElems;   // coverElems[j] = elementos 1-based
    vector<vector<int>> elemSets;     // elemSets[i-1] = conjuntos 0-based
};

inline bool columnActive(int c)
{
    return rightN[c] != c || leftN[c] != c || rightN[header] == c || leftN[header] == c;
}

void checkProgress()
{
    if (totalNos % LOG_EVERY != 0) return;

    double tw = gTimer.elapsedWall();
    cout << fixed << setprecision(2)
         << "[PROGRESSO] nos=" << totalNos
         << " | podas=" << totalPodas
         << " | bestCost=" << (bestCost == INT_MAX ? -1 : bestCost)
         << " | wall=" << tw << "s\n"
         << defaultfloat;
    cout.flush();

    if (tw >= TIME_LIMIT_SEC)
    {
        cout << "[STOP] Limite de tempo (" << TIME_LIMIT_SEC
             << "s) atingido. Retornando melhor solucao conhecida.\n";
        stopSearch = true;
    }
}

void printTimingSummary(double tWall, double tCPU)
{
    cout << "\n";
    cout << "+-------------------------------------------------+\n";
    cout << "|        RESUMO DE TEMPO  (CPLEX-style)           |\n";
    cout << "+-------------------------------------------------+\n";
    cout << fixed << setprecision(4);
    cout << "| Tempo de parede (Elapsed time) : "
         << setw(10) << tWall << " s |\n";
    cout << "| Tempo de CPU   (CPU time)      : "
         << setw(10) << tCPU << " s |\n";
    cout << "+-------------------------------------------------+\n";
    cout << defaultfloat;
}

void printIncumbent(int custoAtual)
{
    cout << "\n[INCUMBENTE]";
    cout << fixed << setprecision(4)
         << " custo=" << custoAtual
         << " | wall=" << gTimer.elapsedWall() << "s"
         << " | CPU=" << gTimer.elapsedCPU() << "s"
         << " | nos=" << totalNos
         << defaultfloat << "\n";

    cout << "  Conjuntos (" << solSize << "): ";
    for (int i = 0; i < solSize; i++)
        cout << rowID[solution[i]] << " ";
    cout << "\n\n";
    cout.flush();
}

void initDLX(int cols)
{
    nElems = cols;
    for (int i = 0; i <= cols; i++)
    {
        leftN[i] = i - 1;
        rightN[i] = i + 1;
        upN[i] = downN[i] = i;
        colSize[i] = 0;
    }
    leftN[0] = cols;
    rightN[cols] = 0;
    nodeCount = cols + 1;
}

void addNode(int r, int c)
{
    int node = nodeCount++;
    if (node >= MAXNODE)
    {
        cerr << "[ERRO] MAXNODE atingido. Aumente MAXNODE.\n";
        exit(1);
    }

    rowID[node] = r;
    colID[node] = c;
    rowCost[r] = rowCost[r];
    colSize[c]++;

    downN[node] = downN[c];
    upN[node] = c;
    upN[downN[c]] = node;
    downN[c] = node;

    if (node == nElems + 1 || rowID[node - 1] != r)
    {
        leftN[node] = rightN[node] = node;
    }
    else
    {
        leftN[node] = leftN[node - 1];
        rightN[node] = node - 1;
        rightN[leftN[node]] = node;
        leftN[node - 1] = node;
    }
}

// DLX classico. Remove a coluna c e todas as linhas que a cobrem.
// Para cada linha removida, remove tambem seus outros nos das colunas.
void coverExact(int c)
{
    rightN[leftN[c]] = rightN[c];
    leftN[rightN[c]] = leftN[c];
    lbActiveSum -= u_star[c];

    for (int i = downN[c]; i != c; i = downN[i])
    {
        for (int j = rightN[i]; j != i; j = rightN[j])
        {
            downN[upN[j]] = downN[j];
            upN[downN[j]] = upN[j];
            colSize[colID[j]]--;
        }
    }
}

void uncoverExact(int c)
{
    for (int i = upN[c]; i != c; i = upN[i])
    {
        for (int j = leftN[i]; j != i; j = leftN[j])
        {
            colSize[colID[j]]++;
            downN[upN[j]] = j;
            upN[downN[j]] = j;
        }
    }

    lbActiveSum += u_star[c];
    rightN[leftN[c]] = c;
    leftN[rightN[c]] = c;
}

int chooseColumn()
{
    int best = -1;
    double bestScore = 1e100;

    for (int c = rightN[header]; c != header; c = rightN[c])
    {
        if (colSize[c] == 0) return c;

        double score = colSize[c] * 1000000.0 - u_star[c];
        if (score < bestScore)
        {
            bestScore = score;
            best = c;
        }
    }
    return best;
}

void search(int k, int custoAtual)
{
    if (stopSearch) return;
    totalNos++;
    checkProgress();

    if (bestCost != INT_MAX)
    {
        double lbReal = (double)custoAtual + lbActiveSum;
        if (lbReal > (double)bestCost - 1.0 + 1e-6)
        {
            totalPodas++;
            return;
        }
    }

    if (rightN[header] == header)
    {
        if (custoAtual < bestCost)
        {
            bestCost = custoAtual;
            bestSolSize = solSize;
            for (int i = 0; i < solSize; i++)
                bestSol[i] = rowID[solution[i]];
            printIncumbent(custoAtual);

            int rootLBceil = (int)ceil(rootLB - 1e-9);
            if (bestCost <= rootLBceil)
            {
                cout << "[OTIMO PROVADO] bestCost=" << bestCost
                     << " == ceil(rootLB)=" << rootLBceil << ". Encerrando.\n";
                provedOptimal = true;
                stopSearch = true;
            }
        }
        return;
    }

    if (k >= nElems) return;

    int c = chooseColumn();
    if (c == -1 || colSize[c] == 0)
    {
        totalPodas++;
        return;
    }

    vector<pair<int, int>> candidatos; // (custo, no)
    candidatos.reserve(colSize[c]);
    for (int r = downN[c]; r != c; r = downN[r])
        candidatos.push_back({rowCost[rowID[r]], r});
    sort(candidatos.begin(), candidatos.end());

    coverExact(c);

    for (auto [_, r] : candidatos)
    {
        if (stopSearch) break;

        int row = rowID[r];
        int novoCusto = custoAtual + rowCost[row];
        if (bestCost != INT_MAX && novoCusto >= bestCost)
        {
            totalPodas++;
            continue;
        }

        solution[solSize++] = r;

        for (int j = rightN[r]; j != r; j = rightN[j])
            coverExact(colID[j]);

        search(k + 1, novoCusto);

        for (int j = leftN[r]; j != r; j = leftN[j])
            uncoverExact(colID[j]);

        solSize--;
    }

    uncoverExact(c);
}

bool lerInstanciaORLibrary(const string& caminho, Instance& inst)
{
    ifstream in(caminho);
    if (!in.is_open())
    {
        cerr << "[ERRO] Nao foi possivel abrir: " << caminho << "\n";
        return false;
    }

    if (!(in >> inst.m >> inst.n))
    {
        cerr << "[ERRO] Falha ao ler m e n.\n";
        return false;
    }
    nSets = inst.n;

    cout << "[LEITURA] m=" << inst.m << " elementos | n=" << inst.n << " conjuntos\n";

    inst.cost.assign(inst.n + 1, 0);
    for (int j = 1; j <= inst.n; j++)
    {
        if (!(in >> inst.cost[j]))
        {
            cerr << "[ERRO] Falha ao ler custo do conjunto " << j << "\n";
            return false;
        }
    }

    inst.coverElems.assign(inst.n + 1, {});
    inst.elemSets.assign(inst.m, {});

    int totalUns = 0;
    for (int i = 1; i <= inst.m; i++)
    {
        int k;
        if (!(in >> k))
        {
            cerr << "[ERRO] Falha ao ler k do elemento " << i << "\n";
            return false;
        }
        for (int t = 0; t < k; t++)
        {
            int j;
            if (!(in >> j) || j < 1 || j > inst.n)
            {
                cerr << "[ERRO] Indice de conjunto invalido no elemento " << i << "\n";
                return false;
            }
            inst.coverElems[j].push_back(i);
            inst.elemSets[i - 1].push_back(j - 1);
            totalUns++;
        }
    }

    for (int j = 1; j <= inst.n; j++)
        sort(inst.coverElems[j].begin(), inst.coverElems[j].end());
    for (int i = 0; i < inst.m; i++)
        sort(inst.elemSets[i].begin(), inst.elemSets[i].end());

    cout << "[LEITURA] Custos lidos. Faixa: "
         << *min_element(inst.cost.begin() + 1, inst.cost.end()) << ".."
         << *max_element(inst.cost.begin() + 1, inst.cost.end()) << "\n";
    cout << "[LEITURA] Matriz lida. Total de 1s: " << totalUns << "\n";
    return true;
}

// Para set partition, a dominancia por superset usada no Set Cover
// nao e valida. Aqui removemos apenas conjuntos duplicados exatos:
// mesmo conjunto de elementos e custo maior/igual.
void preprocessDuplicateSets(const Instance& inst, vector<bool>& alive)
{
    alive.assign(inst.n + 1, true);
    unordered_map<string, int> representative;
    int removed = 0;

    for (int j = 1; j <= inst.n; j++)
    {
        string key;
        key.reserve(inst.coverElems[j].size() * 6);
        for (int e : inst.coverElems[j])
        {
            key += to_string(e);
            key += ',';
        }

        auto it = representative.find(key);
        if (it == representative.end())
        {
            representative.emplace(std::move(key), j);
            continue;
        }

        int r = it->second;
        if (inst.cost[j] < inst.cost[r] || (inst.cost[j] == inst.cost[r] && j < r))
        {
            alive[r] = false;
            it->second = j;
        }
        else
        {
            alive[j] = false;
        }
        removed++;
    }

    cout << "[PRESOLVE] Duplicatas exatas removidas: " << removed
         << " (sobraram " << (inst.n - removed) << ")\n";
}

bool validaSetPartition(const Instance& inst, const vector<int>& sol)
{
    vector<int> cnt(inst.m + 1, 0);
    for (int j : sol)
    {
        if (j < 1 || j > inst.n) return false;
        for (int e : inst.coverElems[j])
            cnt[e]++;
    }
    for (int e = 1; e <= inst.m; e++)
        if (cnt[e] != 1) return false;
    return true;
}

int greedyExactPartition(const Instance& inst,
                         const vector<bool>& alive,
                         vector<int>& chosen,
                         mt19937& rng,
                         bool randomized)
{
    vector<char> covered(inst.m + 1, 0);
    vector<char> blocked(inst.n + 1, 0);
    int nCovered = 0;
    int totalCost = 0;
    chosen.clear();

    while (nCovered < inst.m)
    {
        int bestElem = -1;
        vector<int> candidates;

        for (int e = 1; e <= inst.m; e++)
        {
            if (covered[e]) continue;

            vector<int> local;
            for (int j0 : inst.elemSets[e - 1])
            {
                int j = j0 + 1;
                if (!alive[j] || blocked[j]) continue;

                bool conflicts = false;
                for (int ee : inst.coverElems[j])
                    if (covered[ee]) { conflicts = true; break; }
                if (!conflicts) local.push_back(j);
            }

            if (local.empty()) return INT_MAX;
            if (bestElem == -1 || local.size() < candidates.size())
            {
                bestElem = e;
                candidates.swap(local);
                if (candidates.size() == 1) break;
            }
        }

        sort(candidates.begin(), candidates.end(), [&](int a, int b) {
            double sa = (double)inst.cost[a] / max(1, (int)inst.coverElems[a].size());
            double sb = (double)inst.cost[b] / max(1, (int)inst.coverElems[b].size());
            if (fabs(sa - sb) > 1e-12) return sa < sb;
            return inst.cost[a] < inst.cost[b];
        });

        int pickLimit = randomized ? min<int>(3, candidates.size()) : 1;
        int j = candidates[uniform_int_distribution<int>(0, pickLimit - 1)(rng)];

        chosen.push_back(j);
        totalCost += inst.cost[j];

        for (int e : inst.coverElems[j])
        {
            if (!covered[e])
            {
                covered[e] = 1;
                nCovered++;
            }
            for (int k0 : inst.elemSets[e - 1])
                blocked[k0 + 1] = 1;
        }
    }

    if (!validaSetPartition(inst, chosen)) return INT_MAX;
    return totalCost;
}

int iteratedGreedyExact(const Instance& inst,
                        const vector<bool>& alive,
                        int iters,
                        vector<int>& bestSolOut)
{
    mt19937 rng(42);
    int best = INT_MAX;
    bestSolOut.clear();

    for (int it = 0; it < iters; it++)
    {
        vector<int> sol;
        int cost = greedyExactPartition(inst, alive, sol, rng, it > 0);
        if (cost < best)
        {
            best = cost;
            bestSolOut = sol;
        }
    }
    return best;
}

// Dual factivel para o relaxamento de particionamento:
//   max sum_i u_i
//   s.t. sum_{i in S_j} u_i <= c_j, para todo conjunto j ativo.
// u >= 0 e uma restricao mais forte que o dual irrestrito, mas
// produz um LB valido e incremental para os elementos restantes.
double dualAscentPartition(const Instance& inst,
                           const vector<bool>& alive,
                           double* u)
{
    for (int i = 1; i <= inst.m; i++) u[i] = 0.0;

    vector<double> slack(inst.n + 1, 0.0);
    for (int j = 1; j <= inst.n; j++)
        slack[j] = alive[j] ? inst.cost[j] : 1e18;

    bool improved = true;
    int passes = 0;
    while (improved && passes < 100)
    {
        improved = false;
        passes++;

        vector<int> order(inst.m);
        iota(order.begin(), order.end(), 1);
        sort(order.begin(), order.end(), [&](int a, int b) {
            return inst.elemSets[a - 1].size() < inst.elemSets[b - 1].size();
        });

        for (int e : order)
        {
            double delta = 1e18;
            for (int j0 : inst.elemSets[e - 1])
            {
                int j = j0 + 1;
                if (alive[j]) delta = min(delta, slack[j]);
            }

            if (delta > 1e-9 && delta < 1e17)
            {
                u[e] += delta;
                for (int j0 : inst.elemSets[e - 1])
                {
                    int j = j0 + 1;
                    if (alive[j]) slack[j] -= delta;
                }
                improved = true;
            }
        }
    }

    double sum = 0.0;
    for (int i = 1; i <= inst.m; i++) sum += u[i];
    return sum;
}

int reducedCostFixingPartition(const Instance& inst,
                               vector<bool>& alive,
                               const double* u,
                               double LB,
                               int UB)
{
    if (UB == INT_MAX)
    {
        cout << "[RCFIX] Pulado: sem UB factivel inicial.\n";
        return 0;
    }

    int removed = 0;
    for (int j = 1; j <= inst.n; j++)
    {
        if (!alive[j]) continue;
        double rc = inst.cost[j];
        for (int e : inst.coverElems[j]) rc -= u[e];

        if (rc > 0.0 && LB + rc >= (double)UB - 1e-9)
        {
            alive[j] = false;
            removed++;
        }
    }

    cout << "[RCFIX] Conjuntos eliminados por reduced-cost: " << removed
         << " (sobraram " << (inst.n - removed) << ")\n";
    return removed;
}

void buildDLX(const Instance& inst, const vector<bool>& alive)
{
    initDLX(inst.m);
    for (int j = 1; j <= inst.n; j++)
    {
        rowCost[j] = inst.cost[j];
        if (!alive[j]) continue;
        for (int e : inst.coverElems[j])
            addNode(j, e);
    }

    cout << "[BUILD] DLX exact-cover construida. Nos totais: " << nodeCount
         << " (max=" << MAXNODE << ")\n\n";
}

int main(int argc, char* argv[])
{
    string caminho = "instancias/scp41.txt";
    if (argc >= 2) caminho = argv[1];
    if (argc >= 3) TIME_LIMIT_SEC = atof(argv[2]);

    cout << "iniciando set partition / exact cover (instancia: " << caminho
         << ", time-limit: " << TIME_LIMIT_SEC << "s)\n";

    Instance inst;
    if (!lerInstanciaORLibrary(caminho, inst))
        return 1;

    gTimer.start();

    vector<bool> alive;
    preprocessDuplicateSets(inst, alive);

    vector<int> greedySol;
    int greedyUB = iteratedGreedyExact(inst, alive, 200, greedySol);
    if (greedyUB == INT_MAX)
        cout << "[GREEDY] Nao encontrou particao factivel inicial.\n";
    else
        cout << "[GREEDY] UB inicial = " << greedyUB
             << "  (|sol|=" << greedySol.size() << ")\n";

    static double u_opt[MAXCOL];
    double LB_dual = dualAscentPartition(inst, alive, u_opt);
    int LB_int = (int)ceil(LB_dual - 1e-9);
    cout << fixed << setprecision(4)
         << "[DUAL-ASCENT] LB = " << LB_dual
         << " (ceil=" << LB_int << ", UB="
         << (greedyUB == INT_MAX ? -1 : greedyUB) << ")\n"
         << defaultfloat;

    reducedCostFixingPartition(inst, alive, u_opt, LB_dual, greedyUB);

    bestCost = greedyUB;
    if (greedyUB != INT_MAX)
    {
        bestSolSize = (int)greedySol.size();
        for (int i = 0; i < bestSolSize; i++)
            bestSol[i] = greedySol[i];
    }

    buildDLX(inst, alive);

    for (int i = 0; i < MAXCOL; i++) u_star[i] = 0.0;
    for (int i = 1; i <= inst.m; i++) u_star[i] = u_opt[i];

    lbActiveSum = 0.0;
    for (int i = 1; i <= inst.m; i++) lbActiveSum += u_star[i];
    rootLB = lbActiveSum;

    cout << "Instancia carregada com sucesso!\n";
    cout << "Elementos (DLX colunas) : " << nElems << "\n";
    cout << "Conjuntos (DLX linhas)  : " << nSets << "\n";
    cout << "Limite de tempo         : " << TIME_LIMIT_SEC << " s\n\n";

    if (bestCost != INT_MAX && LB_int >= bestCost)
    {
        cout << "[OTIMO] LB dual == UB, solucao inicial ja e otima.\n";
        provedOptimal = true;
    }
    else
    {
        search(0, 0);
        if (!stopSearch)
        {
            if (bestSolSize > 0)
                cout << "[OTIMO] Busca B&B completou sem time-limit; bestCost e otimo.\n";
            else
                cout << "[INVIAVEL] Busca B&B completou: nao existe set partition factivel.\n";
            provedOptimal = true;
        }
    }

    double tWallTotal = gTimer.elapsedWall();
    double tCPUTotal = gTimer.elapsedCPU();

    cout << "\n=== RESULTADO FINAL ===\n";
    if (bestSolSize == 0)
    {
        cout << "Nenhuma particao factivel encontrada";
        if (stopSearch) cout << " (tempo esgotado antes da primeira solucao)";
        cout << ".\n";
    }
    else
    {
        vector<int> finalSol(bestSol, bestSol + bestSolSize);
        bool valid = validaSetPartition(inst, finalSol);

        cout << "Custo minimo : " << bestCost << "\n";
        cout << "Num. conjuntos: " << bestSolSize << "\n";
        cout << "Conjuntos escolhidos: ";
        for (int i = 0; i < bestSolSize; i++)
            cout << bestSol[i] << " ";
        cout << "\n";
        cout << "Particao valida: " << (valid ? "SIM" : "NAO") << "\n";

        if (stopSearch && !provedOptimal)
            cout << "[AVISO] Solucao pode nao ser otima — busca interrompida por time-limit.\n";
        else if (provedOptimal)
            cout << "[OK] Solucao OTIMA provada.\n";
    }

    cout << "\nEstatisticas de busca:\n";
    cout << "  Nos explorados : " << totalNos << "\n";
    cout << "  Podas          : " << totalPodas << "\n";

    printTimingSummary(tWallTotal, tCPUTotal);
    return 0;
}
