// ============================================================
//  DLX — Set Cover (versão CORRIGIDA + OTIMIZADA p/ otimalidade)
//  Adaptado para instâncias OR-Library (formato Beasley 1987)
//  Testado com scp41.txt  (200 elementos, 1000 conjuntos)
//
//  Mudanças relativas à versão anterior:
//    1) lowerBound() agora é um LB **válido** baseado em
//       relaxação Lagrangiana (Caprara-Fischetti-Toth 1999).
//       Antes, era um UB greedy (= solução viável), o que
//       podava ramos da solução ótima e gerava 463 em vez de 429.
//    2) Subgradient otimiza u* no nó raiz (LP-near-optimal).
//    3) Reduced-cost fixing remove conjuntos que jamais
//       podem fazer parte da solução ótima.
//    4) Preprocessing de dominação de conjuntos.
//    5) Warm-start UB via greedy de Chvátal.
//    6) LB durante a busca é mantido em O(1) por cover/uncover.
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

// ============================================================
//  TIMER — estilo CPLEX (wall clock + CPU time)
// ============================================================
struct Timer
{
    using Clock     = chrono::steady_clock;
    using TimePoint = Clock::time_point;

    TimePoint wallStart;
    clock_t   cpuStart;

    void start()
    {
        wallStart = Clock::now();
        cpuStart  = clock();
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

// ============================================================
//  CONSTANTES — dimensionadas com folga para OR-Library
//  scp4*: 200x1000   scp5*: 200x2000   scp6*: 200x1000
//  scpa*-scpb*: 300x3000   scpc*-scpd*: 400x4000   scpe*: 50x500
// ============================================================
const int    MAXNODE       = 200000;
const int    MAXCOL        = 5001;   // elementos (4000 + folga)
const int    MAXROW        = 5001;   // conjuntos (4000 + folga)
const int    MAXSOL        = 1000;
double       TIME_LIMIT_SEC = 3600.0; // configurável via argumento
const long long LOG_EVERY  = 100000LL;

// ============================================================
//  ESTRUTURA DLX
// ============================================================
int  leftN[MAXNODE], rightN[MAXNODE];
int  upN[MAXNODE],   downN[MAXNODE];
int  rowID[MAXNODE]; // ID do conjunto (1..nSets)
int  colID[MAXNODE]; // ID do elemento (1..nElems)
int  colSize[MAXCOL];
bool colAtiva[MAXCOL];

int header    = 0;
int nodeCount = 0;
int nElems;
int nSets;

int  rowCost[MAXROW];
int  solution[MAXSOL];
int  solSize   = 0;
int  bestCost  = INT_MAX;
int  bestSol[MAXSOL];   // armazena rowID
int  bestSolSize = 0;
bool usedInBranch[MAXROW];
bool forbiddenRow[MAXROW];

long long totalNos    = 0;
long long totalPodas  = 0;
bool      stopSearch  = false;
bool      provedOptimal = false;

// referencias globais para 2-opt no incumbent (set by main before search)
struct Instance;
const struct Instance*    g_inst  = nullptr;
const vector<bool>*       g_alive = nullptr;
typedef int (*FuncLS2)(const Instance&, const vector<bool>&, vector<int>&);
FuncLS2                   g_localSearch2opt = nullptr;

// ============================================================
//  LB LAGRANGIANO  (mantido incrementalmente)
//
//  u_star[i]   = multiplicador Lagrangiano otimizado no raiz
//  lbActiveSum = sum_{i ativo} u_star[i]  (atualizado em cover/uncover)
//  rootLB      = LB Lagrangiano global (no nó raiz)
// ============================================================
double u_star[MAXCOL];
double lbActiveSum = 0.0;
double rootLB      = 0.0;

// ============================================================
//  PROGRESSO + checagem de TIME_LIMIT
//
//  A cada LOG_EVERY nós, imprime status e — se o tempo de parede
//  ultrapassou TIME_LIMIT_SEC — sinaliza parada da busca. O B&B
//  retorna então a melhor solução conhecida (UB do warm-start ou
//  do incumbente atual). Não é ótimo provado, mas é factível.
// ============================================================
inline void checkProgress()
{
    if (totalNos % LOG_EVERY == 0)
    {
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
}

// ============================================================
//  printMelhorSolucao
// ============================================================
void printMelhorSolucao()
{
    double tWall = gTimer.elapsedWall();
    double tCPU  = gTimer.elapsedCPU();

    int total = 0;
    for (int i = 0; i < solSize; i++)
        total += rowCost[rowID[solution[i]]];

    cout << "\n[INCUMBENTE]";
    cout << fixed << setprecision(4)
         << " custo=" << total;
    if (bestCost != INT_MAX)
        cout << " (melhora=" << (bestCost - total) << ")";
    else
        cout << " (primeira solucao)";
    cout << " | wall=" << tWall << "s"
         << " | CPU="  << tCPU  << "s"
         << " | nos="  << totalNos
         << defaultfloat << "\n";

    cout << "  Conjuntos (" << solSize << "): ";
    for (int i = 0; i < solSize; i++)
        cout << rowID[solution[i]] << " ";
    cout << "\n\n";
    cout.flush();
}

// ============================================================
//  printTimingSummary
// ============================================================
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
         << setw(10) << tCPU  << " s |\n";
    cout << "+-------------------------------------------------+\n";
    cout << defaultfloat;
}

// ============================================================
//  cover / uncover — remove/recoloca apenas o cabecalho da coluna.
//
//  Em Set Cover, linhas que intersectam uma coluna ja coberta NAO ficam
//  incompatíveis: elas ainda podem cobrir outros elementos ativos. Por isso
//  nao usamos o cover completo do DLX/Exact Cover aqui.
//
//  As linhas indisponiveis por branching (used/forbidden) sao removidas
//  verticalmente por disableRowNodes(), mantendo colSize exato nas colunas
//  ativas sem mudar a semantica do SCP.
// ============================================================
void cover(int c)
{
    rightN[leftN[c]] = rightN[c];
    leftN[rightN[c]] = leftN[c];
    colAtiva[c] = false;
    lbActiveSum -= u_star[c];
}

void uncover(int c)
{
    rightN[leftN[c]] = c;
    leftN[rightN[c]] = c;
    colAtiva[c] = true;
    lbActiveSum += u_star[c];
}

inline bool colunaAtiva(int c) { return colAtiva[c]; }

inline bool rowAvailable(int row)
{
    return !usedInBranch[row] && !forbiddenRow[row];
}

void disableRowNodes(int rowNode, vector<int>& removedNodes)
{
    int n = rowNode;
    do
    {
        int c = colID[n];
        if (colunaAtiva(c))
        {
            downN[upN[n]] = downN[n];
            upN[downN[n]] = upN[n];
            colSize[c]--;
            removedNodes.push_back(n);
        }
        n = rightN[n];
    } while (n != rowNode);
}

void restoreRowNodes(vector<int>& removedNodes)
{
    for (int idx = (int)removedNodes.size() - 1; idx >= 0; idx--)
    {
        int n = removedNodes[idx];
        int c = colID[n];
        downN[upN[n]] = n;
        upN[downN[n]] = n;
        colSize[c]++;
    }
    removedNodes.clear();
}

int effectiveCount(int c, int* onlyNode = nullptr, int* minCostOut = nullptr)
{
    int cnt = 0;
    int only = -1;
    int minCost = INT_MAX;

    for (int r = downN[c]; r != c; r = downN[r])
    {
        int row = rowID[r];
        if (!rowAvailable(row)) continue;
        cnt++;
        only = r;
        if (rowCost[row] < minCost) minCost = rowCost[row];
    }

    if (onlyNode) *onlyNode = only;
    if (minCostOut) *minCostOut = minCost;
    return cnt;
}

// ============================================================
//  chooseColumn — escolhe o elemento mais restritivo
// ============================================================
int chooseColumn()
{
    int best = -1;
    int bestCnt = INT_MAX;
    double bestU = -1.0;

    // Como disableRowNodes()/restoreRowNodes() mantem colSize efetivo,
    // a escolha volta a ser O(numero de colunas ativas), no espirito DLX.
    for (int c = rightN[header]; c != header; c = rightN[c])
    {
        int cnt = colSize[c];
        if (cnt == 0) return c;

        bool better =
            (cnt < bestCnt) ||
            (cnt == bestCnt && u_star[c] > bestU + 1e-12);

        if (better)
        {
            best = c;
            bestCnt = cnt;
            bestU = u_star[c];
        }
    }
    return best;
}

struct BranchCandidate
{
    double score = 0.0;
    double greedyScore = 0.0;
    int newCovers = 0;
    int cost = 0;
    int node = -1;
    int row = -1;
    vector<uint64_t> newCoverBits;
};

static bool bitSubset(const vector<uint64_t>& a, const vector<uint64_t>& b)
{
    for (size_t i = 0; i < a.size(); i++)
        if (a[i] & ~b[i]) return false;
    return true;
}

static bool sameBits(const vector<uint64_t>& a, const vector<uint64_t>& b)
{
    for (size_t i = 0; i < a.size(); i++)
        if (a[i] != b[i]) return false;
    return true;
}

static void removeDominatedCandidates(vector<BranchCandidate>& cand)
{
    vector<char> dominated(cand.size(), 0);
    int removed = 0;

    for (size_t b = 0; b < cand.size(); b++)
    {
        for (size_t a = 0; a < cand.size(); a++)
        {
            if (a == b || dominated[a]) continue;
            if (cand[a].cost > cand[b].cost) continue;
            if (!bitSubset(cand[b].newCoverBits, cand[a].newCoverBits)) continue;

            bool strictlyBetter = cand[a].cost < cand[b].cost ||
                                  !sameBits(cand[a].newCoverBits, cand[b].newCoverBits);
            bool sameButLowerId = cand[a].cost == cand[b].cost &&
                                  sameBits(cand[a].newCoverBits, cand[b].newCoverBits) &&
                                  cand[a].row < cand[b].row;
            if (strictlyBetter || sameButLowerId)
            {
                dominated[b] = 1;
                removed++;
                break;
            }
        }
    }

    if (removed == 0) return;

    vector<BranchCandidate> kept;
    kept.reserve(cand.size() - removed);
    for (size_t i = 0; i < cand.size(); i++)
        if (!dominated[i]) kept.push_back(std::move(cand[i]));
    cand.swap(kept);
}

// ============================================================
//  search() — Branch & Bound com LB Lagrangiano incremental
//
//  LB(nó) = custoAtual + sum_{i ainda ativo} u_star[i]
//  Como custos são inteiros: poda se ceil(LB) >= bestCost.
// ============================================================
void search(int k, int MAX_K, int custoAtual)
{
    if (stopSearch) return;
    totalNos++;
    checkProgress();

    // ---- PODA POR LB LAGRANGIANO --------------------------
    // LB válido (real): custoAtual + sum_{i ativo} u_star[i].
    // Custos inteiros => poda só se LB > bestCost - 1 (estritamente
    // melhor que bestCost-1 == bestCost). Tolerância numérica 1e-6.
    double lbReal = (double)custoAtual + lbActiveSum;
    if (lbReal > (double)bestCost - 1.0 + 1e-6) { totalPodas++; return; }

    // ---- SOLUÇÃO COMPLETA ---------------------------------
    if (rightN[header] == header)
    {
        bestCost    = custoAtual;
        bestSolSize = solSize;
        for (int i = 0; i < solSize; i++)
            bestSol[i] = rowID[solution[i]];

        // Tenta melhorar via 2-opt antes de imprimir incumbent
        if (g_inst && g_alive && g_localSearch2opt)
        {
            vector<int> tmpSol(bestSol, bestSol + bestSolSize);
            int newCost = g_localSearch2opt(*g_inst, *g_alive, tmpSol);
            if (newCost < bestCost)
            {
                bestCost    = newCost;
                bestSolSize = (int)tmpSol.size();
                for (int i = 0; i < bestSolSize; i++)
                    bestSol[i] = tmpSol[i];
            }
        }

        printMelhorSolucao();

        // Fathom by bound: se atingimos o LB Lagrangiano global,
        // a solução é provadamente ótima -> encerra a busca.
        int rootLBceil = (int)ceil(rootLB - 1e-9);
        if (bestCost <= rootLBceil)
        {
            cout << "[OTIMO PROVADO] bestCost=" << bestCost
                 << " == ceil(rootLB)=" << rootLBceil << ". Encerrando.\n";
            stopSearch    = true;
            provedOptimal = true;
        }
        return;
    }

    if (k >= MAX_K) return;

    int c = chooseColumn();
    if (c == -1 || colSize[c] == 0) { totalPodas++; return; }

    cover(c);

    // ---- ORDENA LINHAS CANDIDATAS -------------------------
    // Buffers locais (heap) — std::vector evita estouro de pilha
    // em recursões profundas e elimina problema de realocação
    // (ponteiros invalidados após search recursivo).
    vector<BranchCandidate> candidatos;
    candidatos.reserve(64);
    int words = (nElems + 63) / 64;

    for (int r = downN[c]; r != c; r = downN[r])
    {
        int row = rowID[r];
        if (!rowAvailable(row)) continue;

        int novasCoberturas = 1;
        double dualGain = u_star[c];
        vector<uint64_t> bits(words, 0);
        bits[(c - 1) >> 6] |= (uint64_t)1 << ((c - 1) & 63);

        for (int j = rightN[r]; j != r; j = rightN[j])
        {
            int col = colID[j];
            if (!colunaAtiva(col)) continue;
            novasCoberturas++;
            dualGain += u_star[col];
            bits[(col - 1) >> 6] |= (uint64_t)1 << ((col - 1) & 63);
        }

        double reduced = (double)rowCost[row] - dualGain;
        BranchCandidate cand;
        cand.score = reduced / novasCoberturas;
        cand.greedyScore = (double)rowCost[row] / novasCoberturas;
        cand.newCovers = novasCoberturas;
        cand.cost = rowCost[row];
        cand.node = r;
        cand.row = row;
        cand.newCoverBits = std::move(bits);
        candidatos.push_back(std::move(cand));
    }

    removeDominatedCandidates(candidatos);

    sort(candidatos.begin(), candidatos.end(),
         [](const BranchCandidate& a, const BranchCandidate& b)
         {
             if (fabs(a.score - b.score) > 1e-12) return a.score < b.score;
             if (fabs(a.greedyScore - b.greedyScore) > 1e-12)
                 return a.greedyScore < b.greedyScore;
             if (a.newCovers != b.newCovers) return a.newCovers > b.newCovers;
             if (a.cost != b.cost) return a.cost < b.cost;
             return a.row < b.row;
         });

    // ---- EXPLORA ------------------------------------------
    for (size_t ci = 0; ci < candidatos.size(); ci++)
    {
        if (stopSearch) break;

        vector<int> proibidosAgora;
        vector<int> nosProibidosAgora;
        proibidosAgora.reserve(ci);
        for (size_t p = 0; p < ci; p++)
        {
            int prevRow = candidatos[p].row;
            if (!forbiddenRow[prevRow])
            {
                forbiddenRow[prevRow] = true;
                disableRowNodes(candidatos[p].node, nosProibidosAgora);
                proibidosAgora.push_back(prevRow);
            }
        }

        int r   = candidatos[ci].node;
        int row = candidatos[ci].row;

        if (forbiddenRow[row] || usedInBranch[row])
        {
            restoreRowNodes(nosProibidosAgora);
            for (int x : proibidosAgora) forbiddenRow[x] = false;
            continue;
        }

        int novoCusto = custoAtual + rowCost[row];
        if (novoCusto >= bestCost)
        {
            totalPodas++;
            restoreRowNodes(nosProibidosAgora);
            for (int x : proibidosAgora) forbiddenRow[x] = false;
            continue;
        }

        usedInBranch[row]   = true;
        solution[solSize++] = r;
        vector<int> nosLinhaSelecionada;
        disableRowNodes(r, nosLinhaSelecionada);

        // Lista de colunas cobertas por esta linha — local ao escopo
        vector<int> cobertas;
        cobertas.reserve(64);

        for (int j = rightN[r]; j != r; j = rightN[j])
        {
            int col = colID[j];
            if (!colunaAtiva(col)) continue;
            cover(col);
            cobertas.push_back(col);
        }

        search(k + 1, MAX_K, novoCusto);

        for (int i = (int)cobertas.size() - 1; i >= 0; i--)
        {
            uncover(cobertas[i]);
        }
        restoreRowNodes(nosLinhaSelecionada);
        solSize--;
        usedInBranch[row] = false;

        restoreRowNodes(nosProibidosAgora);
        for (int x : proibidosAgora) forbiddenRow[x] = false;
    }

    uncover(c);
}

// ============================================================
//  initDLX
// ============================================================
void initDLX(int cols)
{
    nElems = cols;
    for (int i = 0; i <= cols; i++)
    {
        leftN[i]  = i - 1;
        rightN[i] = i + 1;
        upN[i] = downN[i] = i;
        colSize[i] = 0;
        colAtiva[i] = (i > 0);
    }
    colAtiva[0]  = false;
    leftN[0]     = cols;
    rightN[cols] = 0;
    nodeCount    = cols + 1;
    memset(usedInBranch, false, sizeof(usedInBranch));
    memset(forbiddenRow, false, sizeof(forbiddenRow));
    // u_star setado depois; lbActiveSum recalculado depois
}

// ============================================================
//  addNode
// ============================================================
void addNode(int r, int c)
{
    int node = nodeCount++;
    if (node >= MAXNODE)
    {
        cerr << "[ERRO] MAXNODE atingido!\n";
        exit(1);
    }
    rowID[node] = r;
    colID[node] = c;
    colSize[c]++;

    downN[node]   = downN[c];
    upN[node]     = c;
    upN[downN[c]] = node;
    downN[c]      = node;

    if (rowID[node - 1] != r)
        leftN[node] = rightN[node] = node;
    else
    {
        leftN[node]         = leftN[node - 1];
        rightN[node]        = node - 1;
        rightN[leftN[node]] = node;
        leftN[node - 1]     = node;
    }
}

// ============================================================
//  Estruturas auxiliares para preprocessing, greedy e Lagrangian
// ============================================================
struct Instance
{
    int m;                              // elementos
    int n;                              // conjuntos
    vector<int>           cost;         // cost[j] (1-based)
    vector<vector<int>>   coverElems;   // coverElems[j] = elementos cobertos por j (0-based, ordenado)
    vector<vector<int>>   elemSets;     // elemSets[i]   = conjuntos que cobrem i (0-based)
};

// ============================================================
//  preprocessDominance — remove S_j dominado por algum S_k
//  (S_j ⊆ S_k e cost[j] >= cost[k])
// ============================================================
void preprocessDominance(Instance& inst, vector<bool>& alive)
{
    int n = inst.n;
    int m = inst.m;
    int W = (m + 63) / 64;

    vector<vector<uint64_t>> bs(n + 1, vector<uint64_t>(W, 0));
    vector<int> popcnt(n + 1, 0);
    for (int j = 1; j <= n; j++)
    {
        for (int e : inst.coverElems[j])
        {
            int e0 = e - 1;
            bs[j][e0 >> 6] |= (uint64_t)1 << (e0 & 63);
        }
        popcnt[j] = (int)inst.coverElems[j].size();
    }

    alive.assign(n + 1, true);
    int removed = 0;

    vector<int> order(n);
    iota(order.begin(), order.end(), 1);
    sort(order.begin(), order.end(), [&](int a, int b){
        if (popcnt[a] != popcnt[b]) return popcnt[a] > popcnt[b];
        return inst.cost[a] < inst.cost[b];
    });

    for (int idxJ = 0; idxJ < n; idxJ++)
    {
        int j = order[idxJ];
        if (!alive[j]) continue;
        for (int idxK = 0; idxK < n; idxK++)
        {
            int k = order[idxK];
            if (k == j || !alive[k]) continue;
            if (popcnt[k] < popcnt[j]) continue;
            if (inst.cost[k] > inst.cost[j]) continue;

            bool subset = true;
            for (int w = 0; w < W; w++)
                if (bs[j][w] & ~bs[k][w]) { subset = false; break; }

            if (subset)
            {
                if (popcnt[j] == popcnt[k] && inst.cost[j] == inst.cost[k] && j < k)
                    continue;
                alive[j] = false;
                removed++;
                break;
            }
        }
    }
    cout << "[PRESOLVE] Conjuntos dominados removidos: " << removed
         << " (sobraram " << (n - removed) << ")\n";
}

// ============================================================
//  greedyChvatal — UB warm-start
// ============================================================
int greedyChvatal(const Instance& inst,
                  const vector<bool>& alive,
                  vector<int>& chosen)
{
    int m = inst.m;
    int n = inst.n;

    vector<bool> covered(m + 1, false);
    int nCov = 0, totalCost = 0;
    chosen.clear();

    vector<int> remaining(n + 1, 0);
    for (int j = 1; j <= n; j++)
        if (alive[j]) remaining[j] = (int)inst.coverElems[j].size();

    while (nCov < m)
    {
        int    bestJ = -1;
        double best  = 1e18;
        for (int j = 1; j <= n; j++)
        {
            if (!alive[j] || remaining[j] == 0) continue;
            double sc = (double)inst.cost[j] / remaining[j];
            if (sc < best) { best = sc; bestJ = j; }
        }
        if (bestJ == -1)
        {
            cerr << "[ERRO] Greedy nao conseguiu cobrir.\n";
            return INT_MAX;
        }
        chosen.push_back(bestJ);
        totalCost += inst.cost[bestJ];

        for (int e : inst.coverElems[bestJ])
        {
            if (covered[e]) continue;
            covered[e] = true;
            nCov++;
            // elemSets é 0-based na chave: elemSets[e-1] = conjuntos (0-based)
            for (int j0 : inst.elemSets[e - 1])
                if (alive[j0 + 1] && remaining[j0 + 1] > 0)
                    remaining[j0 + 1]--;
        }
    }
    return totalCost;
}

// ============================================================
//  greedyByReducedCost — UB warm-start usando reduced costs
//
//  Após Lagrangian, os reduced costs c_j - sum_{i in S_j} u_i
//  refletem "quão atraente" é cada conjunto. Misturamos custo
//  reduzido e custo real para evitar que muitos rc <= 0 empatem
//  com score zero e virem quase "menor indice". Score:
//     (0.7 * max(0, rc_j) + 0.3 * cost_j) / |S_j ∩ uncovered|
// ============================================================
int greedyByReducedCost(const Instance& inst,
                        const vector<bool>& alive,
                        const double* u,
                        vector<int>& chosen)
{
    int m = inst.m;
    int n = inst.n;

    vector<bool> covered(m + 1, false);
    int nCov = 0, totalCost = 0;
    chosen.clear();

    vector<int>    remaining(n + 1, 0);
    vector<double> rc(n + 1, 0.0);
    for (int j = 1; j <= n; j++)
    {
        if (!alive[j]) continue;
        remaining[j] = (int)inst.coverElems[j].size();
        double r = inst.cost[j];
        for (int e : inst.coverElems[j]) r -= u[e];
        rc[j] = r;
    }

    while (nCov < m)
    {
        int    bestJ = -1;
        double best  = 1e18;
        for (int j = 1; j <= n; j++)
        {
            if (!alive[j] || remaining[j] == 0) continue;
            double effCost = 0.7 * max(0.0, rc[j]) + 0.3 * inst.cost[j];
            double sc = effCost / remaining[j];
            if (sc < best) { best = sc; bestJ = j; }
        }
        if (bestJ == -1) return INT_MAX;
        chosen.push_back(bestJ);
        totalCost += inst.cost[bestJ];

        for (int e : inst.coverElems[bestJ])
        {
            if (covered[e]) continue;
            covered[e] = true;
            nCov++;
            for (int j0 : inst.elemSets[e - 1])
                if (alive[j0 + 1] && remaining[j0 + 1] > 0)
                    remaining[j0 + 1]--;
        }
    }
    return totalCost;
}

// ============================================================
//  removeRedundant — remove conjuntos redundantes da solução
//
//  Percorre conjuntos por custo decrescente; se sua remoção
//  ainda mantém todos os elementos cobertos, descarta. O
//  resultado é uma cobertura mínima por inclusão.
// ============================================================
int removeRedundant(const Instance& inst, vector<int>& sol)
{
    int m = inst.m;

    sort(sol.begin(), sol.end(), [&](int a, int b){
        return inst.cost[a] > inst.cost[b];
    });

    vector<int> coverCount(m + 1, 0);
    for (int j : sol)
        for (int e : inst.coverElems[j]) coverCount[e]++;

    vector<int> kept;
    int totalCost = 0;
    for (int j : sol)
    {
        bool redundant = true;
        for (int e : inst.coverElems[j])
            if (coverCount[e] <= 1) { redundant = false; break; }
        if (redundant)
            for (int e : inst.coverElems[j]) coverCount[e]--;
        else
        {
            kept.push_back(j);
            totalCost += inst.cost[j];
        }
    }
    sol = kept;
    return totalCost;
}

// ============================================================
//  greedyChvatalRandom — greedy de Chvátal com tie-break aleatório
//
//  Útil para iterated greedy (multi-start). Quando há vários
//  conjuntos com o mesmo score (cost / cidades_novas), escolhe
//  um aleatoriamente em vez do menor índice. Em instâncias com
//  custos pequenos (scpe*, scp6*) o tie-break é decisivo para
//  achar o ótimo.
// ============================================================
int greedyChvatalRandom(const Instance& inst,
                        const vector<bool>& alive,
                        vector<int>& chosen,
                        mt19937& rng)
{
    int m = inst.m;
    int n = inst.n;

    vector<bool> covered(m + 1, false);
    int nCov = 0, totalCost = 0;
    chosen.clear();

    vector<int> remaining(n + 1, 0);
    for (int j = 1; j <= n; j++)
        if (alive[j]) remaining[j] = (int)inst.coverElems[j].size();

    vector<int> bestCands;
    bestCands.reserve(64);

    while (nCov < m)
    {
        double best  = 1e18;
        bestCands.clear();

        for (int j = 1; j <= n; j++)
        {
            if (!alive[j] || remaining[j] == 0) continue;
            double sc = (double)inst.cost[j] / remaining[j];
            if (sc < best - 1e-12) { best = sc; bestCands.clear(); bestCands.push_back(j); }
            else if (sc < best + 1e-12) bestCands.push_back(j);
        }

        if (bestCands.empty()) return INT_MAX;
        int bestJ = bestCands[uniform_int_distribution<int>(0, (int)bestCands.size()-1)(rng)];

        chosen.push_back(bestJ);
        totalCost += inst.cost[bestJ];

        for (int e : inst.coverElems[bestJ])
        {
            if (covered[e]) continue;
            covered[e] = true;
            nCov++;
            for (int j0 : inst.elemSets[e - 1])
                if (alive[j0 + 1] && remaining[j0 + 1] > 0)
                    remaining[j0 + 1]--;
        }
    }
    return totalCost;
}

// ============================================================
//  iteratedGreedy — executa greedy várias vezes com seeds
//  diferentes e devolve a melhor solução encontrada (após
//  remover redundantes).
//
//  Inclui também "destruction-construction" (Ruin-and-Recreate):
//  remove K conjuntos aleatórios da melhor solução e refaz o
//  resto via greedy. Isso explora vizinhanças mais distantes
//  que o 2-opt local search.
// ============================================================
int localSearch2opt(const Instance&, const vector<bool>&, vector<int>&); // fwd
int iteratedGreedy(const Instance& inst,
                   const vector<bool>& alive,
                   const double* u,
                   int nIters,
                   vector<int>& bestSolOut)
{
    mt19937 rng(42);
    int bestC = INT_MAX;
    bestSolOut.clear();

    auto tryCand = [&](vector<int>& sol)
    {
        if (sol.empty()) return;
        int c = removeRedundant(inst, sol);
        // local search 2-opt em cada candidata — barato e eficaz
        if ((int)sol.size() <= 80) c = localSearch2opt(inst, alive, sol);
        if (c < bestC) { bestC = c; bestSolOut = sol; }
    };

    // 1ª: greedy clássico determinístico
    {
        vector<int> sol;
        if (greedyChvatal(inst, alive, sol) != INT_MAX) tryCand(sol);
    }

    // 2ª: greedy guiado por reduced cost
    if (u != nullptr)
    {
        vector<int> sol;
        if (greedyByReducedCost(inst, alive, u, sol) != INT_MAX) tryCand(sol);
    }

    // 3ª–N: greedy com tie-break aleatório
    for (int it = 0; it < nIters; it++)
    {
        vector<int> sol;
        if (greedyChvatalRandom(inst, alive, sol, rng) != INT_MAX) tryCand(sol);
    }

    // Ruin-and-recreate: a partir da melhor, remove K conjuntos
    // aleatórios e refaz o resto via greedy guiado por reduced cost.
    if (!bestSolOut.empty() && u != nullptr)
    {
        int n = inst.n;
        int m = inst.m;

        for (int rep = 0; rep < 50; rep++)
        {
            int K = 3 + (int)(uniform_int_distribution<int>(0, 7)(rng));
            if (K > (int)bestSolOut.size()) K = (int)bestSolOut.size();

            // copia e remove K aleatorios
            vector<int> base = bestSolOut;
            shuffle(base.begin(), base.end(), rng);
            base.resize((int)base.size() - K);

            // marca cobertura atual
            vector<int> cov(m + 1, 0);
            for (int j : base)
                for (int e : inst.coverElems[j]) cov[e]++;

            // greedy para cobrir o restante usando reduced cost
            vector<int> remaining(n + 1, 0);
            for (int j = 1; j <= n; j++)
                if (alive[j]) {
                    int c = 0;
                    for (int e : inst.coverElems[j])
                        if (cov[e] == 0) c++;
                    remaining[j] = c;
                }

            int nUncov = 0;
            for (int e = 1; e <= m; e++) if (cov[e] == 0) nUncov++;

            bool ok = true;
            while (nUncov > 0)
            {
                int    bestJ = -1;
                double best  = 1e18;
                for (int j = 1; j <= n; j++)
                {
                    if (!alive[j] || remaining[j] == 0) continue;
                    double rc = inst.cost[j];
                    for (int e : inst.coverElems[j]) rc -= u[e];
                    double effCost = 0.7 * max(0.0, rc) + 0.3 * inst.cost[j];
                    double sc = effCost / remaining[j];
                    if (sc < best) { best = sc; bestJ = j; }
                }
                if (bestJ == -1) { ok = false; break; }
                base.push_back(bestJ);
                for (int e : inst.coverElems[bestJ])
                {
                    if (cov[e]++ == 0) {
                        nUncov--;
                        for (int j0 : inst.elemSets[e - 1])
                            if (alive[j0 + 1] && remaining[j0 + 1] > 0)
                                remaining[j0 + 1]--;
                    }
                }
            }
            if (ok) tryCand(base);
        }
    }

    return bestC;
}

// ============================================================
//  localSearch2opt — busca local "1-flip swap"
//
//  Para cada conjunto j na solução, tenta substitui-lo por um
//  conjunto k tal que (sol \ {j}) ∪ {k} ainda cobre tudo e
//  cost[k] < cost[j]. Se não, tenta retirar j e cobrir os
//  elementos não-cobertos com 1 conjunto extra mais barato.
//
//  Itera até não haver melhora. O(|sol| * n * m) por iter.
// ============================================================
int localSearch2opt(const Instance& inst,
                    const vector<bool>& alive,
                    vector<int>& sol)
{
    int m = inst.m;
    int n = inst.n;

    bool improved = true;
    int  iters = 0;

    while (improved && iters < 50)
    {
        improved = false;
        iters++;

        // contagem de cobertura atual
        vector<int> cov(m + 1, 0);
        for (int j : sol)
            for (int e : inst.coverElems[j]) cov[e]++;

        // tenta substituir cada j por k mais barato
        for (size_t pos = 0; pos < sol.size(); pos++)
        {
            int j = sol[pos];
            int costJ = inst.cost[j];

            // remove j: contagens caem
            for (int e : inst.coverElems[j]) cov[e]--;

            // elementos descobertos por remover j
            vector<int> uncov;
            uncov.reserve(16);
            for (int e : inst.coverElems[j])
                if (cov[e] == 0) uncov.push_back(e);

            // procura k != j alive que cubra TODOS os uncov
            // e tenha custo < costJ
            int bestK = -1;
            int bestKcost = costJ;
            for (int k = 1; k <= n; k++)
            {
                if (k == j || !alive[k]) continue;
                if (inst.cost[k] >= bestKcost) continue;

                // verifica se cobre todos uncov
                bool okk = true;
                for (int e : uncov)
                {
                    auto& ce = inst.coverElems[k];
                    if (!binary_search(ce.begin(), ce.end(), e)) { okk = false; break; }
                }
                if (!okk) continue;

                // checa que k nao está em sol
                bool kInSol = false;
                for (int s : sol) if (s == k) { kInSol = true; break; }
                if (kInSol) continue;

                bestK = k;
                bestKcost = inst.cost[k];
            }

            if (bestK != -1)
            {
                sol[pos] = bestK;
                for (int e : inst.coverElems[bestK]) cov[e]++;
                improved = true;
            }
            else
            {
                // restaura j
                for (int e : inst.coverElems[j]) cov[e]++;
            }
        }

        // tenta remover j sem reposição (caso ele virou redundante)
        if (improved)
        {
            int newCost = removeRedundant(inst, sol);
            (void)newCost;
        }
    }

    int total = 0;
    for (int j : sol) total += inst.cost[j];
    return total;
}

// ============================================================
//  lerInstanciaORLibrary
// ============================================================
bool lerInstanciaORLibrary(const string& caminho, Instance& inst)
{
    ifstream in(caminho);
    if (!in.is_open())
    {
        cerr << "[ERRO] Nao foi possivel abrir: " << caminho << "\n";
        return false;
    }

    int m, n;
    if (!(in >> m >> n))
    {
        cerr << "[ERRO] Falha ao ler m e n.\n";
        return false;
    }
    inst.m = m;
    inst.n = n;
    nSets  = n;

    cout << "[LEITURA] m=" << m << " elementos | n=" << n << " conjuntos\n";

    inst.cost.assign(n + 1, 0);
    for (int j = 1; j <= n; j++)
        if (!(in >> inst.cost[j]))
        {
            cerr << "[ERRO] Falha ao ler custo do conjunto " << j << "\n";
            return false;
        }

    cout << "[LEITURA] Custos lidos. Faixa: "
         << *min_element(inst.cost.begin() + 1, inst.cost.end()) << ".."
         << *max_element(inst.cost.begin() + 1, inst.cost.end()) << "\n";

    inst.coverElems.assign(n + 1, {});
    inst.elemSets.assign(m, {});

    int totalNos_dados = 0;
    for (int i = 1; i <= m; i++)
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
            if (!(in >> j))
            {
                cerr << "[ERRO] Falha ao ler conjunto do elemento " << i << "\n";
                return false;
            }
            if (j < 1 || j > n) { cerr << "[ERRO] Indice fora do range\n"; return false; }
            inst.coverElems[j].push_back(i);
            inst.elemSets[i - 1].push_back(j - 1);
            totalNos_dados++;
        }
    }
    for (int j = 1; j <= n; j++)
        sort(inst.coverElems[j].begin(), inst.coverElems[j].end());
    for (int i = 0; i < m; i++)
        sort(inst.elemSets[i].begin(), inst.elemSets[i].end());

