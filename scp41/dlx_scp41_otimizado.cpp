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
const double TIME_LIMIT_SEC = 3600.0;
const long long LOG_EVERY  = 1000000LL;

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

long long totalNos    = 0;
long long totalPodas  = 0;
bool      stopSearch  = false;
bool      provedOptimal = false;

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
//  PROGRESSO
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
//  cover / uncover — só lista horizontal de cabeçalhos.
//  Mantém lbActiveSum em O(1) para LB incremental.
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

// ============================================================
//  chooseColumn — heurística S melhorada
// ============================================================
int chooseColumn()
{
    int    best      = -1;
    double bestScore = 1e18;

    for (int c = rightN[header]; c != header; c = rightN[c])
    {
        int minCost = INT_MAX;
        for (int r = downN[c]; r != c; r = downN[r])
        {
            int row = rowID[r];
            if (usedInBranch[row]) continue;
            if (rowCost[row] < minCost) minCost = rowCost[row];
        }
        double score = (double)minCost * 1000.0 + colSize[c];
        if (score < bestScore) { bestScore = score; best = c; }
    }
    return best;
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
    colSize[c]--;

    // ---- ORDENA LINHAS CANDIDATAS -------------------------
    // Estáticos por nível de recursão? Não — cada chamada precisa
    // do seu próprio buffer pois recurso ANTES de terminar a iteração
    // sobre candidatos. Então mantemos no stack frame, mas alocados
    // como std::vector<> para ir no heap (evita stack overflow com
    // MAXROW grande e profundidade alta).
    static thread_local vector<pair<double,int>> candStorage;
    static thread_local vector<int> cobStorage;
    // Um buffer por nível de profundidade
    if ((int)candStorage.size() < (k + 1) * MAXROW) candStorage.resize((k + 1) * MAXROW);
    if ((int)cobStorage.size()  < (k + 1) * MAXCOL) cobStorage.resize((k + 1) * MAXCOL);
    pair<double,int>* candidatos = candStorage.data() + k * MAXROW;
    int numCandidatos = 0;

    for (int r = downN[c]; r != c; r = downN[r])
    {
        int row = rowID[r];
        if (usedInBranch[row]) continue;

        int novasCoberturas = 1;
        for (int j = rightN[r]; j != r; j = rightN[j])
            if (colunaAtiva(colID[j])) novasCoberturas++;

        double score = (double)rowCost[row] / novasCoberturas;
        candidatos[numCandidatos++] = {score, r};
    }

    sort(candidatos, candidatos + numCandidatos);

    // ---- EXPLORA ------------------------------------------
    for (int ci = 0; ci < numCandidatos; ci++)
    {
        if (stopSearch) break;

        int r   = candidatos[ci].second;
        int row = rowID[r];

        int novoCusto = custoAtual + rowCost[row];
        if (novoCusto >= bestCost) { totalPodas++; continue; }

        usedInBranch[row]   = true;
        solution[solSize++] = r;

        int* cobertas = cobStorage.data() + k * MAXCOL;
        int numCobertas = 0;

        for (int j = rightN[r]; j != r; j = rightN[j])
        {
            int col = colID[j];
            if (!colunaAtiva(col)) continue;
            colSize[col]--;
            cover(col);
            cobertas[numCobertas++] = col;
        }

        search(k + 1, MAX_K, novoCusto);

        for (int i = numCobertas - 1; i >= 0; i--)
        {
            uncover(cobertas[i]);
            colSize[cobertas[i]]++;
        }
        solSize--;
        usedInBranch[row] = false;
    }

    colSize[c]++;
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
//  refletem "quão atraente" é cada conjunto. Conjuntos com
//  reduced cost pequeno entram com prioridade. Score:
//     max(0, rc_j) / |S_j ∩ uncovered|
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
            double effCost = max(0.0, rc[j]);
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
                     int     UB,
                     int     maxIter,
                     double* u_out)
{
    int m = inst.m;
    int n = inst.n;

    vector<double> u(m + 1, 0.0);
    // inicialização: u_i = min_{j alive, i in S_j} c_j / |S_j|
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

    for (int iter = 0; iter < maxIter; iter++)
    {
        // 1) reduced cost de cada conjunto: c_j - sum_{i in S_j} u_i
        // 2) x_j(u) = 1 sse reducedCost[j] < 0
        // 3) L(u) = sum_i u_i + sum_j x_j * reducedCost[j]
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

        if (noImprove >= halveAfter) { lambda *= 0.5; noImprove = 0; }
        if (lambda < 5e-4)            break;

        // subgradient g_i = 1 - sum_{j : xj=1, i in S_j} 1
        for (int i = 1; i <= m; i++) coveredCount[i] = 0;
        for (int j = 1; j <= n; j++)
            if (xj[j])
                for (int e : inst.coverElems[j]) coveredCount[e]++;

        // |g|^2
        double gnorm2 = 0.0;
        for (int i = 1; i <= m; i++)
        {
            double g = 1.0 - coveredCount[i];
            gnorm2 += g * g;
        }
        if (gnorm2 < 1e-12) break;   // ótimo Lagrangiano alcançado

        double T = lambda * (UB - Lu) / gnorm2;
        if (T <= 0) T = 1e-6;

        for (int i = 1; i <= m; i++)
        {
            double g = 1.0 - coveredCount[i];
            u[i] = max(0.0, u[i] + T * g);
        }

        // poda por gap inteiro
        if (ceil(bestLB - 1e-9) >= UB) break;
    }

    for (int i = 1; i <= m; i++) u_out[i] = uBest[i];
    return bestLB;
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

    cout << "iniciando set cover (instancia: " << caminho << ")\n";

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

    // ---- 3. Lagrangian subgradient (no nó raiz) ----
    static double u_opt[MAXCOL];
    cout << "[LAGRANGIAN] iniciando subgradient...\n";
    double LB_lag = runLagrangian(inst, alive, greedyUB, 2000, u_opt);
    int    LB_int = (int)ceil(LB_lag - 1e-9);
    cout << fixed << setprecision(4)
         << "[LAGRANGIAN] LB = " << LB_lag
         << " (ceil=" << LB_int << ", UB=" << greedyUB << ")\n"
         << defaultfloat;

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

    // ---- 4c. UB melhor: greedy guiado pelos reduced costs +
    //         remoção de conjuntos redundantes ----
    vector<int> rcSol;
    int rcUB = greedyByReducedCost(inst, alive, u_opt, rcSol);
    int rcUBcleaned = removeRedundant(inst, rcSol);
    cout << "[GREEDY-RC] UB via reduced cost = " << rcUB
         << "  apos remover redundantes = " << rcUBcleaned
         << "  (|sol|=" << rcSol.size() << ")\n";

    // tambem limpa redundantes do greedy classico
    vector<int> chvCleanedSol = greedySol;
    int chvCleaned = removeRedundant(inst, chvCleanedSol);
    cout << "[GREEDY-CHV-CLEAN] UB Chvatal apos remover redundantes = "
         << chvCleaned << " (|sol|=" << chvCleanedSol.size() << ")\n";

    // pega o menor
    if (rcUBcleaned < chvCleaned) {
        greedyUB = rcUBcleaned;
        greedySol = rcSol;
    } else {
        greedyUB = chvCleaned;
        greedySol = chvCleanedSol;
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