    cout << "[LEITURA] Cobertura lida. Total de 1s na matriz: "
         << totalNos_dados << "\n";
    return true;
}

// ============================================================
//  LAGRANGIAN SUBGRADIENT  (Caprara-Fischetti-Toth 1999)
//
//  Para o Set Cover IP:
//    min  c^T x
//    s.t. A x >= 1,  x in {0,1}
//
//  Relaxamos as cobertura constraints com multiplicadores u >= 0:
//    L(u) = sum_j min(0, c_j - sum_{i in S_j} u_i)  +  sum_i u_i
//         = sum_i u_i  -  sum_j max(0, sum_{i in S_j} u_i - c_j)
//
//  L(u) <= IP_opt  para todo u >= 0  (LB válido).
//
//  Usamos subgradient ascent (Held-Karp 1971) para maximizar L(u):
//    g_i = 1 - sum_{j : x_j(u)=1} A_{ij}      (subgradient)
//    u_i <- max(0, u_i + lambda * (UB - L(u)) / |g|^2 * g_i)
//
//  com x_j(u) = 1 sse  c_j - sum_{i in S_j} u_i  <  0.
//
//  lambda inicia em 2.0 e cai por metade quando L(u) não melhora
//  por T iterações (heurística clássica).
//
//  Saída:
//    u_star[1..m]  multiplicadores ótimos
//    rootLB        = melhor L(u) encontrado  (LB Lagrangiano)
// ============================================================
double runLagrangian(const Instance& inst,
                     const vector<bool>& alive,
                     int&    UB,                    // ref: pode ser melhorado
                     int     maxIter,
                     double* u_out,
                     vector<int>* bestUBSolOut = nullptr)
{
    int m = inst.m;
    int n = inst.n;

    vector<double> u(m + 1, 0.0);
    for (int i = 1; i <= m; i++)
    {
        double mn = 1e18;
        for (int j0 : inst.elemSets[i - 1])
        {
            int j = j0 + 1;
            if (!alive[j]) continue;
            double s = (double)inst.cost[j] / inst.coverElems[j].size();
            if (s < mn) mn = s;
        }
        u[i] = (mn < 1e17) ? mn : 0.0;
    }

    double bestLB = 0.0;
    vector<double> uBest = u;

    double lambda     = 2.0;
    int    noImprove  = 0;
    int    halveAfter = 30;

    vector<double> reducedCost(n + 1, 0.0);
    vector<char>   xj(n + 1, 0);
    vector<int>    coveredCount(m + 1, 0);

    int    bestSeenUB = UB;
    vector<int> bestSeenSol;

    for (int iter = 0; iter < maxIter; iter++)
    {
        double Lu = 0.0;
        for (int i = 1; i <= m; i++) Lu += u[i];

        for (int j = 1; j <= n; j++)
        {
            if (!alive[j]) { xj[j] = 0; continue; }
            double rc = inst.cost[j];
            for (int e : inst.coverElems[j]) rc -= u[e];
            reducedCost[j] = rc;
            if (rc < 0.0) { xj[j] = 1; Lu += rc; }
            else            xj[j] = 0;
        }

        if (Lu > bestLB) { bestLB = Lu; uBest = u; noImprove = 0; }
        else             noImprove++;

        // a cada 50 iters, tenta extrair UB via greedy guiado por u
        if (bestUBSolOut != nullptr && (iter % 50) == 49)
        {
            vector<int> heurSol;
            int hUB = greedyByReducedCost(inst, alive, u.data(), heurSol);
            if (hUB != INT_MAX) {
                int hUB2 = removeRedundant(inst, heurSol);
                if (hUB2 < bestSeenUB) {
                    bestSeenUB = hUB2;
                    bestSeenSol = heurSol;
                }
            }
        }

        if (noImprove >= halveAfter) { lambda *= 0.5; noImprove = 0; }
        if (lambda < 5e-4)            break;

        for (int i = 1; i <= m; i++) coveredCount[i] = 0;
        for (int j = 1; j <= n; j++)
            if (xj[j])
                for (int e : inst.coverElems[j]) coveredCount[e]++;

        double gnorm2 = 0.0;
        for (int i = 1; i <= m; i++)
        {
            double g = 1.0 - coveredCount[i];
            gnorm2 += g * g;
        }
        if (gnorm2 < 1e-12) break;

        // step size com UB possivelmente atualizado durante a busca
        double T = lambda * (bestSeenUB - Lu) / gnorm2;
        if (T <= 0) T = 1e-6;

        for (int i = 1; i <= m; i++)
        {
            double g = 1.0 - coveredCount[i];
            u[i] = max(0.0, u[i] + T * g);
        }

        if (ceil(bestLB - 1e-9) >= bestSeenUB) break;
    }

    for (int i = 1; i <= m; i++) u_out[i] = uBest[i];
    UB = bestSeenUB;
    if (bestUBSolOut != nullptr) *bestUBSolOut = bestSeenSol;
    return bestLB;
}

// ============================================================
//  dualAscent — refina u (já factível) aumentando-o enquanto
//  mantém factibilidade dual.
//
//  Para cada elemento i, calcula a folga máxima possível:
//      delta_i = min_{j alive : i in S_j} slack_j
//      onde slack_j = c_j - sum_{k in S_j} u_k
//
//  Aumenta u_i por delta_i (se positivo), atualiza slacks, repete.
//  Em uma passada completa pode dar ganho significativo.
//
//  Iteramos várias passadas — pára quando ninguém melhora.
//  Output: novo u (factível) com sum u_i potencialmente maior.
// ============================================================
double dualAscent(const Instance& inst,
                  const vector<bool>& alive,
                  double* u)
{
    int m = inst.m;
    int n = inst.n;

    vector<double> slack(n + 1, 0.0);
    for (int j = 1; j <= n; j++)
    {
        if (!alive[j]) { slack[j] = 1e18; continue; }
        double s = inst.cost[j];
        for (int e : inst.coverElems[j]) s -= u[e];
        slack[j] = max(0.0, s);
    }

    bool improved = true;
    int  passes   = 0;
    while (improved && passes < 100)
    {
        improved = false;
        passes++;

        for (int i = 1; i <= m; i++)
        {
            // delta_i = min slack[j] sobre j alive contendo i
            double delta = 1e18;
            for (int j0 : inst.elemSets[i - 1])
            {
                int j = j0 + 1;
                if (!alive[j]) continue;
                if (slack[j] < delta) delta = slack[j];
            }
            if (delta > 1e-9)
            {
                u[i] += delta;
                for (int j0 : inst.elemSets[i - 1])
                {
                    int j = j0 + 1;
                    if (alive[j]) slack[j] -= delta;
                }
                improved = true;
            }
        }
    }

    double sum = 0.0;
    for (int i = 1; i <= m; i++) sum += u[i];
    return sum;
}

// ============================================================
//  projectDualFeasible — projeta u no poliedro dual factível
//
//    sum_{i in S_j} u_i <= c_j  para todo j alive
//    u_i >= 0
//
//  Algoritmo simples: enquanto existir conjunto violado, escala
//  os u_i deste conjunto por c_j / sum (excesso eliminado em
//  uma só rodada por conjunto). Iteramos até que não haja
//  mais violação. Em problemas práticos converge em <= 50 iters.
//
//  Após factibilidade:  L(u) = sum_i u_i  (LB válido).
//
//  Retorna sum u_i pós-projeção.
// ============================================================
double projectDualFeasible(const Instance& inst,
                           const vector<bool>& alive,
                           double* u)
{
    int m = inst.m;
    int n = inst.n;

    for (int iter = 0; iter < 1000; iter++)
    {
        bool feasible = true;
        for (int j = 1; j <= n; j++)
        {
            if (!alive[j]) continue;
            double s = 0.0;
            for (int e : inst.coverElems[j]) s += u[e];
            if (s > inst.cost[j] + 1e-9)
            {
                feasible = false;
                double scale = (double)inst.cost[j] / s;
                for (int e : inst.coverElems[j]) u[e] *= scale;
            }
        }
        if (feasible) break;
    }

    double sum = 0.0;
    for (int i = 1; i <= m; i++) sum += u[i];
    return sum;
}

double maxDualViolation(const Instance& inst,
                        const vector<bool>& alive,
                        const double* u)
{
    double maxViol = 0.0;
    for (int j = 1; j <= inst.n; j++)
    {
        if (!alive[j]) continue;
        double s = 0.0;
        for (int e : inst.coverElems[j]) s += u[e];
        maxViol = max(maxViol, s - (double)inst.cost[j]);
    }
    return maxViol;
}

// ============================================================
//  reducedCostFixing — remove conjuntos que não podem fazer
//  parte de qualquer solução com custo < UB.
//
//  Se o conjunto j tem reduced cost rc_j > 0 (já pago) e
//  L(u) + rc_j >= UB  ->  forçar x_j = 0 não muda otimalidade.
//
//  Se rc_j < 0 e L(u) - rc_j >= UB -> forçar x_j = 1 é ótimo,
//  mas raramente se aplica em scp41; ignoramos.
// ============================================================
int reducedCostFixing(const Instance& inst,
                      vector<bool>& alive,
                      const double* u,
                      double LB,
                      int    UB)
{
    int n = inst.n;
    int removed = 0;
    for (int j = 1; j <= n; j++)
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
         << " (sobraram " << (n - removed) << " conjuntos)\n";
    return removed;
}

// ============================================================
//  buildDLX
// ============================================================
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
    cout << "[BUILD] DLX construida. Nos totais: " << nodeCount
         << " (max=" << MAXNODE << ")\n\n";
}

// ============================================================
//  main
// ============================================================
int main(int argc, char* argv[])
{
    string caminho = "scp41.txt";
    if (argc >= 2) caminho = argv[1];
    if (argc >= 3) TIME_LIMIT_SEC = atof(argv[2]);

    cout << "iniciando set cover (instancia: " << caminho
         << ", time-limit: " << TIME_LIMIT_SEC << "s)\n";

    Instance inst;
    if (!lerInstanciaORLibrary(caminho, inst))
        return 1;

    // -------------------------------------------------------
    //  Timer inicia AQUI — equivalente ao CPLEX que mede
    //  presolve + root + branch&bound após ler a instância.
    // -------------------------------------------------------
    gTimer.start();

    // ---- 1. presolve por dominação ----
    vector<bool> alive;
    preprocessDominance(inst, alive);

    // ---- 2. UB inicial via greedy Chvátal ----
    vector<int> greedySol;
    int greedyUB = greedyChvatal(inst, alive, greedySol);
    cout << "[GREEDY] UB inicial = " << greedyUB
         << "  (|sol|=" << greedySol.size() << ")\n";

    // ---- 3. Lagrangian subgradient (no nó raiz) com UB-tracking ----
    static double u_opt[MAXCOL];
    cout << "[LAGRANGIAN] iniciando subgradient...\n";
    vector<int> lagSol;
    int    UB_var = greedyUB;
    double LB_lag = runLagrangian(inst, alive, UB_var, 5000, u_opt, &lagSol);

    // Re-run em fase fina: usa UB_var (possivelmente melhor) e mais iters
    if (UB_var < greedyUB && UB_var > (int)ceil(LB_lag - 1e-9)) {
        static double u_opt2[MAXCOL];
        vector<int> lagSol2;
        int UB_var2 = UB_var;
        double LB_lag2 = runLagrangian(inst, alive, UB_var2, 3000, u_opt2, &lagSol2);
        if (LB_lag2 > LB_lag) {
            LB_lag = LB_lag2;
            for (int i = 1; i <= inst.m; i++) u_opt[i] = u_opt2[i];
            cout << "[LAGRANGIAN] segunda fase melhorou LB para " << LB_lag2 << "\n";
        }
        if (UB_var2 < UB_var) {
            UB_var = UB_var2;
            lagSol = lagSol2;
        }
    }

    int    LB_int = (int)ceil(LB_lag - 1e-9);
    cout << fixed << setprecision(4)
         << "[LAGRANGIAN] LB = " << LB_lag
         << " (ceil=" << LB_int << ", UB=" << UB_var << ")\n"
         << defaultfloat;

    if (UB_var < greedyUB) {
        greedyUB  = UB_var;
        greedySol = lagSol;
        cout << "[LAGRANGIAN] UB melhorado durante subgradient: " << greedyUB
             << " (|sol|=" << greedySol.size() << ")\n";
    }

    // ---- 4. reduced-cost fixing ----
    reducedCostFixing(inst, alive, u_opt, LB_lag, greedyUB);

    // ---- 4b. projeta u no dual factível para LB incremental ----
    //
    //  O subgradient gera u que pode violar (sum u_i in S_j) > c_j;
    //  para usar sum u_i como LB durante o B&B, precisamos u dual
    //  factível. A projeção pode reduzir sum u_i abaixo de L(u),
    //  mas em scp41 fica próximo de 429.
    double sumProj = projectDualFeasible(inst, alive, u_opt);
    cout << "[PROJECT] sum u_i (projetado, LB direto) = " << sumProj
         << " (perda vs L(u)=" << (LB_lag - sumProj) << ")\n";

    // ---- 4b'. dual ascent — refina u factível para maximizar sum u_i ----
    double sumAscent = dualAscent(inst, alive, u_opt);
    double dualViol = maxDualViolation(inst, alive, u_opt);
    if (dualViol > 1e-7)
    {
        cout << "[ASCENT] aviso: violacao dual max = " << dualViol
             << ". Reprojetando multiplicadores.\n";
        sumAscent = projectDualFeasible(inst, alive, u_opt);
    }
    cout << "[ASCENT] LB direto pos ascent = " << sumAscent
         << " (ganho vs projecao: " << (sumAscent - sumProj) << ")\n";
    if (sumAscent > LB_lag) {
        LB_lag = sumAscent;
        LB_int = (int)ceil(LB_lag - 1e-9);
        cout << "[ASCENT] LB Lagrangiano atualizado para " << LB_lag
             << " (ceil=" << LB_int << ")\n";
    }

    // ---- 4c. UB melhor: iterated greedy + 2-opt local search ----
    vector<int> bestSolPool;
    int igUB = iteratedGreedy(inst, alive, u_opt, 50, bestSolPool);
    cout << "[ITER-GREEDY] UB pos iterated greedy = " << igUB
         << " (|sol|=" << bestSolPool.size() << ")\n";

    int lsUB = localSearch2opt(inst, alive, bestSolPool);
    if (lsUB < igUB) {
        cout << "[2-OPT] melhorou UB: " << igUB << " -> " << lsUB
             << " (|sol|=" << bestSolPool.size() << ")\n";
    }
    igUB = lsUB;

    if (igUB < greedyUB) {
        greedyUB  = igUB;
        greedySol = bestSolPool;
    }
    cout << "[UB-FINAL] " << greedyUB << " (|sol|=" << greedySol.size() << ")\n";

    // ---- 5. registra UB do greedy ----
    bestCost = greedyUB;
    bestSolSize = (int)greedySol.size();
    for (int i = 0; i < bestSolSize; i++)
        bestSol[i] = greedySol[i];
    // ---- 6. monta DLX e seta multiplicadores ----
    buildDLX(inst, alive);

    // u_star usado no LB incremental durante a busca
    for (int i = 0; i <= MAXCOL - 1; i++) u_star[i] = 0.0;
    for (int i = 1; i <= inst.m; i++)     u_star[i] = u_opt[i];

    lbActiveSum = 0.0;
    for (int i = 1; i <= inst.m; i++) lbActiveSum += u_star[i];
    rootLB = lbActiveSum;

    // Referencias para 2-opt no incumbent
    g_inst             = &inst;
    g_alive            = &alive;
    g_localSearch2opt  = localSearch2opt;

    cout << "Instancia carregada com sucesso!\n";
    cout << "Elementos (DLX colunas) : " << nElems << "\n";
    cout << "Conjuntos (DLX linhas)  : " << nSets  << "\n";
    cout << "Limite de tempo         : " << TIME_LIMIT_SEC << " s\n\n";

    // se LB = UB, já terminamos
    if (LB_int >= greedyUB)
    {
        cout << "[OTIMO] LB Lagrangiano == UB, solucao do greedy ja e otima.\n";
        provedOptimal = true;
    }
    else
    {
        search(0, nElems, 0);

        // Se a busca completou (não foi interrompida por timeout),
        // bestCost é o ótimo provado por exaustão do B&B.
        if (!stopSearch && bestSolSize > 0)
        {
            cout << "[OTIMO] Busca B&B completou sem time-limit; bestCost e otimo.\n";
            provedOptimal = true;
        }
    }

    double tWallTotal = gTimer.elapsedWall();
    double tCPUTotal  = gTimer.elapsedCPU();

    // ---- resultado final ----
    cout << "\n=== RESULTADO FINAL ===\n";
    if (bestSolSize == 0)
    {
        cout << "Nenhuma solucao encontrada";
        if (stopSearch) cout << " (tempo esgotado antes da primeira solucao)";
        cout << ".\n";
    }
    else
    {
        cout << "Custo minimo : " << bestCost << "\n";
        cout << "Num. conjuntos: " << bestSolSize << "\n";
        cout << "Conjuntos escolhidos: ";
        for (int i = 0; i < bestSolSize; i++)
            cout << bestSol[i] << " ";
        cout << "\n";
        if (stopSearch && !provedOptimal)
            cout << "[AVISO] Solucao pode nao ser otima — busca interrompida por time-limit.\n";
        else if (provedOptimal)
            cout << "[OK] Solucao OTIMA provada (bestCost == LB Lagrangiano).\n";
    }

    cout << "\nEstatisticas de busca:\n";
    cout << "  Nos explorados : " << totalNos   << "\n";
    cout << "  Podas          : " << totalPodas << "\n";

    printTimingSummary(tWallTotal, tCPUTotal);
    return 0;
}
